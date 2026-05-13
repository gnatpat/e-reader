#include "ui/screens/bookmarks/book_select_screen.h"

#include "hal/display.h"
#include "storage/library.h"         // g_library
#include "storage/settings_store.h"  // g_settings.lineGap
#include "ui/screens/bookmarks/bookmark_list_screen.h"
#include "ui/screens/bookmarks/session.h"
#include "ui/screens/library_screen.h"
#include "ui/widgets.h"

void BookmarkBookSelectScreen::onEnter() {
  draw();
}

void BookmarkBookSelectScreen::draw() {
  prepareMenuFrame();
  u8g2.setFont(MAIN_FONT);
  int ascent = u8g2.getFontAscent();
  int descent = u8g2.getFontDescent();
  int lineH = (ascent - descent) + g_settings.lineGap + 1;
  int y = drawSectionHeader("Bookmarks");

  if (g_library.bookCount == 0) {
    drawMenuBulletRow(y, "No books", true, false, 0, false);
    display.update();
    return;
  }

  if (g_bookmarkSession.bookIndex < 0) g_bookmarkSession.bookIndex = 0;
  if (g_bookmarkSession.bookIndex >= g_library.bookCount) g_bookmarkSession.bookIndex = g_library.bookCount - 1;

  int visible = (SCREEN_H - y - BOT_PAD) / lineH;
  if (visible < 2) visible = 2;
  if (visible > 6) visible = 6;

  int top = g_bookmarkSession.bookIndex - (visible / 2);
  if (top < 0) top = 0;
  if (top > g_library.bookCount - visible) top = max(0, g_library.bookCount - visible);

  for (int i = 0; i < visible; i++) {
    int idx = top + i;
    if (idx >= g_library.bookCount) break;
    drawMenuBulletRow(y, String(g_library.books[idx].name),
                      idx == g_bookmarkSession.bookIndex,
                      idx == g_bookmarkSession.bookIndex, 0, false);
    y += lineH;
  }

  display.update();
}

void BookmarkBookSelectScreen::onButton(const ButtonEvent& e) {
  if (e.kind == ButtonEvent::Triple) {
    nextScreen = &g_libraryScreen;
    return;
  }

  if (g_library.bookCount == 0) {
    if (e.any()) nextScreen = &g_libraryScreen;
    return;
  }

  if (e.kind == ButtonEvent::Short) {
    g_bookmarkSession.bookIndex++;
    if (g_bookmarkSession.bookIndex >= g_library.bookCount) g_bookmarkSession.bookIndex = 0;
    draw();
    return;
  }

  if (e.kind == ButtonEvent::Long) {
    g_bookmarkSession.bookIndex--;
    if (g_bookmarkSession.bookIndex < 0) g_bookmarkSession.bookIndex = g_library.bookCount - 1;
    draw();
    return;
  }

  if (e.kind == ButtonEvent::Double) {
    g_bookmarkSession.selectedIndex = 0;
    nextScreen = &g_bmListScreen;
    return;
  }
}
