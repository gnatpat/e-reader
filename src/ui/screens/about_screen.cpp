#include "ui/screens/about_screen.h"

#include "hal/display.h"
#include "ui/font.h"
#include "ui/screens/library_screen.h"
#include "ui/widgets.h"

void AboutScreen::onEnter() {
  draw();
}

void AboutScreen::draw() {
  prepareMenuFrame();
  Font::useBody();
  int ascent = u8g2.getFontAscent();
  int lineH = (ascent - u8g2.getFontDescent()) + Font::currentLineGap() + 1;
  int y = drawSectionHeader("Device");

  String rows[5] = {
    "Firmware " FW_VERSION,
    "1x next / down",
    "2x open / select",
    "3x home",
    "Hold bookmark"
  };

  for (int i = 0; i < 5; i++) {
    if (i == 0) Font::useBold();
    else        Font::useBody();
    u8g2.setCursor(MARGIN_X, y);
    u8g2.print(rows[i].c_str());
    y += lineH;
  }

  display.update();
}

void AboutScreen::onButton(const ButtonEvent& e) {
  if (e.any()) nextScreen = &g_libraryScreen;
}
