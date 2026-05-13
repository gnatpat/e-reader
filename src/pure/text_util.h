#pragma once

#include "arduino_compat.h"

// Pure UTF-8 helpers and typography normalization. No hardware deps.

// True for any byte of the form 10xxxxxx (UTF-8 continuation byte).
inline bool isUtf8ContinuationByte(uint8_t b) {
  return (b & 0xC0) == 0x80;
}

// Returns the UTF-8 character byte-length encoded by `b` (the leading byte).
// Falls back to 1 for malformed bytes.
int utf8CharLenFromLead(uint8_t b);

// Returns the UTF-8 character byte-length at `index` in `s`. If `index` is
// out of range or the encoded character would run past the string end, falls
// back to 1 (caller can advance one byte and try again).
int utf8SafeCharLenAt(const String& s, int index);

// Returns the UTF-8 character at `index` (as a String), or empty if out of range.
String utf8CharAt(const String& s, int index);

bool isBreakableWhitespaceChar(const String& ch);
bool isBreakablePunctuationChar(const String& ch);

// Normalize typography: strip BOM, NBSP -> space, smart quotes -> ASCII
// quotes, em/en dashes -> '-', ellipsis -> "...". Preserves all other bytes.
String normalizeTypography(const String& in);

// Compact text for storage: collapse runs of spaces, normalize line endings,
// limit consecutive newlines to 2, strip trailing whitespace.
String compactText(const String& in);
