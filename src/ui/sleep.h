#pragma once

#include "pure/arduino_compat.h"  // uint32_t

// ============================================================================
//  Sleep module — owns the deep-sleep entry sequence and the user-tunable
//  idle-timeout setting (NVS key `cfg_sleep`, default 120s, range [10, 3600]).
// ============================================================================
namespace Sleep {

// Read persisted idle-timeout from NVS into Sleep's internal state.
// Call once from setup() after `prefs.begin`.
void loadSettings();

// Apply + persist the idle timeout. Clamps to [10, 3600].
void setIdleTimeout(int secs);

// Current applied values.
int      idleTimeoutSecs();   // for the web settings UI selects
uint32_t idleTimeoutMs();     // for the main loop's sleep deadline check

// Enter deep sleep right now. Notifies the active screen, draws the sleep
// image, releases peripherals, then `esp_deep_sleep_start`s.
void enter();

}  // namespace Sleep
