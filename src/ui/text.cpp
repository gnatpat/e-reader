#include "ui/text.h"

#include "hal/display.h"          // u8g2 + MAIN_FONT + getMetrics
#include "storage/page_cache.h"   // offset cache + savePageOffsetCacheForBook
#include "ui/reader.h"            // g_reader

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
  auto onLine = [&](const String& line) {
    if (draw) {
      u8g2.setCursor(MARGIN_X, cursorY);
      u8g2.print(line.c_str());
      cursorY += m.lineH;
    }
    if (outText) {
      String t = line;
      t.trim();
      (*outText) += t;
      (*outText) += "\n";
    }
  };

  return paginatePage(stream, startPos, m, u8g2Measure, onLine);
}

uint32_t buildNextOffsetFor(File& f, uint32_t startPos) {
  return readPageFromFile(f, startPos, false, nullptr);
}

uint32_t buildNextOffset(uint32_t startPos) {
  uint32_t next = readPageFromFile(g_reader.file, startPos, false, nullptr);
  // Use file size instead of available() for reliable EOF detection.
  // available() is unreliable after internal seeks inside paginatePage.
  if (next >= (uint32_t)g_reader.file.size()) g_reader.eofReached = true;
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
  if (g_reader.pages.count < 1) {
    g_reader.pages.count = 1;
    g_reader.pages.offsets[0] = 0;
  }

  bool addedOffsets = false;
  while (!g_reader.eofReached && g_reader.pages.count <= targetPage && g_reader.pages.count < MAX_PAGES) {
    uint32_t start = g_reader.pages.offsets[g_reader.pages.count - 1];
    uint32_t next = buildNextOffset(start);
    if (next <= start) {
      g_reader.eofReached = true;
      break;
    }
    g_reader.pages.offsets[g_reader.pages.count] = next;
    storeOffsetCache(g_reader.currentBookPath, g_reader.pages.count, next);
    g_reader.pages.count++;
    addedOffsets = true;
  }

  if (g_reader.pageIndex >= g_reader.pages.count) g_reader.pageIndex = g_reader.pages.count - 1;
  if (g_reader.pageIndex < 0) g_reader.pageIndex = 0;

  if (addedOffsets && (g_reader.pages.count % 50 == 0 || g_reader.eofReached)) {
    if (g_reader.file) savePageOffsetCacheForBook(g_reader.currentBookPath, g_reader.file.size(), g_reader.pages);
  }
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
