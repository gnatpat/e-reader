#pragma once

#include "storage/kv_store.h"
#include "pure/bookmarks_codec.h"
#include "pure/hashing.h"

// All per-book NVS state — explicit bookmarks AND implicit saved-page
// progress — lives behind the same hash-keyed scheme (`<hash>_p`,
// `<hash>_bm`) and follows the same lifecycle (cleared on book delete,
// migrated on rename, invalidated on layout change), so it shares a file.

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
//  Firmware glue — wraps the testable API with a PreferencesStore around
//  the global `prefs` so callers can keep their existing call sites.
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

// Bulk invalidation, used when font size or line spacing changes — the
// stored page numbers and stored bookmark byte offsets are all now wrong
// for the new layout. Bookmark page numbers are preserved (the layout
// change preserves "which paragraph the user marked," roughly), but the
// cached byte offsets are reset to 0xFFFFFFFF so they get recomputed on
// next access.
void resetAllSavedProgress();
void invalidateAllBookmarkOffsets();

// Composite: invalidate everything that depends on the current pagination
// layout. Wipes page caches (RAM + pc_*.bin), resets per-book saved page
// progress, and marks all bookmark byte offsets as unknown. Called when
// font size or line spacing changes via the web settings UI.
void resetAllPagination();

// Per-book lifecycle. Compose the pure NVS operations above with the
// matching page-cache file work in storage/page_cache.h. They do NOT
// touch device wake state or `g_reader` — those are higher-level
// concerns and callers run in contexts (web during upload) where the
// reader has already been fully cleared by `LibraryScreen::onEnter`.
void deleteBookMetadata(const String& path);
void migrateBookMetadata(const String& oldPath, const String& newPath);

// Throttled-save and bookmark-current-page helpers live in ui/reader.h —
// they manipulate g_reader and belong on the ui side of the storage boundary.

#endif  // ARDUINO
