#pragma once

#include <Arduino.h>

#include "config.h"

// ============================================================================
//  Button-input subsystem
//
//  Owns the GPIO-button ISR, the edge-event ring buffer, the debounced
//  click-classification state machine, and the user-activity timer used by
//  the deep-sleep deadline. The hardware-adjacent half of input lives here;
//  the rest of the firmware reads results through `g_btns` and the few
//  free functions declared below.
//
//  See input.cpp for the implementation overview + walked-through examples.
// ============================================================================

// Internal state machine + per-tick output flags for the click classifier.
// `poll()` (called from the main loop) consumes the ISR queue and updates
// the output flags. The flags are valid for exactly one loop iteration —
// `resetClicks()` clears them at the top of each `poll()`.
struct ButtonState {
  // Internal state machine — only `poll()` should be touching these.
  bool stablePressed_ = false;
  uint32_t lastStableChange_ = 0;
  uint32_t pressStart_ = 0;
  bool pressArmed_ = false;
  uint32_t lastRelease_ = 0;
  uint32_t firstClickRelease_ = 0;
  uint8_t clickCount_ = 0;

  // Output flags — set by `poll()`, read by callers (`ButtonEvent::fromButtonState`).
  // Cleared at the top of every `poll()`, so they're valid for exactly one
  // loop iteration after they're set.
  bool shortClick_ = false;
  bool doubleClick_ = false;
  bool tripleClick_ = false;
  bool quadClick_ = false;
  bool longClick_ = false;

  void resetClicks() {
    shortClick_ = false;
    doubleClick_ = false;
    tripleClick_ = false;
    quadClick_ = false;
    longClick_ = false;
  }

  void resetState() {
    stablePressed_ = false;
    lastStableChange_ = 0;
    pressStart_ = 0;
    pressArmed_ = false;
    lastRelease_ = 0;
    firstClickRelease_ = 0;
    clickCount_ = 0;
    resetClicks();
  }

  bool anyClick() const {
    return shortClick_ || doubleClick_ || tripleClick_ || quadClick_ || longClick_;
  }

  void poll();
};

extern ButtonState g_btns;

// Screen-facing collapsed view of `ButtonState`'s output flags. The screen
// dispatcher calls `fromButtonState(g_btns)` once per loop and hands the
// result to the current screen.
struct ButtonEvent {
  enum Kind { None, Short, Double, Triple, Quad, Long } kind = None;

  static ButtonEvent fromButtonState(const ButtonState& b) {
    ButtonEvent e;
    if (b.longClick_)        e.kind = Long;
    else if (b.quadClick_)   e.kind = Quad;
    else if (b.tripleClick_) e.kind = Triple;
    else if (b.doubleClick_) e.kind = Double;
    else if (b.shortClick_)  e.kind = Short;
    return e;
  }

  bool any() const { return kind != None; }
};

// ----------------------------------------------------------------------------
//  Lifecycle / interrupt plumbing
// ----------------------------------------------------------------------------
void IRAM_ATTR btnISR();
void clearButtonQueue();
void resetInputFrontend();

// Inspect the ISR-overflow counter; if it's climbed past the recovery
// threshold, atomically reset it and resync the button state machine.
// Called once per loop iteration after polling. Returns true if a recovery
// happened (purely informational; main loop ignores the result).
bool maybeRecoverFromIsrOverflow();

// ----------------------------------------------------------------------------
//  User-activity timer (drives the deep-sleep deadline)
// ----------------------------------------------------------------------------
void markUserActivity();

// Milliseconds since the most recent accepted user input. The sleep
// deadline check in main.cpp compares this against sleepAfterMs().
uint32_t userIdleMs();
