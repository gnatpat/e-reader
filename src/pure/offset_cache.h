#pragma once

#include "arduino_compat.h"
#include "../config.h"

// LRU cache of (book hash, page index) -> byte offset. Pure; no dependency
// on Preferences or the filesystem. The firmware owns one instance keyed by
// `hashPath32(path)`.
class OffsetCache {
public:
  OffsetCache() { reset(); }

  void reset();

  // Find the highest-page entry for `pathHash` with page <= targetPage.
  // Returns true if any entry was found; on success sets cachedPage/cachedOffset.
  bool lookup(uint32_t pathHash, int targetPage,
              int& cachedPage, uint32_t& cachedOffset) const;

  // Insert or update an entry. Negative `page` is a no-op.
  void store(uint32_t pathHash, int page, uint32_t offset);

  // Capacity (compile-time).
  static constexpr int CAPACITY = OFFSET_CACHE_SIZE;

private:
  struct Entry {
    uint32_t pathHash;
    int page;
    uint32_t offset;
    uint32_t stamp;
  };
  Entry entries_[CAPACITY];
  uint32_t stamp_;
};
