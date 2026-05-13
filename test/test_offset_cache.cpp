#include "test_framework.h"
#include "pure/offset_cache.h"

TEST_CASE("offset cache returns false on empty lookup") {
  OffsetCache c;
  int page = -1;
  uint32_t off = 0;
  CHECK(!c.lookup(0x12345678u, 5, page, off));
}

TEST_CASE("offset cache finds the highest entry <= target") {
  OffsetCache c;
  c.store(0x100u, 0, 0);
  c.store(0x100u, 3, 300);
  c.store(0x100u, 7, 700);
  c.store(0x100u, 15, 1500);

  int page = -1;
  uint32_t off = 0;
  CHECK(c.lookup(0x100u, 5, page, off));
  CHECK_EQ(page, 3);
  CHECK_EQ(off, 300u);

  CHECK(c.lookup(0x100u, 7, page, off));
  CHECK_EQ(page, 7);
  CHECK_EQ(off, 700u);

  CHECK(c.lookup(0x100u, 999, page, off));
  CHECK_EQ(page, 15);
  CHECK_EQ(off, 1500u);
}

TEST_CASE("offset cache isolates entries by hash") {
  OffsetCache c;
  c.store(0x100u, 5, 500);
  c.store(0x200u, 5, 999);

  int page = -1;
  uint32_t off = 0;
  CHECK(c.lookup(0x100u, 10, page, off));
  CHECK_EQ(off, 500u);

  CHECK(c.lookup(0x200u, 10, page, off));
  CHECK_EQ(off, 999u);
}

TEST_CASE("offset cache updates existing (hash,page) entries in place") {
  OffsetCache c;
  c.store(0x100u, 5, 500);
  c.store(0x100u, 5, 555);  // same key — update

  int page = -1;
  uint32_t off = 0;
  CHECK(c.lookup(0x100u, 5, page, off));
  CHECK_EQ(off, 555u);
}

TEST_CASE("offset cache evicts oldest entry when full") {
  OffsetCache c;
  // Fill to capacity with distinct (hash, page) entries.
  for (int i = 0; i < OffsetCache::CAPACITY; i++) {
    c.store((uint32_t)(i + 1), 1, (uint32_t)(i + 1) * 10u);
  }
  // Original first entry still resolvable.
  int p = -1; uint32_t o = 0;
  CHECK(c.lookup(1u, 1, p, o));
  CHECK_EQ(o, 10u);

  // Add another distinct entry — must evict the oldest (hash=1).
  c.store(99999u, 1, 7777u);

  CHECK(c.lookup(99999u, 1, p, o));
  CHECK_EQ(o, 7777u);
  // The oldest entry should now be gone.
  CHECK(!c.lookup(1u, 1, p, o));
}

TEST_CASE("offset cache ignores negative page in store") {
  OffsetCache c;
  c.store(0x100u, -1, 999);  // no-op
  int p = -1; uint32_t o = 0;
  CHECK(!c.lookup(0x100u, 0, p, o));
}

TEST_CASE("offset cache reset clears all entries") {
  OffsetCache c;
  c.store(0x100u, 5, 500);
  c.reset();
  int p = -1; uint32_t o = 0;
  CHECK(!c.lookup(0x100u, 10, p, o));
}
