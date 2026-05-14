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
    clearCurrentBookState();
    nextScreen = &g_bmListScreen;
    return;
  }

  if (e.kind == ButtonEvent::Long) {
    // Commit — keep the book open, hand off to reader. Force-save progress
    // at the bookmark's page so a sleep-before-render still resumes here.
    saveProgressThrottled(true);
    savePageOffsetCacheForBook(g_reader.book.path(), g_reader.book.size(),
                               g_settings.fontSize, g_settings.lineGap,
                               g_reader.pages);
    nextScreen = &g_readerScreen;
    return;
  }

  if (e.kind == ButtonEvent::Double) {
    if (g_reader.cursor.pageIndex > 0) {
      g_reader.cursor.pageIndex--;
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
    if (g_reader.cursor.pageIndex != oldPage) {
      g_reader.cursor.pageTurnsSinceFull++;
      renderCurrentPage();
    }
    return;
  }
}

void BookmarkPreviewScreen::onSleep() {
  // Preview is transient — don't commit progress, don't arm wake state.
  // Just release the file handle for LittleFS hygiene; next boot will land
  // in library because wake state stays empty.
  g_reader.book.close();
}
