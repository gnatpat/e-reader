#include "ui/reader.h"

#include "hal/display.h"
#include "pure/hashing.h"
#include "storage/book_metadata.h"
#include "storage/library.h"   // g_library — openBookByIndex reads it
#include "storage/page_cache.h"
#include "storage/preferences_store.h"
#include "storage/settings_store.h"  // g_settings — fontSize/lineGap for cache stamping

#include "ui/screens/library_screen.h"  // navigateToLibraryRoot — fallback on error
#include "ui/text.h"

// The currently open book and where the user is looking. Produced by
// `openBookByIndex()` below, fully torn down by `resetBookView()` on leaving
// the reader.
BookView g_bookview;

// Auto-save throttle bookkeeping. Private to this translation unit — only
// the save-progress functions and `resetSaveThrottle` touch it, and only
// the reader calls them on its hot path (preview never auto-saves).
// Hiding it here keeps the "this is the open book" public type from
// carrying reader-only baggage.
struct SaveThrottle {
  uint32_t lastSaveMs    = 0;
  int      lastSavedPage = -1;
};
static SaveThrottle s_save;

static void resetSaveThrottle() {
  s_save = SaveThrottle{};
}

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
  if (!g_bookview.book.isOpen()) return;
  prefs.putString("wake_path", g_bookview.book.path());
}

static void clearResumeOnWake() {
  prefs.remove("wake_path");
}

// ============================================================================
//  Reader operations
// ============================================================================
bool openBookByIndex(int idx) {
  g_bookview.book.close();
  if (idx < 0 || idx >= g_library.bookCount) return false;

  String path = String(g_library.books[idx].path);
  if (!g_bookview.book.open(path)) return false;

  g_bookview.pages.count = 1;
  g_bookview.pages.offsets[0] = 0;
  g_bookview.pages.eofReached = false;
  loadPageOffsetCacheForBook(path, g_bookview.book.size(),
                             g_settings.fontSize, g_settings.lineGap,
                             g_bookview.pages);

  // Resolve the reading position. The byte offset (`_off`) is canonical and
  // survives font changes; if it's set, find which page contains it under
  // the current layout. If absent (legacy data from older firmware), fall
  // back to the saved page number — it'll get rewritten as an offset on
  // the next save.
  PreferencesStore kv(prefs);
  uint32_t savedOffset = loadSavedOffset(kv, g_bookview.book.key());
  if (savedOffset != kOffsetUnset) {
    g_bookview.cursor.pageIndex = findPageForOffset(savedOffset);
  } else {
    g_bookview.cursor.pageIndex = loadSavedPage(kv, g_bookview.book.key());
  }
  if (g_bookview.cursor.pageIndex < 0) g_bookview.cursor.pageIndex = 0;
  g_bookview.cursor.pageTurnsSinceFull = 0;
  resetSaveThrottle();

  storeOffsetCache(path, 0, 0);

  int warmTarget = g_bookview.cursor.pageIndex + PREFETCH_AHEAD_PAGES;
  if (warmTarget < 1) warmTarget = 1;
  ensureOffsetsUpTo(warmTarget);
  return true;
}

static void drawStatusBar(uint32_t startOffset) {
  size_t total = g_bookview.book.size();
  if (total == 0) total = 1;

  int pageTextW = 0;
  if (SHOW_PAGE_NUMBER) {
    u8g2.setFont(PAGE_FONT);
    char buf[20];
    snprintf(buf, sizeof(buf), "%d", g_bookview.cursor.pageIndex + 1);
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
// All g_bookview mutations triggered by rendering are concentrated here;
// the actual draw in renderCurrentPage is read-only on view state
// (modulo pageTurnsSinceFull, which is render-side bookkeeping).
static bool prepareForRender() {
  if (g_bookview.book.size() == 0) return false;
  ensureOffsetsUpTo(g_bookview.cursor.pageIndex);
  if (g_bookview.pages.count <= 0) return false;

  if (g_bookview.cursor.pageIndex < 0) g_bookview.cursor.pageIndex = 0;
  if (g_bookview.cursor.pageIndex >= g_bookview.pages.count)
    g_bookview.cursor.pageIndex = g_bookview.pages.count - 1;

  if (g_bookview.pages.offsets[g_bookview.cursor.pageIndex] >= g_bookview.book.size()) {
    g_bookview.cursor.pageIndex = 0;
    g_bookview.pages.count = 1;
    g_bookview.pages.offsets[0] = 0;
    g_bookview.pages.eofReached = false;
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

  uint32_t start = g_bookview.pages.offsets[g_bookview.cursor.pageIndex];
  g_bookview.book.file().seek(start);

  bool doFull = (g_bookview.cursor.pageTurnsSinceFull >= FULL_REFRESH_EVERY_N_PAGES);
  if (doFull) {
    display.fastmodeOff();
    display.clear();
    g_bookview.cursor.pageTurnsSinceFull = 0;
  } else {
    display.fastmodeOn();
  }

  beginPageCanvas();
  u8g2.setFont(MAIN_FONT);

  uint32_t nextOff = readPageFromFile(g_bookview.book.file(), start, true, nullptr);
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
    g_bookview.cursor.pageTurnsSinceFull = FULL_REFRESH_EVERY_N_PAGES;
    return true;
  }
  return false;
}

void idlePrefetchReader() {
  // Only ReaderScreen::onIdleTick calls this, and the dispatcher only ticks
  // the active screen, so "we're in the reader" + "preview isn't active"
  // are guaranteed by construction.
  static uint32_t lastIdlePrefetchMs = 0;
  if (!g_bookview.book.isOpen()) return;
  if (g_bookview.pages.eofReached) return;
  uint32_t now = millis();
  if ((uint32_t)(now - lastIdlePrefetchMs) < 60) return;
  lastIdlePrefetchMs = now;
  ensureOffsetsUpTo(g_bookview.cursor.pageIndex + READER_IDLE_PREFETCH_PAGES);
}

// ============================================================================
//  Cursor navigation — shared between ReaderScreen and BookmarkPreviewScreen.
//  Both move the cursor and bump `pageTurnsSinceFull` on a real change; the
//  caller-side post-move work (save progress, render) varies between the
//  screens and stays at the call site.
// ============================================================================
bool advancePage() {
  int oldPage = g_bookview.cursor.pageIndex;
  g_bookview.cursor.pageIndex++;
  ensureOffsetsUpTo(g_bookview.cursor.pageIndex);
  if (g_bookview.pages.eofReached && g_bookview.cursor.pageIndex >= g_bookview.pages.count)
    g_bookview.cursor.pageIndex = g_bookview.pages.count - 1;
  if (g_bookview.cursor.pageIndex < 0) g_bookview.cursor.pageIndex = 0;
  if (g_bookview.cursor.pageIndex == oldPage) return false;
  g_bookview.cursor.pageTurnsSinceFull++;
  return true;
}

bool retreatPage() {
  if (g_bookview.cursor.pageIndex <= 0) return false;
  g_bookview.cursor.pageIndex--;
  g_bookview.cursor.pageTurnsSinceFull++;
  return true;
}

// ============================================================================
//  Full view reset — used when leaving the reader entirely.
// ============================================================================
void resetBookView() {
  g_bookview.book.close();
  g_bookview.cursor = ReaderCursor{};
  g_bookview.pages  = PageOffsetTable{};
  resetSaveThrottle();
}

// ============================================================================
//  Progress + bookmark glue. Reads/writes g_bookview, delegates the actual
//  persistence to the pure storage API. Lives here (not in storage/) because
//  the throttle decision and the "is a book open" guard are view-state
//  concerns.
// ============================================================================
void saveProgress() {
  if (!g_bookview.book.isOpen()) return;

  PreferencesStore kv(prefs);
  // Canonical: the byte offset where the user currently is. Invariant
  // under font/layout changes.
  saveSavedOffset(kv, g_bookview.book.key(),
                  g_bookview.pages.offsets[g_bookview.cursor.pageIndex]);
  // Hint: the page number at the current layout. Used by the web file
  // list for display and as a legacy fallback for openBookByIndex when
  // no offset has been saved yet.
  saveSavedPage(kv, g_bookview.book.key(), g_bookview.cursor.pageIndex);
  s_save.lastSaveMs    = millis();
  s_save.lastSavedPage = g_bookview.cursor.pageIndex;
}

void saveProgressThrottled() {
  if (!g_bookview.book.isOpen()) return;
  if (g_bookview.cursor.pageIndex == s_save.lastSavedPage) return;
  uint32_t now = millis();
  if (s_save.lastSaveMs != 0 && (now - s_save.lastSaveMs) < SAVE_EVERY_MS) return;
  saveProgress();
}

const char* addBookmarkForCurrentBook() {
  if (!g_bookview.book.isOpen()) return nullptr;

  PreferencesStore kv(prefs);
  Bookmarks bm = loadBookmarks(kv, g_bookview.book.key());

  uint32_t pageOff = g_bookview.pages.offsets[g_bookview.cursor.pageIndex];
  BookmarkAddResult r = addBookmark(bm, (uint16_t)g_bookview.cursor.pageIndex, pageOff);
  if (r.added) {
    saveBookmarks(kv, g_bookview.book.key(), bm);
    savePageOffsetCacheForBook(g_bookview.book.path(), g_bookview.book.size(),
                               g_settings.fontSize, g_settings.lineGap,
                               g_bookview.pages);
  }
  return r.message;
}
