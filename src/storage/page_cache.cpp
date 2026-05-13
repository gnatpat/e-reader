#include "storage/page_cache.h"

#include "storage/library.h"  // g_library (used by invalidateAllPageCaches)
#include "ui/reader.h"        // g_reader.pageOffsets, knownPages, etc.

#include "storage/bookmarks.h"        // loadBookmarks / saveBookmarks
#include "storage/preferences_store.h"
#include "pure/hashing.h"

// ============================================================================
//  RAM offset cache — single instance owned here; external callers go
//  through the path-based wrappers below.
// ============================================================================
static OffsetCache s_offsetCache;

void resetOffsetCache() {
  s_offsetCache.reset();
}

bool lookupOffsetCache(const String& path, int targetPage,
                       int& cachedPage, uint32_t& cachedOffset) {
  return s_offsetCache.lookup(hashPath32(path), targetPage, cachedPage, cachedOffset);
}

void storeOffsetCache(const String& path, int page, uint32_t offset) {
  s_offsetCache.store(hashPath32(path), page, offset);
}

// ============================================================================
//  On-disk page-offset cache
// ============================================================================
String pageCachePathForBook(const String& path) {
  return String("/pc_") + prefKeyForBook(path) + ".bin";
}

bool loadPageOffsetCacheForBook(const String& path, size_t expectedSize) {
  String cachePath = pageCachePathForBook(path);
  File f = FS.open(cachePath, "r");
  if (!f) return false;

  uint32_t magic = 0;
  uint32_t fileSize = 0;
  uint16_t count = 0;

  if (f.read((uint8_t*)&magic, sizeof(magic)) != sizeof(magic))      { f.close(); return false; }
  if (f.read((uint8_t*)&fileSize, sizeof(fileSize)) != sizeof(fileSize)) { f.close(); return false; }
  if (f.read((uint8_t*)&count, sizeof(count)) != sizeof(count))      { f.close(); return false; }

  if (magic != 0x50434F46UL || fileSize != (uint32_t)expectedSize || count == 0 || count > MAX_PAGES) {
    f.close();
    return false;
  }

  g_reader.knownPages = 0;
  for (uint16_t i = 0; i < count; i++) {
    uint32_t off = 0;
    if (f.read((uint8_t*)&off, sizeof(off)) != sizeof(off)) break;
    g_reader.pageOffsets[i] = off;
    g_reader.knownPages++;
  }
  f.close();

  if (g_reader.knownPages == 0) {
    g_reader.knownPages = 1;
    g_reader.pageOffsets[0] = 0;
    return false;
  }
  return true;
}

void savePageOffsetCacheForBook(const String& path, size_t fileSize) {
  if (g_reader.knownPages <= 1) return;

  String cachePath = pageCachePathForBook(path);
  File f = FS.open(cachePath, "w");
  if (!f) return;

  uint32_t magic = 0x50434F46UL;
  uint32_t size32 = (uint32_t)fileSize;
  uint16_t count16 = (uint16_t)min(g_reader.knownPages, MAX_PAGES);

  f.write((const uint8_t*)&magic, sizeof(magic));
  f.write((const uint8_t*)&size32, sizeof(size32));
  f.write((const uint8_t*)&count16, sizeof(count16));
  f.write((const uint8_t*)g_reader.pageOffsets, count16 * sizeof(uint32_t));
  f.close();
}

void invalidateAllPageCaches() {
  // Page offsets are computed from the current font size and line spacing.
  // When either changes, ALL cached offsets are invalid -- both the in-memory
  // arrays and the on-disk .bin files. We also reset the saved page index to 0
  // for every book, because page N at font size 8 is a completely different
  // byte position at font size 12.

  resetOffsetCache();

  // Remove all on-disk page-cache files (pc_*.bin)
  File root = FS.open("/");
  if (root && root.isDirectory()) {
    File f = root.openNextFile();
    while (f) {
      String name = String(f.name());
      bool removeIt = name.startsWith("/pc_") && name.endsWith(".bin");
      f.close();
      if (removeIt) FS.remove(name);
      f = root.openNextFile();
    }
    root.close();
  } else if (root) {
    root.close();
  }

  // Reset progress and bookmark offsets for every book. Bookmark page numbers
  // are kept but offsets are set to 0xFFFFFFFF so they get recomputed on next
  // access.
  PreferencesStore kv(prefs);
  for (int i = 0; i < g_library.bookCount; i++) {
    String key = prefKeyForBook(String(g_library.books[i].path));
    kv.putInt((key + "_p").c_str(), 0);

    Bookmarks bm = loadBookmarks(kv, key);
    if (bm.count > 0) {
      for (uint8_t j = 0; j < bm.count; j++) bm.offsets[j] = 0xFFFFFFFFUL;
      saveBookmarks(kv, key, bm);
    }
  }

  // Reset the currently open book's in-memory state.
  if (g_reader.currentBookPath.length() > 0) {
    g_reader.pageIndex    = 0;
    g_reader.knownPages   = 1;
    g_reader.pageOffsets[0] = 0;
    g_reader.eofReached   = false;
    resetSaveThrottle();
    if (g_reader.currentBookKey.length() > 0) {
      kv.putInt((g_reader.currentBookKey + "_p").c_str(), 0);
    }
  }
}
