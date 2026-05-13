#pragma once

#include "config.h"
#include "state.h"  // File, FS macro
#include "pure/page_offset_table.h"

// ============================================================================
//  The active reader session — currently-open book + pagination state.
//  Produced by `openBookByIndex()`, cleaned up by the lifecycle helpers
//  below, and read by `reader.cpp` / `text.cpp` while a book is open.
// ============================================================================
struct ReaderState {
  File file;
  String currentBookKey;
  String currentBookPath;
  int pageIndex = 0;             // cursor: which page the user is viewing

  PageOffsetTable pages;         // map: page index -> absolute byte offset
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

// ============================================================================
//  Reader lifecycle — owns the open-book transitions on `g_reader`.
// ============================================================================

// Single source of truth for the (path, key) invariant. Empty path leaves an
// empty key (i.e. "no book open"), not the hash of an empty string.
void setCurrentBook(const String& path);

// Close the open file if any. No-op if nothing is open.
void safeCloseCurrentBook();

// Reset the entire reader struct to "no book open". Equivalent to
// safeCloseCurrentBook + setCurrentBook("") + zeroing the pagination state.
void clearCurrentBookState();

// Re-open the book at `g_reader.currentBookPath` (e.g. after the web layer
// closed the file under our feet). Returns true on success. No-op if there
// is no current path.
bool reopenCurrentBookIfNeeded();

// Rename a book on disk. Updates NVS metadata, page cache, and wake_path via
// storage; keeps `g_reader` in sync if it happens to be holding this book.
// The caller is responsible for the actual `FS.rename()` of the file.
void renameBook(const String& oldPath, const String& newPath);

// Called when font size or line spacing changes — every cached page offset
// is now meaningless. Wraps `invalidateAllPageCaches()` (storage: wipes the
// RAM LRU, deletes pc_*.bin, resets per-book NVS progress + bookmark offsets)
// and then resets the open reader's in-memory pagination state so the next
// draw paginates from byte 0 with the new metrics.
void resetAllPagination();

// ============================================================================
//  Progress + bookmark glue — manipulates `g_reader`, persists via storage.
//  Lives on the ui side of the boundary because the bulk of the logic is
//  reader-state reasoning (throttle window, "is a book even open right now");
//  the underlying save is delegated to the pure storage API.
// ============================================================================
void resetSaveThrottle();
void saveProgressThrottled(bool force = false);

// Add a bookmark at the currently-open reader page. Returns a UI message
// (e.g. "Bookmark saved" / "Bookmark exists") or nullptr if no book is open.
const char* addBookmarkForCurrentBook();
