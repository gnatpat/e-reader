#include "ui/screens/library_screen.h"

#include "hal/display.h"
#include "storage/library.h"
#include "storage/list_items.h"
#include "ui/reader.h"
#include "ui/screens/about_screen.h"
#include "ui/screens/bookmarks/book_select_screen.h"
#include "ui/screens/bookmarks/session.h"
#include "ui/screens/list_screen.h"
#include "ui/screens/reader_screen.h"
#include "ui/screens/upload_screen.h"
#include "ui/widgets.h"

// LibraryScreen-specific row indenting. Other menu screens pass nothing
// extra to drawMenuRow; the library is the only one with folder nesting
// and a distinguishing nudge for "system" entries (Bookmarks/List/Device/
// Upload), so the math lives here, not in the shared widget header.
static const int LIBRARY_DEPTH_INDENT = 10;
static const int LIBRARY_SYSTEM_NUDGE = 2;

void LibraryScreen::onEnter() {
  // Full reset on every library entry. Web/upload flows run from here, so
  // by clearing now, downstream code never has to reason about stale
  // reader state. NVS holds whatever persistent state matters. Wake state
  // doesn't need clearing — it's consumed at boot, so it's already empty
  // during runtime unless the reader has explicitly set it for next-boot.
  clearCurrentBookState();
  resetBookmarkSession();
  resetNavigationState();
  draw();
}

static bool isSystemEntry(int idx) {
  LibraryEntryType t = g_library.entryTypes[idx];
  return t == LIB_ENTRY_BOOKMARKS || t == LIB_ENTRY_LIST
      || t == LIB_ENTRY_ABOUT || t == LIB_ENTRY_UPLOAD;
}

static int libraryRowIndent(int idx) {
  int indent = g_library.entryDepths[idx] * LIBRARY_DEPTH_INDENT;
  if (isSystemEntry(idx)) indent += LIBRARY_SYSTEM_NUDGE;
  return indent;
}

void LibraryScreen::draw() {
  prepareMenuFrame();
  u8g2.setFont(MAIN_FONT);
  buildLibraryEntries();
  int y = drawSectionHeader("Library");

  drawScrollableList(y, g_library.entryCount, g_library.selectedItem,
    [&](int idx, int rowY, bool selected, int /*budget*/) {
      drawMenuRow(rowY, libraryEntryLabel(idx), selected, libraryRowIndent(idx));
      return 1;
    });

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
