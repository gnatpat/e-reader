#pragma once

#include "storage/kv_store.h"
#include "pure/bookmarks_codec.h"
#include "pure/hashing.h"

// ============================================================================
//  Testable KV-store-backed API (host-compatible)
// ============================================================================

// Load the bookmarks for `bookKey` from `kv`. Decodes both v1 (legacy,
// 2-byte/entry) and v2 (current, 6-byte/entry) blobs.
Bookmarks loadBookmarks(KeyValueStore& kv, const String& bookKey);

// Save `bm` for `bookKey` into `kv` using the v2 format.
void saveBookmarks(KeyValueStore& kv, const String& bookKey, const Bookmarks& bm);

// Per-book reading progress (page index, 0-based). Default is 0.
int  loadSavedPage(KeyValueStore& kv, const String& bookKey);
void saveSavedPage(KeyValueStore& kv, const String& bookKey, int pageIndex);

// Remove all metadata for one book (progress + bookmarks). Returns true if
// anything was removed.
bool clearBookMetadata(KeyValueStore& kv, const String& bookKey);

// Move metadata (progress + bookmarks) from oldKey to newKey.
void renameBookMetadata(KeyValueStore& kv, const String& oldKey, const String& newKey);

#ifdef ARDUINO
// ============================================================================
//  Legacy firmware API — wraps the testable API with a PreferencesStore
//  around the global `prefs` so callers can keep their existing call sites.
// ============================================================================
#include "config.h"
#include "state.h"

uint8_t loadBookmarksForKey(const String& bookKey,
                            uint16_t outPages[MAX_BOOKMARKS],
                            uint32_t outOffsets[MAX_BOOKMARKS]);
void saveBookmarksForKey(const String& bookKey,
                         const uint16_t pages[MAX_BOOKMARKS],
                         const uint32_t offsets[MAX_BOOKMARKS],
                         uint8_t count);
int  savedPageForBookPath(const String& path);

// Throttled-save and bookmark-current-page helpers live in ui/reader.h —
// they manipulate g_reader and belong on the ui side of the storage boundary.

#endif  // ARDUINO
