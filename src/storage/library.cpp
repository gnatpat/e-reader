#include "storage/library.h"

#include "pure/paths.h"
#include "state.h"                   // FS macro
#include "storage/book_state.h"      // bookLeafLabel, isFolderExpanded
#include "storage/fs_util.h"         // ensureBooksDir
#include "storage/list_items.h"      // loadListItems, listHasVisibleItems
#include "storage/page_cache.h"      // resetOffsetCache

// The library catalog. Populated by `loadBooks()` from on-disk `/books/**`,
// flattened into the menu entries by `buildLibraryEntries()`.
LibraryState g_library;

void addFolderIfMissing(const String& folderRel) {
  if (folderRel.length() == 0) return;
  for (int i = 0; i < g_library.folderCount; i++) {
    if (strcmp(g_library.folders[i], folderRel.c_str()) == 0) return;
  }
  if (g_library.folderCount < MAX_FOLDERS) {
    strncpy(g_library.folders[g_library.folderCount], folderRel.c_str(), 63);
    g_library.folders[g_library.folderCount][63] = '\0';
    g_library.folderCount++;
  }
}

static void sortFolders() {
  for (int i = 0; i < g_library.folderCount - 1; i++) {
    for (int j = i + 1; j < g_library.folderCount; j++) {
      if (strcmp(g_library.folders[j], g_library.folders[i]) < 0) {
        char tmp[64];
        memcpy(tmp, g_library.folders[i], 64);
        memcpy(g_library.folders[i], g_library.folders[j], 64);
        memcpy(g_library.folders[j], tmp, 64);
      }
    }
  }
}

static void sortBooks() {
  for (int i = 0; i < g_library.bookCount - 1; i++) {
    for (int j = i + 1; j < g_library.bookCount; j++) {
      if (strcmp(g_library.books[j].name, g_library.books[i].name) < 0) {
        BookInfo tmp = g_library.books[i];
        g_library.books[i] = g_library.books[j];
        g_library.books[j] = tmp;
      }
    }
  }
}

void scanBooksRecursive(const String& absDir, const String& relDir) {
  File dir = FS.open(absDir);
  if (!dir || !dir.isDirectory()) return;

  File f = dir.openNextFile();
  while (f) {
    String entryName = String(f.name());
    String absPath = entryName.startsWith("/") ? entryName : (absDir + "/" + entryName);
    String leaf = lastPathComponent(absPath);

    if (f.isDirectory()) {
      String childRel = relDir.length() ? (relDir + "/" + leaf) : leaf;
      addFolderIfMissing(childRel);
      scanBooksRecursive(absPath, childRel);
    } else if (g_library.bookCount < MAX_BOOKS && absPath.endsWith(".txt")) {
      String relFile = relDir.length() ? (relDir + "/" + leaf) : leaf;
      BookInfo& b = g_library.books[g_library.bookCount];

      strncpy(b.path, absPath.c_str(), 95);
      b.path[95] = '\0';
      strncpy(b.folder, relDir.c_str(), 63);
      b.folder[63] = '\0';

      String pretty = prettyRelativeLabel(relFile);
      strncpy(b.name, pretty.c_str(), 79);
      b.name[79] = '\0';
      b.size = f.size();
      g_library.bookCount++;
    }

    f.close();
    f = dir.openNextFile();
  }

  dir.close();
}

void loadBooks() {
  int savedListSel = g_list.selectedIndex;

  char expandedBefore[MAX_FOLDERS][64];
  int expandedCount = 0;
  for (int i = 0; i < g_library.folderCount && expandedCount < MAX_FOLDERS; i++) {
    if (g_library.folderExpanded[i]) {
      strncpy(expandedBefore[expandedCount], g_library.folders[i], 63);
      expandedBefore[expandedCount][63] = '\0';
      expandedCount++;
    }
  }

  loadListItems();
  if (g_list.count > 0) {
    if (savedListSel >= g_list.count) savedListSel = g_list.count - 1;
    if (savedListSel < 0) savedListSel = 0;
    g_list.selectedIndex = savedListSel;
  } else {
    g_list.selectedIndex = 0;
  }

  g_library.bookCount = 0;
  g_library.folderCount = 0;
  for (int i = 0; i < MAX_FOLDERS; i++) g_library.folderExpanded[i] = false;
  resetOffsetCache();

  ensureBooksDir();
  scanBooksRecursive("/books", "");
  sortFolders();
  sortBooks();

  for (int i = 0; i < g_library.folderCount; i++) {
    for (int j = 0; j < expandedCount; j++) {
      if (strcmp(g_library.folders[i], expandedBefore[j]) == 0) {
        g_library.folderExpanded[i] = true;
        break;
      }
    }
  }

  buildLibraryEntries();
  if (g_library.selectedItem < 0) g_library.selectedItem = 0;
  if (g_library.selectedItem >= g_library.entryCount) g_library.selectedItem = max(0, g_library.entryCount - 1);
}

bool libraryFolderExists(const String& folderRel) {
  if (folderRel.length() == 0) return true;
  for (int i = 0; i < g_library.folderCount; i++) {
    if (strcmp(g_library.folders[i], folderRel.c_str()) == 0) return true;
  }
  for (int i = 0; i < g_library.bookCount; i++) {
    if (strcmp(g_library.books[i].folder, folderRel.c_str()) == 0) return true;
  }
  return false;
}

String libraryEntryLabel(int idx) {
  if (idx < 0 || idx >= g_library.entryCount) return "";
  switch (g_library.entryTypes[idx]) {
    case LIB_ENTRY_FOLDER: {
      int folderIdx = g_library.entryRefs[idx];
      String prefix = isFolderExpanded(folderIdx) ? "- " : "+ ";
      return prefix + folderLeafLabel(String(g_library.folders[folderIdx]));
    }
    case LIB_ENTRY_BOOK:      return bookLeafLabel(g_library.entryRefs[idx]);
    case LIB_ENTRY_BOOKMARKS: return "Bookmarks";
    case LIB_ENTRY_LIST:      return "List";
    case LIB_ENTRY_ABOUT:     return "Device";
    case LIB_ENTRY_UPLOAD:    return "Upload";
  }
  return "";
}

static void addLibraryBookEntry(int bookIdx, int depth) {
  if (g_library.entryCount >= MAX_LIBRARY_ENTRIES) return;
  g_library.entryTypes[g_library.entryCount] = LIB_ENTRY_BOOK;
  g_library.entryRefs[g_library.entryCount] = bookIdx;
  g_library.entryDepths[g_library.entryCount] = depth;
  g_library.entryCount++;
}

static void addLibraryFolderTree(const String& parent, int depth) {
  for (int i = 0; i < g_library.folderCount && g_library.entryCount < MAX_LIBRARY_ENTRIES; i++) {
    String folderRel = String(g_library.folders[i]);
    if (folderParent(folderRel) != parent) continue;

    g_library.entryTypes[g_library.entryCount] = LIB_ENTRY_FOLDER;
    g_library.entryRefs[g_library.entryCount] = i;
    g_library.entryDepths[g_library.entryCount] = depth;
    g_library.entryCount++;

    if (!isFolderExpanded(i)) continue;

    for (int b = 0; b < g_library.bookCount && g_library.entryCount < MAX_LIBRARY_ENTRIES; b++) {
      if (String(g_library.books[b].folder) == folderRel) {
        addLibraryBookEntry(b, depth + 1);
      }
    }

    addLibraryFolderTree(folderRel, depth + 1);
  }
}

void buildLibraryEntries() {
  g_library.entryCount = 0;

  addLibraryFolderTree(String(""), 0);

  for (int b = 0; b < g_library.bookCount && g_library.entryCount < MAX_LIBRARY_ENTRIES; b++) {
    if (String(g_library.books[b].folder).length() == 0) {
      addLibraryBookEntry(b, 0);
    }
  }

  if (g_library.entryCount < MAX_LIBRARY_ENTRIES) {
    g_library.entryTypes[g_library.entryCount] = LIB_ENTRY_BOOKMARKS;
    g_library.entryRefs[g_library.entryCount] = -1;
    g_library.entryDepths[g_library.entryCount] = 0;
    g_library.entryCount++;
  }
  if (listHasVisibleItems() && g_library.entryCount < MAX_LIBRARY_ENTRIES) {
    g_library.entryTypes[g_library.entryCount] = LIB_ENTRY_LIST;
    g_library.entryRefs[g_library.entryCount] = -1;
    g_library.entryDepths[g_library.entryCount] = 0;
    g_library.entryCount++;
  }
  if (g_library.entryCount < MAX_LIBRARY_ENTRIES) {
    g_library.entryTypes[g_library.entryCount] = LIB_ENTRY_ABOUT;
    g_library.entryRefs[g_library.entryCount] = -1;
    g_library.entryDepths[g_library.entryCount] = 0;
    g_library.entryCount++;
  }
  if (g_library.entryCount < MAX_LIBRARY_ENTRIES) {
    g_library.entryTypes[g_library.entryCount] = LIB_ENTRY_UPLOAD;
    g_library.entryRefs[g_library.entryCount] = -1;
    g_library.entryDepths[g_library.entryCount] = 0;
    g_library.entryCount++;
  }

  if (g_library.selectedItem < 0) g_library.selectedItem = 0;
  if (g_library.selectedItem >= g_library.entryCount) g_library.selectedItem = max(0, g_library.entryCount - 1);
}
