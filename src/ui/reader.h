#pragma once

#include "config.h"
#include "state.h"  // File, FS macro
#include "pure/page_offset_table.h"

// ============================================================================
//  The active reader session — split into four focused pieces.
//
//  Strong invariant: a book is "open" iff `book.isOpen()` returns true,
//  and that's true iff `book.path()` is non-empty. The file handle, path,
//  and derived NVS key always move as a unit through `OpenBook`'s methods.
//
//  Library / upload / web flows fully clear `g_reader` on leaving the
//  reader (see `LibraryScreen::onEnter`), so non-reader code never has to
//  reason about stale book state.
// ============================================================================

// File handle + path + derived NVS key, managed together. The class enforces
// the invariant "file is open iff path is non-empty" — public callers can't
// observe the file-closed-with-path-still-set intermediate state.
class OpenBook {
public:
  // Open `path` for reading. Sets path + key + opens file as one atomic
  // step. Returns false (and leaves the object closed/empty) if the file
  // can't be opened or is a directory.
  bool open(const String& path);

  // Close the file and clear path + key. No-op if already closed.
  void close();

  bool          isOpen() const { return (bool)file_; }
  const String& path() const   { return path_; }
  const String& key() const    { return key_; }
  File&         file()         { return file_; }
  size_t        size()         { return file_ ? file_.size() : 0; }

private:
  File   file_;
  String path_;
  String key_;
};

// UI cursor state: where the user is looking + how long since a full refresh.
struct ReaderCursor {
  int pageIndex = 0;
  int pageTurnsSinceFull = 0;
};

// Throttle bookkeeping for periodic progress-save to NVS.
struct SaveThrottle {
  uint32_t lastSaveMs = 0;
  int      lastSavedPage = -1;
};

struct ReaderState {
  OpenBook        book;
  PageOffsetTable pages;
  ReaderCursor    cursor;
  SaveThrottle    save;
};

extern ReaderState g_reader;

bool openBookByIndex(int idx);
void drawStatusBar(uint32_t startOffset);
void renderCurrentPage();
void idlePrefetchReader();

// Boot-time wake-resume. Checks if wake state asks us to resume reading and
// if the book still exists. On success the reader is fully set up for an
// immediate renderCurrentPage(); the caller owns the actual render and the
// screen transition.
bool tryRestoreReadingSession();

// Wake state — a single NVS key (`wake_path`) that survives deep sleep and
// tells `setup()` whether to resume the reader on next boot. Owned by the
// reader: armed by `ReaderScreen::onSleep` (via `armResumeOnWake`),
// consumed and cleared at boot by `tryRestoreReadingSession`. Nothing
// outside the reader should touch it.
void armResumeOnWake();    // arm: persist currently-open book for resume
void clearResumeOnWake();  // disarm: next wake lands in library

// ============================================================================
//  Reader lifecycle — full reset of every piece of `g_reader`. Called when
//  leaving the reader entirely (library entry, factory reset).
// ============================================================================
void clearCurrentBookState();

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
