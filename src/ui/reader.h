#pragma once

#include "config.h"
#include "state.h"  // File, FS macro

// ============================================================================
//  The active reader session — currently-open book + pagination state.
//  Produced by `openBookByIndex()`, cleaned up by `storage/book_state.cpp`'s
//  lifecycle helpers, and read by `reader.cpp` / `text.cpp` while a book is
//  open. Storage-side glue (bookmarks, page_cache) reaches in too — those
//  accesses are all behind `#ifdef ARDUINO`.
// ============================================================================
struct ReaderState {
  File file;
  String currentBookKey;
  String currentBookPath;
  int pageIndex = 0;

  uint32_t pageOffsets[MAX_PAGES];
  int knownPages = 0;
  bool eofReached = false;

  uint32_t lastPageStartOffset = 0;
  int pageTurnsSinceFull = 0;

  uint32_t lastSaveMs = 0;
  int lastSavedPage = -1;
};

extern ReaderState g_reader;

bool openBookByIndex(int idx);
void drawStatusBar(uint32_t startOffset);
void renderCurrentPage();
void idlePrefetchReader();
