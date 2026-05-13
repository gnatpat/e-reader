#pragma once

#include <functional>

#include "arduino_compat.h"
#include "stream.h"

// Layout dimensions for the paginator. All values are in pixels.
struct LayoutMetrics {
  int ascent = 0;
  int descent = 0;
  int lineH = 0;
  int maxWidth = 0;
  int maxLines = 1;
};

// Measure the rendered width (in pixels) of a UTF-8 string under the layout's
// current font. The paginator never sets fonts itself — callers must ensure
// the measurement function is consistent with the metrics they pass in.
using MeasureFn = std::function<int(const char*)>;

// Called once per emitted line, in order. The String is the line's text with
// trailing spaces trimmed.
using LineCallback = std::function<void(const String& line)>;

// Pure pagination engine. Reads bytes from `in` starting at `startPos` and
// emits at most `metrics.maxLines` lines via `onLine`. Returns the absolute
// byte offset where the next page begins.
//
// `onLine` may be null (just compute the next offset). `measure` MUST be set.
uint32_t paginatePage(IReadStream& in,
                      uint32_t startPos,
                      const LayoutMetrics& metrics,
                      const MeasureFn& measure,
                      const LineCallback& onLine = nullptr);
