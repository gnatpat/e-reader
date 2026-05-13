#include "storage/book_state.h"

#include "hal/display.h"                // g_toast — resetUiEphemeralState clears it
#include "storage/bookmarks.h"          // loadBookmarks / saveBookmarks
#include "storage/library.h"            // g_library, BookInfo
#include "storage/page_cache.h"         // pageCachePathForBook
#include "ui/reader.h"                  // g_reader — these helpers are its lifecycle
#include "storage/preferences_store.h"
#include "pure/hashing.h"
#include "pure/paths.h"


void safeCloseCurrentBook() {
  if (g_reader.file) g_reader.file.close();
}

void clearCurrentBookState() {
  safeCloseCurrentBook();
  g_reader.currentBookKey = "";
  g_reader.currentBookPath = "";
  g_reader.pageIndex = 0;
  g_reader.knownPages = 0;
  g_reader.eofReached = false;
  g_reader.lastPageStartOffset = 0;
  g_reader.pageTurnsSinceFull = 0;
  g_reader.lastSaveMs = 0;
  g_reader.lastSavedPage = -1;
}

bool reopenCurrentBookIfNeeded() {
  if (g_reader.currentBookPath.length() == 0) return false;
  safeCloseCurrentBook();
  g_reader.file = FS.open(g_reader.currentBookPath, "r");
  return (bool)g_reader.file;
}

void resetUiEphemeralState() {
  g_toast.msg = "";
  g_toast.untilMs = 0;
}

void resetNavigationState() {
  g_library.currentFolder = "";
  g_library.selectedItem = 0;
}

void syncWakeState(bool reading) {
  prefs.putInt("wake_mode", reading ? 1 : 0);
  if (reading && g_reader.currentBookPath.length() > 0) {
    prefs.putString("wake_path", g_reader.currentBookPath);
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

  if (g_reader.currentBookPath == oldPath) {
    g_reader.currentBookPath = newPath;
    g_reader.currentBookKey = newKey;
  }
}
