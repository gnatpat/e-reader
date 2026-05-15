#include "ui/screens/bookmarks/book_select_screen.h"

#include "hal/display.h"
#include "storage/library.h"         // g_library
#include "ui/font.h"
#include "ui/screens/bookmarks/bookmark_list_screen.h"
#include "ui/screens/bookmarks/session.h"
#include "ui/screens/library_screen.h"
#include "ui/widgets.h"

void BookmarkBookSelectScreen::onEnter() {
  draw();
}

void BookmarkBookSelectScreen::draw() {
  prepareMenuFrame();
  Font::useBody();
  int y = drawSectionHeader("Bookmarks");

  if (g_library.bookCount == 0) {
    drawMenuRow(y, "No books", false);
    display.update();
    return;
  }

  drawScrollableList(y, g_library.bookCount, g_bookmarkSession.bookIndex,
    [&](int idx, int rowY, bool selected, int /*budget*/) {
      drawMenuRow(rowY, String(g_library.books[idx].name), selected);
      return 1;
    });

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
