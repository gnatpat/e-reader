# BookmarkSession redesign — open problem

> Last remaining shared global in `state.h`. Picked up by a fresh session
> after the rest of phase 4 was completed.

## TL;DR

`g_bookmarkUi` is a single global struct that three screens
(`BookmarkBookSelectScreen`, `BookmarkListScreen`, `BookmarkPreviewScreen`)
share to navigate "pick a book → see its bookmarks → preview one." It has
no clear owner because *every* screen in the flow reads + writes it.
We want to replace it with a `BookmarkSession` that's created on entering
the flow, destroyed on leaving, and referenced by the three screens while
active.

## Current shape

[`state.h`](../src/state.h):

```cpp
struct BookmarkUiState {
  int bookIndex = 0;           // which book in g_library
  int selectedIndex = 0;       // which bookmark in the loaded list
  uint16_t pages[MAX_BOOKMARKS];
  uint32_t offsets[MAX_BOOKMARKS];
  uint8_t count = 0;

  bool previewActive = false;  // <-- now dead, see below
  int previewSavedPage = 0;    // pre-preview page, restored on commit/sleep
};

extern BookmarkUiState g_bookmarkUi;
```

## Who touches each field

| Field | Read | Written |
|---|---|---|
| `bookIndex` | all 3 bookmark screens, `bookmark_list_screen` uses to index `g_library.books[]` | `LibraryScreen` (init to 0 on entry), `BookmarkBookSelectScreen` (cursor), `book_state::resetNavigationState` (clear) |
| `selectedIndex` | `BookmarkListScreen` | `BookmarkBookSelectScreen` (init to 0 on entry to list), `BookmarkListScreen` (cursor), `book_state::resetNavigationState` (clear) |
| `pages[]` / `offsets[]` / `count` | `BookmarkListScreen` (render), `BookmarkListScreen::onButton` (resolve target on double-click) | `BookmarkListScreen::draw` loads via `loadBookmarksForKey()` |
| `previewSavedPage` | `BookmarkPreviewScreen::onSleep` (restore actual reading position before saving progress) | `BookmarkListScreen` (captures `g_reader.pageIndex` or prefs value when entering preview), `book_state::resetPreviewState` (clear) |
| `previewActive` | **No live readers** | `web.cpp` (on font change), `book_state::resetPreviewState`, `BookmarkPreviewScreen::onButton`, `BookmarkListScreen::onButton` (set true on entry) |

### Incidental cleanup: `previewActive` is now dead

It was previously read by `sleep.cpp::goToSleep` and
`reader.cpp::idlePrefetchReader`. Both reads were removed during the
`onSleep()` refactor and the `idlePrefetchReader` dead-guard cleanup
(phase 4, option 1). Every remaining touch is a *write*. **Delete it as
part of the session redesign** — six write sites and zero readers.

`previewSavedPage` is still needed: `BookmarkPreviewScreen::onSleep`
reads it to save progress against the pre-preview page rather than the
previewed one.

## Lifecycle today (de facto)

```
[anywhere]
   │  user double-clicks "Bookmarks" entry in library
   ▼
LibraryScreen::onButton sets g_bookmarkUi.bookIndex = 0
   │
   │  (state inherited: pages[], selectedIndex, etc. from last time)
   ▼
BookmarkBookSelectScreen
   │  navigates bookIndex with click/long-click
   │  double-click → set selectedIndex = 0, transition
   ▼
BookmarkListScreen
   │  draw() loads pages[]/offsets[]/count for g_library.books[bookIndex]
   │  user cycles selectedIndex
   │  long-click → back to BookmarkBookSelectScreen (state preserved)
   │  double-click → capture previewSavedPage, openBook, set previewActive,
   │                 transition
   ▼
BookmarkPreviewScreen
   │  page-turn with click/double-click (no progress save)
   │  long-click "accept" → save progress, transition to ReaderScreen
   │  triple-click "cancel" → close book, transition back to BookmarkListScreen
   ▼
[ReaderScreen or back up the stack to LibraryScreen]
   │
   │  on returning to LibraryScreen (any triple-click home):
   │    book_state::resetNavigationState clears bookIndex + selectedIndex
   │    book_state::resetPreviewState clears previewSavedPage (+ dead previewActive)
```

So the session is *implicitly* "alive" from `LibraryScreen` → bookmark flow
entry until you go back to `LibraryScreen` (which resets it). The fields
just happen to survive in `g_bookmarkUi` because nothing nukes them mid-flow.

## What needs designing

A `BookmarkSession` struct that captures only what the three screens
actually share, with explicit create/destroy points.

### Open questions

1. **Where does it live?**

   - Option A — file-scope `static BookmarkSession s_session;` in
     `ui/screens/bookmark_book_select_screen.cpp` (the entry screen),
     exposed via `BookmarkBookSelectScreen::session()` accessor.
     Mirrors `UploadScreen::s_state` / `UploadScreen::state()`.
   - Option B — a free `BookmarkSession& bookmarkSession()` in a new
     `ui/screens/bookmark_session.{h,cpp}` shared by all three screens.
     Avoids implying that `BookmarkBookSelectScreen` "owns" it more than
     the others.

   B is probably cleaner. The session is conceptually shared between
   three equal screens, not owned by one.

2. **What's the lifetime?**

   The simplest model: it always exists (file-scope static), but
   "starts" when `LibraryScreen` transitions in and resets to defaults.
   That matches what happens today, just made explicit.

   An alternative — true RAII, construct on entry / destroy on exit —
   is overkill for fixed-size POD on a 320 KB device. Static is fine.

3. **What gets a "reset" entry point?**

   Need a `void resetBookmarkSession()` (or similar) called from:
   - `LibraryScreen::onEnter` (clear cursor on going home)
   - or from the entry transition (when LibraryScreen → BookmarkBookSelectScreen)

   The current code resets from `LibraryScreen::onEnter` via
   `book_state::resetNavigationState`. Moving that reset into the session
   header is the natural next step.

4. **What about `previewSavedPage`?**

   It's only used between "entered preview" and "left preview." That's
   a *narrower* sub-lifetime than the rest of the session. Two valid
   structures:

   - Keep it as a field of `BookmarkSession` — slightly wider scope than
     necessary but matches current behavior (preserved across the
     transient transitions within the flow).
   - Make it a private member of `BookmarkPreviewScreen`, set by
     `BookmarkListScreen` via a setter before transitioning. Cleaner
     scope but introduces a screen-to-screen handoff API.

   The first option is simpler and probably fine.

5. **Should the loaded `pages[]/offsets[]/count` data live in the
   session, or in a separate "currently-loaded bookmarks for book X"
   struct?**

   They're already coupled to `bookIndex` (one specific book's
   bookmarks). Keeping them in the session is fine, but rename:
   `loadedBookmarks` or split into `BookmarkSession { cursor; loaded; }`
   if it helps readability.

6. **What about the dependency on `book_state::resetNavigationState`?**

   That function currently resets `bookIndex` and `selectedIndex` (plus
   `g_library.selectedItem` and `g_library.currentFolder`). After this
   refactor, the bookmark fields move out of it — either into the new
   `resetBookmarkSession()` or fold the whole concept differently.

   `resetNavigationState` then becomes "reset library cursor" only.
   Worth checking if other callers (e.g., factory reset in `web.cpp`)
   still need a unified reset entry point.

## Proposed sketch

```cpp
// ui/screens/bookmark_session.h
#pragma once
#include "config.h"
#include "pure/arduino_compat.h"

struct BookmarkSession {
  // Cursor — which book / which bookmark of that book is selected.
  int bookIndex = 0;
  int selectedIndex = 0;

  // Bookmarks loaded for g_library.books[bookIndex]. Refreshed each time
  // BookmarkListScreen is entered.
  uint16_t pages[MAX_BOOKMARKS];
  uint32_t offsets[MAX_BOOKMARKS];
  uint8_t  count = 0;

  // Where the user was reading BEFORE entering the preview, so
  // sleep-during-preview saves their actual reading position rather than
  // the previewed bookmark.
  int previewSavedPage = 0;
};

BookmarkSession& bookmarkSession();
void resetBookmarkSession();
```

Implementation lives in `bookmark_session.cpp`, file-scope `static
BookmarkSession s_session;`, accessors trivial.

Then:

- The three bookmark screens `#include "ui/screens/bookmark_session.h"`
  and reference `bookmarkSession().bookIndex` etc.
- `LibraryScreen::onEnter()` calls `resetBookmarkSession()` (replacing
  the bookmark portion of `resetNavigationState`).
- `web.cpp::handleSettingsPost`'s `previewActive = false` line just
  disappears (along with the dead field).
- `g_bookmarkUi` global is deleted; `state.h` loses its last
  application-state struct.

## Risk + estimate

- **Risk: medium.** Touches three screens + book_state + web + library
  screen. Mechanical once the design is settled, but easy to subtly
  break preview-mode behavior. Smoke test: library → bookmarks →
  pick book → pick bookmark → preview → accept (sleep mid-preview,
  wake, confirm correct page restored).
- **Estimate: half a day** including the rename of
  `resetNavigationState` and the deletion of `previewActive`.

## After this lands

`state.h` shrinks to **just device handles** (`prefs`, `server`,
`display`, `AP_SSID`, `AP_PASS`, `FS` macro). Three ways to handle what's
left:

### Option A — distribute the handles to their natural modules (recommended)

Continues the phase-4 pattern. Each handle goes where it logically belongs:

| Handle | Destination |
|---|---|
| `display`, `gfx`, `u8g2` | `hal/display.h` (already declares `gfx` + `u8g2`; just add `display`) |
| `server` | `web/web.h` |
| `prefs` | `storage/preferences_store.h` |
| `AP_SSID`, `AP_PASS` | `web/web.h` (Wi-Fi credentials, used only by upload + about screens) |
| `#define FS LittleFS` macro | The awkward one — needs to come after `<LittleFS.h>` declares `class FS`. Either leave as the one-liner content of `state.h`, or move into a tiny `firmware_compat.h` |

`state.h` ends up either deleted or reduced to the `FS` macro alone. Each
firmware module that needs a handle includes the specific header that
owns it. **Most consistent with the rest of the codebase.**

### Option B — rename `state.h` to `device.h`

Leave the structure alone, just rename the file to reflect what it's
actually become (device-handle singletons + Wi-Fi credentials). Minimal
churn. Loses the per-module ownership consistency Option A buys.

### Option C — fold into `config.h` with `#ifdef ARDUINO`

Move the device handles into `config.h` inside an `#ifdef ARDUINO`
block:

```cpp
#ifdef ARDUINO
#include <heltec-eink-modules.h>
#include <WebServer.h>
#include <Preferences.h>
#include <LittleFS.h>
#define FS LittleFS
extern WebServer server;
extern Preferences prefs;
extern EInkDisplay_WirelessPaperV1_2 display;
extern char AP_SSID[24];
extern const char* AP_PASS;
#endif
```

Pure modules ignore the firmware block, so the host-test invariant
survives. Single fewer header in the tree. **Trade-offs:**
- `config.h` becomes a mixed-purpose file (tunables + device handles).
- Every firmware translation unit transitively pulls in three
  heavyweight library headers via `config.h`, where today they're
  included only by files that need them.
- The "config.h must stay host-compatible" doc comment gets more
  nuanced ("outside the `#ifdef ARDUINO` block").

Recommend **A** for consistency, but B and C are both defensible.
