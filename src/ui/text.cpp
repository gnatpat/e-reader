#include "ui/text.h"

#include "hal/display.h"            // u8g2 + MAIN_FONT + getMetrics
#include "storage/page_cache.h"     // offset cache + savePageOffsetCacheForBook
#include "storage/settings_store.h" // g_settings — fontSize/lineGap for cache stamping
#include "ui/reader.h"              // g_bookview

// Measure-width adapter so `paginatePage` can ask the u8g2 instance about
// rendered widths under the current font.
static int u8g2Measure(const char* s) {
  return u8g2.getUTF8Width(s);
}

uint32_t readPageFromFile(File& f, uint32_t startPos, bool draw, String* outText) {
  FileReadStream stream(f);
  const LayoutMetrics& m = getMetrics();
  u8g2.setFont(MAIN_FONT);

  int cursorY = TOP_PAD + m.ascent;
  auto onLine = [&](const char* buf, size_t len) {
    if (draw) {
      u8g2.setCursor(MARGIN_X, cursorY);
      u8g2.print(buf);
      cursorY += m.lineH;
    }
    if (outText) {
      // Trim leading whitespace (paginator already trims trailing).
      const char* start = buf;
      size_t remaining = len;
      while (remaining > 0 && (*start == ' ' || *start == '\t')) { start++; remaining--; }
      outText->concat(start, remaining);
      outText->concat('\n');
    }
  };

  return paginatePage(stream, startPos, m, u8g2Measure, onLine);
}

uint32_t buildNextOffsetFor(File& f, uint32_t startPos) {
  return readPageFromFile(f, startPos, false, nullptr);
}

uint32_t buildNextOffset(uint32_t startPos) {
  uint32_t next = readPageFromFile(g_bookview.book.file(), startPos, false, nullptr);
  // Use file size instead of available() for reliable EOF detection.
  // available() is unreliable after internal seeks inside paginatePage.
  if (next >= (uint32_t)g_bookview.book.size()) g_bookview.pages.eofReached = true;
  return next;
}

uint32_t pageOffsetForPage(File& f, const String& path, int page) {
  if (page < 0) page = 0;

  int cachedPage = 0;
  uint32_t cachedOffset = 0;
  if (!lookupOffsetCache(path, page, cachedPage, cachedOffset)) {
    cachedPage = 0;
    cachedOffset = 0;
  }

  uint32_t off = cachedOffset;
  for (int p = cachedPage; p < page; p++) {
    uint32_t next = buildNextOffsetFor(f, off);
    if (next == off) break;
    off = next;
    storeOffsetCache(path, p + 1, off);
  }

  storeOffsetCache(path, page, off);
  return off;
}

void ensureOffsetsUpTo(int targetPage) {
  if (g_bookview.pages.count < 1) {
    g_bookview.pages.count = 1;
    g_bookview.pages.offsets[0] = 0;
  }

  bool addedOffsets = false;
  while (!g_bookview.pages.eofReached && g_bookview.pages.count <= targetPage && g_bookview.pages.count < MAX_PAGES) {
    uint32_t start = g_bookview.pages.offsets[g_bookview.pages.count - 1];
    uint32_t next = buildNextOffset(start);
    if (next <= start) {
      g_bookview.pages.eofReached = true;
      break;
    }
    g_bookview.pages.offsets[g_bookview.pages.count] = next;
    storeOffsetCache(g_bookview.book.path(), g_bookview.pages.count, next);
    g_bookview.pages.count++;
    addedOffsets = true;
  }

  if (g_bookview.cursor.pageIndex >= g_bookview.pages.count) g_bookview.cursor.pageIndex = g_bookview.pages.count - 1;
  if (g_bookview.cursor.pageIndex < 0) g_bookview.cursor.pageIndex = 0;

  if (addedOffsets && (g_bookview.pages.count % 50 == 0 || g_bookview.pages.eofReached)) {
    if (g_bookview.book.isOpen()) {
      savePageOffsetCacheForBook(g_bookview.book.path(), g_bookview.book.size(),
                                 g_settings.fontSize, g_settings.lineGap,
                                 g_bookview.pages);
    }
  }
}

int findPageForOffset(uint32_t targetOffset) {
  // Paginate forward until the last known page starts at or past the target,
  // or we hit EOF / the max page limit. ensureOffsetsUpTo(count) extends by
  // one page each call.
  while (!g_bookview.pages.eofReached
      && g_bookview.pages.count > 0
      && g_bookview.pages.offsets[g_bookview.pages.count - 1] < targetOffset
      && g_bookview.pages.count < MAX_PAGES) {
    int prev = g_bookview.pages.count;
    ensureOffsetsUpTo(prev);
    if (g_bookview.pages.count == prev) break;   // no progress = give up
  }
  // The page containing `targetOffset` is the largest N with
  // offsets[N] <= targetOffset.
  for (int i = g_bookview.pages.count - 1; i >= 0; i--) {
    if (g_bookview.pages.offsets[i] <= targetOffset) return i;
  }
  return 0;
}

uint32_t resolveBookmarkOffset(const String& path, uint16_t page, uint32_t storedOffset) {
  File f = FS.open(path, "r");
  if (!f) return 0;

  size_t size = f.size();
  if (storedOffset != 0xFFFFFFFFUL && storedOffset < size) {
    storeOffsetCache(path, page, storedOffset);
    f.close();
    return storedOffset;
  }

  uint32_t off = pageOffsetForPage(f, path, page);
  f.close();
  return off;
}

String readPageTextForWeb(const String& path, int page) {
  File f = FS.open(path, "r");
  if (!f) return String("Open failed.");
  uint32_t off = pageOffsetForPage(f, path, page);
  String out;
  out.reserve(900);
  (void)readPageFromFile(f, off, false, &out);
  f.close();
  out.trim();
  if (out.length() == 0) out = "(empty)";
  return out;
}
