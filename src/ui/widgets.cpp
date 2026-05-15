#include "ui/widgets.h"

#include "hal/battery.h"
#include "hal/display.h"
#include "storage/settings_store.h"  // g_settings.lineGap

static const int UI_HEADER_TOP = 6;
static const int UI_HEADER_GAP = 6;

int drawSectionHeader(const char* title) {
  u8g2.setFont(BOLD_FONT);
  int ascent = u8g2.getFontAscent();
  int yTitle = UI_HEADER_TOP + ascent - 2;

  u8g2.setCursor(MARGIN_X, yTitle);
  u8g2.print(title);

#if HAS_BATTERY
  drawBatteryTopRight();
#endif

  int lineY = yTitle + 4;
  gfx.drawFastHLine(MARGIN_X, lineY, SCREEN_W - (MARGIN_X * 2), 1);

  int contentTop = lineY + UI_HEADER_GAP + 11;

  u8g2.setFont(MAIN_FONT);
  return contentTop;
}

void drawMenuRow(int yBaseline, const String& label, bool selected, int extraIndent) {
  u8g2.setForegroundColor(1);
  u8g2.setFont(selected ? BOLD_FONT : MAIN_FONT);
  u8g2.setCursor(UI_LIST_LEFT + extraIndent, yBaseline);
  u8g2.print(label.c_str());
  u8g2.setFont(MAIN_FONT);
}

int menuLineH() {
  int ascent = u8g2.getFontAscent();
  int descent = u8g2.getFontDescent();
  return (ascent - descent) + g_settings.lineGap + 1;
}

void drawScrollableList(int contentTopY, int itemCount, int selectedIndex,
                        const DrawListRowFn& drawRow) {
  if (itemCount <= 0) return;

  int lineH = menuLineH();

  // A row of N items uses (N-1)*lineH + textHeight pixels — the last row
  // doesn't need a trailing inter-line gap. Crediting that leftover gap
  // lets us fit one more row than `available / lineH` would suggest.
  // `descent` is negative (e.g. -2), so adding it tightens the bound by
  // |descent| to keep descenders on-screen.
  int descent = u8g2.getFontDescent();
  int available = SCREEN_H - BOT_PAD + descent - contentTopY;
  int visibleRows = (available / lineH) + 1;
  if (visibleRows < 1) visibleRows = 1;

  if (selectedIndex < 0) selectedIndex = 0;
  if (selectedIndex >= itemCount) selectedIndex = itemCount - 1;

  // Center the selected item in the visible window, then clamp at the
  // ends so we don't leave blank rows past the list.
  int top = selectedIndex - (visibleRows / 2);
  if (top < 0) top = 0;
  if (top > itemCount - visibleRows) top = max(0, itemCount - visibleRows);

  int y = contentTopY;
  int rowsUsed = 0;
  for (int idx = top; idx < itemCount && rowsUsed < visibleRows; idx++) {
    int budget = visibleRows - rowsUsed;
    int consumed = drawRow(idx, y, idx == selectedIndex, budget);
    if (consumed < 1) consumed = 1;
    y += consumed * lineH;
    rowsUsed += consumed;
  }
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
