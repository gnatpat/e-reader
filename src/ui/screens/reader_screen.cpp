#include "ui/screens/reader_screen.h"

#include "hal/display.h"
#include "ui/reader.h"
#include "ui/screens/library_screen.h"
#include "ui/text.h"
#include "ui/toast.h"  // Toast::show for bookmark-saved feedback

void ReaderScreen::onEnter() {
  draw();
}

void ReaderScreen::draw() {
  renderCurrentPage();
}

void ReaderScreen::onButton(const ButtonEvent& e) {
  if (e.kind == ButtonEvent::Triple) {
    // Catch any progress / pagination since the last throttled save before
    // we lose the active book.
    persistReaderState();
    navigateToLibraryRoot();
    return;
  }

  if (e.kind == ButtonEvent::Long) {
    const char* msg = addBookmarkForCurrentBook();
    if (msg) Toast::show(msg);
    g_bookview.cursor.pageTurnsSinceFull++;
    draw();
    return;
  }

  if (e.kind == ButtonEvent::Double) {
    if (retreatPage()) {
      saveProgressThrottled();
      draw();
    }
    return;
  }

  if (e.kind == ButtonEvent::Short) {
    if (advancePage()) {
      saveProgressThrottled();
      draw();
    }
    return;
  }
}

void ReaderScreen::onSleep() {
  // Strong invariant: reader screen is never active without an open book.
  persistReaderState();
  armResumeOnWake();        // captures path before resetBookView() drops it
  resetBookView();
}
