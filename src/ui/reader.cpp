#include "ui/reader.h"

#include "hal/display.h"
#include "pure/hashing.h"
#include "storage/book_metadata.h"
#include "storage/library.h"   // g_library — openBookByIndex reads it
#include "storage/page_cache.h"
#include "storage/preferences_store.h"

#include "ui/screens/library_screen.h"  // navigateToLibraryRoot — fallback on error
#include "ui/text.h"

// The active reader session. Produced by `openBookByIndex()` below,
// fully torn down by `clearCurrentBookState()` on leaving the reader.
ReaderState g_reader;

// ============================================================================
//  OpenBook — file + path + key managed together. Strong invariant:
//  `isOpen()` ⇔ `path()` is non-empty. Public callers never observe
//  the file-closed-with-path-still-set intermediate state.
// ============================================================================
bool OpenBook::open(const String& path) {
  close();
  File f = FS.open(path, "r");
  if (!f || f.isDirectory()) {
    if (f) f.close();
    return false;
  }
  file_ = f;
  path_ = path;
  key_  = prefKeyForBook(path);
  return true;
}

void OpenBook::close() {
  if (file_) file_.close();
  path_ = "";
  key_  = "";
}

void armResumeOnWake() {
  if (!g_reader.book.isOpen()) return;
  prefs.putString("wake_path", g_reader.book.path());
}

void clearResumeOnWake() {
  prefs.remove("wake_path");
}

// ============================================================================
//  Reader operations
// ============================================================================
bool openBookByIndex(int idx) {
  g_reader.book.close();
  if (idx < 0 || idx >= g_library.bookCount) return false;

  String path = String(g_library.books[idx].path);
  if (!g_reader.book.open(path)) return false;

  g_reader.pages.count = 1;
  g_reader.pages.offsets[0] = 0;
  g_reader.pages.eofReached = false;
  loadPageOffsetCacheForBook(path, g_reader.book.size(), g_reader.pages);
  g_reader.cursor.pageIndex = prefs.getInt((g_reader.book.key() + "_p").c_str(), 0);
  if (g_reader.cursor.pageIndex < 0) g_reader.cursor.pageIndex = 0;
  g_reader.cursor.pageTurnsSinceFull = 0;
  resetSaveThrottle();

  storeOffsetCache(path, 0, 0);

  int warmTarget = g_reader.cursor.pageIndex + PREFETCH_AHEAD_PAGES;
  if (warmTarget < 1) warmTarget = 1;
  ensureOffsetsUpTo(warmTarget);
  return true;
}

void drawStatusBar(uint32_t startOffset) {
  size_t total = g_reader.book.size();
  if (total == 0) total = 1;

  int pageTextW = 0;
  if (SHOW_PAGE_NUMBER) {
    u8g2.setFont(PAGE_FONT);
    char buf[20];
    snprintf(buf, sizeof(buf), "%d", g_reader.cursor.pageIndex + 1);
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

// State normalization for rendering: lazily paginates up to the cursor,
// clamps the cursor to a valid range, and recovers from out-of-file
// offsets. Returns false if the book has nothing renderable (empty file
// or no pages) — caller should show an error and bail.
//
// All g_reader mutations triggered by rendering are concentrated here;
// the actual draw in renderCurrentPage is read-only on session state
// (modulo pageTurnsSinceFull, which is render-side bookkeeping).
static bool prepareForRender() {
  if (g_reader.book.size() == 0) return false;
  ensureOffsetsUpTo(g_reader.cursor.pageIndex);
  if (g_reader.pages.count <= 0) return false;

  if (g_reader.cursor.pageIndex < 0) g_reader.cursor.pageIndex = 0;
  if (g_reader.cursor.pageIndex >= g_reader.pages.count)
    g_reader.cursor.pageIndex = g_reader.pages.count - 1;

  if (g_reader.pages.offsets[g_reader.cursor.pageIndex] >= g_reader.book.size()) {
    g_reader.cursor.pageIndex = 0;
    g_reader.pages.count = 1;
    g_reader.pages.offsets[0] = 0;
    g_reader.pages.eofReached = false;
  }
  return true;
}

void renderCurrentPage() {
  // Strong invariant: if we got here, a book is open. The reader screen
  // is never active otherwise.
  if (!prepareForRender()) {
    drawCenter("Book empty", "Back to library");
    navigateToLibraryRoot();
    return;
  }

  uint32_t start = g_reader.pages.offsets[g_reader.cursor.pageIndex];
  g_reader.book.file().seek(start);

  bool doFull = (g_reader.cursor.pageTurnsSinceFull >= FULL_REFRESH_EVERY_N_PAGES);
  if (doFull) {
    display.fastmodeOff();
    display.clear();
    g_reader.cursor.pageTurnsSinceFull = 0;
  } else {
    display.fastmodeOn();
  }

  beginPageCanvas();
  u8g2.setFont(MAIN_FONT);

  uint32_t nextOff = readPageFromFile(g_reader.book.file(), start, true, nullptr);
  (void)nextOff;

  bool toastActive = (g_toast.untilMs != 0) && ((int32_t)(millis() - g_toast.untilMs) <= 0);
  if (toastActive) drawToastIfActive();
  else drawStatusBar(start);

  display.update();
}

bool tryRestoreReadingSession() {
  // Single-shot: read wake intent, then clear it. From this point on
  // (during this runtime session) wake state is empty unless the reader
  // re-arms it on its way into deep sleep.
  String wp = prefs.getString("wake_path", "");
  clearResumeOnWake();

  if (wp.length() == 0) return false;

  for (int i = 0; i < g_library.bookCount; i++) {
    if (strcmp(g_library.books[i].path, wp.c_str()) != 0) continue;
    if (!openBookByIndex(i)) return false;
    // Force a full e-ink refresh on the first post-wake render — partial
    // refresh would leave the sleep image showing through.
    g_reader.cursor.pageTurnsSinceFull = FULL_REFRESH_EVERY_N_PAGES;
    return true;
  }
  return false;
}

void idlePrefetchReader() {
  // Only ReaderScreen::onIdleTick calls this, and the dispatcher only ticks
  // the active screen, so "we're in the reader" + "preview isn't active"
  // are guaranteed by construction.
  static uint32_t lastIdlePrefetchMs = 0;
  if (!g_reader.book.isOpen()) return;
  if (g_reader.pages.eofReached) return;
  uint32_t now = millis();
  if ((uint32_t)(now - lastIdlePrefetchMs) < 60) return;
  lastIdlePrefetchMs = now;
  ensureOffsetsUpTo(g_reader.cursor.pageIndex + READER_IDLE_PREFETCH_PAGES);
}

// ============================================================================
//  Full reader reset — used when leaving the reader entirely.
// ============================================================================
void clearCurrentBookState() {
  g_reader.book.close();
  g_reader.cursor = ReaderCursor{};
  g_reader.pages  = PageOffsetTable{};
  g_reader.save   = SaveThrottle{};
}

// ============================================================================
//  Progress + bookmark glue. Reads/writes g_reader, delegates the actual
//  persistence to the pure storage API. Lives here (not in storage/) because
//  the throttle decision and the "is a book open" guard are reader-state
//  concerns.
// ============================================================================
void resetSaveThrottle() {
  g_reader.save = SaveThrottle{};
}

void saveProgressThrottled(bool force) {
  if (!g_reader.book.isOpen()) return;

  if (!force) {
    if (g_reader.cursor.pageIndex == g_reader.save.lastSavedPage) return;
    uint32_t now = millis();
    if (g_reader.save.lastSaveMs != 0 && (now - g_reader.save.lastSaveMs) < SAVE_EVERY_MS) return;
  }

  PreferencesStore kv(prefs);
  saveSavedPage(kv, g_reader.book.key(), g_reader.cursor.pageIndex);
  g_reader.save.lastSaveMs = millis();
  g_reader.save.lastSavedPage = g_reader.cursor.pageIndex;
}

const char* addBookmarkForCurrentBook() {
  if (!g_reader.book.isOpen()) return nullptr;

  PreferencesStore kv(prefs);
  Bookmarks bm = loadBookmarks(kv, g_reader.book.key());

  uint32_t pageOff = g_reader.pages.offsets[g_reader.cursor.pageIndex];
  const char* msg = addBookmark(bm, (uint16_t)g_reader.cursor.pageIndex, pageOff);
  if (String(msg) == "Bookmark saved") {
    saveBookmarks(kv, g_reader.book.key(), bm);
    savePageOffsetCacheForBook(g_reader.book.path(), g_reader.book.size(), g_reader.pages);
  }
  return msg;
}
