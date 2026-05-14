#pragma once

#include "config.h"
#include "state.h"
#include "pure/page_offset_table.h"

// ============================================================================
//  In-RAM offset cache wrappers (path-based). Backed by a private OffsetCache
//  instance inside page_cache.cpp. The wrappers hash the path internally so
//  callers can keep using string paths.
// ============================================================================
void resetOffsetCache();
bool lookupOffsetCache(const String& path, int targetPage,
                       int& cachedPage, uint32_t& cachedOffset);
void storeOffsetCache(const String& path, int page, uint32_t offset);

// ============================================================================
//  On-disk page-offset cache (pc_<hash>.bin files in LittleFS root)
// ============================================================================
String pageCachePathForBook(const String& path);

// Load the persisted offset table for `path` into `out`. Returns true on
// success (magic + size check passed); on false, `out` is left untouched
// (callers typically seed it with offsets[0]=0, count=1 themselves).
bool loadPageOffsetCacheForBook(const String& path, size_t expectedSize,
                                PageOffsetTable& out);

// Persist `in` for `path`. No-op if `in.count <= 1` (nothing useful to save).
void savePageOffsetCacheForBook(const String& path, size_t fileSize,
                                const PageOffsetTable& in);

// Remove the on-disk page-cache file for `path` (no-op if absent).
void deletePageCacheForBook(const String& path);

// Move the on-disk page-cache file from `oldPath` to `newPath` (no-op if
// no source file). If a stale destination exists, it's removed first.
void renamePageCacheForBook(const String& oldPath, const String& newPath);

// Wipes the RAM LRU offset cache and deletes every pc_*.bin file on disk.
// Does NOT touch saved progress or bookmark offsets — those are separate
// concerns. For the full "layout changed, reset everything" composition,
// use `resetAllPagination()` in storage/book_metadata.h.
void invalidateAllPageCaches();
