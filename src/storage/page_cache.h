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
//
//  Files are layout-stamped: the saved header includes a `layoutVersion`
//  derived from font size + line gap, and load rejects mismatched files.
//  No external "invalidate everything" pass is needed on font change —
//  stale files are silently ignored on load and overwritten on next save.
// ============================================================================
String pageCachePathForBook(const String& path);

// Load the persisted offset table for `path` into `out`. The cache file
// is layout-stamped at save time; load rejects any file whose stamp
// doesn't match `(fontSize, lineGap)`. Returns true on success (magic +
// layout + size check all passed); on false, `out` is left untouched
// (callers typically seed it with offsets[0]=0, count=1 themselves).
bool loadPageOffsetCacheForBook(const String& path, size_t expectedSize,
                                int fontSize, int lineGap,
                                PageOffsetTable& out);

// Persist `in` for `path`, stamped with `(fontSize, lineGap)`. No-op if
// `in.count <= 1` (nothing useful to save).
void savePageOffsetCacheForBook(const String& path, size_t fileSize,
                                int fontSize, int lineGap,
                                const PageOffsetTable& in);

// Remove the on-disk page-cache file for `path` (no-op if absent).
void deletePageCacheForBook(const String& path);

// Move the on-disk page-cache file from `oldPath` to `newPath` (no-op if
// no source file). If a stale destination exists, it's removed first.
void renamePageCacheForBook(const String& oldPath, const String& newPath);
