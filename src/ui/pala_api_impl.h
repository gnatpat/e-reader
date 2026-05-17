#pragma once

// ============================================================================
//  PalaAPI shim — wires the v3 function-pointer table apps see at runtime
//  to the firmware's own modules. Field assignments live in one place
//  (`initPalaAPI`) so the frozen field order has exactly one source of truth.
//
//  See apps/include/pala_api.h for the public contract; docs/APPS_LAYER.md
//  §4.3 for why this lives in `ui/` rather than `hal/`.
// ============================================================================

// Populate the static PalaAPI table. Call once from setup() after fonts,
// input, and FS are up. Safe to call again after the table has been used
// (re-writes the same pointers).
void initPalaAPI();

// STAGE-5 STUB: log "would run X" without actually loading the binary.
// Stage 6 replaces this with the real loader. Exposed so AppsScreen
// (Stage 7) can wire its launch path against a stable name today.
void runApp(const char* path);
