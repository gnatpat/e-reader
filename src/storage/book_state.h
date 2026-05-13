#pragma once

#include "config.h"
#include "state.h"

// Reader / book state cleanup helpers. All operate on `g_reader`,
// `g_toast`, and `g_library`.

void safeCloseCurrentBook();
void clearCurrentBookState();
bool reopenCurrentBookIfNeeded();
void resetUiEphemeralState();
void resetNavigationState();
void syncWakeState(bool reading);

// Library-related metadata helpers (depend on `g_library`).
String bookLeafLabel(int idx);
bool   isFolderExpanded(int idx);
void   setFolderExpanded(int idx, bool expanded);

// Remove all metadata for a book path (preferences + page cache + wake state).
void deleteBookMetadata(const String& path);

// Move metadata for a book from oldPath to newPath.
void migrateBookMetadata(const String& oldPath, const String& newPath);
