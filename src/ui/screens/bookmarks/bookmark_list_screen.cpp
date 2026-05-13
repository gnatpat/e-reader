#include "ui/screens/bookmarks/bookmark_list_screen.h"

#include "hal/display.h"
#include "pure/hashing.h"
#include "storage/bookmarks.h"
#include "storage/library.h"         // g_library
#include "storage/settings_store.h"  // g_settings.lineGap
#include "ui/reader.h"
#include "ui/screens/bookmarks/book_select_screen.h"
#include "ui/screens/bookmarks/preview_screen.h"
#include "ui/screens/bookmarks/session.h"
#include "ui/screens/library_screen.h"
#include "ui/text.h"
#include "ui/widgets.h"

void BookmarkListScreen::onEnter() {
  draw();
}

void BookmarkListScreen::draw() {
  prepareMenuFrame();
  u8g2.setFont(MAIN_FONT);
  int ascent = u8g2.getFontAscent();
  int descent = u8g2.getFontDescent();
  int lineH = (ascent - descent) + g_settings.lineGap;
  int y = drawSectionHeader("Bookmarks");

  String bookPath = String(g_library.books[g_bookmarkSession.bookIndex].path);
  String key = prefKeyForBook(bookPath);
  g_bookmarkSession.count = loadBookmarksForKey(key, g_bookmarkSession.pages, g_bookmarkSession.offsets);
  if (g_bookmarkSession.selectedIndex >= (int)g_bookmarkSession.count)
    g_bookmarkSession.selectedIndex = max(0, (int)g_bookmarkSession.count - 1);

  if (g_bookmarkSession.count == 0) {
    drawMenuBulletRow(y, "No bookmarks", true, false, 0, false);
    display.update();
    return;
  }

  File f = FS.open(bookPath, "r");
  if (!f) {
    drawMenuBulletRow(y, "Open failed", true, false, 0, false);
    display.update();
    return;
  }

  int visible = (SCREEN_H - y - BOT_PAD) / lineH;
  if (visible < 1) visible = 1;
  if (visible > 5) visible = 5;

  int top = g_bookmarkSession.selectedIndex - (visible / 2);
  if (top < 0) top = 0;
  if (top > (int)g_bookmarkSession.count - visible) top = max(0, (int)g_bookmarkSession.count - visible);

  for (int i = 0; i < visible; i++) {
    int idx = top + i;
    if (idx >= (int)g_bookmarkSession.count) break;

    int targetPage = (int)g_bookmarkSession.pages[idx];
    if (targetPage < 0) targetPage = 0;

    uint32_t pageOff = resolveBookmarkOffset(bookPath, (uint16_t)targetPage, g_bookmarkSession.offsets[idx]);
    FileReadStream fs(f);
    String sn = readBookmarkLabelAtOffset(fs, pageOff, targetPage);
    drawMenuBulletRow(y, sn, idx == g_bookmarkSession.selectedIndex,
                      idx == g_bookmarkSession.selectedIndex, 0, false);
    y += lineH;
  }

  f.close();
  display.update();
}

void BookmarkListScreen::onButton(const ButtonEvent& e) {
  if (!e.any()) return;

  if (g_bookmarkSession.selectedIndex >= (int)g_bookmarkSession.count)
    g_bookmarkSession.selectedIndex = max(0, (int)g_bookmarkSession.count - 1);

  if (e.kind == ButtonEvent::Short) {
    if (g_bookmarkSession.count > 0) {
      g_bookmarkSession.selectedIndex++;
      if (g_bookmarkSession.selectedIndex >= (int)g_bookmarkSession.count) g_bookmarkSession.selectedIndex = 0;
    }
    draw();
    return;
  }

  if (e.kind == ButtonEvent::Double) {
    if (g_bookmarkSession.count == 0) return;

    if (openBookByIndex(g_bookmarkSession.bookIndex)) {
      g_reader.pageIndex = (int)g_bookmarkSession.pages[g_bookmarkSession.selectedIndex];
      if (g_reader.pageIndex < 0) g_reader.pageIndex = 0;
      nextScreen = &g_bmPreviewScreen;
    } else {
      nextScreen = &g_libraryScreen;
    }
    return;
  }

  if (e.kind == ButtonEvent::Triple) {
    nextScreen = &g_libraryScreen;
    return;
  }

  if (e.kind == ButtonEvent::Long) {
    nextScreen = &g_bmBookSelectScreen;
    return;
  }
}
