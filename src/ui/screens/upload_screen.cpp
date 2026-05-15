#include "ui/screens/upload_screen.h"

#include <WiFi.h>
#include <esp_bt.h>
#include <esp_wifi.h>

#include "hal/display.h"
#include "storage/library.h"
#include "ui/font.h"
#include "ui/screens/library_screen.h"
#include "ui/widgets.h"

UploadState UploadScreen::s_state;

UploadState& UploadScreen::state() {
  return s_state;
}

void UploadScreen::onEnter() {
  startSession();
}

void UploadScreen::draw() {
  // Draw is part of startSession; nothing to redraw mid-session — the screen
  // is static while the upload is in flight.
}

void UploadScreen::startSession() {
  s_state.startedMs = millis();

  setCpuFrequencyMhz(240); // WiFi AP needs full speed

  prepareMenuFrame();

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  IPAddress ip = WiFi.softAPIP();
  String url = String("http://") + ip.toString();

  int y = drawSectionHeader("Upload");

  Font::useBold();
  u8g2.setCursor(MARGIN_X, y);
  u8g2.print("Wi-Fi");
  y += 14;

  Font::useBody();
  u8g2.setCursor(MARGIN_X, y);
  u8g2.print(AP_SSID);
  y += 16;

  Font::useBold();
  u8g2.setCursor(MARGIN_X, y);
  u8g2.print("Password");
  y += 14;

  Font::useBody();
  u8g2.setCursor(MARGIN_X, y);
  u8g2.print(AP_PASS);
  y += 16;

  Font::useBold();
  u8g2.setCursor(MARGIN_X, y);
  u8g2.print("Open");
  y += 14;

  Font::useBody();
  u8g2.setCursor(MARGIN_X, y);
  u8g2.print(url.c_str());
  y += 18;

  display.update();
  server.begin();
}

void UploadScreen::stopSessionToLibrary() {
  server.stop();

  if (s_state.bookTmpFile) s_state.bookTmpFile.close();
  if (s_state.sleepTmpFile) s_state.sleepTmpFile.close();

  WiFi.softAPdisconnect(true);
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_OFF);
  delay(100);
  esp_wifi_stop();
  btStop();

  s_state.bookPendingUtf8Tail = "";
  s_state.bookTmpPath = "";
  s_state.bookOk = false;
  s_state.bookError = "";
  s_state.bookFinalName = "";

  s_state.sleepOk = false;
  s_state.sleepError = "";
  s_state.sleepTmpPath = "";

  loadBooks();
  resetInputFrontend();
  setCpuFrequencyMhz(80); // back to low-power idle
  nextScreen = &g_libraryScreen;
}

void UploadScreen::onButton(const ButtonEvent& e) {
  if (e.kind == ButtonEvent::Short || e.kind == ButtonEvent::Triple) {
    stopSessionToLibrary();
  }
}

void UploadScreen::onIdleTick() {
  server.handleClient();
  if ((uint32_t)(millis() - s_state.startedMs) > UPLOAD_AUTO_EXIT_MS) {
    stopSessionToLibrary();
  }
}
