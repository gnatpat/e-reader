#include "ui/screens/library_screen.h"

#include "hal/display.h"
#include "storage/book_state.h"
#include "storage/library.h"
#include "storage/list_items.h"
#include "storage/settings_store.h"  // g_settings.lineGap
#include "ui/reader.h"
#include "ui/screens/about_screen.h"
#include "ui/screens/bookmarks/book_select_screen.h"
#include "ui/screens/bookmarks/session.h"
#include "ui/screens/list_screen.h"
#include "ui/screens/reader_screen.h"
#include "ui/screens/upload_screen.h"
#include "ui/widgets.h"

void LibraryScreen::onEnter() {
  safeCloseCurrentBook();
  resetBookmarkSession();
  resetNavigationState();
  syncWakeState(false);
  draw();
}

void LibraryScreen::draw() {
  prepareMenuFrame();
  u8g2.setFont(MAIN_FONT);
  buildLibraryEntries();

  int ascent = u8g2.getFontAscent();
  int descent = u8g2.getFontDescent();
  int lineH = (ascent - descent) + g_settings.lineGap + 1;
  int y = drawSectionHeader("Library");

  int totalItems = g_library.entryCount;
  int visible = (SCREEN_H - y - BOT_PAD) / lineH;
  if (visible < 3) visible = 3;
  if (visible > 6) visible = 6;

  int top = g_library.selectedItem - (visible / 2);
  if (top < 0) top = 0;
  if (top > totalItems - visible) top = max(0, totalItems - visible);

  for (int i = 0; i < visible; i++) {
    int idx = top + i;
    if (idx >= totalItems) break;

    String label = libraryEntryLabel(idx);
    bool isSystem = (g_library.entryTypes[idx] == LIB_ENTRY_BOOKMARKS ||
                     g_library.entryTypes[idx] == LIB_ENTRY_LIST ||
                     g_library.entryTypes[idx] == LIB_ENTRY_ABOUT ||
                     g_library.entryTypes[idx] == LIB_ENTRY_UPLOAD);
    bool boldText = (idx == g_library.selectedItem);
    drawMenuBulletRow(y, label, idx == g_library.selectedItem, boldText,
                      g_library.entryDepths[idx], isSystem);
    y += lineH;
  }

  display.update();
}

void LibraryScreen::onButton(const ButtonEvent& e) {
  if (!e.any()) return;

  int totalItems = g_library.entryCount;

  if (e.kind == ButtonEvent::Short) {
    g_library.selectedItem++;
    if (g_library.selectedItem >= totalItems) g_library.selectedItem = 0;
    draw();
    return;
  }

  if (e.kind != ButtonEvent::Double) return;

  if (g_library.selectedItem < 0 || g_library.selectedItem >= g_library.entryCount) {
    draw();
    return;
  }

  LibraryEntryType entryType = g_library.entryTypes[g_library.selectedItem];
  int entryRef = g_library.entryRefs[g_library.selectedItem];

  if (entryType == LIB_ENTRY_FOLDER) {
    bool expanded = isFolderExpanded(entryRef);
    setFolderExpanded(entryRef, !expanded);
    draw();
    return;
  }

  if (entryType == LIB_ENTRY_BOOK) {
    if (openBookByIndex(entryRef)) {
      nextScreen = &g_readerScreen;
    } else {
      drawCenter("Open failed", "Try upload again");
      draw();
    }
    return;
  }

  if (entryType == LIB_ENTRY_BOOKMARKS) {
    nextScreen = &g_bmBookSelectScreen;
    return;
  }

  if (entryType == LIB_ENTRY_LIST) {
    g_list.selectedIndex = 0;
    nextScreen = &g_listScreen;
    return;
  }

  if (entryType == LIB_ENTRY_ABOUT) {
    nextScreen = &g_aboutScreen;
    return;
  }

  nextScreen = &g_uploadScreen;
}

void navigateToLibraryRoot() {
  if (g_currentScreen) g_currentScreen->nextScreen = &g_libraryScreen;
}

void resetNavigationState() {
  g_library.currentFolder = "";
  g_library.selectedItem = 0;
}
