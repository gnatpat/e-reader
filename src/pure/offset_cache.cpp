#include "offset_cache.h"

void OffsetCache::reset() {
  for (int i = 0; i < CAPACITY; i++) {
    entries_[i].pathHash = 0;
    entries_[i].page = -1;
    entries_[i].offset = 0;
    entries_[i].stamp = 0;
  }
  stamp_ = 1;
}

bool OffsetCache::lookup(uint32_t pathHash, int targetPage,
                         int& cachedPage, uint32_t& cachedOffset) const {
  bool found = false;
  int bestPage = -1;
  uint32_t bestOffset = 0;
  uint32_t bestStamp = 0;

  for (int i = 0; i < CAPACITY; i++) {
    if (entries_[i].pathHash != pathHash) continue;
    if (entries_[i].page > targetPage) continue;
    if (entries_[i].page > bestPage ||
        (entries_[i].page == bestPage && entries_[i].stamp > bestStamp)) {
      bestPage = entries_[i].page;
      bestOffset = entries_[i].offset;
      bestStamp = entries_[i].stamp;
      found = true;
    }
  }

  if (found) {
    cachedPage = bestPage;
    cachedOffset = bestOffset;
  }
  return found;
}

void OffsetCache::store(uint32_t pathHash, int page, uint32_t offset) {
  if (page < 0) return;

  int slot = -1;
  uint32_t oldestStamp = 0xFFFFFFFFu;

  for (int i = 0; i < CAPACITY; i++) {
    if (entries_[i].pathHash == pathHash && entries_[i].page == page) {
      slot = i;
      break;
    }
    if (entries_[i].page < 0) {
      slot = i;
      break;
    }
    if (entries_[i].stamp < oldestStamp) {
      oldestStamp = entries_[i].stamp;
      slot = i;
    }
  }

  if (slot < 0) return;
  entries_[slot].pathHash = pathHash;
  entries_[slot].page = page;
  entries_[slot].offset = offset;
  entries_[slot].stamp = stamp_++;
  if (stamp_ == 0) stamp_ = 1;
}
