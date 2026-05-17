# Apps layer — design

> Design for porting the position-independent app loader from
> `old/Pala_One_2_1.ino` into the rewrite. Pairs with the public
> headers in [`apps/include/`](../apps/include/).
>
> Audience: someone about to implement this. Assumes familiarity with
> [ARCHITECTURE.md](ARCHITECTURE.md) and [REFACTOR_NOTES.md](REFACTOR_NOTES.md).
>
> If you came here looking for the *app-author* guide (how to write +
> upload an app, the PalaAPI surface, compiler flags), see
> [APPS.md](APPS.md) — this file is for firmware developers.

## 1. What this layer does

Apps are **self-contained position-independent C binaries** uploaded over Wi-Fi
to `/apps/*.bin` and launched from a new screen. Each binary is:

1. Read into a `MALLOC_CAP_EXEC` heap buffer.
2. Validated by header (`magic`, `api_version`, sane offsets).
3. Relocated by walking a small `R_XTENSA_RELATIVE` table that the build's
   Python post-step embedded in the binary.
4. Called at `entry_offset` with a pointer to a `PalaAPI` struct of function
   pointers wrapping firmware services (display, input, RTC, storage, …).
5. Freed when `app_main` returns.

The contract between firmware and apps lives in two headers
(`pala_app.h`, `pala_api.h`). **Field order is frozen** — extending the API
means appending fields and bumping `PALA_API_VERSION`. App binaries already
in the wild assume v3 layout.

## 2. Why this needs design, not just copy-paste

The old code put everything on one global (`g_apps`, `g_palaAPI`,
`g_appExecBuf`, `g_appExecSize`, plus a `MODE_APPS` branch in the dispatcher)
in one file. The rewrite has:

- a layered tree (`pure/` → `storage/` → `hal/` → `ui/` → `web/`),
- `Screen`-object dispatcher instead of `Mode` enum,
- input/display already split into `hal/`, with per-screen state private
  to each screen,
- a strict invariant that `g_reader` is empty outside `ReaderScreen`.

A verbatim drop-in would create exactly the kind of cross-layer god-globals
that #1, #2, and #5 of [REFACTOR_NOTES](REFACTOR_NOTES.md) just deleted. The
design below splits the old monolithic chunk along the same fault lines as
the existing code.

## 3. File-by-file shape

```
apps/include/                    ← stable include path for external app builds
  pala_app.h                       (copy of old/pala_app.h)
  pala_api.h                       (copy of old/pala_api.h)

src/pure/
  app_header.{h,cpp}             ← validate PalaAppHeader against a buffer
                                   (magic, api_version, offset ranges).
                                   Pure POD, host-testable.

src/storage/
  app_catalog.{h,cpp}            ← scan /apps/*.bin, read header, build a
                                   Catalog-shaped struct. Parallels
                                   storage/library.cpp.

src/hal/
  app_loader.{h,cpp}             ← the platform-specific half: exec-buf
                                   allocation, IRAM↔DRAM mapping, reloc
                                   walk, entry call, teardown.
  input.{h,cpp}                  ← add a small public surface (see §6.2).

src/ui/
  pala_api_impl.{h,cpp}          ← the 15 api_* wrappers + initPalaAPI().
                                   Lives in ui/ because most wrappers
                                   call into ui/widgets / hal/display.
  screens/apps_screen.{h,cpp}    ← the AppsScreen. Modeled on ListScreen /
                                   bookmark_list_screen.

src/web/
  apps_upload.{h,cpp}            ← /upload-app route. Streaming receiver,
                                   magic-check before commit, .bin landing
                                   in /apps/.
```

**Roughly 6 new modules.** No existing file gets bigger than +5 lines except
`library_screen.cpp` (one new system-entry row) and `main.cpp` (one
`registerAppUploadRoutes()` call).

## 4. The four interesting decisions

### 4.1 Where do the public headers live?

External apps include them via paths like `../../Pala_One_2_1/pala_app.h`
today. That path is **already shipped** with built `.bin`s' build environment —
the README in the repo tells external authors to point there. Two options:

| Option | Pro | Con |
|---|---|---|
| Keep them under firmware tree (e.g. `src/apps_include/`) | One source of truth, no duplication | Forces external builds to point inside `src/` which is awkward for a "public API" |
| Move them to a top-level `apps/include/` | Clear "this is the external contract", easy submodule extraction later | Firmware needs an `-I apps/include/` to include them too |

**Recommendation: `apps/include/`.** The headers describe a versioned contract
with external builds — they're not internal firmware headers. The
`-I apps/include/` in `platformio.ini` is one line.

Update the README's path examples in the same PR; the path change is silent
to already-built `.bin` files (their header layout is what matters, not the
include path used when they were compiled).

### 4.2 What runs during `app_main`?

The old code calls `entry(&g_palaAPI)` *directly* — `app_main` blocks the
main loop forever, draws to the framebuffer directly, polls input via
`api_pollEvent` / `api_waitForEvent`. The rewrite's `Screen` interface
expects a `draw()` / `onButton()` shape where the dispatcher stays in
control.

**Recommendation: keep the old blocking model.** Apps are intentionally
hostile to the screen abstraction (the whole point is "user code runs"),
and trying to bridge it would force apps to expose a coroutine-shaped main
that nobody wants to write. Concretely:

```cpp
class AppsScreen : public Screen {
  void onButton(const ButtonEvent& e) override {
    if (e.kind == ButtonEvent::Double && hasSelection()) {
      runApp(s_apps.entries[s_cursor].path);  // ← blocks here
      draw();                                  // repaint our menu on return
    }
    // single → advance cursor, etc.
  }
};
```

`runApp` is the only call site of `hal::app_loader::load_and_run`. While it's
executing, `g_currentScreen` is still `AppsScreen` (we never reassigned it),
so when control returns, the main loop continues dispatching to us normally.
This means apps see a stable model and the dispatcher's contract is
unchanged for everything else.

**Sleep interaction.** While an app runs, the main loop's idle-timer check
isn't running — but the API exposes `markUserActivity()` indirectly: every
`waitForEvent` / `pollEvent` / `pendingPresses` call refreshes the timer
(matches old code). So when the app returns, the deadline is fresh. An app
that *never* polls input genuinely never gets idle-slept; that matches the
old behavior and is fine — apps are short-lived enough that nobody hit it.

`AppsScreen::allowSleep()` returns true (default). If a future app wanted
"keep awake while running" semantics during long computation, the API would
need a new entry — out of scope for v1.

### 4.3 Where does the `PalaAPI` shim live?

The 15 `api_*` functions are *almost* all one-liners that delegate to:

- `hal/display.cpp` (`clearScreen`, `drawHeader`, `drawTextAt`, `drawCenteredLarge`, `refreshDisplay`)
- `hal/input.cpp` (`waitForEvent`, `pollEvent`, `buttonPressed`, `pendingPresses`)
- `Arduino`/IDF (`millisNow`, `delayMs`, `rtcSeconds`, `snprintf_wrap`)
- LittleFS (`storageRead`, `storageWrite`)

They cut across two layers, so they don't naturally live in *one* existing
module. **Put them in `ui/pala_api_impl.cpp`** — the ui layer is the one
allowed to call into all of `hal/`, `storage/`, and `pure/`. The file is
~120 lines of mechanical wrappers + a 20-line `initPalaAPI()` that fills
in the function pointers. `g_palaAPI` is a `static PalaAPI` inside that
TU; nothing else needs to see it.

**The freezing rule:** there must be exactly one place where `PalaAPI`
field assignment happens, and the assignments must be ordered the same as
the struct declaration. Add a comment at the top of `initPalaAPI()`:

```cpp
// Field order is frozen — assignments below MUST match pala_api.h struct
// order. Adding a field = append here AND in pala_api.h AND bump
// PALA_API_VERSION in pala_app.h.
```

### 4.4 What does `hal/input.h` need to expose?

The old API uses three things the rewrite hasn't surfaced publicly yet:

| Old call | What it touches | New surface |
|---|---|---|
| `api_buttonPressed` → `btns.stablePressed` | `ButtonState::stablePressed_` (now private) | Add `bool g_btns.isPressed()` accessor |
| `api_pendingPresses` → `btns.rawPressCount` | `rawPressCount` (not in rewrite at all yet) | Add field + clear-on-read accessor `uint32_t consumePressCount()` |
| `api_waitForEvent` blocks polling `btns.poll()` until a click flag fires | Loop calling `poll()`+`fromButtonState()` | Add a blocking helper `ButtonEvent waitForNextEvent()` in `hal/input.cpp` |

These are the only `hal/input` changes. They're additive — no existing
caller has to change. The `rawPressCount` field needs to be incremented in
the click-classifier whenever a release is accepted (one line in
`ButtonState::poll`).

## 5. Loader port — what changes from the old code

`old/Pala_One_2_1.ino:2773-2854` is the meat. The new
`hal/app_loader.cpp::load_and_run(const char* path, const PalaAPI* api)`
keeps the same shape:

1. Open file, validate size (header + 4 bytes minimum).
2. Reject files > `MAX_APP_BINARY` (48 KB today).
3. `heap_caps_malloc(MALLOC_CAP_EXEC | MALLOC_CAP_32BIT)`.
4. Map IRAM → DRAM via `MAP_IRAM_TO_DRAM` for reads/writes.
5. Read whole file into the data view.
6. **Header validation** → delegate to `pure::validate_app_header(buf, size)`,
   which is host-testable.
7. Walk the reloc table and add `base` to each offset's contents.
8. Call entry with `api`.
9. `heap_caps_free`, reset the input frontend.

Diffs from the old version:

- Error reporting: old code calls `drawCenter("App not found", path); delay(1500)`
  inline. New version takes a small `LoadResult` enum and lets `AppsScreen`
  paint the error string itself. This is the only place `app_loader` would
  otherwise need to know about `ui/widgets`.
- `resetInputFrontend()` calls before *and* after — preserved, they're
  load-bearing for "the click that launched me must not also count as a
  click inside the app, and vice versa".
- The "validate header bytes" path runs *before* the malloc — small enough
  to read a sizeof(PalaAppHeader) chunk first, validate, then commit to
  the big malloc. Saves ~30 KB of allocation churn for malformed files.
  (Optional; skip if the simpler one-pass version is fine.)

## 6. Catalog port

`storage/app_catalog.cpp::loadApps()` mirrors `storage/library.cpp::loadBooks()`:

```cpp
struct AppEntry {
  char name[MAX_APP_NAME + 1];
  char path[MAX_APP_PATH + 1];
};

struct AppCatalog {
  AppEntry entries[MAX_APPS];
  int count = 0;
};

extern AppCatalog g_apps;
void loadApps();   // scan /apps/, read headers, populate g_apps
```

- Same iterator pattern as `scanBooksRecursive`.
- For each `.bin`: read first `sizeof(PalaAppHeader)` bytes, check magic,
  use `hdr.name` if valid, else fall back to filename stem.
- Skips `.dat` (storage files), `.tmp` (in-flight uploads), `.disabled`,
  and anything starting with `_` (reserved).

Constants (`MAX_APPS=16`, `MAX_APP_NAME=32`, `MAX_APP_PATH=80`) move into
`config.h` alongside `MAX_FOLDERS`/`MAX_LIBRARY_ENTRIES`.

The selection cursor is **not** here — that's nav state, owned by
`AppsScreen` (matches the post-#5 split where catalogs are inventory and
cursors are screen-local).

## 7. AppsScreen behavior

Modeled on `BookmarkListScreen` — uses `drawScrollableList` from
`ui/widgets`, same row/click idioms as the rest.

| Gesture | Action |
|---|---|
| Short click | Cursor down, wrap at end |
| Double click | Launch selected app |
| Triple click | Back to library root |
| Long press | (Reserved — could be "delete app" later) |

> **Note:** an earlier draft had Long→launch and Double→reserved. Reversed
> during implementation to match every other rewrite screen
> (LibraryScreen, BookmarkListScreen, ListScreen all use Double as the
> activate gesture). Old monolithic firmware also used Double here.

Empty-state row: `"No apps installed"` selected/non-actionable.

Entry into AppsScreen: a new system entry on `LibraryScreen` (the post-#5
"system entries appended in given order" array). One line edit, same shape
as the existing `About`/`Upload` rows.

State (`static int s_cursor`) is local to `apps_screen.cpp`. It survives
across visits (entering apps doesn't reset the cursor), matching the
library's behavior.

## 8. Upload route

`web/apps_upload.cpp::registerAppUploadRoutes()`:

- `POST /upload-app` — multipart streaming, identical shape to the existing
  book upload handler.
- Filename sanitization: strip path components, force `.bin` extension,
  fall back to `app.bin` if empty.
- Write to `/apps/<name>.bin.tmp`, magic-check after `UPLOAD_FILE_END`,
  rename to final on success, delete `.tmp` on failure.
- After successful commit, call `loadApps()` so the menu reflects the new
  app immediately (no reboot needed). Matches how the book upload calls
  `loadBooks()`.

There's a small wrinkle: the existing `UploadState` is per-upload-type
(`bookOk`, `sleepOk`). Add `appOk` / `appError` / `appFinalName` /
`appTmpPath` / `appTmpFile` siblings. Not pretty, but the rewrite hasn't
generalized the upload-state struct yet — this is a small enough addition
to not justify generalization now.

## 9. Things deliberately NOT in v1

- **App `.dat` storage namespacing.** Old API gives apps `/apps/{key}.dat`.
  Two apps using the same key clobber each other. Could prefix by app
  binary basename. Skipped: matches old behavior, and the migration story
  would be ugly. Document the footgun in the README.
- **App-side font selection.** Old API exposes `drawTextAt(bold)` but not
  font size. Honoring `g_settings.fontSize` in the wrappers means apps
  silently change appearance when the reader's font changes — could be
  feature or bug. Leave matching old behavior (always `MAIN_FONT` /
  `BOLD_FONT`); add a `drawTextSized()` later if needed and bump API.
- **Reading from /apps/.dat files outside `/apps/`.** `storageRead`/`Write`
  hard-codes `/apps/`. Means apps can't, e.g., read user books. Intentional —
  isolation.
- **Concurrent apps / nested apps.** Single global exec buffer; `runApp`
  is a leaf call.
- **Per-app uninstall in the UI.** Use the `/delete` web route on
  `/apps/<name>.bin` for now.
- **Verification (signatures, hashes).** Magic check only. The threat model
  here is "the user uploaded a corrupt file", not "untrusted code".

## 10. Suggested implementation order

Each step compiles + runs standalone; tests added as the host-testable
pieces land.

| # | Step | Status |
|---|---|---|
| 1 | Drop `pala_app.h` / `pala_api.h` into `apps/include/`. Wire `-I apps/include` into `platformio.ini`. | **Done** |
| 2 | Write `pure/app_header.{h,cpp}` + tests (`test/test_app_header.cpp` — valid header round-trip, bad magic, bad api_version, offset-out-of-range cases). | **Done** |
| 3 | Write `storage/app_catalog.{h,cpp}`. No screen wiring yet; expose via a quick `Serial.print` from setup() to sanity-check. | **Done** |
| 4 | Surface the three new `hal/input` accessors. Run the existing reader to confirm nothing regressed. | **Done** |
| 5 | Write `ui/pala_api_impl.{h,cpp}`. Stub `runApp()` that just prints "would run X" — confirms wiring without dealing with the loader yet. | **Done** |
| 6 | Write `hal/app_loader.{h,cpp}`. Now `runApp` is real. Test with one of the existing example .bin files (e.g. `click_counter.bin`). | **Done** (on-device verification pending) |
| 7 | Write `ui/screens/apps_screen.{h,cpp}` + library-screen system entry. Apps are now reachable from the UI. | **Done** |
| 8 | Write `web/apps_upload.{h,cpp}` + register route in main. End-to-end working. | **Done** (on-device verification pending) |

Rough budget: **~2 focused days**, with steps 6 + 7 being the bulk of
the time.

### Stage 1 notes

- Headers landed at [apps/include/pala_app.h](../apps/include/pala_app.h)
  and [apps/include/pala_api.h](../apps/include/pala_api.h) — verbatim
  copies of `old/pala_app.h` / `old/pala_api.h`.
- `platformio.ini` got `-I apps/include` in both env build_flags
  (v1_1 and v1_2). No firmware code includes them yet — that arrives
  with Stage 2.
- The old `old/Pala_One_2_1` directory the README mentions as the
  external-app include path is now superseded by `apps/include/`. The
  README needs an update when Stage 8 lands.

### Stage 2 notes

- [src/pure/app_header.h](../src/pure/app_header.h) /
  [.cpp](../src/pure/app_header.cpp) implement two pure validators:
  - `validateAppHeader(buf, fileSize, outVersion?)` — checks size, magic,
    api version, entry offset range, reloc table range.
  - `validateRelocEntries(buf, fileSize)` — walks the table and verifies
    every entry targets a 4-byte slot strictly before the table itself.
- Status enum returns specific failure codes so the loader can show
  precise error strings ("API v3, need v4") instead of just "bad app".
- Reads everything through `memcpy` — `buf` is not assumed aligned.
- The reloc-table size check is overflow-safe
  (`reloc_count > (fileSize - reloc_offset) / 4`); the old inline code
  used `reloc_offset + reloc_count * 4u > fileSize` which would wrap on
  a huge `reloc_count`. Tightened on the way through.
- 12 new tests in [test/test_app_header.cpp](../test/test_app_header.cpp):
  minimal-valid round trip, too-small, bad magic, api mismatch +
  reported version, entry inside header, entry at fileSize, count==0
  ignores offset, table overflow, offset inside header, empty reloc
  table, reloc targeting before table, reloc self-referential into table.
  All pass on Windows / MSVC.
- Test CMake adds `apps/include` to the include path so `pala_app.h`
  resolves from a host build.
- Nothing wired into firmware yet — header is host-only until Stage 6
  consumes it from `hal/app_loader`.

### Stage 3 notes

- New module: [src/storage/app_catalog.h](../src/storage/app_catalog.h) /
  [.cpp](../src/storage/app_catalog.cpp). `AppCatalog g_apps` is the
  global; `loadApps()` (re)populates it from `/apps/*.bin`. Mirrors
  `storage/library.{h,cpp}` but simpler — no folders, no derived view.
- Constants `MAX_APPS=16`, `MAX_APP_NAME=32`, `MAX_APP_PATH=80` moved
  into [config.h](../src/config.h) alongside the other catalog ceilings.
- Display name: prefer `PalaAppHeader.name` (when magic matches), fall
  back to the filename stem with `_` → ` `. Means a partially-corrupt
  binary still appears in the menu — the loader's error path takes
  over when the user opens it, rather than the file being silently
  invisible.
- Defensive details:
  - `hdr.name` is 32 bytes and not guaranteed NUL-terminated; we clamp
    via a 33-byte temp before copying.
  - `f.name()` on this LittleFS port returns the basename; we
    re-prepend `/apps/` so the loader gets a path it can reopen.
  - Path longer than `MAX_APP_PATH` → skip rather than truncate (a
    truncated path would point at a different file or none).
  - Only `.bin` files; `.dat` / `.tmp` siblings are ignored.
- `/apps/` is auto-created on first boot if missing.
- Sorted by name on every scan, matching `loadBooks()` semantics.
- Wiring: [src/main.cpp](../src/main.cpp) now calls `loadApps()` in
  setup and `Serial.printf`s the discovered count + each entry. This
  is a **temporary sanity check** — to be deleted in Stage 7 when
  AppsScreen makes the catalog visible in the UI.
- Firmware compiles clean on `wireless-paper-v1_2`; host tests still
  pass (catalog file is firmware-only, not in the host SUT list).

### Stage 4 notes

Three additions to `hal/input` — all additive, nothing existing changed
externally:

- **`ButtonState::isPressed() const`** — debounced state accessor. Maps
  to `api_buttonPressed`. One-liner.
- **`ButtonState::rawPressCount_` field + `consumePressCount()`** — a
  free-running counter of accepted short press-releases. Bumped inside
  the classifier *before* `clickCount_++`, so an app sees every release
  even when the same one later ends up grouped into a double/triple.
  Long-press releases don't count (the classifier already routes them
  through the `dur >= LONG_MS` branch). Cleared on `resetState()`.
  Maps to `api_pendingPresses`.
- **Free function `ButtonEvent waitForNextEvent()`** in
  [hal/input.cpp](../src/hal/input.cpp) — tight `poll()` + 1ms-delay
  loop that returns the first non-`None` `ButtonEvent`. Marks user
  activity on entry and exit so the deep-sleep deadline is fresh when
  the app returns. Maps to `api_waitForEvent` — except the old code
  re-implemented hold-detection inline; the rewrite's classifier
  already fires `longClick_` while held, so this loop just watches
  the flags like any other consumer would.

All three are intentionally on the *outside* of the classifier — they
read what `poll()` produces; they don't reach into intermediate state
the way the old `api_waitForEvent` poked `pressArmed` / `pressStart`
directly.

Firmware builds clean (+32 bytes). Reader behavior unchanged
(verified by build only — there's no on-device smoke test in this
loop; user is expected to flash and exercise the reader once before
Stage 5 lands new entry points).

### Stage 5 notes

New module: [src/ui/pala_api_impl.h](../src/ui/pala_api_impl.h) /
[.cpp](../src/ui/pala_api_impl.cpp). Two exports:

- `initPalaAPI()` — populates a file-local `static PalaAPI s_palaAPI`
  with the 15 function pointers. The struct-order comment block at the
  top of `initPalaAPI` is the canonical "field order is frozen, ABI v3"
  reminder.
- `runApp(const char* path)` — **stub for now**. Logs to Serial and
  reports the API table size + pointer. Stage 6 replaces the body with
  `hal::app_loader::loadAndRun(path, &s_palaAPI)`.

The 15 wrappers map straightforwardly to existing modules:

| API field | Delegates to |
|---|---|
| `clearScreen` | `prepareMenuFrame()` |
| `drawHeader` | `drawSectionHeader()` |
| `drawTextAt` | `Font::useBody/useBold` + `u8g2.setCursor/print` |
| `drawCenteredLarge` | new `Font::useAppLarge()` + manual centering |
| `refreshDisplay` | `display.update()` |
| `waitForEvent` | `waitForNextEvent()` from Stage 4 |
| `pollEvent` | `g_btns.poll() + ButtonEvent::fromButtonState` |
| `buttonPressed` | `g_btns.isPressed()` |
| `pendingPresses` | `g_btns.poll() + g_btns.consumePressCount()` |
| `millisNow` | `millis()` |
| `delayMs` | `delay()` |
| `rtcSeconds` | `esp_rtc_get_time_us() / 1000000` |
| `snprintf_wrap` | variadic `vsnprintf` wrapper |
| `storageRead` / `storageWrite` | `LittleFS` open `/apps/{key}.dat` |

Sub-decisions:

- **`Font::useAppLarge()`** added to [ui/font.h](../src/ui/font.h) +
  [.cpp](../src/ui/font.cpp) as a new role (Helvetica B14). Keeps the
  "no u8g2 font identifiers outside font.cpp" rule intact — apps don't
  participate in body-size selection, so they need their own role rather
  than reusing Body/Bold.
- **`esp_rtc_get_time_us`** — header `<esp_rtc_time.h>` from the old
  code is gone in current ESP-IDF; the function now lives in chip-
  specific `soc/<chip>/rtc.h`. Forward-declared the symbol locally
  instead of taking a chip-specific include path. Symbol always resolves
  at link time.
- **`ButtonEvent::Quad`** has no `PALA_*` equivalent in v3 — mapped to 0
  (treated as "no event"). The old code had the same gap. A future API
  bump could add `PALA_QUAD`.
- **Storage keys** are NOT sanitized (`/apps/{key}.dat` is built with
  `snprintf` straight from `key`). Preserves v3 behavior; the doc's
  §9 "deliberately NOT in v1" already calls this out.
- **`markUserActivity`** fires on every event-returning API call
  (`waitForEvent`, `pollEvent`, `pendingPresses`) so the deep-sleep
  deadline stays fresh while an app is running, matching the old code.

Wiring: [main.cpp](../src/main.cpp) now calls `initPalaAPI()` in setup
after `loadApps()`. The temporary Serial sanity probe from Stage 3 is
still there; both go away when Stage 7 lands AppsScreen.

Firmware builds clean (+~1.5 KB flash from the 15 wrappers + the new
font role). No runtime path exercises any of it yet — that's Stage 7.

### Stage 6 notes

The real loader. New module:
[src/hal/app_loader.h](../src/hal/app_loader.h) /
[.cpp](../src/hal/app_loader.cpp). Exports one function:

```cpp
LoadResult loadAndRunApp(const char* path, const PalaAPI* api,
                         uint32_t* outFileApiVersion = nullptr);
```

Flow (matches the doc §5 outline):

1. `FS.open(path)`, validate size against `[sizeof(header)+4, MAX_APP_BINARY]`.
2. `heap_caps_malloc(MALLOC_CAP_EXEC | MALLOC_CAP_32BIT)`.
3. Map IRAM → DRAM via `MAP_IRAM_TO_DRAM` for the read/relocate passes.
4. Read whole file into the DRAM view.
5. `validateAppHeader` → `LoadResult` (specific failure codes).
6. `validateRelocEntries` → `BadRelocEntry` on out-of-range.
7. Walk the table and add the DRAM base to each patched word.
8. `resetInputFrontend()` → call entry → `resetInputFrontend()`.
9. `heap_caps_free`.

Sub-decisions:

- **`MAX_APP_BINARY = 48 KB`** moved to [config.h](../src/config.h)
  alongside the other catalog ceilings. Same value the old monolithic
  firmware used.
- **`MAP_IRAM_TO_DRAM`** lives in `<soc/soc.h>` on this ESP-IDF — found
  by grepping the framework headers; the symbol resolves at link time.
- **Error reporting split**: `loadAndRunApp` returns a `LoadResult` enum
  and nothing else; it stays free of `ui/widgets`. `runApp` in
  `pala_api_impl.cpp` is the single switch statement that maps each
  failure to a `drawCenter("...","...") + delay(1500)` pair. This is
  the *ui* layer drawing the error, satisfying the layering rule
  ("hal can't include ui") without forcing AppsScreen to learn the
  `LoadResult` enum.
- **Reloc walk** dereferences `(uint32_t*)(dataBuf + off)` directly
  (matches old code). Safe because the allocator returns a 4-aligned
  buffer (`MALLOC_CAP_32BIT`) and the build's Python step lays the
  reloc table on a 4-byte boundary.
- **`resetInputFrontend()` bracketing** preserved: the launching click
  must not leak into the app's first frame; presses during the app must
  not leak back to AppsScreen.
- **`freeExecBuf()` is called on every exit path**, including the
  defensive top-of-function call so an OOM on a prior invocation can't
  leak across calls.

Firmware builds clean (+176 bytes). Cannot exercise on-device from
here — Stage 7 will surface a launch path; the real "does it load and
relocate a `.bin`" smoke test wants one of the example binaries
uploaded to `/apps/` and AppsScreen reachable to click into.

### Stage 7 notes

New screen: [src/ui/screens/apps_screen.h](../src/ui/screens/apps_screen.h)
/ [.cpp](../src/ui/screens/apps_screen.cpp). Cursor (`s_cursor`) lives
as a file-local static, same pattern as ListScreen's cursor — survives
across visits but doesn't leak through any global. On `onEnter()` we
re-scan via `loadApps()` so newly uploaded apps appear without a
reboot.

Empty state: `"No apps installed"` painted as a non-selected row when
`g_apps.count == 0`. Cursor is clamped on every entry.

Library wiring:

- **New enum value `LIB_ENTRY_APPS`** in
  [src/pure/library_nav.h](../src/pure/library_nav.h), placed between
  `LIB_ENTRY_LIST` and `LIB_ENTRY_ABOUT`. Shifts the numeric values of
  ABOUT and UPLOAD — host tests use the names, not the numbers, so all
  87 pass unchanged.
- **`LibraryScreen` updates** in [src/ui/screens/library_screen.cpp](../src/ui/screens/library_screen.cpp):
  added `LIB_ENTRY_APPS` to `isSystemEntryType`, `entryLabel` ("Apps"),
  the system-entries array (now sized 5), and the `onButton` dispatch
  switch (`nextScreen = &g_appsScreen`).
- **Screen instance** `g_appsScreen` added to
  [src/main.cpp](../src/main.cpp); include + extern come along with it.

Gesture deviation from doc: the original draft (§7 table) had Long→launch
/ Double→reserved. Flipped during implementation to match every other
rewrite screen's idiom (Double is the activate gesture). Doc table now
reflects the implementation.

Cleanup:

- Temporary Stage-3 `Serial.printf` probe deleted from `main.cpp`. The
  catalog is visible in the UI now; the print served its purpose.

Firmware builds clean (+~2 KB, mostly the new screen + small library
wiring growth). Host tests: 86 pass, 1 pre-existing `compactText`
failure — same as before, no new regressions from the enum change.

On-device verification still pending: upload a `.bin` to `/apps/`,
navigate to Apps from the library, double-click it. The loader's
real-binary smoke test happens here.

### Stage 8 notes

New module: [src/web/apps_upload.h](../src/web/apps_upload.h) /
[.cpp](../src/web/apps_upload.cpp). One route, `POST /upload-app`,
registered via `registerAppUploadRoutes()` from
[src/web/web.cpp](../src/web/web.cpp).

Streaming receiver mirrors the existing book/sleep upload handlers:
START allocates a temp file at `/apps/<sanitized>.bin.tmp`, WRITE
appends with a running size cap (`MAX_APP_BINARY`), END validates the
header bytes via `pure::validateAppHeader` and atomic-renames to the
canonical path on success. ABORTED cleans up the temp file.

Sub-decisions and details:

- **Header validation at install time** uses the *same* pure validator
  the on-device loader runs. Same magic / `api_version` / entry-offset
  / reloc-range checks; same error phrasing where it overlaps. Fail at
  install time, not at launch time — the browser shows the error while
  the user has the file in hand to fix.
- **Streaming size cap** rejects mid-stream once cumulative size would
  cross `MAX_APP_BINARY`, so a runaway POST can't fill the partition
  before END fires.
- **Free-space pre-check** at START is conservative — it reserves the
  full `MAX_APP_BINARY` even if the user uploads a 2 KB app. Trading a
  bit of false-positive "Not enough free space" for the simplicity of
  not having to know the file size upfront.
- **MAX_APPS guard** at START re-runs `loadApps()` defensively to
  protect against a stale catalog (matches what `handleUploadBookStream`
  does for books).
- **Filename sanitization**: new `sanitizeUploadedAppFilename` in
  [pure/paths.cpp](../src/pure/paths.cpp). Strips path components
  (both `/` and `\` — files uploaded from Windows browsers come in with
  backslashes), strips *all* extensions before re-adding `.bin` (so
  `foo.tar.bin` → `foo.bin`), collapses spaces to `_`, keeps `a-zA-Z0-9_-`,
  empty falls back to `app`. 9 new host tests covering the cases.
- **`UploadState` fields**: `appOk` / `appError` / `appFinalName` /
  `appTmpPath` / `appTmpFile` added as siblings to the book/sleep
  fields in [src/ui/screens/upload_screen.h](../src/ui/screens/upload_screen.h).
  Not pretty (the per-type duplication still wants a generalization)
  but matches the existing pattern and is scoped to a few lines.
- **Discoverability**: the home page (`/files` handler shows it on
  `/`) gained an "Install app" card next to the "Upload book" form.
  Both forms are now visible without digging through Settings or Files.

Firmware build: clean, +~5 KB flash (mostly the streaming handler +
the success page HTML strings). Host tests: 87 pass, 1 pre-existing
`compactText` failure — same baseline as before.

End-to-end verification path is now reachable from a browser:

1. Boot the device, triple-click into Upload mode.
2. Connect to `PALA-xxxxxx` Wi-Fi, open `http://192.168.4.1/`.
3. "Install app" card → pick a `.bin` from `old/apps/<example>/` →
   submit. Should land on the success page with the file size.
4. Exit upload mode (triple-click), navigate the library to "Apps",
   double-click the new entry. Loader runs.

Anything weird at step 3 → check `pure::app_header` test cases (the
validator runs there). Anything weird at step 4 → flip the loader's
serial logs and watch for the `LoadResult` codepath taken.

---

## End state

All 8 stages landed and the firmware compiles green. The apps layer
now lives inside the rewrite's layered shape: pure validation, storage
catalog, hal loader, ui shim + screen, web upload — each in its own
file, each callable in isolation.

See the per-stage notes above for the deviations from the original
design (gesture mapping flipped, error-rendering split between layers,
validator's overflow guard tightened). Nothing landed that wasn't
either in the doc or noted on the way.

The only thing genuinely pending is on-device wall-clock verification:
flash, upload a real `.bin`, run it. The doc has been the system of
record throughout; if something surprises during that test, this is
the first place to update.

## 11. Open questions worth asking before starting

- Should the `apps/include/` headers ship in a separate repo / git
  submodule for clean external versioning, or stay in-tree?
- Do we want a build-time check that `PALA_API_VERSION` in `pala_app.h`
  matches the number of fields in `PalaAPI` (static_assert on `sizeof`)?
  Catches "added field, forgot to bump version" at compile time.
- `MAX_APP_BINARY = 48 KB` is from the old code. With the rewrite's
  RAM layout (catalog + expansion names + caches), worth measuring free
  heap-caps-exec-32bit budget on a real device before locking in. Knob
  in `config.h`.
