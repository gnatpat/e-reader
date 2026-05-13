#pragma once

#include "config.h"
#include "state.h"

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
bool   loadPageOffsetCacheForBook(const String& path, size_t expectedSize);
void   savePageOffsetCacheForBook(const String& path, size_t fileSize);

// Wipes both the RAM cache and every pc_*.bin file. Used when font size or
// line spacing changes (any cached offset becomes invalid).
void invalidateAllPageCaches();
