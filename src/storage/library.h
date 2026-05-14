#pragma once

#include "config.h"
#include "pure/arduino_compat.h"

// ============================================================================
//  Library catalog — books + folders discovered on LittleFS, plus the
//  flattened "library entries" list that the library screen iterates over.
//  Owned here; populated by `loadBooks()` / `buildLibraryEntries()` below.
// ============================================================================
enum LibraryEntryType {
  LIB_ENTRY_FOLDER,
  LIB_ENTRY_BOOK,
  LIB_ENTRY_BOOKMARKS,
  LIB_ENTRY_LIST,
  LIB_ENTRY_ABOUT,
  LIB_ENTRY_UPLOAD
};

struct BookInfo {
  char name[80];
  char path[96];
  size_t size;
  char folder[64];
};

struct LibraryState {
  BookInfo books[MAX_BOOKS];
  int bookCount = 0;

  char folders[MAX_FOLDERS][64];
  int folderCount = 0;

  int selectedItem = 0;
  String currentFolder;

  LibraryEntryType entryTypes[MAX_LIBRARY_ENTRIES];
  int entryRefs[MAX_LIBRARY_ENTRIES];
  int entryDepths[MAX_LIBRARY_ENTRIES];
  int entryCount = 0;

  bool folderExpanded[MAX_FOLDERS] = {false};
};

extern LibraryState g_library;

void addFolderIfMissing(const String& folderRel);
void scanBooksRecursive(const String& absDir, const String& relDir);
void loadBooks();

bool libraryFolderExists(const String& folderRel);
String libraryEntryLabel(int idx);
void buildLibraryEntries();

// Folder expand/collapse cursor state on `g_library`.
bool isFolderExpanded(int idx);
void setFolderExpanded(int idx, bool expanded);
