# Building a Pala One App

Apps for the Pala One are self-contained position-independent C binaries
uploaded to the device over Wi-Fi. This guide covers everything from
prerequisites to uploading your first app.

> Implementation-side notes — how the firmware loads / validates apps,
> the module layout, design decisions — live in
> [APPS_LAYER.md](APPS_LAYER.md). This file is for *app authors*; that
> one is for *firmware developers*.

## Prerequisites

- **xtensa-esp32s3 toolchain** — installed automatically by the Arduino
  IDE when you add the Heltec ESP32 board package. On Linux/macOS,
  found under `~/.arduino15/packages/esp32/tools/esp-x32/<version>/bin/`;
  override `TOOLCHAIN` on the `make` command line if yours is elsewhere.
- **Python 3** — used by the post-build step that patches the binary
  header with the entry-point offset and the relocation table.
- **`make`** — standard GNU make.

## App Binary Format

Every app binary begins with a `PalaAppHeader` struct at byte offset 0.
The firmware reads this header to validate the app before it is loaded.

```c
typedef struct {
    uint32_t magic;         // Must be PALA_APP_MAGIC (0x50414C41, 'PALA')
    uint32_t api_version;   // Must equal PALA_API_VERSION in pala_app.h
    char     name[32];      // Display name shown in the app launcher
    uint32_t entry_offset;  // Byte offset to app_main() — patched by Makefile
    uint32_t reloc_offset;  // Byte offset to relocation table — patched by Makefile
    uint32_t reloc_count;   // Number of relocation entries — patched by Makefile
} PalaAppHeader;
```

Declare it in the `.header` section and initialise `entry_offset`,
`reloc_offset`, and `reloc_count` to 0 — the Makefile's post-link step
patches the real values:

```c
#include "pala_app.h"
#include "pala_api.h"

__attribute__((section(".header")))
const PalaAppHeader pala_header = {
    .magic        = PALA_APP_MAGIC,
    .api_version  = PALA_API_VERSION,
    .name         = "My App",
    .entry_offset = 0,
    .reloc_offset = 0,
    .reloc_count  = 0,
};
```

The header files live at [`apps/include/pala_app.h`](../apps/include/pala_app.h)
and [`apps/include/pala_api.h`](../apps/include/pala_api.h) in this
repo. Add `-I /path/to/this/repo/apps/include` to your app's `CFLAGS`
so the `#include` lines above resolve.

## Entry Point

Your app must define exactly one function with this signature:

```c
void app_main(const PalaAPI* api);
```

The firmware calls it after loading the binary and relocating it into
RAM. The `api` pointer gives access to all firmware services. When
`app_main` returns, the firmware returns to the app launcher.

## The PalaAPI (v3)

All interaction with the device goes through function pointers in
`PalaAPI`. Field order is frozen — new fields are always appended, never
inserted. Do not cache the pointer; use it directly.

| Function | Description |
|---|---|
| `clearScreen()` | Erase the frame buffer |
| `drawHeader(title)` | Draw the standard title bar at the top |
| `drawTextAt(x, y, text, bold)` | Draw text at pixel coordinates; `bold=1` for bold weight |
| `drawCenteredLarge(text)` | Draw large bold text horizontally centred |
| `refreshDisplay()` | Flush the frame buffer to the e-ink panel |
| `waitForEvent()` | Block until a button gesture arrives; returns an event code |
| `pollEvent()` | Non-blocking version; returns 0 if no event is ready |
| `buttonPressed()` | Returns 1 if the button is physically held down right now |
| `pendingPresses()` | Count of individual short press-release events since last call; bypasses multi-click grouping |
| `millisNow()` | Milliseconds since boot (resets after deep sleep) |
| `rtcSeconds()` | Monotonic seconds; **survives deep sleep** — use for cross-session timing |
| `delayMs(ms)` | Yield for the given number of milliseconds |
| `snprintf_wrap(buf, len, fmt, ...)` | `snprintf` — the only safe way to do formatted strings (no stdlib) |
| `storageRead(key, buf, maxlen)` | Read `/apps/{key}.dat`; returns bytes read, -1 on error |
| `storageWrite(key, buf, len)` | Write `/apps/{key}.dat`; returns bytes written, -1 on error |

### Button Event Codes

```c
#define PALA_CLICK   1   // single click
#define PALA_DOUBLE  2   // double click
#define PALA_TRIPLE  3   // triple click
#define PALA_LONG    4   // long press (fired on release, via waitForEvent/pollEvent)
```

For an exit-while-held pattern (button held → exit immediately, no
release needed), poll `buttonPressed()` and time it yourself.

> The firmware also emits a "quad" gesture (four short clicks in quick
> succession) internally, but v3 of the API does not surface a code for
> it — quads arrive as `0` (no event) through `waitForEvent` /
> `pollEvent`. Future API versions may add `PALA_QUAD`.

## Display

The e-ink panel is **250 × 122 pixels**. The coordinate origin (0, 0)
is the top-left corner; y increases downward. Text y coordinates are
baselines, not tops.

`drawHeader()` occupies roughly the top 18 pixels. Draw content below
y ≈ 20 to avoid overlap.

The font used by `drawTextAt` is proportional (Helvetica), so character
widths vary. If you need fixed-width columns (e.g. stat bars), draw
each character individually at a manually computed x offset.

## Building

A typical app directory looks like:

```
my_app/
  app.c         your app source (with the PalaAppHeader struct above)
  Makefile      build rules
  pala_app.ld   linker script (identical for all apps)
```

```
cd my_app
make
```

This produces `my_app.bin`. The Makefile:

1. Compiles `app.c` with `-fPIC -mlongcalls -Os` into a shared-style ELF.
2. Strips to a raw binary with `objcopy`.
3. Runs a Python snippet that locates `app_main` via `nm`, finds all
   `R_XTENSA_RELATIVE` relocations via `readelf`, and patches the
   header fields in-place.

### Critical Compiler Flags

| Flag | Why it is required |
|---|---|
| `-fno-jump-tables` | **Required for apps with if-else chains or enums.** GCC may emit jump tables in `.rodata`. The firmware's relocator only patches `.literal` pool entries; `.rodata` jump tables are **not** patched. At runtime the dispatch jumps to unrelocated addresses and the device crashes. |
| `-fPIC` | Generates position-independent code so the binary can be loaded at any address. |
| `-mlongcalls` | Needed for long-range calls in PIC mode on Xtensa. |
| `-nostdlib -nodefaultlibs` | No standard library — `malloc`, `printf`, `memcpy`, etc. are unavailable. Use only `api->snprintf_wrap` for formatting. |

> **If the device restarts the moment you open your app**, the most
> likely cause is a jump table in `.rodata`. Make sure
> `-fno-jump-tables` is in your CFLAGS.

## Uploading

1. On the device, navigate to **Upload** from the library menu.
2. The device shows its Wi-Fi AP name (`PALA-xxxxxx`) and password (`palaread`).
3. Connect to that network, then open `http://192.168.4.1` in a browser.
4. Use the **Install app** card to upload your `.bin` file.

The app is validated at upload time — the firmware re-runs the header
check (magic + `api_version`) before committing the file, so a corrupt
or wrong-version binary is rejected with a browser-side error before it
can pollute the catalog.

5. Triple-click to exit upload mode. The app appears in the **Apps**
   entry of the library menu immediately; no reboot needed.

## Constraints and Tips

- **No dynamic memory.** Allocate everything on the stack or as static
  locals. `malloc`/`free` are not linked in.
- **No globals containing absolute pointers.** Plain int / char / array
  globals are fine; globals whose *value* is a pointer to other parts
  of the binary won't be relocated correctly. Pass data through
  function arguments instead. (String literals are the exception —
  they land in `.rodata` or `.literal` and are patched by the
  relocator.)
- **Keep binaries small.** The hard cap is **48 KB**; the app is loaded
  into exec-capable RAM at runtime and the device has limited free
  heap. A few kilobytes is typical.
- **API version must match exactly.** The firmware rejects binaries
  whose `api_version` field differs from the current `PALA_API_VERSION`
  — you'll see "API v3, need v4"-style errors at upload or launch time.
- **Test with `pollEvent` + `delayMs(10)`** in your main loop if you
  need timers to fire alongside input — `waitForEvent` blocks
  indefinitely.
- **Use `rtcSeconds()` for persistence timing**, not `millisNow()`.
  The millisecond timer resets after every deep-sleep cycle (idle
  timeout is 120 s by default); the RTC clock survives.

## Examples

Working example apps (`click_counter`, `palagotchi`, …) and the
canonical `Makefile` / `pala_app.ld` templates live in
[Paul Lagier's upstream Pala project](https://ko-fi.com/s/e14ed892ea).
This rewrite ships only the public headers in
[`apps/include/`](../apps/include/) — copy a Makefile from the upstream
project and update its `-I` flag to point at this repo's header
directory.

If you build and would like an example added here, open an issue or
pull request.
