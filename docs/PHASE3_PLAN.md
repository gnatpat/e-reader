# Phase 3: Screen interface refactor

> Working document for the next big refactor. Started in a previous Claude
> session — written down here so a fresh session can pick it up cold.

## Why this refactor

The current `loop()` is a `switch(mode)` over 8 enum values, each handled by a
`handleModeX()` function in `main.cpp`, each drawn by a `drawX()` function in
`ui/widgets.cpp` (formerly `ui/menu.cpp`). Adding a new screen today touches
5 places. A lot of "screen-local" UI state lives in globals because there's
no natural place to put it.

A `Screen` interface collapses that to one new file per screen, naturally
localises screen-local state, and removes the `mode` enum + switch.

## What's already been done (context for a fresh session)

This codebase is a DIY e-reader running on the Heltec Wireless Paper
(ESP32-S3 + e-ink). Five rounds of cleanup so far:

1. **File split** — original 4239-line `main.cpp` carved into focused files
2. **Pure modules** — typography / pagination / hashing / sanitizers
   extracted to `src/pure/`, all host-testable with no Arduino dependency
3. **Storage split** — old `storage.cpp` split into `fs_util`, `page_cache`,
   `bookmarks`, `list_items`, `settings_store`, `book_state` (firmware) +
   `bookmarks_codec`, `list_codec`, `offset_cache` (pure)
4. **Test infrastructure** — 74 host tests under `test/` via CMake, with a
   custom `TEST_CASE`/`CHECK` framework in `test/test_framework.h` and a
   minimal Arduino `String` shim in `src/pure/arduino_compat.h`
5. **File reorganization** — `src/{hal,storage,ui,web,pure}/` layout;
   `storage/storage.h` umbrella header removed in favour of specific includes

## Current layout

```
src/
  config.h, state.h, state.cpp, main.cpp
  hal/      display, battery, file_stream
  storage/  fs_util, page_cache, bookmarks, list_items,
            settings_store, book_state, library,
            kv_store, preferences_store
  ui/       menu, reader, sleep, text, pala_one_sleep_black_icon_v4
  web/      web
  pure/     paths, text_util, hashing, paginator, bookmark_label,
            bookmarks_codec, list_codec, offset_cache,
            stream, arduino_compat

test/
  CMakeLists.txt, README.md, test_framework.h, main.cpp, map_kv_store.h
  test_*.cpp (10 files)
```

74 host tests pass. Firmware: 968 KB flash, 36% RAM. No warnings.

## Build / test

```bash
# Firmware
pio run

# Host tests
cd test
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
# Or verbose:
./build/Release/host_tests.exe
```

## The plan

### Step 1: Define `Screen` + `ButtonEvent`

New file: **`src/ui/screen.h`**

```cpp
#pragma once
#include "state.h"

struct ButtonEvent {
  enum Kind { None, Short, Double, Triple, Quad, Long } kind = None;
  static ButtonEvent fromButtonState(const ButtonState& b);
  bool any() const { return kind != None; }
};

class Screen {
public:
  virtual ~Screen() = default;
  virtual void onEnter() {}                       // called once on entry
  virtual void onButton(const ButtonEvent& e) = 0;
  virtual void draw() = 0;
  virtual void onIdleTick() {}                    // prefetch / autosave
  Screen* nextScreen = nullptr;                   // non-null = transition
};
```

`fromButtonState` definition can live in `screen.h` (inline) or
`src/main.cpp` — short and one-shot, inline is fine.

### Step 2: Rename `ui/menu.{h,cpp}` → `ui/widgets.{h,cpp}`

Keep these helpers (used by multiple screens):
- `drawSectionHeader(const char* title)`
- `drawMenuBulletRow(int y, const String& label, bool selected, bool boldText, int depth, bool systemItem)`
- `splitListLabelForDisplay(...)`

Delete these (they become screen methods in step 3):
- `drawLibrary`, `drawListScreen`, `drawAbout`,
  `drawBookmarksBookSelect`, `drawBookmarksList`, `enterLibraryRoot`

Update includes everywhere: `#include "ui/menu.h"` → `#include "ui/widgets.h"`.

Build firmware here as a checkpoint before doing step 3.

### Step 3: Create the 8 screen classes

New directory: **`src/ui/screens/`**

| File | Replaces | Notes |
|---|---|---|
| `about_screen.{h,cpp}` | `handleModeAbout` + `drawAbout` | Simplest — do first |
| `list_screen.{h,cpp}` | `handleModeList` + `drawListScreen` | |
| `library_screen.{h,cpp}` | `handleModeLibrary` + `drawLibrary` | The "home" screen |
| `reader_screen.{h,cpp}` | `handleModeReader` (drawing via `renderCurrentPage`) | Uses `idlePrefetchReader` in `onIdleTick` |
| `bookmark_book_select_screen.{h,cpp}` | `handleModeBookmarkBookSelect` + `drawBookmarksBookSelect` | |
| `bookmark_list_screen.{h,cpp}` | `handleModeBookmarkList` + `drawBookmarksList` | |
| `bookmark_preview_screen.{h,cpp}` | `handleModeBookmarkPreview` (drawing via `renderCurrentPage`) | |
| `upload_screen.{h,cpp}` | `handleModeUpload` + `startUploadMode` + `stopUploadModeToLibrary` | Also owns `g_upload` — see step 4 |

Each screen looks roughly like:

```cpp
class AboutScreen : public Screen {
public:
  void onEnter() override { draw(); }
  void draw() override { /* moved from drawAbout() body */ }
  void onButton(const ButtonEvent& e) override {
    if (e.any()) nextScreen = &g_libraryScreen;
  }
};
```

The body of each `handleModeXxx` and `drawXxx` translates mechanically. The
`mode = MODE_X; drawX();` sequences become `nextScreen = &g_xScreen;` and
the new screen's `onEnter()` does the draw.

### Step 4: Move `g_upload` into `UploadScreen`

The one piece of opportunistic global cleanup in this refactor.
`g_upload` is only used during upload mode. Make it a private member of
`UploadScreen`. The web upload-stream handlers (in `web/web.cpp`,
`handleUploadBookStream` and `handleUploadSleepStream`) currently access
`g_upload` directly — they'll need a way to reach the screen's state.

**Options for connecting web → upload screen**:
- (a) Expose a `static UploadState& UploadScreen::state()` accessor —
  simplest; preserves the global-ish pattern but keeps the type private.
- (b) Pass an `UploadState*` to the route handlers via a closure — more
  decoupled but `WebServer.on()` takes function pointers, no closure state.
- (c) Keep `g_upload` global for now and defer this cleanup.

Recommend **(a)** — the upload screen is unique anyway (only one upload
session at a time), so a static-instance accessor is fine.

### Step 5: Wire up dispatch in `main.cpp`

Add at the top of `main.cpp`:

```cpp
#include "ui/screen.h"
#include "ui/screens/about_screen.h"
#include "ui/screens/bookmark_book_select_screen.h"
// ... etc

// Static screen instances — one of each kind.
LibraryScreen              g_libraryScreen;
ReaderScreen               g_readerScreen;
UploadScreen               g_uploadScreen;
AboutScreen                g_aboutScreen;
ListScreen                 g_listScreen;
BookmarkBookSelectScreen   g_bmBookSelectScreen;
BookmarkListScreen         g_bmListScreen;
BookmarkPreviewScreen      g_bmPreviewScreen;

Screen* g_currentScreen = &g_libraryScreen;
```

Then `loop()` becomes:

```cpp
void loop() {
  btns.poll();

  if (g_isrDropCount > BTN_QUEUE_RECOVER_THRESHOLD) {
    noInterrupts(); g_isrDropCount = 0; interrupts();
    clearButtonQueue(); btns.resetState();
  }

  ButtonEvent ev = ButtonEvent::fromButtonState(btns);
  if (ev.any()) markUserActivity();

  if (ENABLE_DEEP_SLEEP && mode != MODE_UPLOAD) {
    if ((uint32_t)(millis() - lastUserActionMs) > sleepAfterMs()) {
      goToSleep();
      return;
    }
  }

  g_currentScreen->onButton(ev);
  g_currentScreen->onIdleTick();

  if (g_currentScreen->nextScreen) {
    g_currentScreen = g_currentScreen->nextScreen;
    g_currentScreen->nextScreen = nullptr;
    g_currentScreen->onEnter();
  }
}
```

Delete all `handleModeXxx` and `enterLibraryRoot` from `main.cpp`.

### Step 6: Keep `mode` for now — update it on transition

`sleep.cpp` reads `mode == MODE_READER || mode == MODE_BM_PREVIEW`. `web.cpp`
writes `mode = MODE_LIBRARY` after editing the list. Simplest: each screen's
`onEnter()` sets `mode` to its corresponding enum value. The enum stays as
a parallel indicator. Remove it as a follow-up task once nothing reads it.

Alternative: add `Screen::modeId()` returning the matching enum. Cleaner
but a touch more boilerplate.

### Step 7: Verify

- `pio run` — firmware build clean
- Host tests pass (they shouldn't be affected — pure modules only)
- Smoke-test on device if possible: library → open book → bookmark →
  back to library → upload mode → out → etc.

## Design decisions already made

1. **Static instances, not heap** — `g_libraryScreen` etc. are statically
   allocated. Transitions are pointer swaps. No `new`/`delete`. Re-entering
   a screen keeps its state (which is usually what we want for back-nav).

2. **`Screen` is the navigation primitive** — transitions are requested by
   setting `nextScreen`, swap happens at top of next `loop()` iteration.
   Avoids re-entrant problems where a screen tries to draw the next one
   inside its own `onButton`.

3. **`onEnter()` does the draw** — `onEnter() { draw(); }` is the common
   pattern. Some screens (`ReaderScreen`) may also draw inside `onButton`
   after a page turn — that's fine.

4. **Global click-event dispatch lives in screens, not `loop()`** — today,
   `loop()` does special-case triple-click handling. After this refactor,
   each screen handles its own triple-click. (Library and About could share
   a "tap-to-home" base if it gets repetitive — premature for 8 screens.)

5. **`g_bookmarkUi` stays global for now** — shared across 3 screens. A
   proper migration needs a small shared "BookmarkSession" struct that
   the entry screen creates and subsequent screens reference. Out of scope
   for this phase.

## What gets cleaned up incidentally

- Dead `LIB_ENTRY_BACK` enum value (declared in `state.h`, never assigned)
- The `(void)selected;` in `drawMenuBulletRow` — re-examine while moving
- The global `enterLibraryRoot` helper — gone (replaced by transition)
- The `handleModeXxx` family in `main.cpp` — gone

## What does NOT change

- Pagination, storage modules, KV store, web routing
- `renderCurrentPage`, `drawSectionHeader` etc. — still called, just from
  inside screen methods
- Button ISR, deep sleep, battery monitoring
- All 74 host tests should pass unchanged

## Scope estimate

- **+9 files** (`screen.h` + 8 screens × 2 files each, condensed into
  combined `.h`/`.cpp` per screen or kept split — call it ~70 lines avg)
- **+1 file** renamed (`menu.{h,cpp}` → `widgets.{h,cpp}`)
- **`main.cpp`**: 415 → ~210 lines
- **Firmware flash**: probably +1-2 KB for 8 vtables — negligible
- **Existing tests**: zero changes

## Phase 4 candidates (not in scope here)

After this lands, the remaining items from the original review:

- **Display abstraction (`Canvas` interface)** — wrap `display` + `u8g2` +
  `gfx` behind one interface. Enables off-device rendering via SDL.
- **Web HTML extraction** — `web.cpp` is 1244 lines, ~700 of which are
  inline HTML. Either factor per-route or move templates to LittleFS.
- **Move `g_bookmarkUi`, `g_list.selectedIndex`, `g_library.selectedItem`
  into their owning screens** — needs a small shared-session struct for
  the bookmark flow.
- **Remove `mode` enum** — once nothing outside the screen layer reads it.

## Risks / things to watch

1. **Vtables in static-storage objects with virtual destructors** — should
   be fine on ESP32 GCC, but if linking gets weird, fall back to allocating
   screens on the heap inside `setup()` (one-time `new`, never freed).
   Pre-flight: build firmware after step 1 to confirm.

2. **`renderCurrentPage` and friends** — `ReaderScreen` and
   `BookmarkPreviewScreen` both use them. They access `g_reader` directly,
   which is fine for now. Don't try to refactor those at the same time.

3. **Sleep timing** — `loop()` calls `goToSleep()` before dispatching to
   the screen. Make sure that order is preserved so we don't accidentally
   draw on a screen that's about to sleep.

4. **`onIdleTick` cadence** — currently `idlePrefetchReader` has its own
   60ms throttle. Call it from `ReaderScreen::onIdleTick` and let the
   internal throttle handle the rest. Don't add a wrapper throttle.
