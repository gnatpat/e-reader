# AGENTS.md

ESP32 e-ink book reader for the Heltec Wireless Paper board (250×122 mono e-paper, one button). PlatformIO + Arduino framework. Single-build env: `wireless-paper`.

## Build / test

- Firmware: `pio run` (build), `pio run -t upload` (flash), `pio device monitor`.
- Host tests: `cd test && cmake -S . -B build && cmake --build build && ctest --test-dir build --output-on-failure`.

## Layout

- `src/pure/` — pure C++, no Arduino headers. Compiles into both firmware and host tests via `arduino_compat.h` shim.
- `src/storage/` — KV-store abstraction + on-disk persistence (book metadata, page cache, library catalog). Firmware-only code lives behind `#ifdef ARDUINO`.
- `src/hal/` — device wrappers (display, battery, input/button).
- `src/ui/` — `font`, `sleep`, `toast`, `widgets`, `reader`, `text`, plus the screens under `screens/`.
- `src/web/` — HTTP server split by route group (`chrome`, `files`, `bookmarks`, `list`, `settings`, `upload`, `reset`). Shared CSS at `/style.css`.
- `src/main.cpp` — `setup()` + main `loop()` dispatcher.
- `test/` — CMake-driven host unit tests for `pure/` and KV-backed modules.

## Key conventions

- **Layering**: `pure → storage → hal → ui`. Pure modules never include `Arduino.h`. Storage uses `KeyValueStore` so codec/store logic is testable with `MapKvStore` off-device.
- **Per-module settings**: each domain owns its own NVS keys + load/save (`Font::loadSettings/setBodySize/setLineGap`, `Sleep::loadSettings/setIdleTimeout`). No global settings struct.
- **Screens** implement `Screen` (`onEnter`, `onButton`, `draw`, `onIdleTick?`, `onSleep?`). Main loop dispatches to `g_currentScreen`; transitions via `nextScreen`.
- **Reader**: active book in `g_bookview` (file + page table + cursor). `persistReaderState()` saves at every "leaving" moment (sleep, exit-to-home, preview commit). The on-disk page cache is layout-stamped — stale entries self-invalidate on load.
- **Web catalog freshness**: handlers that change `/books` call `loadBooks()` *after* the mutation; reads trust the catalog.