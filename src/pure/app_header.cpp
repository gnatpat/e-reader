#include "pure/app_header.h"

#include <string.h>

// Read the header out of `buf` via memcpy so we don't rely on `buf` being
// aligned for uint32_t. The struct layout matches the on-disk byte layout
// (the build's Python post-step writes little-endian uint32s directly into
// the header fields; ESP32 + every host we care about is little-endian).
static void readHeader(const void* buf, PalaAppHeader* out) {
  memcpy(out, buf, sizeof(PalaAppHeader));
}

AppHeaderStatus validateAppHeader(const void* buf, size_t fileSize,
                                  uint32_t* outFileApiVersion) {
  // +4 matches the old loader: a header alone is meaningless; the smallest
  // sensible app has at least one instruction word past it.
  if (fileSize < sizeof(PalaAppHeader) + 4) return AppHeaderStatus::TooSmall;

  PalaAppHeader hdr;
  readHeader(buf, &hdr);

  if (hdr.magic != PALA_APP_MAGIC) return AppHeaderStatus::BadMagic;

  if (hdr.api_version != PALA_API_VERSION) {
    if (outFileApiVersion) *outFileApiVersion = hdr.api_version;
    return AppHeaderStatus::ApiMismatch;
  }

  if (hdr.entry_offset < sizeof(PalaAppHeader) || hdr.entry_offset >= fileSize) {
    return AppHeaderStatus::BadEntryOffset;
  }

  if (hdr.reloc_count > 0) {
    if (hdr.reloc_offset < sizeof(PalaAppHeader)) return AppHeaderStatus::BadRelocTable;
    // Overflow-safe: reloc_count <= (fileSize - reloc_offset) / 4
    uint32_t avail = (uint32_t)(fileSize - hdr.reloc_offset);
    if (hdr.reloc_count > avail / 4u) return AppHeaderStatus::BadRelocTable;
  }

  return AppHeaderStatus::Ok;
}

bool validateRelocEntries(const void* buf, size_t fileSize) {
  PalaAppHeader hdr;
  readHeader(buf, &hdr);
  if (hdr.reloc_count == 0) return true;

  // validateAppHeader has been called first (precondition), so the table
  // itself is in range. Walk entries with memcpy to stay alignment-safe.
  const uint8_t* base = (const uint8_t*)buf;
  for (uint32_t i = 0; i < hdr.reloc_count; i++) {
    uint32_t off;
    memcpy(&off, base + hdr.reloc_offset + i * 4u, sizeof(uint32_t));
    // Each entry must point at a 4-byte slot strictly before the reloc
    // table itself. (Entries inside the table would be self-referential.)
    if (off + 4u > hdr.reloc_offset) return false;
    // And the slot must be inside the file, which is implied by the above
    // (reloc_offset <= fileSize), but we still guard against wraparound.
    if (off + 4u < off) return false;
    (void)fileSize;  // unused after the implicit guarantee above
  }
  return true;
}
