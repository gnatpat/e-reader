#include "storage/book_metadata.h"

// ============================================================================
//  Testable KV-store-backed API
// ============================================================================
Bookmarks loadBookmarks(KeyValueStore& kv, const String& bookKey) {
  Bookmarks bm;
  uint8_t buf[BOOKMARKS_ENCODED_MAX_SIZE] = {0};
  size_t got = kv.getBytes(bmKeyFor(bookKey).c_str(), buf, sizeof(buf));
  decodeBookmarks(buf, got, bm);
  return bm;
}

void saveBookmarks(KeyValueStore& kv, const String& bookKey, const Bookmarks& bm) {
  uint8_t buf[BOOKMARKS_ENCODED_MAX_SIZE];
  size_t n = encodeBookmarks(bm, buf);
  kv.putBytes(bmKeyFor(bookKey).c_str(), buf, n);
}

int loadSavedPage(KeyValueStore& kv, const String& bookKey) {
  int p = kv.getInt((bookKey + "_p").c_str(), 0);
  return (p < 0) ? 0 : p;
}

void saveSavedPage(KeyValueStore& kv, const String& bookKey, int pageIndex) {
  kv.putInt((bookKey + "_p").c_str(), pageIndex);
}

bool clearBookMetadata(KeyValueStore& kv, const String& bookKey) {
  kv.remove((bookKey + "_p").c_str());
  kv.remove(bmKeyFor(bookKey).c_str());
  return true;
}

void renameBookMetadata(KeyValueStore& kv, const String& oldKey, const String& newKey) {
  int progress = kv.getInt((oldKey + "_p").c_str(), -1);
  if (progress >= 0) {
    kv.putInt((newKey + "_p").c_str(), progress);
    kv.remove((oldKey + "_p").c_str());
  }

  uint8_t buf[BOOKMARKS_ENCODED_MAX_SIZE] = {0};
  size_t got = kv.getBytes(bmKeyFor(oldKey).c_str(), buf, sizeof(buf));
  if (got > 0) {
    kv.putBytes(bmKeyFor(newKey).c_str(), buf, got);
    kv.remove(bmKeyFor(oldKey).c_str());
  }
}

#ifdef ARDUINO
// ============================================================================
//  Firmware glue
// ============================================================================
#include "storage/library.h"           // g_library (bulk invalidation iterates books)
#include "storage/page_cache.h"        // deletePageCacheForBook / renamePageCacheForBook
#include "storage/preferences_store.h"

uint8_t loadBookmarksForKey(const String& bookKey,
                            uint16_t outPages[MAX_BOOKMARKS],
                            uint32_t outOffsets[MAX_BOOKMARKS]) {
  PreferencesStore kv(prefs);
  Bookmarks bm = loadBookmarks(kv, bookKey);
  for (uint8_t i = 0; i < bm.count; i++) {
    outPages[i] = bm.pages[i];
    outOffsets[i] = bm.offsets[i];
  }
  return bm.count;
}

void saveBookmarksForKey(const String& bookKey,
                         const uint16_t pages[MAX_BOOKMARKS],
                         const uint32_t offsets[MAX_BOOKMARKS],
                         uint8_t count) {
  Bookmarks bm;
  bm.count = (count > MAX_BOOKMARKS) ? MAX_BOOKMARKS : count;
  for (uint8_t i = 0; i < bm.count; i++) {
    bm.pages[i] = pages[i];
    bm.offsets[i] = offsets[i];
  }
  PreferencesStore kv(prefs);
  saveBookmarks(kv, bookKey, bm);
}

int savedPageForBookPath(const String& path) {
  PreferencesStore kv(prefs);
  return loadSavedPage(kv, prefKeyForBook(path));
}

void resetAllSavedProgress() {
  PreferencesStore kv(prefs);
  for (int i = 0; i < g_library.bookCount; i++) {
    String key = prefKeyForBook(String(g_library.books[i].path));
    saveSavedPage(kv, key, 0);
  }
}

void invalidateAllBookmarkOffsets() {
  PreferencesStore kv(prefs);
  for (int i = 0; i < g_library.bookCount; i++) {
    String key = prefKeyForBook(String(g_library.books[i].path));
    Bookmarks bm = loadBookmarks(kv, key);
    if (bm.count == 0) continue;
    for (uint8_t j = 0; j < bm.count; j++) bm.offsets[j] = 0xFFFFFFFFUL;
    saveBookmarks(kv, key, bm);
  }
}

void resetAllPagination() {
  invalidateAllPageCaches();         // page_cache: RAM LRU + pc_*.bin
  resetAllSavedProgress();           // per-book NVS saved page -> 0
  invalidateAllBookmarkOffsets();    // per-book bookmark offsets -> unknown
}

void deleteBookMetadata(const String& path) {
  PreferencesStore kv(prefs);
  clearBookMetadata(kv, prefKeyForBook(path));   // NVS: progress + bookmarks
  deletePageCacheForBook(path);                  // disk: pc_<hash>.bin
}

void migrateBookMetadata(const String& oldPath, const String& newPath) {
  PreferencesStore kv(prefs);
  renameBookMetadata(kv, prefKeyForBook(oldPath), prefKeyForBook(newPath));
  renamePageCacheForBook(oldPath, newPath);
}
#endif  // ARDUINO
