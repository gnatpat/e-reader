#include "ui/screens/reader_screen.h"

#include "hal/display.h"
#include "storage/page_cache.h"
#include "storage/settings_store.h"  // g_settings — fontSize/lineGap for cache stamping
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
    g_reader.cursor.pageTurnsSinceFull++;
    renderCurrentPage();
    return;
  }

  if (e.kind == ButtonEvent::Double) {
    if (g_reader.cursor.pageIndex > 0) {
      g_reader.cursor.pageIndex--;
      saveProgressThrottled(false);
      g_reader.cursor.pageTurnsSinceFull++;
      renderCurrentPage();
    }
    return;
  }

  if (e.kind == ButtonEvent::Short) {
    int oldPage = g_reader.cursor.pageIndex;
    g_reader.cursor.pageIndex++;
    ensureOffsetsUpTo(g_reader.cursor.pageIndex);
    if (g_reader.pages.eofReached && g_reader.cursor.pageIndex >= g_reader.pages.count)
      g_reader.cursor.pageIndex = g_reader.pages.count - 1;
    if (g_reader.cursor.pageIndex < 0) g_reader.cursor.pageIndex = 0;
    if (g_reader.cursor.pageIndex != oldPage) {
      saveProgressThrottled(false);
      g_reader.cursor.pageTurnsSinceFull++;
      renderCurrentPage();
    }
    return;
  }
}

void ReaderScreen::onIdleTick() {
  idlePrefetchReader();
}

void ReaderScreen::onSleep() {
  // Strong invariant: reader screen is never active without an open book.
  saveProgressThrottled(true);
  savePageOffsetCacheForBook(g_reader.book.path(), g_reader.book.size(),
                             g_settings.fontSize, g_settings.lineGap,
                             g_reader.pages);
  armResumeOnWake();
  g_reader.book.close();   // release LittleFS handle before deep sleep
}
