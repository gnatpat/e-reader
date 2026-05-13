#include "storage/book_state.h"

#include "storage/bookmarks.h"          // clearBookMetadata / renameBookMetadata
#include "storage/library.h"            // g_library, BookInfo
#include "storage/page_cache.h"         // pageCachePathForBook
#include "storage/preferences_store.h"
#include "pure/hashing.h"
#include "pure/paths.h"

void syncWakeState(bool reading, const String& path) {
  prefs.putInt("wake_mode", reading ? 1 : 0);
  if (reading && path.length() > 0) {
    prefs.putString("wake_path", path);
  } else {
    prefs.remove("wake_path");
  }
}

String bookLeafLabel(int idx) {
  String leaf = stripTxtExt(lastPathComponent(String(g_library.books[idx].path)));
  leaf.replace('_', ' ');
  return leaf;
}

bool isFolderExpanded(int idx) {
  if (idx < 0 || idx >= g_library.folderCount) return false;
  return g_library.folderExpanded[idx];
}

void setFolderExpanded(int idx, bool expanded) {
  if (idx < 0 || idx >= g_library.folderCount) return;
  g_library.folderExpanded[idx] = expanded;
}

void deleteBookMetadata(const String& path) {
  String key = prefKeyForBook(path);
  PreferencesStore kv(prefs);
  clearBookMetadata(kv, key);

  String cachePath = pageCachePathForBook(path);
  if (FS.exists(cachePath)) FS.remove(cachePath);

  if (prefs.getString("wake_path", "") == path) {
    prefs.remove("wake_path");
    prefs.putInt("wake_mode", 0);
  }
}

void migrateBookMetadata(const String& oldPath, const String& newPath) {
  String oldKey = prefKeyForBook(oldPath);
  String newKey = prefKeyForBook(newPath);

  PreferencesStore kv(prefs);
  renameBookMetadata(kv, oldKey, newKey);

  String oldCache = pageCachePathForBook(oldPath);
  String newCache = pageCachePathForBook(newPath);
  if (FS.exists(oldCache)) {
    if (FS.exists(newCache)) FS.remove(newCache);
    FS.rename(oldCache, newCache);
  }

  if (prefs.getString("wake_path", "") == oldPath) {
    prefs.putString("wake_path", newPath);
  }
}
