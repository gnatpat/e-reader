# Pala One — codebase guide

> A working document for anyone (human or fresh Claude session) coming to this
> project cold. Pairs with [PHASE3_PLAN.md](PHASE3_PLAN.md) (the most recent
> refactor) and [test/README.md](../test/README.md) (the host-test setup).

## Contents

1. [What this is](#1-what-this-is)
2. [The big picture in 30 seconds](#2-the-big-picture-in-30-seconds)
3. [Build and test](#3-build-and-test)
4. [File layout — where things live](#4-file-layout--where-things-live)
5. [Key concepts](#5-key-concepts) — [Screen interface](#51-the-screen-interface-the-new-core) ·
   [button input](#52-the-button-input-frontend) ·
   [pagination + offset caches](#53-pagination--the-three-offset-caches) ·
   [storage](#54-the-storage-stack) ·
   [display](#55-display--rendering) ·
   [battery](#56-battery-monitoring) ·
   [deep sleep](#57-deep-sleep--wake) ·
   [web mode](#58-web-mode-upload--manage) ·
   [`pure/`](#59-the-pure-module--and-why-it-matters)
6. [Globals](#6-globals)
7. [End-to-end flows](#7-end-to-end-tracing-a-few-flows) — boot · short-click · open book · sleep+wake · upload
8. [Things that surprise people](#8-things-that-surprise-people)
9. [What gets persisted, and where](#9-what-gets-persisted-and-where)
10. [Roadmap pointers](#10-roadmap-pointers)
11. [Where to start reading the code](#11-where-to-start-reading-the-code)

---

## 1. What this is

Pala One is a DIY e-reader. The hardware is a **Heltec Wireless Paper** — an
ESP32-S3 module with a 250×122 black-and-white e-ink panel. The firmware
turns it into a self-contained text reader that:

- reads UTF-8 `.txt` files from on-board LittleFS,
- paginates them on the fly with word-wrapped typography,
- remembers your place per book, supports per-book bookmarks,
- exposes a Wi-Fi AP + web UI for getting books onto the device,
- runs from a small LiPo, with deep-sleep + a single hardware button driving
  every interaction (clicks, double-clicks, triple-clicks, long-press).

It is a single firmware (`src/`) plus a parallel **host-test build**
(`test/`) that compiles a subset of the same source on the developer's
laptop and runs unit tests without needing the device.

---

## 2. The big picture in 30 seconds

```
                ┌─────────────────────────────────────┐
                │ main.cpp — setup() + loop()         │
                │   poll button, dispatch to screen   │
                └────────────────┬────────────────────┘
                                 │ Screen* g_currentScreen
                ┌────────────────▼────────────────────┐
                │ src/ui/screens/  (8 screens)        │
                │   library, reader, upload, list,    │
                │   about, bm_book_select, bm_list,   │
                │   bm_preview                        │
                └─┬──────────┬────────┬───────────────┘
                  │          │        │
   ┌──────────────▼──┐  ┌────▼─────┐  ▼  ┌────────────────┐
   │ src/hal/        │  │ src/ui/  │     │ src/storage/   │
   │  display        │  │  reader  │     │  library       │
   │  battery        │  │  text    │     │  bookmarks     │
   │  file_stream    │  │  widgets │     │  page_cache    │
   │                 │  │  sleep   │     │  list_items    │
   └──────────────┬──┘  └────┬─────┘     │  book_state    │
                  │          │           │  fs_util       │
                  │          │           │  kv_store      │
                  ▼          ▼           │  settings_store│
        ┌──────────────────────────┐     └────────┬───────┘
        │ src/pure/                │              │
        │  paginator               │◀─────────────┘
        │  text_util / hashing     │
        │  paths / offset_cache    │
        │  bookmarks_codec         │
        │  list_codec / bookmark_label
        │  stream / arduino_compat │
        └──────────────────────────┘
                                 ▲
                                 │ host-compiled (no ESP32)
                                 │
                  ┌──────────────┴──────────────────────┐
                  │ test/ — host-side unit tests        │
                  └─────────────────────────────────────┘

  src/web/web.cpp ⤴  attaches its routes to the global WebServer at boot;
                     only `.begin()`s when UploadScreen is active.
```

The arrows point in the direction of dependency. **`pure/` depends on
nothing** but its own `arduino_compat` shim, which is why it's testable on
a laptop. Everything above it can pull in `pure/` freely. Nothing in
`pure/` ever calls back up.

---

## 3. Build and test

```bash
# Firmware (PlatformIO)
pio run                  # builds for env wireless-paper
pio run -t upload        # flashes the connected board

# Host tests (CMake)
cd test
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

`platformio.ini` sets `test_ignore = *`, so `pio test` is a no-op — host
tests are explicitly CMake-only. The two builds never overlap: source under
`pure/` and the host-compatible parts of `storage/` compile under both;
everything else is firmware-only and lives behind hardware-specific headers
(`Arduino.h`, `<WiFi.h>`, etc.). The bridge is `pure/arduino_compat.h`,
which provides a minimal `String` shim when `ARDUINO` is not defined.

Current numbers: **~970 KB flash, 36 % RAM, 74 host tests passing**.

---

## 4. File layout — where things live

| Directory | Role | Compiles where |
|---|---|---|
| [src/](../src/) (top-level) | `main.cpp`, `state.h`/`state.cpp`, `config.h` | firmware |
| [src/hal/](../src/hal/) | Display adapter, battery monitor, file-stream adapter | firmware |
| [src/ui/](../src/ui/) | Reader rendering, pagination glue, sleep flow, widget primitives | firmware |
| [src/ui/screens/](../src/ui/screens/) | The 8 `Screen` subclasses | firmware |
| [src/storage/](../src/storage/) | LittleFS + Preferences (NVS) + library scan | firmware + (some) host |
| [src/web/](../src/web/) | Web UI / upload mode | firmware |
| [src/pure/](../src/pure/) | Algorithms with no Arduino deps | firmware + host |
| [test/](../test/) | CMake host tests | host only |

`config.h` holds compile-time constants and pin definitions. `state.h`
declares every global; `state.cpp` defines them and contains the
button-poll state machine. Everything else either reads those globals or
operates on a passed-in subset.

---

## 5. Key concepts

### 5.1 The `Screen` interface (the new core)

Until phase 3 this was a `switch(mode)` over an enum with one
`handleModeX()` per case. After the refactor:

- [src/ui/screen.h](../src/ui/screen.h) defines a tiny abstract `Screen`
  interface (`onEnter`, `onButton`, `draw`, `onIdleTick`, plus a
  `Screen* nextScreen` request slot) and a `ButtonEvent` value type.
- Each of the 8 screens lives in its own `.h` + `.cpp` under
  [src/ui/screens/](../src/ui/screens/) and is statically allocated as a single
  global (`g_libraryScreen`, `g_readerScreen`, …) in
  [src/main.cpp](../src/main.cpp).
- [main.cpp](../src/main.cpp)'s `loop()` is now a generic dispatcher: poll the
  button, build a `ButtonEvent`, call `onButton` then `onIdleTick`, swap
  `g_currentScreen` if the screen requested a transition.

**Transitions are deferred.** A screen handling a button never swaps
itself out mid-method — it just sets `nextScreen = &g_someOtherScreen` and
returns. The dispatcher performs the swap at the top of the next loop
iteration and calls the new screen's `onEnter()` to handle setup + the
initial draw. This avoids re-entrant problems where a screen might
otherwise try to draw the next one inside its own `onButton`.

```
loop():
   ev = ButtonEvent::fromButtonState(btns)
   ...sleep check...
   g_currentScreen->onButton(ev)
   g_currentScreen->onIdleTick()
   if g_currentScreen->nextScreen:
        g_currentScreen = g_currentScreen->nextScreen
        g_currentScreen->nextScreen = nullptr    # dispatcher clears it
        g_currentScreen->onEnter()               # new screen sets up + draws
```

**Re-entering a screen keeps its state.** Bookmark navigation, library
cursor position, list selection — none of that is destroyed by leaving and
coming back. That's the natural consequence of static instances + no `new`.

**A few callers transition from outside a screen.** `web.cpp`'s settings
form sets `g_currentScreen->nextScreen = &g_readerScreen` to live-reflow on
font change; `reader.cpp`'s error fallbacks call
`navigateToLibraryRoot()` (a tiny helper exported by
[library_screen.cpp](../src/ui/screens/library_screen.cpp)). Same mechanism,
just reached through a global pointer.

### 5.2 The button input frontend

A single GPIO (`BTN`, pin 0) with `INPUT_PULLUP`, plus a `CHANGE` ISR.

There's a small ring buffer (`btnQ*` arrays in
[state.cpp](../src/state.cpp)) where the ISR queues raw `(state, time)`
edges. `ButtonState::poll()` (called once per `loop()`) drains the queue,
debounces with `DEBOUNCE_MS`, and classifies the result into one of:

- `shortClick`, `doubleClick`, `tripleClick`, `quadClick`, `longClick`.

Click-count classification has to **wait for a trailing gap** to
disambiguate "this is a single click" from "the user is about to press
again". Specifically:

- `shortClick` / `doubleClick` are emitted after `DOUBLE_MS` of silence
  past the most recent release.
- `tripleClick` is emitted after `TRIPLE_MS` of silence past the *first*
  release of the sequence.
- `quadClick` is emitted immediately on the 4th release (no wait — by
  then the user clearly meant four).
- `longClick` is emitted immediately on release if the press was held
  ≥ `LONG_MS`. It doesn't participate in the click-count machinery at all.

`ButtonEvent::fromButtonState` in [screen.h](../src/ui/screen.h) is the only
caller that needs to look at those booleans now; it collapses them to a
single `enum Kind { None, Short, Double, Triple, Quad, Long }` so each
screen can just `switch (e.kind)`.

### 5.3 Pagination + the three offset caches

A "page" is identified by its **byte offset** into the book file. Page
boundaries are not stored in the book itself — they're computed by the
paginator from the current font + line gap. The first time you read a
book they're discovered lazily as the reader walks forward; subsequent
reads use the persisted offset cache and the experience feels instant.

The wrapping algorithm itself lives in
[src/pure/paginator.cpp](../src/pure/paginator.cpp). It reads from an abstract
`IReadStream` (so it's testable in-memory), takes a `LayoutMetrics`
struct + a `measure(text) -> px` callback, and emits one line at a time
via an `onLine` callback. It handles word boundaries, breakable punctuation,
hard-breaks long tokens, and is UTF-8 safe.

The firmware glue is in [src/ui/text.cpp](../src/ui/text.cpp):
`readPageFromFile` adapts the `File` + `u8g2` to the paginator and either
draws the line or appends it to a buffer (used by the "view page" web
route).

Three offset caches sit in front of this:

1. **Per-book in-RAM array.** `ReaderState::pageOffsets[MAX_PAGES]` holds
   the start offset of every page in the currently open book.
   `ensureOffsetsUpTo(page)` extends it forward by walking pages until it
   reaches the target or EOF.
2. **Global LRU cache.** `OffsetCache` in
   [src/pure/offset_cache.h](../src/pure/offset_cache.h) is a fixed-size LRU
   keyed by `(book hash, page)`. Used when a book is closed and reopened
   (the per-book array is gone) or when seeking deep into a known book.
3. **On-disk persistent cache.** `savePageOffsetCacheForBook` writes
   `/pc_<bookHash>.bin` with a 12-byte header + the offset array. This is
   what makes "reopen a 200-page book and jump to page 180" instant after
   first read. The file is invalidated when its `magic`/`fileSize` header
   doesn't match the current book — that catches font-size changes and
   file replacement.

When font size or line gap changes, **all three caches are wiped together
in `invalidateAllPageCaches()`** ([page_cache.cpp](../src/storage/page_cache.cpp)).
Bookmark *page numbers* survive but bookmark *offsets* are set to
`0xFFFFFFFF` so they're recomputed on next view.

### 5.4 The storage stack

- [src/storage/fs_util.cpp](../src/storage/fs_util.cpp) — `fsBegin()` mounts
  LittleFS, formatting once on first boot if needed; helpers for free-space
  accounting and `mkdir -p`-style directory creation.
- [src/storage/library.cpp](../src/storage/library.cpp) — recursive scan of
  `/books/**.txt`, populates `g_library.books[]` / `folders[]`, then
  flattens the visible tree into `entryTypes[]` / `entryRefs[]` /
  `entryDepths[]` for the library screen to iterate.
- [src/storage/bookmarks.cpp](../src/storage/bookmarks.cpp),
  [src/storage/list_items.cpp](../src/storage/list_items.cpp) — load/save
  bookmark + todo-list byte blobs via a `KeyValueStore` interface
  ([kv_store.h](../src/storage/kv_store.h)). The firmware wraps the global
  `prefs` (Arduino `Preferences`, backed by NVS) in
  [preferences_store.h](../src/storage/preferences_store.h); the host tests
  pass in a `MapKvStore` (in [test/map_kv_store.h](../test/map_kv_store.h)),
  which is the entire point of the abstraction.
- [src/storage/book_state.cpp](../src/storage/book_state.cpp) — operations
  that touch *multiple* pieces of book/reader state at once (close, reset
  preview, reset navigation, sync wake state, rename metadata). These are
  the helpers that screens reach for when they need to be sure they're in a
  clean state.
- [src/storage/page_cache.cpp](../src/storage/page_cache.cpp) — the second
  and third offset caches described above.
- [src/storage/settings_store.cpp](../src/storage/settings_store.cpp) —
  loads `g_settings` (font size, sleep timeout, line gap, long-press
  action) from prefs with clamping.

**Preference-key conventions.** All per-book keys are prefixed
`b_<8hex of FNV-1a 32 of path>`:
- `b_<hash>_p`  → integer, last viewed page
- `b_<hash>_bm` → bytes, bookmark blob (v2 = 1 + N×6 bytes; legacy v1 = 1 + N×2)

Plus a few global keys: `cfg_font`, `cfg_sleep`, `cfg_lgap`, `wake_mode`,
`wake_path`, `list_v1`.

### 5.5 Display + rendering

The Heltec driver exposes a low-level pixel API. We layer **two** APIs on
top of it:

1. **`HeltecGFXAdapter`** ([display.h](../src/hal/display.h)) — an
   `Adafruit_GFX` subclass that flips the panel 180° (the physical
   orientation we want) and clips to screen bounds. Used for shapes
   (rect, line, bitmap).
2. **`U8G2_FOR_ADAFRUIT_GFX`** — text rendering, including the UTF-8 +
   variable-width fonts (Helvetica family at 4 sizes).

The two refresh modes — `fastmodeOn()` (partial, ghost-prone) and
`fastmodeOff()` (full, slow but clean) — are alternated by
`prepareMenuFrame()` / page render based on counters
(`menuDrawsSinceFull`, `g_reader.pageTurnsSinceFull`). Every Nth draw
forces a full refresh to clear ghosting.

`LayoutMetrics` (ascent / descent / lineH / maxWidth / maxLines) is
computed lazily by `getMetrics()` and cached in `g_metrics` /
`g_metricsValid`. **Anything that changes the font invalidates that
cache** (`applyFontSize`, the settings web route after a font change).
The paginator never sets fonts itself; whoever calls in is responsible
for matching the metrics it provides.

### 5.6 Battery monitoring

Heltec's design exposes battery voltage through a divider gated by a
control pin so the divider isn't always burning current. Hence
[battery.cpp](../src/hal/battery.cpp):

- pulls `BAT_ADC_CTRL` low only when sampling,
- takes 11 samples, drops the 2 high + 2 low (median-ish), averages 7,
- applies a calibration factor + an exponential filter,
- maps voltage to % via a 20-point open-circuit-voltage LUT,
- caches the result for `BAT_CACHE_MS` (3 min) so menu redraws don't
  re-read the ADC.

Low-battery state has 4-point hysteresis (`≤ 8 %` to enter, `≥ 12 %` to
exit) so the icon doesn't flicker.

### 5.7 Deep sleep + wake

The loop checks idle time every iteration; after `sleepSecs` of no button
activity it calls `goToSleep()` in [sleep.cpp](../src/ui/sleep.cpp). That:

1. Saves reading progress (force, ignoring the throttle).
2. Persists the in-RAM page-offset cache to disk.
3. Writes `wake_mode = (reading ? 1 : 0)` + `wake_path = <book path>` to
   NVS so the next boot can resume.
4. Closes the file handle, draws the sleep-screen XBM (custom user image
   from `/sleep.bin` if present, otherwise the bundled
   `pala_one_sleep_black_icon_v4`).
5. Drops Wi-Fi/BT, configures `ext0` wakeup on the button pin going low,
   calls `esp_deep_sleep_start()`.

Wake from deep sleep is **a full reset** — `setup()` runs from scratch.
At the end of setup it reads `wake_mode` and either opens the saved book
and goes straight to `ReaderScreen`, or falls through to `LibraryScreen`.

The button press that woke the device is dampened by `resetInputFrontend()`:
it waits for physical release, debounces, and advances the ISR-queue tail
past anything queued before release. This is what stops the wake press
from immediately turning a page.

### 5.8 Web mode (upload + manage)

[src/web/web.cpp](../src/web/web.cpp) is the largest single file (~1200 lines)
and is essentially a small framework: handlers + inline HTML templates.
All routes are attached to the global `WebServer` at boot via
`registerWebRoutes()`. The server is only `.begin()`ed when
`UploadScreen` is active, so there's no Wi-Fi cost during normal use.

Routes (HTTP method in parens):
- `/`, `/files` (GET) — index, file management
- `/upload` (POST, streamed) — book upload
- `/upload-sleep` (POST) — replace the sleep-screen image (must be 3904 bytes)
- `/del`, `/rmdir`, `/mkdir`, `/move` (POST) — file ops
- `/list`, `/list-clear-done` (GET/POST) — todo list editor
- `/bookmarks`, `/delbm`, `/viewbm`, `/exportbm` — bookmark management
- `/settings`, `/jumppage` — runtime settings + reader jump
- `/del-sleep` (GET) — delete the custom sleep image (revert to bundled icon)
- `/reset` (POST) — factory reset

**Upload streaming** trickles each chunk through `normalizeTypography` +
`compactText` (see [text_util.h](../src/pure/text_util.h)) so smart quotes,
NBSPs, etc. are normalized before they ever hit disk. The streamer holds
the trailing 4 bytes of each chunk in `bookPendingUtf8Tail` so a multi-byte
UTF-8 character is never split across chunk boundaries.

**Upload-screen state was the one global that phase 3 encapsulated.**
It now lives as `static UploadState s_state` inside `UploadScreen`,
accessed by the streaming handlers via `UploadScreen::state()`. See
[upload_screen.h](../src/ui/screens/upload_screen.h).

### 5.9 The `pure/` module — and why it matters

Everything under [src/pure/](../src/pure/) is written so it compiles on a
laptop with nothing more than a C++17 compiler. The trick is a small
[arduino_compat.h](../src/pure/arduino_compat.h) that:

- includes `<Arduino.h>` on-device,
- on the host, defines a minimal `String` class on top of `std::string`
  with the subset of methods we use (`substring`, `indexOf`, `replace`,
  `trim`, …).

Why bother? Because the algorithms that are hardest to debug on-device —
UTF-8 wrapping, typography normalization, byte-blob encoding/decoding,
LRU caching, path sanitization — are exactly the ones that benefit most
from fast, deterministic, host-side unit tests. The 74 tests in
[test/](../test/) take under a second to run and have caught real bugs
that would have been a nightmare to chase on the e-ink.

Same principle applies to two storage modules ([bookmarks.cpp](../src/storage/bookmarks.cpp),
[list_items.cpp](../src/storage/list_items.cpp)): the testable parts live
above an injected `KeyValueStore` and are also host-compiled, with
firmware-only glue gated behind `#ifdef ARDUINO`.

---

## 6. Globals

All globals are declared `extern` in [state.h](../src/state.h) and defined in
[state.cpp](../src/state.cpp). They are grouped here by what they're for,
not by declaration order.

**Hardware / drivers**
- `display`  — the Heltec e-ink driver
- `gfx`      — the `Adafruit_GFX` adapter wrapping `display`
- `u8g2`     — text renderer talking to `gfx`
- `prefs`    — Arduino `Preferences` (NVS namespace `"ereader"`)
- `server`   — the always-allocated `WebServer` (only `.begin()`d in upload mode)

**User-facing config**
- `g_settings` — `RuntimeSettings`: font size, sleep timeout, line gap, long-press action

**Library / reader content**
- `g_library`     — book list, folder list, expanded-folder flags, flattened entries for the menu
- `g_reader`      — currently open book: `File`, page index, offset array, save-throttle, EOF flag
- `g_bookmarkUi`  — bookmark navigation state (which book, which bookmark, preview mode)
- `g_list`        — todo / shopping list items
- `g_toast`       — short status message + expiry time
- `g_offsetCache` — global LRU `OffsetCache` instance

**Button + input**
- `btns`                                              — the debounced `ButtonState`
- `btnQHead`, `btnQTail`, `btnQState[]`, `btnQTimeMs[]`, `g_isrDropCount` — the ISR ring buffer

**Display metrics + refresh counters**
- `g_metrics`, `g_metricsValid` — cached `LayoutMetrics`
- `menuDrawsSinceFull` — full-refresh counter (menu side)
- (reader-side full-refresh counter lives inside `g_reader.pageTurnsSinceFull`)

**Power state**
- `g_battery`        — voltage / % cache
- `lastUserActionMs` — for sleep-timeout calculation

**Mode mirror (legacy)**
- `mode` — `Mode` enum, parallel mirror of "which screen am I on". Each
  screen's `onEnter()` sets it. Still read by `sleep.cpp` and the
  idle-prefetch guard in `reader.cpp`. Candidate for removal in phase 4
  once nothing outside the screen layer reads it.

**Screen layer (defined in [main.cpp](../src/main.cpp))**
- `g_libraryScreen`, `g_readerScreen`, `g_uploadScreen`, `g_aboutScreen`,
  `g_listScreen`, `g_bmBookSelectScreen`, `g_bmListScreen`,
  `g_bmPreviewScreen` — the 8 screen singletons
- `g_currentScreen` — points to whichever one is active

**Wi-Fi credentials**
- `AP_SSID[]` — set at boot to `"PALA-<6 hex of MAC>"`
- `AP_PASS`   — compile-time constant

**Upload session state (post-phase-3, NOT in state.cpp)**
- `UploadScreen::s_state` (private, accessed via `UploadScreen::state()`).
  This was `g_upload` until phase 3 moved it into the only screen that uses
  it.

---

## 7. End-to-end: tracing a few flows

### 7.1 Boot

```
setup()
  Serial.begin                     -- 115200 baud, mostly for development
  attachInterrupt(BTN, btnISR)
  u8g2.begin(gfx)
  invalidateMetrics(); getMetrics()
  resetOffsetCache()
  adcSetupOnce(); updateBatteryCached(force=true)
  display.clear() + fastmodeOff()
  fsBegin() + ensureBooksDir()
  build AP_SSID from chip MAC
  prefs.begin("ereader")
  loadSettings(); loadBooks(); registerWebRoutes()
  markUserActivity()
  if wake_mode == 1 and wake_path is a known book:
     openBookByIndex, mode=MODE_READER, renderCurrentPage,
     resetInputFrontend, g_currentScreen = &g_readerScreen
  else:
     g_currentScreen = &g_libraryScreen
     g_libraryScreen.onEnter()
     resetInputFrontend()
  setCpuFrequencyMhz(80)
```

The `setCpuFrequencyMhz` calls bracket the heavyweight boot work at 240
MHz, then drop to 80 MHz for normal operation. Upload mode bumps back to
240 (Wi-Fi needs it), then back to 80 on exit.

### 7.2 A short-click in the library

```
loop():
  btns.poll()                      drains ISR queue, decides shortClick
  ev = {Short}
  markUserActivity()                resets sleep deadline
  ...not asleep yet...
  g_currentScreen == &g_libraryScreen
  LibraryScreen::onButton({Short})
    g_library.selectedItem++  (wrap)
    draw()                          repaints library
  onIdleTick()                     no-op for library
  nextScreen == null  → no transition
```

### 7.3 Opening a book

```
LibraryScreen::onButton({Double})
  entryType == LIB_ENTRY_BOOK
  openBookByIndex(idx):
     safeCloseCurrentBook()
     FS.open(path)
     g_reader.{file,currentBookKey,currentBookPath} = ...
     loadPageOffsetCacheForBook(path, fileSize)
                                    ↳ loads /pc_<hash>.bin if valid
     g_reader.pageIndex = prefs.getInt("b_<hash>_p", 0)
     syncWakeState(reading=true)    persists wake state for next deep sleep
     ensureOffsetsUpTo(pageIndex + PREFETCH_AHEAD_PAGES)
  resetPreviewState()
  nextScreen = &g_readerScreen

(next loop iteration)
  swap g_currentScreen ← g_readerScreen
  g_readerScreen.onEnter():
     mode = MODE_READER
     renderCurrentPage()            paginate + draw the page
```

### 7.4 Idle → deep sleep → wake

```
loop():
  ...no button press for sleepAfterMs()...
  if mode != MODE_UPLOAD: goToSleep()
    saveProgressThrottled(force=true)
    savePageOffsetCacheForBook(...)
    syncWakeState(wasReading)        wake_mode=1, wake_path=current book
    safeCloseCurrentBook()
    drawSleepScreen()                XBM bitmap, full refresh
    WiFi off, BT off
    esp_sleep_enable_ext0_wakeup(BTN, LOW)
    esp_deep_sleep_start()
                                     <hard reset on wake>
setup() runs again from the top
  reads wake_mode/wake_path → resumes book if valid
```

### 7.5 Upload session

```
LibraryScreen::onButton  → entryType == LIB_ENTRY_UPLOAD
  nextScreen = &g_uploadScreen
(swap)
  UploadScreen::onEnter() = startSession():
     mode = MODE_UPLOAD
     setCpuFrequencyMhz(240)
     WiFi.softAP(AP_SSID, AP_PASS)
     prepareMenuFrame + draw the "Wi-Fi / Password / Open" panel
     server.begin()
loop() (subsequent):
  onIdleTick():
     server.handleClient()           drives all web routes
     if 15-min timeout: stopSessionToLibrary()
  onButton({Short or Triple}):
     stopSessionToLibrary():
        server.stop(); WiFi off; close any tmp upload files
        loadBooks()                       refreshes g_library
        resetInputFrontend()              swallows the exit-press
        setCpuFrequencyMhz(80)
        nextScreen = &g_libraryScreen
(swap)  LibraryScreen::onEnter() resets book state + redraws
```

---

## 8. Things that surprise people

- **`mode` is a mirror, not the source of truth** anymore. `g_currentScreen`
  is. The enum is still set by each `onEnter()` so legacy readers in
  `sleep.cpp` / `idlePrefetchReader` keep working until phase 4 removes
  them.
- **Re-entering a screen keeps state.** This is by design — library cursor,
  bookmark selection, etc. all survive a side trip. If you want a clean
  slate, the screen's `onEnter` (or a caller before transitioning) has to
  reset it explicitly. `LibraryScreen::onEnter` does this aggressively
  because returning to library should feel like "home".
- **Triple-click is screen-local.** There is no longer a global
  "triple-click goes home" branch in `loop()`. Each screen that wants it
  handles `ButtonEvent::Triple` itself. Most do; bookmark sub-screens
  intentionally use it for back-navigation instead.
- **Paginator does not own fonts.** It receives a `measure(text)`
  function pointer and `LayoutMetrics`. If the metrics don't match the
  font that `measure` is configured to use, pagination will be wrong.
  This is the source of every "the cache is now wrong" bug after a font
  change — always wipe `g_metrics`/`g_metricsValid` and the offset caches
  together.
- **Upload mode draws once.** `UploadScreen::draw()` is a no-op; the
  screen is painted entirely from `startSession()` and then stays static.
  Don't add anything to `draw()` expecting periodic updates.
- **The button ISR is rate-limited only by hardware.** A storm of edges
  can fill the queue. `g_isrDropCount` is incremented when the queue
  overruns; the top of `loop()` watches that counter and force-resets the
  button state if it climbs past a threshold. This is mostly defensive
  against switch bounce on flaky boards.
- **Wake from deep sleep is a fresh `setup()`.** There is no persistence
  beyond what we wrote to NVS / LittleFS before sleeping. Anything you
  see "still there" after wake — the open book, the page number — was
  reconstructed in `setup()` from prefs.
- **`g_bookmarkUi` is shared across 3 screens.** The plan flags this as a
  phase-4 candidate to encapsulate; for now it's a documented coupling.
- **`web.cpp` is a single 1200-line file.** Most of that is inline HTML
  for the route responses. Splitting that out is also a phase-4
  candidate.

---

## 9. What gets persisted, and where

| Where | What | Format |
|---|---|---|
| `/books/**.txt` | The actual book content | UTF-8 text, normalized at upload time |
| `/pc_<hash>.bin` | Per-book page-offset cache | 12-byte header (magic + fileSize + count) then N × 4-byte offsets |
| `/sleep.bin` | Optional custom sleep-screen image | Exactly 3904 bytes (250×122 monochrome XBM) |
| NVS `b_<hash>_p` | Last viewed page | int |
| NVS `b_<hash>_bm` | Bookmark list | byte blob, v2 = `1 + N×6`, v1 legacy `1 + N×2` (auto-migrated on next save) |
| NVS `cfg_*` | Settings | `cfg_font`, `cfg_sleep`, `cfg_lgap` ints |
| NVS `wake_mode` / `wake_path` | Resume state for deep sleep | int + string |
| NVS `list_v1` | Todo/shopping list | byte blob, `1 + N × (1 + MAX_LIST_TEXT + 1)` |

`<hash>` is the 8-hex-digit FNV-1a 32-bit hash of the book's absolute
path. The hash provides namespacing without exposing path bytes inside
NVS keys (NVS key length is limited to 15 chars).

---

## 10. Roadmap pointers

- [PHASE3_PLAN.md](PHASE3_PLAN.md) — the just-completed `Screen` refactor.
  Phase 4 candidates are listed at the bottom of that file: display
  abstraction, web HTML extraction, encapsulating `g_bookmarkUi`, removing
  the `mode` enum.
- [test/README.md](../test/README.md) — how to add a host test for new
  pure / KV-store-backed code.

---

## 11. Where to start reading the code

Depending on what you're trying to learn:

| Goal | Start here |
|---|---|
| How does the main loop work? | [src/main.cpp](../src/main.cpp) — 150 lines, top to bottom |
| What is a screen? | [src/ui/screen.h](../src/ui/screen.h) + any one of the simpler ones, e.g. [about_screen.cpp](../src/ui/screens/about_screen.cpp) |
| How does the library menu get built? | [src/storage/library.cpp](../src/storage/library.cpp), then [library_screen.cpp](../src/ui/screens/library_screen.cpp) |
| How does pagination work? | [src/pure/paginator.cpp](../src/pure/paginator.cpp) (pure algorithm) → [src/ui/text.cpp](../src/ui/text.cpp) (firmware glue) |
| How is button input handled? | [src/state.cpp](../src/state.cpp) `ButtonState::poll()` + the ISR right above it |
| How does the reader save progress? | [src/storage/bookmarks.cpp](../src/storage/bookmarks.cpp) `saveProgressThrottled` |
| How does deep sleep work? | [src/ui/sleep.cpp](../src/ui/sleep.cpp) — `goToSleep()` |
| How does the web UI hang together? | [src/web/web.cpp](../src/web/web.cpp) — start at `registerWebRoutes()` at the bottom and follow whichever route interests you |
| How are bookmarks encoded on disk? | [src/pure/bookmarks_codec.cpp](../src/pure/bookmarks_codec.cpp) + [test/test_bookmarks_codec.cpp](../test/test_bookmarks_codec.cpp) for examples of every shape |
| How do host tests work? | [test/test_framework.h](../test/test_framework.h) + any `test_*.cpp` |
