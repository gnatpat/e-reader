#pragma once

#include "pure/arduino_compat.h"
#include "pure/library_nav.h"  // Catalog, BookInfo, LibraryEntryType, LibEntry

// ============================================================================
//  The discovered library. Pure types live in `pure/library_nav.h`; this
//  header owns the global instance + the disk-loading operations that
//  populate it. Navigation state (cursor, folder expansion, the derived
//  display-list) lives on the library screen.
// ============================================================================
extern Catalog g_library;

// Catalog-only refresh. Reloads `g_library` from disk. Safe to call
// freely: the library screen keys folder expansion by name, so any
// reshuffle of `g_library.folders[]` here is reflected on the next draw
// without callers having to do anything special.
void loadBooks();
