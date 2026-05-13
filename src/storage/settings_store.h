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

// Load persisted settings into `g_settings`, clamping out-of-range values.
// Pure storage read — does NOT touch the display. After calling this,
// callers must invoke `applyFontSize(g_settings.fontSize)` (in hal/display.h)
// to push the loaded font choice into the global font pointers + metrics.
void loadSettings();
