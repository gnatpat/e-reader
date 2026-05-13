#include "ui/sleep.h"

#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_bt.h>
#include <esp_sleep.h>
#include <driver/rtc_io.h>

#include "config.h"
#include "state.h"
#include "hal/display.h"
#include "storage/book_state.h"
#include "ui/pala_one_sleep_black_icon_v4.h"
#include "ui/reader.h"     // safeCloseCurrentBook
#include "ui/screen.h"

void drawSleepScreen() {
  display.fastmodeOff();
  display.clear();
  beginPageCanvas();

  File sf = FS.open("/sleep.bin", "r");
  if (sf && sf.size() >= 3904) {
    static uint8_t sleepBuf[3904];
    sf.read(sleepBuf, 3904);
    sf.close();
    gfx.fillScreen(1);
    gfx.drawXBitmap(0, 0, sleepBuf, SCREEN_W, SCREEN_H, 0);
  } else {
    if (sf) sf.close();
    gfx.fillScreen(1);
    gfx.drawXBitmap(0, 0, pala_one_sleep_black_icon_v4_bits, SCREEN_W, SCREEN_H, 0);
  }
  display.update();
}

void goToSleep() {
  if (!ENABLE_DEEP_SLEEP) return;

  // Each screen decides how to persist itself for resume. Default is
  // "clear wake state" (defined in screen.h); reader 
  // overrides to save progress + set wake_mode for resume.
  if (g_currentScreen) g_currentScreen->onSleep();

  delay(50);

  safeCloseCurrentBook();

  drawSleepScreen();
  delay(600);

  WiFi.softAPdisconnect(true);
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_OFF);
  delay(100);
  esp_wifi_stop();
  btStop();

  Platform::prepareToSleep();
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  // INPUT_PULLUP is in the digital IO domain, which powers down in deep sleep.
  // Enable the RTC-domain pull-up so BTN doesn't float and spuriously wake us.
  rtc_gpio_pullup_en((gpio_num_t)BTN);
  rtc_gpio_pulldown_dis((gpio_num_t)BTN);
  esp_sleep_enable_ext0_wakeup((gpio_num_t)BTN, 0);
  delay(50);
  Serial.printf("[sleep] BTN=%d entering deep sleep\n", digitalRead(BTN));
  Serial.flush();
  esp_deep_sleep_start();
}
