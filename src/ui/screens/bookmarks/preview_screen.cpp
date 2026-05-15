#include "ui/screens/bookmarks/preview_screen.h"

#include "storage/page_cache.h"
#include "storage/settings_store.h"  // g_settings — fontSize/lineGap for cache stamping
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
    // Cancel — full clear so we don't leave a half-open state for the next
    // screen. The bookmark list screen opens its own file handle for labels.
    resetBookView();
    nextScreen = &g_bmListScreen;
    return;
  }

  if (e.kind == ButtonEvent::Long) {
    // Commit — keep the book open, hand off to reader. Force-save progress
    // at the bookmark's page so a sleep-before-render still resumes here.
    saveProgress();
    savePageOffsetCacheForBook(g_bookview.book.path(), g_bookview.book.size(),
                               g_settings.fontSize, g_settings.lineGap,
                               g_bookview.pages);
    nextScreen = &g_readerScreen;
    return;
  }

  if (e.kind == ButtonEvent::Double) {
    if (retreatPage()) renderCurrentPage();
    return;
  }

  if (e.kind == ButtonEvent::Short) {
    if (advancePage()) renderCurrentPage();
    return;
  }
}

void BookmarkPreviewScreen::onSleep() {
  // Preview is transient — don't commit progress, don't arm wake state.
  // Just release the file handle for LittleFS hygiene; next boot will land
  // in library because wake state stays empty.
  g_bookview.book.close();
}
