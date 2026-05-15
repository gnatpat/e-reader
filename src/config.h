#pragma once

// ============================================================================
//  Project-wide compile-time configuration.
//
//  IMPORTANT: This header is included transitively by the pure/ modules
//  (paginator, list_codec, bookmarks_codec, etc.), which are also compiled
//  into the host-test build. It MUST therefore remain host-compatible:
//
//    - No <Arduino.h>, <WiFi.h>, or any library header.
//    - No code, only `#define`s, `static const` constants, type aliases,
//      and `extern` declarations of host-safe types.
//    - If you need to add hardware- or library-specific declarations,
//      put them in a different header that only firmware sources include.
//
//  The build will catch violations (the host test compile will fail), but
//  treat this comment as the convention so the surprise is small.
// ============================================================================

#include "pure/arduino_compat.h"

#define FW_VERSION "2.1"

static const int SCREEN_W = 250;
static const int SCREEN_H = 122;

static const uint8_t MAX_BOOKMARKS = 12;
static const int MAX_BOOKS = 80;
static const int MAX_FOLDERS = 32;
static const int MAX_FOLDER_PATH = 63;  // chars, excluding null
static const int MAX_PAGES = 10000;
static const int MAX_LIBRARY_ENTRIES = (MAX_BOOKS * 2) + (MAX_FOLDERS * 2) + 8;
static const int MAX_LIST_ITEMS = 16;
static const int MAX_LIST_TEXT = 64;

// Max silence after the most recent release before we commit a click sequence.
// Conceptually: "longest pause the user can take between consecutive clicks."
static const uint32_t MAX_CLICK_GAP_MS = 300;

// Max total duration of a multi-click sequence, measured from the first release.
// Conceptually: "the whole multi-click input has to complete within this window."
// Combined with MAX_CLICK_GAP_MS this caps both per-gap and overall sloppiness.
static const uint32_t MAX_CLICK_SEQUENCE_MS = 550;

static const uint32_t LONG_MS = 850;
static const uint32_t DEBOUNCE_MS = 14;

static const uint32_t SAVE_EVERY_MS = 7000;
static const uint32_t TOAST_MS = 650;
static const uint32_t UPLOAD_AUTO_EXIT_MS = 15UL * 60UL * 1000UL;
static const uint32_t BAT_CACHE_MS = 180000; // 3 min — battery changes slowly

static const int FULL_REFRESH_EVERY_N_PAGES = 100;
static const int MENU_FULL_REFRESH_EVERY = 60;

static const int MARGIN_X = 6;
static const int TOP_PAD = 0;
static const int BOT_PAD = 0;
static const int STATUS_H = 8;

static const bool SHOW_PROGRESS_BAR = true;
static const bool SHOW_PAGE_NUMBER = true;
static const bool ENABLE_DEEP_SLEEP = true;

#define BTN 0
#define HAS_BATTERY 1
#if HAS_BATTERY
  #define BAT_ADC_CTRL 19
  #define BAT_ADC_IN   20
#endif

// NOTE: `#define FS LittleFS` lives in state.h AFTER all system headers, so it
// doesn't collide with the `class FS` declared inside Arduino's FS.h.

// Fonts live behind the role API in `ui/font.h` (Font::useBody/useBold/
// useUiSmall/useUiTiny). No code outside font.cpp references u8g2 font
// tables directly.
