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
  uint32_t sleepSecs = 120;
  int readerLongPressAction = LONGPRESS_BOOKMARK;
};

extern RuntimeSettings g_settings;

// Idle-deadline helper: converts `g_settings.sleepSecs` to ms. Compared
// against `userIdleMs()` in the main loop's sleep gate.
inline uint32_t sleepAfterMs() {
  return g_settings.sleepSecs * 1000UL;
}

// Load persisted settings into `g_settings`, clamping out-of-range values.
// Body font + line gap are owned by the Font module — see Font::loadSettings()
// in ui/font.h, which the boot sequence calls separately.
void loadSettings();
