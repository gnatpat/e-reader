#include "storage/settings_store.h"

#include "state.h"   // prefs

// The active runtime-tunable settings. Loaded by `loadSettings()` below,
// mutated live by the /settings web route.
RuntimeSettings g_settings;

void loadSettings() {
  int fs = prefs.getInt("cfg_font", 8);
  if (fs != 8 && fs != 10 && fs != 12 && fs != 14) fs = 10;
  g_settings.fontSize = fs;

  g_settings.sleepSecs = (uint32_t)prefs.getInt("cfg_sleep", 120);
  if (g_settings.sleepSecs < 10) g_settings.sleepSecs = 10;
  if (g_settings.sleepSecs > 3600) g_settings.sleepSecs = 3600;

  g_settings.lineGap = prefs.getInt("cfg_lgap", 0);
  if (g_settings.lineGap < 0) g_settings.lineGap = 0;
  if (g_settings.lineGap > 4) g_settings.lineGap = 4;

  g_settings.readerLongPressAction = LONGPRESS_BOOKMARK;
}
