#include "storage/settings_store.h"

#include "state.h"   // prefs

// The active runtime-tunable settings. Loaded by `loadSettings()` below,
// mutated live by the /settings web route.
RuntimeSettings g_settings;

void loadSettings() {
  g_settings.sleepSecs = (uint32_t)prefs.getInt("cfg_sleep", 120);
  if (g_settings.sleepSecs < 10) g_settings.sleepSecs = 10;
  if (g_settings.sleepSecs > 3600) g_settings.sleepSecs = 3600;

  g_settings.readerLongPressAction = LONGPRESS_BOOKMARK;
}
