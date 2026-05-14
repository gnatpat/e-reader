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
//
//  Files are stamped with a `layoutVersion` derived from the settings that
//  affect pagination (font size + line gap). Load rejects any file whose
//  stamp doesn't match the current layout — stale files are silently
//  ignored and overwritten on next save. No cross-cutting "invalidate
//  everything" pass is needed on font change; layout-correctness is a
//  property of the file format itself.
// ============================================================================

// Magic was bumped from 0x50434F46 -> 0x50434F47 when `layoutVersion` was
// added. Old files fail the magic check, get ignored, then overwritten.
static constexpr uint32_t kPageCacheMagic = 0x50434F47UL;

// Compact encoding of "what layout were the offsets in this file computed
// under?" — both inputs are tiny ints (fontSize ∈ {8,10,12,14}, lineGap
// ∈ [0,4]) so a stride-by-256 packing is unambiguous and trivially injective.
static uint16_t encodeLayoutVersion(int fontSize, int lineGap) {
  return (uint16_t)((fontSize << 8) | (lineGap & 0xFF));
}

String pageCachePathForBook(const String& path) {
  return String("/pc_") + prefKeyForBook(path) + ".bin";
}

bool loadPageOffsetCacheForBook(const String& path, size_t expectedSize,
                                int fontSize, int lineGap,
                                PageOffsetTable& out) {
  String cachePath = pageCachePathForBook(path);
  File f = FS.open(cachePath, "r");
  if (!f) return false;

  uint32_t magic = 0;
  uint16_t layoutVersion = 0;
  uint32_t fileSize = 0;
  uint16_t count = 0;

  if (f.read((uint8_t*)&magic, sizeof(magic)) != sizeof(magic))                 { f.close(); return false; }
  if (f.read((uint8_t*)&layoutVersion, sizeof(layoutVersion)) != sizeof(layoutVersion)) { f.close(); return false; }
  if (f.read((uint8_t*)&fileSize, sizeof(fileSize)) != sizeof(fileSize))        { f.close(); return false; }
  if (f.read((uint8_t*)&count, sizeof(count)) != sizeof(count))                 { f.close(); return false; }

  if (magic != kPageCacheMagic
      || layoutVersion != encodeLayoutVersion(fontSize, lineGap)
      || fileSize != (uint32_t)expectedSize
      || count == 0 || count > MAX_PAGES) {
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
                                int fontSize, int lineGap,
                                const PageOffsetTable& in) {
  if (in.count <= 1) return;

  String cachePath = pageCachePathForBook(path);
  File f = FS.open(cachePath, "w");
  if (!f) return;

  uint32_t magic = kPageCacheMagic;
  uint16_t layoutVersion = encodeLayoutVersion(fontSize, lineGap);
  uint32_t size32 = (uint32_t)fileSize;
  uint16_t count16 = (uint16_t)min(in.count, MAX_PAGES);

  f.write((const uint8_t*)&magic, sizeof(magic));
  f.write((const uint8_t*)&layoutVersion, sizeof(layoutVersion));
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

