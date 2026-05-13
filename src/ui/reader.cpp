#include "ui/reader.h"

#include "hal/display.h"
#include "pure/hashing.h"
#include "storage/book_state.h"
#include "storage/bookmarks.h"
#include "storage/library.h"   // g_library — openBookByIndex reads it
#include "storage/page_cache.h"

// The active reader session. Produced by `openBookByIndex()` below,
// torn down by `clearCurrentBookState()` / `safeCloseCurrentBook()` in
// `storage/book_state.cpp`.
ReaderState g_reader;
#include "ui/screens/library_screen.h"  // navigateToLibraryRoot — fallback on error
#include "ui/text.h"

bool openBookByIndex(int idx) {
  safeCloseCurrentBook();
  if (idx < 0 || idx >= g_library.bookCount) return false;

  String path = String(g_library.books[idx].path);
  File f = FS.open(path, "r");
  if (!f || f.isDirectory()) {
    if (f) f.close();
    return false;
  }

  g_reader.file = f;
  g_reader.currentBookKey = prefKeyForBook(path);
  g_reader.currentBookPath = path;
  g_reader.knownPages = 1;
  g_reader.pageOffsets[0] = 0;
  g_reader.eofReached = false;
  loadPageOffsetCacheForBook(path, g_reader.file.size());
  g_reader.pageIndex = prefs.getInt((g_reader.currentBookKey + "_p").c_str(), 0);
  if (g_reader.pageIndex < 0) g_reader.pageIndex = 0;
  g_reader.pageTurnsSinceFull = 0;
  resetSaveThrottle();
  syncWakeState(true);

  storeOffsetCache(path, 0, 0);

  int warmTarget = g_reader.pageIndex + PREFETCH_AHEAD_PAGES;
  if (warmTarget < 1) warmTarget = 1;
  ensureOffsetsUpTo(warmTarget);
  return true;
}

void drawStatusBar(uint32_t startOffset) {
  size_t total = g_reader.file.size();
  if (total == 0) total = 1;

  int pageTextW = 0;
  if (SHOW_PAGE_NUMBER) {
    u8g2.setFont(PAGE_FONT);
    char buf[20];
    snprintf(buf, sizeof(buf), "%d", g_reader.pageIndex + 1);
    pageTextW = u8g2.getUTF8Width(buf);
    u8g2.setCursor(SCREEN_W - MARGIN_X - pageTextW, SCREEN_H - 1);
    u8g2.print(buf);
    u8g2.setFont(MAIN_FONT);
  }

  if (SHOW_PROGRESS_BAR) {
    const int padR = SHOW_PAGE_NUMBER ? (pageTextW + 8) : 0;
    int w = (SCREEN_W - 2 * MARGIN_X) - padR;
    if (w < 40) w = 40;

    int x0 = MARGIN_X;
    int yTop = SCREEN_H - 7;
    int barH = 4;
    int filled = (int)((startOffset * (uint32_t)w) / (uint32_t)total);
    if (filled < 0) filled = 0;
    if (filled > w) filled = w;

    gfx.drawRect(x0, yTop, w, barH, 1);
    if (filled > 0) gfx.fillRect(x0 + 1, yTop + 1, max(0, filled - 2), barH - 2, 1);
  }
}

void renderCurrentPage() {
  if (!g_reader.file && !reopenCurrentBookIfNeeded()) {
    drawCenter("Open failed", "Back to library");
    navigateToLibraryRoot();
    return;
  }

  if (!g_reader.file || g_reader.file.isDirectory()) {
    drawCenter("Open failed", "Back to library");
    navigateToLibraryRoot();
    return;
  }

  size_t bookSize = g_reader.file.size();
  if (bookSize == 0) {
    drawCenter("Book empty", "Back to library");
    navigateToLibraryRoot();
    return;
  }

  ensureOffsetsUpTo(g_reader.pageIndex);
  if (g_reader.knownPages <= 0) {
    drawCenter("Book empty", "Back to library");
    navigateToLibraryRoot();
    return;
  }

  if (g_reader.pageIndex < 0) g_reader.pageIndex = 0;
  if (g_reader.pageIndex >= g_reader.knownPages) g_reader.pageIndex = g_reader.knownPages - 1;

  if (g_reader.pageOffsets[g_reader.pageIndex] >= bookSize) {
    g_reader.pageIndex = 0;
    g_reader.knownPages = 1;
    g_reader.pageOffsets[0] = 0;
    g_reader.eofReached = false;
  }

  uint32_t start = g_reader.pageOffsets[g_reader.pageIndex];
  g_reader.lastPageStartOffset = start;
  g_reader.file.seek(start);

  bool doFull = (g_reader.pageTurnsSinceFull >= FULL_REFRESH_EVERY_N_PAGES);
  if (doFull) {
    display.fastmodeOff();
    display.clear();
    g_reader.pageTurnsSinceFull = 0;
  } else {
    display.fastmodeOn();
  }

  beginPageCanvas();
  u8g2.setFont(MAIN_FONT);

  uint32_t nextOff = readPageFromFile(g_reader.file, start, true, nullptr);
  (void)nextOff;

  bool toastActive = (g_toast.untilMs != 0) && ((int32_t)(millis() - g_toast.untilMs) <= 0);
  if (toastActive) drawToastIfActive();
  else drawStatusBar(start);

  display.update();
}

void idlePrefetchReader() {
  // Only ReaderScreen::onIdleTick calls this, and the dispatcher only ticks
  // the active screen, so "we're in the reader" + "preview isn't active"
  // are guaranteed by construction. Just guard the file/EOF state.
  static uint32_t lastIdlePrefetchMs = 0;
  if (!g_reader.file) return;
  if (g_reader.eofReached) return;
  uint32_t now = millis();
  if ((uint32_t)(now - lastIdlePrefetchMs) < 60) return;
  lastIdlePrefetchMs = now;
  ensureOffsetsUpTo(g_reader.pageIndex + READER_IDLE_PREFETCH_PAGES);
}
