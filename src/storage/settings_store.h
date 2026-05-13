#pragma once

#include "config.h"
#include "pure/arduino_compat.h"

// ============================================================================
//  Runtime-tunable settings — loaded from NVS at boot by `loadSettings()`,
//  modified live by the /settings web route.
// ============================================================================
enum ReaderLongPressAction {
  LONGPRESS_BOOKMARK = 0
};

struct RuntimeSettings {
  int fontSize = 8;
  uint32_t sleepSecs = 120;
  int lineGap = 0;
  int readerLongPressAction = LONGPRESS_BOOKMARK;
};

extern RuntimeSettings g_settings;

// Idle-deadline helper: converts `g_settings.sleepSecs` to ms. Compared
// against `userIdleMs()` in the main loop's sleep gate.
inline uint32_t sleepAfterMs() {
  return g_settings.sleepSecs * 1000UL;
}

// Load persisted settings into `g_settings` and apply the font size to the
// global font pointers. Clamps out-of-range values.
void loadSettings();
