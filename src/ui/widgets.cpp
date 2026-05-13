#include "ui/widgets.h"

#include "hal/battery.h"
#include "hal/display.h"

static const int UI_HEADER_TOP = 6;
static const int UI_HEADER_GAP = 6;

int drawSectionHeader(const char* title) {
  u8g2.setFont(BOLD_FONT);
  int ascent = u8g2.getFontAscent();
  int yTitle = UI_HEADER_TOP + ascent - 2;

  // Library = Pala One, other screens = their own title
  const char* headerText = "Pala One";
  if (title && strcmp(title, "Library") != 0) {
    headerText = title;
  }

  u8g2.setCursor(MARGIN_X, yTitle);
  u8g2.print(headerText);

#if HAS_BATTERY
  drawBatteryTopRight();
#endif

  int lineY = yTitle + 4;
  gfx.drawFastHLine(MARGIN_X, lineY, SCREEN_W - (MARGIN_X * 2), 1);

  int contentTop = lineY + UI_HEADER_GAP + 11;

  u8g2.setFont(MAIN_FONT);
  return contentTop;
}

void drawMenuBulletRow(int yBaseline, const String& label, bool /*selected*/,
                       bool boldText, int depth, bool systemItem) {
  int textX = UI_LIST_LEFT + (depth * UI_DEPTH_INDENT);
  if (systemItem) textX += 2;

  u8g2.setForegroundColor(1);
  u8g2.setFont(boldText ? BOLD_FONT : MAIN_FONT);
  u8g2.setCursor(textX, yBaseline);
  u8g2.print(label.c_str());
  u8g2.setFont(MAIN_FONT);
}

void splitListLabelForDisplay(const String& in, int maxWidth, String& line1, String& line2) {
  line1 = in;
  line2 = "";
  if (u8g2.getUTF8Width(in.c_str()) <= maxWidth) return;

  int bestBreak = -1;
  for (int i = 0; i < (int)in.length(); i++) {
    if (in[i] != ' ') continue;
    String left = in.substring(0, i);
    left.trim();
    if (left.length() == 0) continue;
    if (u8g2.getUTF8Width(left.c_str()) <= maxWidth) bestBreak = i;
    else break;
  }

  if (bestBreak < 0) {
    for (int i = 1; i < (int)in.length(); i++) {
      String left = in.substring(0, i);
      if (u8g2.getUTF8Width(left.c_str()) > maxWidth) {
        bestBreak = max(1, i - 1);
        break;
      }
    }
  }

  if (bestBreak < 0) return;

  line1 = in.substring(0, bestBreak);
  line1.trim();
  line2 = in.substring(bestBreak);
  line2.trim();

  while (line2.length() > 0 && u8g2.getUTF8Width(line2.c_str()) > maxWidth) {
    line2.remove(line2.length() - 1);
  }
}
