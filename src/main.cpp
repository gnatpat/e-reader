#include <Arduino.h>
#include <esp_sleep.h>

#include "config.h"
#include "state.h"
#include "hal/battery.h"
#include "hal/display.h"
#include "hal/input.h"
#include "pure/hashing.h"
#include "storage/fs_util.h"
#include "storage/library.h"
#include "storage/list_items.h"
#include "storage/page_cache.h"
#include "ui/font.h"
#include "ui/reader.h"
#include "ui/screen.h"
#include "ui/widgets.h"  // drawCenter
#include "ui/screens/about_screen.h"
#include "ui/screens/bookmarks/book_select_screen.h"
#include "ui/screens/bookmarks/bookmark_list_screen.h"
#include "ui/screens/bookmarks/preview_screen.h"
#include "ui/screens/library_screen.h"
#include "ui/screens/list_screen.h"
#include "ui/screens/reader_screen.h"
#include "ui/screens/upload_screen.h"
#include "ui/sleep.h"
#include "ui/text.h"
#include "ui/toast.h"
#include "web/web.h"

// ============================================================================
//  Screen instances + current-screen pointer
// ============================================================================
LibraryScreen              g_libraryScreen;
ReaderScreen               g_readerScreen;
UploadScreen               g_uploadScreen;
AboutScreen                g_aboutScreen;
ListScreen                 g_listScreen;
BookmarkBookSelectScreen   g_bmBookSelectScreen;
BookmarkListScreen         g_bmListScreen;
BookmarkPreviewScreen      g_bmPreviewScreen;

Screen* g_currentScreen = &g_libraryScreen;

// ============================================================================
//  Setup
// ============================================================================
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.printf("[boot] wake cause: %d\n", esp_sleep_get_wakeup_cause());
  setCpuFrequencyMhz(240); // full speed for init; lowered to 80 MHz at end of setup

  pinMode(BTN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BTN), btnISR, CHANGE);

  u8g2.begin(gfx);

#if HAS_BATTERY
  adcSetupOnce();
  pinMode(BAT_ADC_CTRL, INPUT);
  updateBatteryCached(true);
#endif

  display.fastmodeOff();
  display.clear();

  if (!fsBegin()) {
    drawCenter("Storage error", "Try factory reset");
    return;
  }
  ensureBooksDir();

  {
    uint64_t chipId = ESP.getEfuseMac();
    snprintf(AP_SSID, sizeof(AP_SSID), "PALA-%06llX", chipId & 0xFFFFFFULL);
  }

  prefs.begin("ereader", false);
  Font::loadSettings();
  Sleep::loadSettings();
  loadBooks();
  loadListItems();
  registerWebRoutes();
  markUserActivity();

  if (tryRestoreReadingSession()) {
    renderCurrentPage();      // ~300ms draw — wake-press releases during this
    resetInputFrontend();     // then discard the wake-press only
    g_currentScreen = &g_readerScreen;
  } else {
    g_currentScreen = &g_libraryScreen;
    g_libraryScreen.onEnter();
    resetInputFrontend();
  }

  // Drop to 80 MHz for normal operation — saves significant power.
  // Upload mode will raise it back to 240 MHz temporarily.
  setCpuFrequencyMhz(80);
}

// ============================================================================
//  Main loop
// ============================================================================
void loop() {
  g_btns.poll();
  maybeRecoverFromIsrOverflow();

  ButtonEvent ev = ButtonEvent::fromButtonState(g_btns);
  if (ev.any()) markUserActivity();

  if (ENABLE_DEEP_SLEEP && g_currentScreen->allowSleep()) {
    if (userIdleMs() > Sleep::idleTimeoutMs()) {
      Sleep::enter();
      return;
    }
  }

  g_currentScreen->onButton(ev);
  g_currentScreen->onIdleTick();

  // Toast just expired? Repaint so its pixels actually disappear.
  if (Toast::clearIfExpired()) g_currentScreen->draw();

  if (g_currentScreen->nextScreen) {
    g_currentScreen = g_currentScreen->nextScreen;
    g_currentScreen->nextScreen = nullptr;
    g_currentScreen->onEnter();
  }
}
