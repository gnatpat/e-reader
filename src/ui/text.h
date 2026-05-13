#pragma once

#include "config.h"
#include "state.h"

// Re-export pure modules so existing callers that include "ui/text.h" keep
// working unchanged.
#include "pure/text_util.h"
#include "pure/paginator.h"
#include "pure/bookmark_label.h"
#include "hal/file_stream.h"

// ============================================================================
//  Firmware pagination glue — wraps `paginatePage` with the u8g2-backed
//  width-measure function, draws to the e-ink, and threads the in-memory
//  offset cache + page-cache file from storage.
// ============================================================================
uint32_t readPageFromFile(File& f, uint32_t startPos, bool draw, String* outText);
uint32_t buildNextOffsetFor(File& f, uint32_t startPos);
uint32_t buildNextOffset(uint32_t startPos);
uint32_t pageOffsetForPage(File& f, const String& path, int page);
void ensureOffsetsUpTo(int targetPage);
uint32_t resolveBookmarkOffset(const String& path, uint16_t page, uint32_t storedOffset);
String readPageTextForWeb(const String& path, int page);
