#pragma once

#include "config.h"
#include "state.h"

// Storage-side helpers for book + library metadata. Reader-lifecycle
// functions (safeCloseCurrentBook, clearCurrentBookState, renameBook, ...)
// live in ui/reader.h — they manipulate g_reader and belong above the
// storage line. The toast-reset helper lives in hal/display.h next to
// g_toast. The library-screen cursor reset lives in
// ui/screens/library_screen.h.

// Persist the device's wake-up intent. `reading=true` arms the next boot to
// resume the reader at `path`; `reading=false` clears any pending wake.
void syncWakeState(bool reading, const String& path = String(""));

// Library-related metadata helpers (depend on `g_library`).
String bookLeafLabel(int idx);
bool   isFolderExpanded(int idx);
void   setFolderExpanded(int idx, bool expanded);

// Remove all metadata for a book path (preferences + page cache + wake state).
void deleteBookMetadata(const String& path);

// Storage primitive: move a book's metadata (NVS keys + page-cache file +
// wake_path) from oldPath to newPath. Does NOT update g_reader; callers
// holding the open book should use `renameBook` in ui/reader.h instead,
// which wraps this and keeps g_reader in sync.
void migrateBookMetadata(const String& oldPath, const String& newPath);
