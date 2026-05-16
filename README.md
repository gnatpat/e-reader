# Pala One e-reader

Pala One — A tiny E-Ink reader project by Paul Lagier

The goal of the project was to create a simple, distraction-free reading device that feels minimal, portable and easy to build while still looking and behaving more like a real product than a typical DIY electronics project.

## This codebase

Based on Paul's original Pala firmware, this is a rewrite with a focus on readability, maintainability, separation of concerns and testability.
The architecture is layered to isolate hardware-specific code and allow for host-side unit testing of core logic.

It also adds a web installer for easy flashing.

## Install (no toolchain needed)

[Web Installer](https://gnatpat.github.io/e-reader/)

The easiest way to flash a board is via the web installer. Plug your Heltec Wireless Paper into a desktop computer running Chrome, Edge, or Opera, then open the installer page and click **Install** for your display revision (V1.1 or V1.2).

The installer keeps existing reading progress, bookmarks, and uploaded books across re-flashes.

## Build from source

Pala One is a PlatformIO project. To build and flash locally:

1. Install [PlatformIO Core](https://platformio.org/install/cli) (CLI) or the PlatformIO IDE extension for VS Code.
2. Clone this repo.
3. Pick the env that matches your board's display revision and flash:
   ```
   pio run -e wireless-paper-v1_2 -t upload    # V1.2 panel
   pio run -e wireless-paper-v1_1 -t upload    # V1.1 panel
   ```
4. Open the serial monitor (115200 baud) if you want to see logs:
   ```
   pio device monitor
   ```

Host-side unit tests (paginator, library navigation, codecs) live in `test/` and use CMake — they run on your laptop, not on the board:

```
cmake -S test -B test/build && cmake --build test/build && ctest --test-dir test/build
```

## Codebase overview

The firmware is laid out so that logic is testable on a laptop and hardware code is isolated:

- **`src/pure/`** — pure logic with no Arduino or hardware dependencies (paginator, library navigation, codecs). Compiled into the host test build.
- **`src/hal/`** — hardware adapters (e-ink display wrapper, GFX glue).
- **`src/storage/`** — KV store + per-book metadata (progress, bookmarks).
- **`src/ui/`** — on-device UI: screens (reader, library, list, about, upload, settings), widgets, fonts, sleep, toasts.
- **`src/web/`** — captive-portal web server for uploading books, editing the list, viewing bookmarks, factory reset.
- **`src/main.cpp`, `src/state.{h,cpp}`, `src/config.h`** — entry point, global state, compile-time configuration.
- **`install/`** — ESP Web Tools installer page (deployed to GitHub Pages by CI).
- **`scripts/build_info.py`** — PlatformIO pre-build script that injects the current git short hash as a `-D` macro.
- **`docs/`** — architecture notes and refactor journal.

## Hardware

Pala One is based on:

- Heltec Wireless Paper
- 3D printed housing
- LiPo battery

## Downloads

This repository contains the firmware source code for the project.

Additional files such as:

- STL files
- STEP files
- assembly guides
- printable files
- project downloads

are available separately via Ko-fi:

https://ko-fi.com/s/e14ed892ea

Please consider supporting the original project if you find it interesting or useful!

Thanks to Paul for open-sourcing the firmware and allowing this rewrite to exist. Check out his original Pala project for more details on the hardware and design.