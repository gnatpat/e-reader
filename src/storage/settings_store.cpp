#include "storage/settings_store.h"

#include "hal/display.h"  // applyFontSize / invalidateMetrics
#include "state.h"        // prefs

// The active runtime-tunable settings. Loaded by `loadSettings()` below,
// mutated live by the /settings web route.
RuntimeSettings g_settings;

void loadSettings() {
  applyFontSize(prefs.getInt("cfg_font", 8));

  g_settings.sleepSecs = (uint32_t)prefs.getInt("cfg_sleep", 120);
  if (g_settings.sleepSecs < 10) g_settings.sleepSecs = 10;
  if (g_settings.sleepSecs > 3600) g_settings.sleepSecs = 3600;

  g_settings.lineGap = prefs.getInt("cfg_lgap", 0);
  if (g_settings.lineGap < 0) g_settings.lineGap = 0;
  if (g_settings.lineGap > 4) g_settings.lineGap = 4;

  g_settings.readerLongPressAction = LONGPRESS_BOOKMARK;
  invalidateMetrics();
}
