#include "ui/screens/reader_screen.h"

#include "hal/display.h"
#include "storage/page_cache.h"
#include "ui/font.h"  // currentBodySize/currentLineGap for cache stamping
#include "ui/reader.h"
#include "ui/screens/library_screen.h"
#include "ui/text.h"
#include "ui/toast.h"  // showToast for bookmark-saved feedback

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
    g_bookview.cursor.pageTurnsSinceFull++;
    renderCurrentPage();
    return;
  }

  if (e.kind == ButtonEvent::Double) {
    if (retreatPage()) {
      saveProgressThrottled();
      renderCurrentPage();
    }
    return;
  }

  if (e.kind == ButtonEvent::Short) {
    if (advancePage()) {
      saveProgressThrottled();
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
  saveProgress();
  savePageOffsetCacheForBook(g_bookview.book.path(), g_bookview.book.size(),
                             Font::currentBodySize(), Font::currentLineGap(),
                             g_bookview.pages);
  armResumeOnWake();        // captures path before resetBookView() drops it
  resetBookView();
}
