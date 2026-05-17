# Refactor opportunities

Captured from a code-review pass. The seven items below were the original
candidates, roughly ordered by payoff-per-effort. Items 1, 2, 3, 5, and 7
are done, plus several bonus bug fixes (font-change correctness, on-disk
page cache invalidation, folder expansion alignment) — see the "Bonus
bug" tail near the bottom.

## Status summary

| # | Item | Status |
|---|---|---|
| 1 | Invert storage → ui dependency | **Done** (plus an unplanned storage-file reorganization) |
| 2 | Split `g_reader` god-struct | **Done** (plus a cascade of "trust the invariant" cleanups) |
| 3 | Extract a `ScrollableList` widget | **Done** (plus a `drawMenuRow` API cleanup) |
| 4 | `web.cpp` is 1180 lines of hand-rolled HTML | Open |
| 5 | Move more logic into `pure/` | **Done** (split `g_library` god-struct + `pure/library_nav` with host tests) |
| 6 | Split `state.h` to speed up builds | Open |
| 7 | Heap-allocated `String` on hot paths | **Done** |

---

## 1. Invert the storage → ui dependency — DONE

Started as "stop storage from `#include`ing `ui/...`," ended as a broader
file-by-file reshape of `src/storage/`. Two principles drove the work:

1. **Vertical**: storage is below ui — a file in `storage/` may include
   `pure/` (lower) but never `ui/` or `hal/` (higher).
2. **Horizontal**: each storage file owns one nameable concept. If you
   can't describe it in a single line, it's the wrong grouping.

### Verification

- `grep '^#include\s+"(ui|hal)/' src/storage/` → empty
- Every file under `src/storage/` has a one-line description (see table
  at the bottom of this section)

### Functions that moved up to ui/

Storage had quietly been doing reader-state work. These were lifted out:

| Function | New home |
|---|---|
| `safeCloseCurrentBook` / `clearCurrentBookState` / `reopenCurrentBookIfNeeded` (the last two later renamed/dissolved) | `ui/reader.cpp` |
| `saveProgressThrottled`, `resetSaveThrottle`, `addBookmarkForCurrentBook` | `ui/reader.cpp` |
| `resetUiEphemeralState` (clears `g_toast`) | `hal/display.cpp` |
| `resetNavigationState` (clears the library cursor) | `ui/screens/library_screen.cpp` |
| `applyFontSize` orchestration (was called from inside `loadSettings`) | caller (`main.cpp::setup`) |

### Signature changes

- `loadPageOffsetCacheForBook` / `savePageOffsetCacheForBook` now take a
  `PageOffsetTable&` / `const PageOffsetTable&` instead of writing into
  `g_reader` directly.

### File reorganization

After fixing the *layer* boundary, several `storage/` files were in the
wrong *neighborhood*:

- `storage/bookmarks.{h,cpp}` → `storage/book_metadata.{h,cpp}`. The old
  name lied — the file owned bookmarks AND saved-page progress AND their
  joint lifecycle. All three are "per-book NVS state."
- `storage/book_state.{h,cpp}` deleted entirely. Its contents were misc:
  - `deleteBookMetadata` / `migrateBookMetadata` → `book_metadata.cpp`
  - `isFolderExpanded` / `setFolderExpanded` → `library.cpp`
  - `bookLeafLabel(int idx)` → became pure `bookLeafLabel(path)` in `pure/paths.cpp`
  - `syncWakeState` → first to `storage/wake_state.{h,cpp}`, later (after #2) into `ui/reader.{h,cpp}` as a reader-internal concept
- `page_cache.cpp` lost two cross-cutting concerns: the per-book
  bookmark/progress invalidation in `invalidateAllPageCaches` moved to
  `book_metadata.cpp` (as `resetAllSavedProgress` + `invalidateAllBookmarkOffsets`),
  and the `pc_*.bin` rename/delete logic became `renamePageCacheForBook` /
  `deletePageCacheForBook` primitives.

### Final shape of `src/storage/`

| File | Owns |
|---|---|
| `book_metadata.{h,cpp}` | Per-book NVS state — bookmarks, saved progress, joint lifecycle |
| `library.{h,cpp}` | Library catalog + folder UI state |
| `page_cache.{h,cpp}` | Page-offset RAM LRU + `pc_*.bin` files |
| `list_items.{h,cpp}` | Todo list |
| `settings_store.{h,cpp}` | Runtime settings load |
| `fs_util.{h,cpp}` | Filesystem setup |
| `preferences_store.h`, `kv_store.h` | KV interface + Preferences adapter |

---

## 2. Split `g_reader` god-struct — DONE

`ReaderState` is now a composition of four focused pieces, each owning a
single concern:

```cpp
struct ReaderState {
  OpenBook        book;     // file + path + key (class, enforces invariant)
  PageOffsetTable pages;    // offsets[], count, eofReached
  ReaderCursor    cursor;   // pageIndex, pageTurnsSinceFull
  SaveThrottle    save;     // lastSaveMs, lastSavedPage
};
```

`OpenBook` is the only one that's a class with methods (open/close/isOpen/path/key/file/size).
The strong invariant it enforces — **`isOpen()` ⇔ `path()` is non-empty** —
turned out to be load-bearing for the cascade of cleanups below.

### Deletions enabled by the invariant

A *lot* of defensive scaffolding stopped being necessary:

- `lastPageStartOffset` field — was redundant cache of `pages.offsets[pageIndex]`
- `safeCloseCurrentBook` / `reopenCurrentBookIfNeeded` / `setCurrentBook` / `renameBook` — all collapsed into `OpenBook` methods or deleted as unused
- `handleDelete`'s "if this is the current book" branch — under the new design, web handlers run with `g_reader` already cleared
- `handleMoveBook`'s close-rename-reopen dance — same reason
- `handleJumpPageWeb`'s in-memory update — NVS write is sufficient
- `renderCurrentPage`'s defensive `!file && !reopen()` guard — invariant guarantees the file is open
- `OpenBook::renameInPlace` — YAGNI'd before commit (no caller needed it)
- `resetAllPagination`'s "if a book is open" branch — dead code under current architecture; later, the whole function moved to `storage/book_metadata.cpp` because it became pure storage composition
- `web.cpp`'s `ui/reader.h` include — gone entirely
- Catch-all `g_reader.book.close()` in `goToSleep` — each screen owns its own close now

### Library entry now does a full clear

`LibraryScreen::onEnter` calls `clearCurrentBookState()` (full reset) instead
of just `safeCloseCurrentBook()` (partial). This is what makes the invariant
hold strongly: anywhere outside the reader, `g_reader` is empty. Web handlers,
factory reset, sleep from non-reader screens — all can trust this.

### Wake state collapsed

Wake state was sprayed across the codebase. Now:

- **Only `ReaderScreen::onSleep` sets it** (via `armResumeOnWake()`)
- **Only `tryRestoreReadingSession` reads + clears it** (single-shot consume at boot)
- API renamed from one-function-with-bool to two named verbs
- Two NVS keys collapsed to one (just `wake_path`; presence is the signal)
- Moved out of `storage/wake_state.{h,cpp}` into `ui/reader.{h,cpp}` — wake state is a reader-internal detail

### Render separated from state mutation (option 2)

`renderCurrentPage` was doing both rendering and state normalization
(lazy pagination, cursor clamping, EOF recovery). Pulled the mutations
into a `prepareForRender()` helper so the render function reads almost
read-only on session state. (`pageTurnsSinceFull` is the one exception
and is render-side bookkeeping.)

---

## 3. Extract a `ScrollableList` widget — DONE

Four screens (`LibraryScreen`, `ListScreen`, `BookmarkBookSelectScreen`,
`BookmarkListScreen`) used to reimplement the same scrolling-list math
each. ~150 lines of duplication, with per-screen variation in clamps,
`+1` lineH discrepancies, and top-of-window strategy. Now they all call
a single `drawScrollableList` and describe one row via a callback.

### New public API in `ui/widgets.{h,cpp}`

```cpp
// Draw one row at UI_LIST_LEFT + extraIndent on the given baseline.
// Bold if selected. Resets font afterwards.
void drawMenuRow(int yBaseline, const String& label, bool selected, int extraIndent = 0);

// Row height for menu screens. Exposed so ListScreen's continuation row
// can advance y consistently with the widget.
int menuLineH();

// Iterate visible rows; widget owns scroll math + clamps + centering.
using DrawListRowFn = std::function<int(int idx, int y, bool selected, int budget)>;
void drawScrollableList(int contentTopY, int itemCount, int selectedIndex,
                        const DrawListRowFn& drawRow);
```

### `drawMenuBulletRow` cleanup that came along the way

The old `drawMenuBulletRow(y, label, selected, bold, depth, systemItem)`
had a misleading name (no bullet) and two args nobody used the way the
signature implied:

- `selected` was completely unused (commented out in impl)
- `boldText` was always passed the same value as `selected` — they were
  the same concept
- `depth` and `systemItem` were LibraryScreen-only concepts polluting a
  shared helper

The new `drawMenuRow(y, label, selected, extraIndent=0)` is four args,
all meaningful, with `extraIndent` defaulted to 0. LibraryScreen owns
its own `libraryRowIndent(idx)` that returns the folder-depth +
system-item offset. `UI_DEPTH_INDENT` and the hardcoded 2px nudge moved
out of `widgets.h` into `library_screen.cpp` as `LIBRARY_DEPTH_INDENT`
and `LIBRARY_SYSTEM_NUDGE`.

### Per-screen scroll math standardized

Old per-screen variation got normalized:

| Quirk | Old | New |
|---|---|---|
| `lineH` formula | `+1` in some screens, no `+1` in others | All `+1` (via `menuLineH()`) |
| Visible-row clamps | Per-screen magic numbers (3-6, 2-6, 1-5) | One pair inside the widget |
| Top math | Center selected (3 screens) or anchor 2-from-top (ListScreen) | Center selected for all |

### What got preserved

- **ListScreen's two-row selected**: handled via callback returning 2
  when the item is selected with a long label and `budget >= 2`. The
  widget doesn't know about it; the lambda decides.
- **Strikethrough on done items**: lives in ListScreen's lambda.
- **Library folder depth + system-item nudge**: lives in
  `library_screen.cpp` as the `extraIndent` arg to `drawMenuRow`.

### Files affected

| File | Change |
|---|---|
| `ui/widgets.h` | Declared `drawMenuRow`, `menuLineH`, `drawScrollableList`. Dropped `UI_DEPTH_INDENT`. |
| `ui/widgets.cpp` | Implemented all three. Renamed `drawMenuBulletRow` → `drawMenuRow` and stripped dead args. |
| `ui/screens/library_screen.cpp` | Adopted widget; added `LIBRARY_DEPTH_INDENT`, `LIBRARY_SYSTEM_NUDGE`, `libraryRowIndent`, `isSystemEntry`. Dropped `g_settings.lineGap` direct use. |
| `ui/screens/list_screen.cpp` | Adopted widget; kept strikethrough + two-row selected logic in the lambda. |
| `ui/screens/bookmarks/bookmark_list_screen.cpp` | Adopted widget. |
| `ui/screens/bookmarks/book_select_screen.cpp` | Adopted widget. |

## 4. `web/web.cpp` is 1180 lines of hand-rolled HTML

Two paths, increasing in scope:

- **Cheap**: extract CSS to a `/style.css` route with a cache header; lift more
  template helpers out of `webPageStart` / `webPageEnd` (`card(title, body)`,
  `formRow(...)`, `statsGrid(...)`); collapse the 30+ `UploadScreen::state().X`
  repetitions with local references in each handler.
- **Bigger**: turn the server into `/api/...` JSON endpoints + ship a small
  static SPA (one HTML + one JS file in LittleFS at `/web/`). Iterate on UI
  without re-flashing.

The SPA version is the right end state if web is going to keep growing; the
template extraction is enough if web is "done".

## 5. Move more logic into `pure/` — DONE

Started as "port menu navigation into pure," ended as a deeper split of
`g_library` along the same fault lines as `g_reader` in #2. The original
`LibraryState` was bundling three independent concerns:

- **Catalog** (`books[]`, `folders[]`) — stable inventory of disk
- **Navigation state** (`selectedItem`, `folderExpanded[]`) — UI cursor
- **Derived view** (`entryTypes/Refs/Depths[]`) — recomputed every draw

Three lifecycles, three consumers, one struct. After the split:

| Was on `g_library` | Now lives in | Owner |
|---|---|---|
| `books`, `folders` (+ counts) | `pure/library_nav.h::Catalog`, instance `g_library` | `storage/library.{h,cpp}` |
| `selectedItem` | `static int s_cursor` | `library_screen.cpp` |
| `folderExpanded[]` | `static char s_expandedNames[][]` (name-keyed) | `library_screen.cpp` |
| `entries[]` (3 parallel arrays) | `static LibEntry s_entries[]` (single AoS) | `library_screen.cpp` |
| `currentFolder` | (deleted — was unused) | — |

### Catalog moved to `pure/`

`Catalog` and `BookInfo` are pure POD (`char[]` arrays + `size_t`), no
device deps. Putting them in `pure/library_nav.h` matches the existing
pattern of `pure/page_offset_table.h` + `storage/page_cache.cpp` — pure
owns the data type, storage owns the I/O ops that populate it. Means the
assembler (below) can take the catalog directly without view-building
glue at the call site.

### Pure assembler

`buildLibraryEntries(catalog, folderExpanded, systemEntries, out, cap)`
in `pure/library_nav.cpp` flattens catalog + expansion + system entries
into the menu rows the screen draws. Replaced three storage-side helpers
(`buildLibraryEntries`, `addLibraryFolderTree`, `addLibraryBookEntry`)
that were reaching directly into `g_library` and a `g_library`-borrowed
`folderExpanded[]`.

System entries (`Bookmarks`/`List`/`About`/`Upload`) are now passed in
by the screen rather than hardcoded in the storage function. Adding /
reordering is a one-line edit in the screen.

8 new host tests in `test/test_library_nav.cpp`: empty catalog, root
books after folder tree, expanded folder shows children at depth+1,
subfolders nest, collapsed parent hides expanded child, system entries
appended in given order, `outCap` respected, catalog ordering preserved.

### Folder expansion: name-keyed, not index-keyed

Initial Phase-1 split kept `folderExpanded[]` as a `bool[MAX_FOLDERS]`
keyed by folder index. That was wrong: web handlers call `loadBooks()`
13+ times across the file, each of which re-sorts `g_library.folders[]`
and shifts indices arbitrarily. The expansion flags would silently drift
out of sync with whatever folder happened to land at each index.

The `refreshLibrary()` wrapper that snapshot-and-restored expansion
across `loadBooks()` only patched the *last* misalignment (when leaving
upload mode); everything between web mutations was wrong.

Fix: store the *names* of expanded folders directly:

```cpp
static char s_expandedNames[MAX_FOLDERS][MAX_FOLDER_PATH + 1];
static int  s_expandedCount = 0;
```

`isExpanded(name)` is a linear name search; `toggleExpanded(name)` adds
or removes by name and prunes dead entries (folders that no longer exist
in the catalog) on the way through. `draw()` builds a temporary
`bool[MAX_FOLDERS]` view from the current folder ordering just before
calling the pure assembler.

Now `loadBooks()` is safe to call freely — any reshuffle of
`g_library.folders[]` is reflected on the next draw with no
reconciliation step. `refreshLibrary` deleted entirely; upload-screen
reverts to plain `loadBooks()`.

Cost: ~2 KB extra RAM (32 × 64-byte name slots vs 32 booleans). Worth
it for correctness.

### Other cleanups along the way

- **`loadBooks` no longer drags the todo list along.** Old `loadBooks`
  silently called `loadListItems()` and clamped the list cursor —
  unrelated concern that was here because the original god-struct mixed
  globals. Stripped out; `loadListItems()` now preserves its own cursor
  across reload (the workaround moved to where it actually belongs);
  boot adds an explicit `loadListItems()` call.
- **`isFolderExpanded`/`setFolderExpanded`/`libraryFolderExists`/
  `currentFolder`** all deleted — the split made it visible they had
  no remaining callers.
- **`addFolderIfMissing`/`scanBooksRecursive`** marked `static` — only
  used internally.
- **`MAX_FOLDER_PATH = 63`** added to `config.h`; `BookInfo.folder` and
  `Catalog::folders` use it (was hardcoded `64` in three places).
- `storage/library.h` is now ~10 lines: `extern Catalog g_library;` and
  `void loadBooks();`. The whole header fits in an editor preview.

### What was deliberately not done

- **Cursor wrap as a pure helper.** `(s_cursor + 1) % s_entryCount` is
  one line at one call site; a pure `cursorDown(int, int)` helper would
  add file overhead for no readability gain.
- **Bookmark-flow state machine into pure.** Still attached to
  `BookmarkPreviewScreen`'s share-vs-don't-share question (see the
  follow-up at the bottom). Deferred until that decision is made.

## 6. `state.h` as a universal include is hurting build times

Every firmware file `#include`s it, and it pulls in `Arduino.h`,
`heltec-eink-modules.h`, `WebServer.h`, `Preferences.h`, `LittleFS.h`. Touching
`state.h` rebuilds the world.

Split into `state_fwd.h` (just `extern` declarations + forward declarations
where possible) and let TUs pull in the implementation headers themselves.
Incremental rebuilds get noticeably faster and the dependency graph becomes
legible.

## 7. Heap-allocated `String` on hot paths — DONE

The paginator's hot loop went from ~1500 heap operations per page-turn to
**zero**. Three steps, each independently testable against the host suite.

### Step 1 — kill the per-byte `String ch`

The read loop wrapped each byte in a one-byte heap-backed `String` purely
so it could call `isBreakableWhitespaceChar(const String&)` /
`isBreakablePunctuationChar(const String&)` and do `token += ch`. Renamed
those helpers to byte-wise siblings (`isBreakableWhitespaceByte(char)` /
`isBreakablePunctuationByte(char)`), dropped `String ch` entirely. Also
removed dead code: `if (c == '\t') ch = " ";` was unreachable because
`isBreakableWhitespaceChar("\t")` was already true and the next branch
always `continue`d.

The Unicode angle: there isn't one. The loop reads byte-by-byte and
multi-byte UTF-8 continuation bytes never match any of the ASCII
whitespace/punctuation checks, so they accumulate into `token` correctly
whether `token` is a `String` or a `char[]`.

### Step 2 — fixed buffers for `line` and `token`

Replaced `String line / token` with `char line[256] / token[512]` plus a
`char scratch[769]`. The "candidate = line + token" measurement that was
allocating ~10–20 KB of memcpy traffic per page (an arithmetic series in
line length) became a `memcpy` into the scratch buffer with a
NUL-terminator. Same trick inside `hardBreakToken`: temporarily NUL-terminate
the prefix, measure, restore.

Added a buffer-friendly overload `utf8SafeCharLenAt(const char*, size_t,
size_t)` to `text_util` (kept the existing `String&` version — tests use
it). Also added a defensive token-overflow path: if `token` fills (a
500-byte non-breakable run), force a flush. Normal text never reaches it.

Sizes: 256 line + 512 token + 769 scratch = ~1.5 KB stack. ESP32 task
stacks are 8 KB+, no concern.

### Step 3 — change `LineCallback` signature

Was `void(const String& line)` — forced one `String` allocation per
emitted line (~14/page) just to satisfy the callback contract. Changed to
`void(const char* buf, size_t len)` where `buf` is the paginator's working
buffer (NUL-terminated, valid for the call). Two call sites updated:

- [`ui/text.cpp::onLine`](../src/ui/text.cpp): draw branch is now
  `u8g2.print(buf)` directly. The `outText` branch (rare — only fires for
  the web preview / bookmark export endpoints) uses `outText->concat(buf,
  len)` to write directly into the caller's pre-reserved buffer instead
  of round-tripping through a `String t = line; outText += t;`.
- [`test/test_paginator.cpp`](../test/test_paginator.cpp): the test
  callback constructs a `String(buf)` once per line.

### Final allocation tally per page-turn

| Path | Before | After |
|---|---|---|
| Reader render (e-ink draw) | ~1500 | **0** |
| Web preview (rare) | ~1500 | ~14 (one `concat` call per line, into caller's buffer) |

### What changed in the public API

- `paginator.h`: `LineCallback` is now `void(const char*, size_t)`.
- `text_util.h`: added `utf8SafeCharLenAt(const char*, size_t, size_t)`
  overload; renamed `isBreakableWhitespaceChar`/`isBreakablePunctuationChar`
  to `*Byte` siblings taking `char`.

### What was deliberately not done

The note suggested introducing a `StringView` type. Skipped — `MeasureFn`
already takes `const char*`, the new `LineCallback` takes `const char* +
size_t`, and there's only one translation unit doing the work. A view
abstraction would have been pure surface area without buying anything. If
#5 (more into pure/) ever wants a view type, that's the moment to add one.

---

## Suggested next moves

With #1, #2, #3, #5, and #7 done, only structural-polish items remain.
Priority:

1. **BookmarkPreviewScreen / `g_reader` split** (smaller follow-ups
   tail, below) — the most coherent thing left. Resolves the
   share-vs-don't-share fuzziness that was the original ask of the
   session that landed #5 and #7.

2. **#6 (state.h split)** — build-time win. Worth doing eventually but
   doesn't change runtime or readability much.

3. **#4 (web template/SPA)** — biggest scope, lowest cross-cutting
   impact. Web layer is already self-contained. Defer unless web is going
   to keep growing.

### Bonus bug: "byte offset is invariant; page number is layout-dependent"

The original firmware (pre-refactor) had a misconception baked in: it
treated *both* page numbers and byte offsets as "font-layout dependent."
On every font change, `resetAllPagination` reset saved page numbers to
0 *and* invalidated all bookmark byte offsets. The author's comment in
the original code made the misunderstanding explicit:

> // page N and offset O at font 8 point to different text at font 12.

This is wrong about offsets. Byte offset O is a position in the file —
the file's bytes don't move with layout. Only the *mapping of pages to
byte offsets* changes with layout.

Symptom: every font change wiped both the user's reading position and
the byte-offset of every bookmark, requiring the reader to re-paginate
and bookmarks to re-derive "page N at current font" — landing on
completely different content than what was bookmarked.

**Fix landed in two passes:**

1. **Bookmarks.** Stop invalidating bookmark byte offsets on font
   change. `bookmark_list_screen` navigates by stored offset (with
   `findPageForOffset` to derive the page at the current layout), not
   by the stored page number. Legacy data with `0xFFFFFFFF` offsets
   falls back to page-number navigation, healing on next re-save.
2. **Saved reading progress.** Introduced `loadSavedOffset` /
   `saveSavedOffset` under a new `<bookKey>_off` NVS key. The reader
   writes both `_off` (canonical) and `_p` (stale display hint) on every
   save. `openBookByIndex` prefers `_off`; falls back to `_p` for
   legacy data. `resetAllSavedProgress` deleted entirely.
   `resetAllPagination` collapsed to just `invalidateAllPageCaches()`.

After this, layout changes don't lose anything that matters. The page
caches go (correctly stale), but reading position and bookmarks survive
exactly because they're now keyed by byte offset.

### Bonus bug to the bonus bug: stale on-disk page caches survived font change

The "Bookmarks" + "Saved reading progress" fixes above made byte offsets
canonical and survive font changes — which is correct. But that change
exposed a latent landmine that had been dormant in the original code:
**`invalidateAllPageCaches` was never actually deleting the on-disk
`pc_*.bin` files**.

The iteration loop checked `f.name().startsWith("/pc_")`, but on this
LittleFS port `f.name()` returns just the basename (`"pc_xxx.bin"`, no
leading slash). The check was always false; the files were always kept.
The hint was already in the codebase: `library.cpp` has
`entryName.startsWith("\")` (a backslash typo for `"/"`) whose else
branch always runs because `f.name()` never returns leading slashes.

In the **original** code this was masked: `invalidateAllPageCaches` also
reset every book's saved page to 0 and invalidated every bookmark's byte
offset. So after a font change, the user always landed at byte 0 of
page 0, where stale offsets aren't consulted. Annoying-but-consistent.

Once the byte-offset fixes above made saved positions survive font
changes, the bug surfaced: `openBookByIndex` loaded the stale `pc_*.bin`
into `g_reader.pages.offsets[]` (font-A boundaries), `findPageForOffset`
walked the stale table to land at a font-A boundary, and rendering at
font B between two font-A boundaries showed only "a few lines" of
content per page (font-A page = fewer bytes, fits in fewer font-B
lines). User-visible symptom: font change → next/back advances by a
sliver, not a page.

**Fix: layout-stamped cache files** instead of cross-cutting invalidation.

The cache file header gained a `layoutVersion` field (a packing of
`fontSize` and `lineGap`). Magic bumped `0x50434F46 → 0x50434F47` so
existing files fail load and get overwritten on next save. Load rejects
any file whose stamp doesn't match the layout the caller is operating
under. Save writes the current stamp.

Layout-correctness is now a property of the file format itself, not of
a filesystem-walk that depended on platform-specific `f.name()` quirks.

What got deleted:
- `invalidateAllPageCaches` (the whole iteration + delete loop)
- `resetAllPagination` (was a one-line wrapper around the above)
- The `if (layoutChanged) resetAllPagination();` web handler reduces to
  `resetOffsetCache();` (only the in-memory LRU still needs clearing,
  because its entries are keyed by page numbers under the old layout).

API change worth noting: `loadPageOffsetCacheForBook` and
`savePageOffsetCacheForBook` now take `int fontSize, int lineGap`
explicitly instead of reaching into `g_settings`. The `storage/`
layer no longer knows that `g_settings` exists; the four ui-side
callers pass the values explicitly. Slightly more verbose at the call
site, but the contract — "this cache is valid only under this layout" —
is now visible at every call.

### Known-failing test

- `test_text_util.cpp::"compactText limits consecutive newlines to two"`
  has been failing on `main` for at least as long as the apps-layer work
  has been going on. Both two-newline cases (4→2 and 2→2) come back
  wrong; the one-newline case passes, so the regression is in the
  collapse path of `compactText`. See the FIXME above the test for the
  diagnosis hand-off.

### Smaller follow-ups noted along the way

- **Drop page number from bookmark storage.** Today bookmarks store
  `(page, byteOffset)` pairs. Byte offset is canonical; page number is
  just a stale display hint. Cleaner long-term: bookmark format becomes
  offset-only, with the page number computed on the fly when rendering
  the bookmark list. Storage format change with v1/v2/v3 decoder
  backwards-compat.

- **`BookmarkPreviewScreen` borrows `g_reader` as a scratchpad.** Now
  that the strong invariant is in place, the share-vs-don't-share
  decision is visible. The natural cut is between **"viewing a book"**
  (open the file, paginate, render pages, move the cursor in memory)
  and **"progressing through a book"** (save progress to NVS, update
  wake state, throttle saves, persist the page cache). Both screens do
  the first; only ReaderScreen does the second. Today, the throttle
  state and "is this my reading session?" semantics live on the same
  `g_reader` struct that preview borrows — which is why the boundary
  feels fuzzy. A clean split would have `OpenBook + pages + cursor` as
  "view state" (shared) and `SaveThrottle + persistence ops` as "reader
  session" (reader-only). Preview becomes "I have my own view; on
  commit, hand it to the reader as a session."
- **Render/session file split (option 3).** We did option 2
  (`prepareForRender` extraction). If a future need wants to test
  rendering in isolation, that's the moment to also split the file.
- **Revisit the `#ifdef ARDUINO` walls** in `storage/book_metadata.cpp`
  and `storage/list_items.cpp`. Some of what they guard is now
  straightforwardly host-testable post-#1.
