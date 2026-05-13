#include "ui/screens/reader_screen.h"

#include "hal/display.h"
#include "storage/book_state.h"
#include "storage/bookmarks.h"
#include "storage/page_cache.h"
#include "ui/reader.h"
#include "ui/screens/library_screen.h"
#include "ui/text.h"

void ReaderScreen::onEnter() {
  draw();
}

void ReaderScreen::draw() {
  renderCurrentPage();
}

void ReaderScreen::onButton(const ButtonEvent& e) {
  if (e.kind == ButtonEvent::Triple) {
    navigateToLibraryRoot();
    return;
  }

  if (e.kind == ButtonEvent::Long) {
    const char* msg = addBookmarkForCurrentBook();
    if (msg) showToast(msg);
    g_reader.pageTurnsSinceFull++;
    renderCurrentPage();
    return;
  }

  if (e.kind == ButtonEvent::Double) {
    if (g_reader.pageIndex > 0) {
      g_reader.pageIndex--;
      saveProgressThrottled(false);
      g_reader.pageTurnsSinceFull++;
      renderCurrentPage();
    }
    return;
  }

  if (e.kind == ButtonEvent::Short) {
    int oldPage = g_reader.pageIndex;
    g_reader.pageIndex++;
    ensureOffsetsUpTo(g_reader.pageIndex);
    if (g_reader.eofReached && g_reader.pageIndex >= g_reader.pages.count)
      g_reader.pageIndex = g_reader.pages.count - 1;
    if (g_reader.pageIndex < 0) g_reader.pageIndex = 0;
    if (g_reader.pageIndex != oldPage) {
      saveProgressThrottled(false);
      g_reader.pageTurnsSinceFull++;
      renderCurrentPage();
    }
    return;
  }
}

void ReaderScreen::onIdleTick() {
  idlePrefetchReader();
}

void ReaderScreen::onSleep() {
  saveProgressThrottled(true);
  if (g_reader.file) savePageOffsetCacheForBook(g_reader.currentBookPath, g_reader.file.size(), g_reader.pages);
  syncWakeState(g_reader.currentBookPath.length() > 0, g_reader.currentBookPath);
}
