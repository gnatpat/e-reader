# Host-side unit tests

These tests run on your laptop (not on the device) against the pure modules
in [`src/pure/`](../src/pure/) and the KV-store-backed modules in
[`src/storage/`](../src/storage/) (`bookmarks.cpp`, `list_items.cpp`).

They're a separate build from the firmware. The firmware build (`pio run`)
ignores this directory; CMake here ignores everything that depends on the
ESP32 toolchain.

> **Note:** PlatformIO's built-in test runner (`pio test`) also defaults to
> this directory. We don't use it — `platformio.ini` sets `test_ignore = *`
> so `pio test` is a no-op. Run tests with CMake / `ctest` as shown below.

## Requirements

- CMake 3.13+
- A C++17 compiler (MSVC, GCC, or Clang — CMake picks one automatically)

## Run

```bash
cd test
cmake -S . -B build         # configure (once, or after CMakeLists.txt changes)
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Or run the binary directly for the verbose per-test output:

```bash
./build/Release/host_tests.exe        # Windows / MSVC
./build/host_tests                    # Linux / macOS / MinGW
```

## What's covered

| Test file | Module under test |
|---|---|
| [`test_paths.cpp`](test_paths.cpp) | path / folder / filename sanitization |
| [`test_text_util.cpp`](test_text_util.cpp) | UTF-8 helpers + typography normalization |
| [`test_hashing.cpp`](test_hashing.cpp) | FNV-1a + per-book preference keys |
| [`test_paginator.cpp`](test_paginator.cpp) | line wrapping at words / punctuation / UTF-8 |
| [`test_bookmark_label.cpp`](test_bookmark_label.cpp) | first-words bookmark label |
| [`test_bookmarks_codec.cpp`](test_bookmarks_codec.cpp) | bookmark byte-blob encode/decode + v1 migration |
| [`test_list_codec.cpp`](test_list_codec.cpp) | todo-list byte-blob encode/decode |
| [`test_offset_cache.cpp`](test_offset_cache.cpp) | LRU page-offset cache |
| [`test_bookmarks_store.cpp`](test_bookmarks_store.cpp) | `loadBookmarks` / `saveBookmarks` against `MapKvStore` |
| [`test_list_store.cpp`](test_list_store.cpp) | `loadList` / `saveList` against `MapKvStore` |

## Adding a new test

1. Add a `test_<thing>.cpp` here. Mirror the source file you're testing.
2. List the source file (and the new test file) in
   [`CMakeLists.txt`](CMakeLists.txt) under `SUT_SRC` / `TEST_SRC`.
3. Use `TEST_CASE("name") { ... }` with `CHECK(expr)` / `CHECK_EQ(a, b)` /
   `REQUIRE(expr)` from [`test_framework.h`](test_framework.h).

The pure modules compile here because they include
[`src/pure/arduino_compat.h`](../src/pure/arduino_compat.h) — which provides
a minimal `String` shim when `ARDUINO` isn't defined. If you need a method
on `String` that isn't there yet, add it to the shim.
