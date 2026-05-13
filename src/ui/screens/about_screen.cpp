#include "ui/screens/about_screen.h"

#include "hal/display.h"
#include "storage/settings_store.h"  // g_settings.lineGap
#include "ui/screens/library_screen.h"
#include "ui/widgets.h"

void AboutScreen::onEnter() {
  draw();
}

void AboutScreen::draw() {
  prepareMenuFrame();
  u8g2.setFont(MAIN_FONT);
  int ascent = u8g2.getFontAscent();
  int lineH = (ascent - u8g2.getFontDescent()) + g_settings.lineGap + 1;
  int y = drawSectionHeader("Device");

  String rows[5] = {
    "Firmware " FW_VERSION,
    "1x next / down",
    "2x open / select",
    "3x home",
    "Hold bookmark"
  };

  for (int i = 0; i < 5; i++) {
    u8g2.setFont(i == 0 ? BOLD_FONT : MAIN_FONT);
    u8g2.setCursor(MARGIN_X, y);
    u8g2.print(rows[i].c_str());
    y += lineH;
  }

  display.update();
}

void AboutScreen::onButton(const ButtonEvent& e) {
  if (e.any()) nextScreen = &g_libraryScreen;
}
