# Phase 4 candidates

> Continuation of [PHASE3_PLAN.md](PHASE3_PLAN.md). Five options identified;
> first one drafted in detail since it's the natural next step. The rest are
> sketches for later.

---

## Option 1 — Finish the screen-layer story: remove `mode`

**Status: done.** Executed with the `onSleep()` + `allowSleep()` refinements
described below; `idlePrefetchReader`'s dead guards dropped too. The
`Mode` enum + global is the most
visible piece of legacy left over from phase 3. Every screen's `onEnter()`
sets `mode = MODE_X` as a mirror, but only two outside callers actually
read it. Replacing both with screen-layer hooks lets us delete the enum,
the global, and the `mode = MODE_X` lines from every screen.

### Where `mode` is still read

```
sleep.cpp::goToSleep   reads mode to decide "save progress before sleep"
                       reads mode to set wake_mode for resume
reader.cpp::idlePrefetchReader  reads mode as a defensive "am I active?" guard
```

That's it. Once these are dealt with, `mode` is dead.

### Refinement: `Screen::onSleep()` instead of pointer comparisons

The first instinct ("replace `mode == MODE_READER` with `g_currentScreen ==
&g_readerScreen`") works but leaks screen identity into `sleep.cpp`. A
cleaner move is to add an `onSleep()` hook to the `Screen` interface and
let each screen take care of its own pre-sleep work.

Today in `sleep.cpp`:

```cpp
void goToSleep() {
  if (!ENABLE_DEEP_SLEEP) return;

  if (g_bookmarkUi.previewActive) {
    int tmpPage = g_reader.pageIndex;
    g_reader.pageIndex = g_bookmarkUi.previewSavedPage;
    saveProgressThrottled(true);
    if (g_reader.file) savePageOffsetCacheForBook(g_reader.currentBookPath, g_reader.file.size());
    g_reader.pageIndex = tmpPage;
  } else if (mode == MODE_READER) {
    saveProgressThrottled(true);
    if (g_reader.file) savePageOffsetCacheForBook(g_reader.currentBookPath, g_reader.file.size());
  }

  bool wasReading = (mode == MODE_READER || mode == MODE_BM_PREVIEW)
                    && g_reader.currentBookPath.length() > 0;
  syncWakeState(wasReading);
  // ... drop wifi, sleep image, esp_deep_sleep_start
}
```

After:

```cpp
// screen.h
class Screen {
public:
  // ...
  virtual void onSleep() { syncWakeState(false); }   // default: clear wake state
};

// reader_screen.cpp
void ReaderScreen::onSleep() {
  saveProgressThrottled(true);
  if (g_reader.file) savePageOffsetCacheForBook(g_reader.currentBookPath, g_reader.file.size());
  syncWakeState(g_reader.currentBookPath.length() > 0);
}

// bookmark_preview_screen.cpp
void BookmarkPreviewScreen::onSleep() {
  // Save against the page the user was on before preview, not the previewed one.
  int tmpPage = g_reader.pageIndex;
  g_reader.pageIndex = g_bookmarkUi.previewSavedPage;
  saveProgressThrottled(true);
  if (g_reader.file) savePageOffsetCacheForBook(g_reader.currentBookPath, g_reader.file.size());
  g_reader.pageIndex = tmpPage;
  syncWakeState(g_reader.currentBookPath.length() > 0);
}

// sleep.cpp
void goToSleep() {
  if (!ENABLE_DEEP_SLEEP) return;
  g_currentScreen->onSleep();
  delay(50);
  safeCloseCurrentBook();
  drawSleepScreen();
  // ... drop wifi, sleep image, esp_deep_sleep_start
}
```

Wins:

- `sleep.cpp` no longer reads `mode` or knows about specific screens.
- The bookmark-preview page-swap trick lives next to the rest of the
  bookmark-preview logic, where it belongs.
- New screens that need pre-sleep work (hypothetical: save form state on
  upload abort) just override `onSleep()`.

### Refinement: most guards in `idlePrefetchReader` are dead

The current function:

```cpp
void idlePrefetchReader() {
  static uint32_t lastIdlePrefetchMs = 0;
  if (mode != MODE_READER) return;              // (1)
  if (g_bookmarkUi.previewActive) return;       // (2)
  if (!g_reader.file) return;                   // (3)
  if (g_reader.eofReached) return;              // (4)
  uint32_t now = millis();
  if ((uint32_t)(now - lastIdlePrefetchMs) < 60) return;
  lastIdlePrefetchMs = now;
  ensureOffsetsUpTo(g_reader.pageIndex + READER_IDLE_PREFETCH_PAGES);
}
```

Why each guard exists:

| # | Guard | Still needed? |
|---|---|---|
| 1 | `mode != MODE_READER` | **Dead.** Only caller is `ReaderScreen::onIdleTick`, and the dispatcher only calls `onIdleTick` on the *current* screen. By construction, when this runs, we're on `ReaderScreen`. |
| 2 | `g_bookmarkUi.previewActive` | **Dead.** `previewActive` is set true only inside `BookmarkListScreen` when opening a preview, and cleared before transitioning out of `BookmarkPreviewScreen`. While on `ReaderScreen` it's always false. |
| 3 | `!g_reader.file` | **Real.** Belt-and-braces — if we entered ReaderScreen without a file (shouldn't happen but harmless to guard), do nothing. |
| 4 | `eofReached` | **Real.** Nothing to prefetch past EOF. |

Drop (1) and (2). They're leftover defensive checks from before the screen
dispatcher made them impossible.

### Mechanical changes

- **`src/state.h`** — delete `enum Mode { ... };` and `extern Mode mode;`.
- **`src/state.cpp`** — delete `Mode mode = MODE_LIBRARY;`.
- **`src/ui/screen.h`** — add `virtual void onSleep() { syncWakeState(false); }`. Note: needs `#include "storage/book_state.h"` for the syncWakeState forward, or move the default to a `.cpp`.
- **`src/ui/screens/reader_screen.{h,cpp}`** — add `onSleep()`.
- **`src/ui/screens/bookmark_preview_screen.{h,cpp}`** — add `onSleep()`.
- **`src/ui/screens/*.cpp`** — remove `mode = MODE_X;` from every `onEnter()`.
- **`src/ui/sleep.cpp`** — replace the body of `goToSleep()` as above.
- **`src/ui/reader.cpp`** — remove guards (1) and (2) from `idlePrefetchReader`.
- **`src/web/web.cpp`** — already migrated off `mode` reads in phase 3 (transitions go through `g_currentScreen->nextScreen`). Verify no stale reads remain.
- **`src/main.cpp`** — the wake-from-deep-sleep path in `setup()` currently does `mode = MODE_READER;` before `renderCurrentPage()` then sets `g_currentScreen = &g_readerScreen`. Drop the `mode` write.

### Risk

Low. Build + smoke test on device, exercise: library → open book → sleep → wake → confirm reader resumes; bookmark → preview → sleep → wake → confirm reader resumes at *pre-preview* page (not the previewed bookmark).

### Estimate

Half a day with testing.

---

## Option 2 — Naming-convention sweep

**Status: done.** Smaller scope than originally drafted — see rationale below.

In phase 3 we did `ButtonState` (trailing-underscore on members + `g_`
prefix audit). Original plan was to carry the `_` suffix to every state
struct and prefix every bare global. On the second pass we narrowed:

### Final rule applied

- **`_` suffix on members of classes/structs that have methods.** Already
  the case in `OffsetCache`, `StringReadStream`, `PreferencesStore`,
  `FileReadStream`. Extended to `ButtonState` in phase 3. Nothing else
  needed: `ReaderState`, `LibraryState`, `BookmarkUiState`, etc. are
  pure records — every access is `g_X.member` from external code, never
  bare inside a method body, so the `g_X.` prefix already disambiguates.
  Renaming would have churned dozens of call sites for no clarity gain.
- **`g_` prefix on app-level globals**: `g_btns`, `g_lastUserActionMs`,
  `g_menuDrawsSinceFull`. Three additions.
- **Library handles stay bare** (`display`, `gfx`, `u8g2`, `prefs`,
  `server`). Idiomatic Arduino; cost of renaming exceeded the value.
- **`ALL_CAPS` constants stay** (`SCREEN_W`, `MAX_BOOKS`, `LONG_MS`,
  `AP_SSID`/`AP_PASS`). The convention already distinguishes them.
- **ISR ring buffer absorbed into a struct.** The five loose globals
  (`btnQHead`, `btnQTail`, `btnQState`, `btnQTimeMs`, `g_isrDropCount`)
  were conceptually one thing. They're now fields of a single
  `ButtonQueue g_btnQ`. Volatile semantics preserved per-field; ISR
  works unchanged (struct field offsets resolve at compile time, no
  flash fetch).

### Where the convention now lives

- `g_btns.poll()` — the input frontend.
- `g_btnQ.head`, `g_btnQ.tail`, `g_btnQ.state[]`, `g_btnQ.timeMs[]`,
  `g_btnQ.isrDropCount` — the ISR ring buffer.
- `g_lastUserActionMs`, `g_menuDrawsSinceFull` — sleep + full-refresh
  counters.

Net code change was small — ~3 globals renamed + 1 struct introduced.
Built clean, no host-test impact (tests don't reference firmware globals).

---

## Option 3 — Web HTML extraction

**Status: sketched.**

`src/web/web.cpp` is 1179 lines, ~700 of which are inline HTML string
literals. The single file is hard to navigate when you only want to fix
one route.

Two roads:

### 3a. Per-route split (recommended first move)

Each route handler → its own file under `src/web/routes/`. `web.cpp`
shrinks to: shared helpers (`htmlEscape`, `humanBytes`, `webUiStyle`),
shared page-shell builders, and `registerWebRoutes()`. Each route file
owns its HTML.

```
src/web/
  web.cpp                shared helpers + route registration
  web.h
  routes/
    files.cpp
    upload.cpp
    settings.cpp
    bookmarks.cpp
    list.cpp
    reset.cpp
    ...
```

Mechanical, no runtime change.

### 3b. Templates on LittleFS (follow-up if 3a feels good)

HTML moves to `/www/*.html` files on the filesystem; routes load + render
them with a tiny interpolation pass. Lets you edit UI without reflashing.
Costs flash for a fallback copy in firmware and adds template-loader
logic. Useful but bigger.

### Risk

Medium. Inline HTML in C string concatenation is fiddly — easy to drop a
closing `</div>` during a move. Smoke-test every route after.

### Estimate

2–3 days for the split alone.

---

## Option 4 — Move screen-local state into screens

**Status: sketched.**

`g_upload` moved into `UploadScreen` in phase 3. Same template applies to:

- `g_library.selectedItem` → `LibraryScreen` (only LibraryScreen reads/writes it).
- `g_list.selectedIndex` → `ListScreen` (only ListScreen reads/writes it).
- `g_bookmarkUi` → trickier — three screens share it. Needs a
  `BookmarkSession` struct created by `BookmarkBookSelectScreen` on
  entry, owned/destroyed there, referenced by `BookmarkListScreen` and
  `BookmarkPreviewScreen` while active.

Best done *after* option 1: with `mode` gone, the externally-facing
surface of these globals shrinks, making encapsulation cleaner.

### Risk

Medium. Bookmark-session lifetime needs care — destroying the session on
exit must not lose the user's place if they're just navigating between
the three bookmark screens.

### Estimate

2 days.

---

## Option 5 — `Canvas` display abstraction + SDL host UI

**Status: sketched (and ambitious).**

Wrap `display + u8g2 + gfx` behind one `Canvas`-style interface; provide
a real implementation for firmware and an SDL implementation for the
host build. Suddenly screens can render to a 250×122 window on your
laptop, no flash cycle.

Transformative for UI iteration speed. Also opens the door to host-side
*UI* tests (current host tests cover pure modules only). But it's a
significant refactor — every drawing call has to go through the
abstraction, including the e-ink-specific fastmode/full-refresh logic.

Worth it iff you're iterating on UI a lot. Possibly skip-able otherwise.

### Risk

High. Touches every screen + `reader.cpp` + `widgets.cpp` + `sleep.cpp`.

### Estimate

A week+ for the firmware-equivalent SDL implementation; longer if you
actually want to drive the host UI with synthesized button events for
automated tests.

---

## Recommended order

1. ~~Option 1 (remove `mode`).~~ **Done.**
2. ~~Option 2 (naming sweep).~~ **Done.**
3. **Option 3a** (per-route web split). Biggest practical win remaining
   on the code shape; independent of the others.
4. **Option 4** (state encapsulation). Easier now that mode is gone and
   naming is consistent.
5. **Option 5** (Canvas + SDL). Only if UI iteration speed becomes a
   bottleneck.

Phase 5 candidates not yet investigated:

- Long-press snappy emit (commit `longClick_` on hold-elapsed, not on
  release — TODO already in [`state.cpp`](../src/state.cpp)).
- Delete `ButtonState::anyClick()` once nobody calls it (currently dead).
- Persistent settings versioning (right now adding a new `cfg_*` key is
  safe, but renaming/restructuring would need a migration step).
