#include "ui/screens/list_screen.h"

#include "hal/display.h"
#include "storage/list_items.h"
#include "storage/settings_store.h"  // g_settings.lineGap
#include "ui/screens/library_screen.h"
#include "ui/widgets.h"

void ListScreen::onEnter() {
  draw();
}

void ListScreen::draw() {
  prepareMenuFrame();
  u8g2.setFont(MAIN_FONT);
  int ascent = u8g2.getFontAscent();
  int descent = u8g2.getFontDescent();
  int lineH = (ascent - descent) + g_settings.lineGap + 1;
  int y = drawSectionHeader("List");

  if (!listHasVisibleItems()) {
    drawMenuBulletRow(y, "No items", true, false, 0, false);
    display.update();
    return;
  }

  if (g_list.selectedIndex < 0) g_list.selectedIndex = 0;
  if (g_list.selectedIndex >= g_list.count) g_list.selectedIndex = g_list.count - 1;

  int visibleRows = (SCREEN_H - y - BOT_PAD) / lineH;
  if (visibleRows < 3) visibleRows = 3;

  int top = g_list.selectedIndex - 2;
  if (top < 0) top = 0;
  if (top > g_list.count - 1) top = max(0, g_list.count - 1);

  int rowsUsed = 0;
  for (int idx = top; idx < g_list.count; idx++) {
    if (rowsUsed >= visibleRows) break;

    String label = String(g_list.items[idx].text);
    bool selected = (idx == g_list.selectedIndex);
    String line1, line2;
    int maxWidth = SCREEN_W - UI_LIST_LEFT - MARGIN_X;

    if (selected) splitListLabelForDisplay(label, maxWidth, line1, line2);
    else {
      line1 = label;
      line2 = "";
      while (line1.length() > 0 && u8g2.getUTF8Width(line1.c_str()) > maxWidth) {
        line1.remove(line1.length() - 1);
      }
    }

    drawMenuBulletRow(y, line1, selected, selected, 0, false);
    if (g_list.items[idx].done) {
      int w1 = u8g2.getUTF8Width(line1.c_str());
      int strikeY1 = y - ((ascent - descent) / 3);
      gfx.drawFastHLine(UI_LIST_LEFT, strikeY1, w1, 1);
    }
    y += lineH;
    rowsUsed++;

    if (selected && line2.length() > 0 && rowsUsed < visibleRows) {
      u8g2.setFont(BOLD_FONT);
      u8g2.setCursor(UI_LIST_LEFT, y);
      u8g2.print(line2.c_str());
      if (g_list.items[idx].done) {
        int w2 = u8g2.getUTF8Width(line2.c_str());
        int strikeY2 = y - ((ascent - descent) / 3);
        gfx.drawFastHLine(UI_LIST_LEFT, strikeY2, w2, 1);
      }
      y += lineH;
      rowsUsed++;
      u8g2.setFont(MAIN_FONT);
    }
  }

  display.update();
}

void ListScreen::onButton(const ButtonEvent& e) {
  if (!listHasVisibleItems()) {
    nextScreen = &g_libraryScreen;
    return;
  }

  switch (e.kind) {
    case ButtonEvent::Short:
      g_list.selectedIndex++;
      if (g_list.selectedIndex >= g_list.count) g_list.selectedIndex = 0;
      draw();
      return;
    case ButtonEvent::Long:
      if (g_list.selectedIndex >= 0 && g_list.selectedIndex < g_list.count) {
        g_list.items[g_list.selectedIndex].done = g_list.items[g_list.selectedIndex].done ? 0 : 1;
        saveListItems();
        draw();
      }
      return;
    case ButtonEvent::Double:
    case ButtonEvent::Triple:
      nextScreen = &g_libraryScreen;
      return;
    default:
      return;
  }
}
