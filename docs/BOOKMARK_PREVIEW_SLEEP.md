# Sleep-during-bookmark-preview: behavior tradeoff

## TL;DR

If the device falls asleep while you're on `BookmarkPreviewScreen`, you
currently wake up at the library. The original sketch tried to wake you
back into your book at the page you were really reading — but did this
with logic that silently switched books in one edge case. We picked the
simpler-and-correct behavior; this doc records what we'd need to change
to restore the "wake into your book" behavior properly.

## User-visible difference

| Now (Option 1) | Original sketch |
|---|---|
| Sleep mid-preview → wake at library | Sleep mid-preview → wake into the *previewed* book at the pre-preview page |

## Why the original was buggy

By the time the original's `goToSleep` ran, `g_reader.currentBookPath`
was the **previewed** book — `openBookByIndex` had overwritten it on
preview entry. So:

- **Case 1** — you were reading Book A and previewed a bookmark in
  Book A → wake into Book A. ✓
- **Case 2** — you were reading Book A and previewed a bookmark in
  Book B, or weren't reading anything → wake into **Book B**. You
  silently switched books just by peeking at a bookmark.

The `previewSavedPage` field was a partial bandage: it remembered the
pre-preview *page*, but not the pre-preview *book*. That's why case 2
broke.

## What we do now (Option 1)

[`BookmarkPreviewScreen::onSleep`](../src/ui/screens/bookmarks/preview_screen.cpp)
was deleted. The base-class default kicks in:

```cpp
void Screen::onSleep() { syncWakeState(false); }   // wake to library
```

No special-casing, no `previewSavedPage`, no preview-time progress save.
Case 2 can no longer happen.

## How to restore the "wake into your book" behavior properly (Option 2)

Remember the **pre-preview `(path, page)`** explicitly, instead of
relying on state that `openBookByIndex` overwrote.

1. Add to `BookmarkSession` in
   [src/ui/screens/bookmarks/session.h](../src/ui/screens/bookmarks/session.h):
   ```cpp
   String prePreviewPath;   // "" if user wasn't reading anything
   int    prePreviewPage = 0;
   ```
   Clear them in `resetBookmarkSession()`.

2. In
   [bookmark_list_screen.cpp](../src/ui/screens/bookmarks/bookmark_list_screen.cpp),
   capture pre-preview state **before** `openBookByIndex` (on
   `ButtonEvent::Double`):
   ```cpp
   g_bookmarkSession.prePreviewPath = g_reader.currentBookPath;
   g_bookmarkSession.prePreviewPage = g_reader.pageIndex;
   ```

3. Add a `BookmarkPreviewScreen::onSleep` override that points
   `g_reader` back at the pre-preview book before saving:
   ```cpp
   void BookmarkPreviewScreen::onSleep() {
     safeCloseCurrentBook();
     if (g_bookmarkSession.prePreviewPath.length() > 0) {
       g_reader.currentBookPath = g_bookmarkSession.prePreviewPath;
       g_reader.currentBookKey  = prefKeyForBook(g_bookmarkSession.prePreviewPath);
       g_reader.pageIndex       = g_bookmarkSession.prePreviewPage;
       saveProgressThrottled(true);
       syncWakeState(true);   // wake into reader at prePreviewPath
     } else {
       syncWakeState(false);  // no pre-preview book → wake to library
     }
   }
   ```
   We don't reopen the file — `saveProgressThrottled` writes prefs
   against `currentBookKey`, and we're about to deep-sleep anyway. Skip
   the offset cache save too; the pre-preview book's cache was last
   written while it was active in reader mode.

4. Consider doing the same restore on the triple-click "cancel
   preview" path (`ButtonEvent::Triple` in `BookmarkPreviewScreen`) if
   you want symmetry — currently cancel just transitions back to
   `BookmarkListScreen` without restoring the reader to the pre-preview
   book. Today that's fine because preview is the only thing that
   mutated `g_reader`; if Option 2 lands, the cancel path should also
   restore so the next reader-screen entry doesn't open the wrong book.

The functional difference vs. the original: this saves progress to the
**correct** book's prefs (the one you were actually reading), and the
wake path points at *that* book. Case 2 is fixed.

## Why we didn't do this initially

1. The bookmark-session refactor was specifically about removing the
   global. Wider behavior changes deserve their own diff.
2. Option 1 is arguably better UX anyway — preview is exploratory;
   "fell asleep peeking at a bookmark" doesn't obviously deserve a
   silent book-resume. Reasonable people can disagree; the doc is here
   so we can revisit.
