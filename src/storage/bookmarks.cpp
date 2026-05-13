#include "storage/bookmarks.h"

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
//  Legacy firmware API
// ============================================================================
#include "storage/page_cache.h"        // savePageOffsetCacheForBook
#include "storage/preferences_store.h"
#include "ui/reader.h"                  // g_reader (firmware glue only)

static PreferencesStore makeKv() { return PreferencesStore(prefs); }

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

void resetSaveThrottle() {
  g_reader.lastSaveMs = 0;
  g_reader.lastSavedPage = -1;
}

void saveProgressThrottled(bool force) {
  if (g_reader.currentBookKey.length() == 0) return;

  if (!force) {
    if (g_reader.pageIndex == g_reader.lastSavedPage) return;
    uint32_t now = millis();
    if (g_reader.lastSaveMs != 0 && (now - g_reader.lastSaveMs) < SAVE_EVERY_MS) return;
  }

  PreferencesStore kv(prefs);
  saveSavedPage(kv, g_reader.currentBookKey, g_reader.pageIndex);
  g_reader.lastSaveMs = millis();
  g_reader.lastSavedPage = g_reader.pageIndex;
}

const char* addBookmarkForCurrentBook() {
  if (g_reader.currentBookKey.length() == 0) return nullptr;

  PreferencesStore kv(prefs);
  Bookmarks bm = loadBookmarks(kv, g_reader.currentBookKey);

  const char* msg = addBookmark(bm, (uint16_t)g_reader.pageIndex, g_reader.lastPageStartOffset);
  if (String(msg) == "Bookmark saved") {
    saveBookmarks(kv, g_reader.currentBookKey, bm);
    if (g_reader.file) savePageOffsetCacheForBook(g_reader.currentBookPath, g_reader.file.size());
  }
  return msg;
}
#endif  // ARDUINO
