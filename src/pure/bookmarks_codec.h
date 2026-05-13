#pragma once

#include "arduino_compat.h"
#include "../config.h"

// Pure encoder/decoder for the bookmark byte blob stored in Preferences under
// "<bookKey>_bm". Two formats are supported:
//   v1 (legacy): 1 byte count + N * 2-byte page (offsets unknown)
//   v2 (current): 1 byte count + N * 6-byte (2-byte page + 4-byte offset)
// Decoders accept both; encoders only emit v2.

struct Bookmarks {
  uint16_t pages[MAX_BOOKMARKS];
  uint32_t offsets[MAX_BOOKMARKS];
  uint8_t  count = 0;
};

// Size of the largest possible encoded blob (v2: 1 + MAX_BOOKMARKS * 6 bytes).
constexpr size_t BOOKMARKS_ENCODED_MAX_SIZE = 1u + (size_t)MAX_BOOKMARKS * 6u;

// Decode `got` bytes from `buf` into `out`. Legacy v1 entries get an
// offset of 0xFFFFFFFF (placeholder — firmware will recompute).
// Returns the decoded count (always == out.count).
uint8_t decodeBookmarks(const uint8_t* buf, size_t got, Bookmarks& out);

// Encode `bm` into `outBuf` (must be at least BOOKMARKS_ENCODED_MAX_SIZE bytes).
// Returns the number of bytes written (== 1 + bm.count * 6).
size_t encodeBookmarks(const Bookmarks& bm, uint8_t* outBuf);

// Insert one bookmark at the current page/offset. Returns:
//   "Bookmark saved"  on success (sorted by page)
//   "Bookmark exists" if a bookmark for `page` already exists
// If the bookmark list is full, the oldest bookmark is evicted.
const char* addBookmark(Bookmarks& bm, uint16_t page, uint32_t offset);
