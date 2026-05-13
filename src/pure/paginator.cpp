#include "paginator.h"

#include "text_util.h"

static void trimTrailingSpaces(String& s) {
  while (s.length() > 0 && s[s.length() - 1] == ' ') s.remove(s.length() - 1);
}

static void trimLeadingSpaces(String& s) {
  while (s.length() > 0 && s[0] == ' ') s.remove(0, 1);
}

static bool lineEndsWithSpace(const String& s) {
  return s.length() > 0 && s[s.length() - 1] == ' ';
}

uint32_t paginatePage(IReadStream& in,
                      uint32_t startPos,
                      const LayoutMetrics& m,
                      const MeasureFn& measure,
                      const LineCallback& onLine) {
  in.seek(startPos);

  int linesUsed = 0;
  String line;
  String token;
  line.reserve(96);
  token.reserve(48);

  uint32_t lineStartPos = startPos;
  uint32_t tokenStartPos = startPos;

  auto flushLine = [&](const String& toPrint) {
    String printable = toPrint;
    trimTrailingSpaces(printable);
    if (onLine) onLine(printable);
    linesUsed++;
  };

  auto safeReturn = [&](uint32_t off) -> uint32_t {
    if (off <= startPos) off = startPos + 1;
    size_t sz = in.size();
    if (sz > 0 && off > sz) off = sz;
    return off;
  };

  auto hardBreakToken = [&](String& t, uint32_t& tStartPos) -> uint32_t {
    while (t.length() > 0) {
      String chunk;
      chunk.reserve(32);
      int i = 0;
      while (i < (int)t.length()) {
        int clen = utf8SafeCharLenAt(t, i);
        if (clen <= 0) break;
        String candidate = chunk + t.substring(i, i + clen);
        if (measure(candidate.c_str()) > m.maxWidth) break;
        chunk = candidate;
        i += clen;
      }
      if (chunk.length() == 0) {
        int clen = utf8SafeCharLenAt(t, 0);
        if (clen <= 0) clen = 1;
        chunk = t.substring(0, clen);
      }
      flushLine(chunk);
      if (linesUsed >= m.maxLines) return safeReturn(tStartPos + (uint32_t)chunk.length());
      t.remove(0, chunk.length());
      tStartPos += (uint32_t)chunk.length();
    }
    return 0;
  };

  auto appendTokenToLine = [&](String& t, uint32_t tPos) -> uint32_t {
    if (t.length() == 0) return 0;

    if (line.length() == 0) {
      trimLeadingSpaces(t);
      if (t.length() == 0) return 0;
      if (measure(t.c_str()) > m.maxWidth) {
        return hardBreakToken(t, tPos);
      }
      line = t;
      lineStartPos = tPos;
      t = "";
      return 0;
    }

    trimLeadingSpaces(t);
    if (t.length() == 0) return 0;

    String candidate = line + t;
    if (measure(candidate.c_str()) > m.maxWidth) {
      trimTrailingSpaces(line);
      flushLine(line);
      if (linesUsed >= m.maxLines) return safeReturn(tPos);

      if (measure(t.c_str()) > m.maxWidth) {
        return hardBreakToken(t, tPos);
      } else {
        line = t;
        lineStartPos = tPos;
      }
    } else {
      line = candidate;
    }

    t = "";
    return 0;
  };

  while (in.available() && linesUsed < m.maxLines) {
    uint32_t charPos = in.position();
    int rb = in.read();
    if (rb < 0) break;
    char c = (char)rb;
    if (c == '\r') continue;

    String ch;
    ch += c;

    if (c == '\n') {
      uint32_t forcedNext = appendTokenToLine(token, tokenStartPos);
      if (forcedNext != 0) return forcedNext;
      flushLine(line);
      if (linesUsed >= m.maxLines) return safeReturn(in.position());
      line = "";
      lineStartPos = in.position();
      continue;
    }

    if (c == '\t') ch = " ";

    if (isBreakableWhitespaceChar(ch)) {
      uint32_t forcedNext = appendTokenToLine(token, tokenStartPos);
      if (forcedNext != 0) return forcedNext;
      if (line.length() > 0 && !lineEndsWithSpace(line)) line += " ";
      continue;
    }

    if (token.length() == 0) tokenStartPos = charPos;
    token += ch;

    if (isBreakablePunctuationChar(ch)) {
      uint32_t forcedNext = appendTokenToLine(token, tokenStartPos);
      if (forcedNext != 0) return forcedNext;
    }
  }

  uint32_t forcedNext = appendTokenToLine(token, tokenStartPos);
  if (forcedNext != 0) return forcedNext;

  if (linesUsed < m.maxLines && line.length() > 0) {
    trimTrailingSpaces(line);
    flushLine(line);
  }

  return safeReturn(in.position());
}
