#include "storage/page_cache.h"

#include "pure/hashing.h"   // prefKeyForBook (used in pageCachePathForBook)

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

bool loadPageOffsetCacheForBook(const String& path, size_t expectedSize,
                                PageOffsetTable& out) {
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

  int loaded = 0;
  for (uint16_t i = 0; i < count; i++) {
    uint32_t off = 0;
    if (f.read((uint8_t*)&off, sizeof(off)) != sizeof(off)) break;
    out.offsets[i] = off;
    loaded++;
  }
  f.close();

  if (loaded == 0) return false;
  out.count = loaded;
  return true;
}

void savePageOffsetCacheForBook(const String& path, size_t fileSize,
                                const PageOffsetTable& in) {
  if (in.count <= 1) return;

  String cachePath = pageCachePathForBook(path);
  File f = FS.open(cachePath, "w");
  if (!f) return;

  uint32_t magic = 0x50434F46UL;
  uint32_t size32 = (uint32_t)fileSize;
  uint16_t count16 = (uint16_t)min(in.count, MAX_PAGES);

  f.write((const uint8_t*)&magic, sizeof(magic));
  f.write((const uint8_t*)&size32, sizeof(size32));
  f.write((const uint8_t*)&count16, sizeof(count16));
  f.write((const uint8_t*)in.offsets, count16 * sizeof(uint32_t));
  f.close();
}

void deletePageCacheForBook(const String& path) {
  String cachePath = pageCachePathForBook(path);
  if (FS.exists(cachePath)) FS.remove(cachePath);
}

void renamePageCacheForBook(const String& oldPath, const String& newPath) {
  String oldCache = pageCachePathForBook(oldPath);
  if (!FS.exists(oldCache)) return;
  String newCache = pageCachePathForBook(newPath);
  if (FS.exists(newCache)) FS.remove(newCache);
  FS.rename(oldCache, newCache);
}

void invalidateAllPageCaches() {
  // Page offsets are computed from the current font size and line spacing.
  // When either changes, both the in-memory LRU and the on-disk .bin files
  // are stale and have to go.
  //
  // Per-book saved progress + bookmark byte offsets are also stale at this
  // point — those live in `storage/book_metadata.cpp` and are reset there by
  // `resetAllSavedProgress` + `invalidateAllBookmarkOffsets`. The full
  // "layout changed, reset everything" composition is `resetAllPagination()`
  // in `storage/book_metadata.h`, which is what callers actually invoke
  // when font size or line spacing changes.
  resetOffsetCache();

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
}
