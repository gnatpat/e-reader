#include "ui/screens/bookmarks/preview_screen.h"

#include "storage/book_state.h"
#include "storage/bookmarks.h"
#include "storage/page_cache.h"
#include "ui/reader.h"
#include "ui/screens/bookmarks/bookmark_list_screen.h"
#include "ui/screens/reader_screen.h"
#include "ui/text.h"

void BookmarkPreviewScreen::onEnter() {
  draw();
}

void BookmarkPreviewScreen::draw() {
  renderCurrentPage();
}

void BookmarkPreviewScreen::onButton(const ButtonEvent& e) {
  if (e.kind == ButtonEvent::Triple) {
    safeCloseCurrentBook();
    nextScreen = &g_bmListScreen;
    return;
  }

  if (e.kind == ButtonEvent::Long) {
    saveProgressThrottled(true);
    if (g_reader.file) savePageOffsetCacheForBook(g_reader.currentBookPath, g_reader.file.size());
    nextScreen = &g_readerScreen;
    return;
  }

  if (e.kind == ButtonEvent::Double) {
    if (g_reader.pageIndex > 0) {
      g_reader.pageIndex--;
      g_reader.pageTurnsSinceFull++;
      renderCurrentPage();
    }
    return;
  }

  if (e.kind == ButtonEvent::Short) {
    int oldPage = g_reader.pageIndex;
    g_reader.pageIndex++;
    ensureOffsetsUpTo(g_reader.pageIndex);
    if (g_reader.eofReached && g_reader.pageIndex >= g_reader.knownPages)
      g_reader.pageIndex = g_reader.knownPages - 1;
    if (g_reader.pageIndex != oldPage) {
      g_reader.pageTurnsSinceFull++;
      renderCurrentPage();
    }
    return;
  }
}
