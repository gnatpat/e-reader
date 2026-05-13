#pragma once

#include <Adafruit_GFX.h>
#include <U8g2_for_Adafruit_GFX.h>

#include "config.h"
#include "state.h"

// ============================================================================
//  Display adapter — wraps the Heltec EInk display so Adafruit_GFX can draw
//  to it. Rotates the screen 180° because of the panel orientation.
// ============================================================================
class HeltecGFXAdapter : public Adafruit_GFX {
public:
  explicit HeltecGFXAdapter(EInkDisplay_WirelessPaperV1_2& d)
    : Adafruit_GFX(SCREEN_W, SCREEN_H), disp(d) {}

  void drawPixel(int16_t x, int16_t y, uint16_t color) override {
    if (x < 0 || y < 0 || x >= SCREEN_W || y >= SCREEN_H) return;
    uint16_t c = color ? BLACK : WHITE;
    int16_t xx = (SCREEN_W - 1) - x;
    int16_t yy = (SCREEN_H - 1) - y;
    disp.drawPixel(xx, yy, c);
  }

private:
  EInkDisplay_WirelessPaperV1_2& disp;
};

extern HeltecGFXAdapter gfx;
extern U8G2_FOR_ADAFRUIT_GFX u8g2;

// ============================================================================
//  Fonts / metrics
// ============================================================================
void invalidateMetrics();
const LayoutMetrics& getMetrics();
void applyFontSize(int sz);

// ============================================================================
//  Drawing primitives
// ============================================================================
void beginPageCanvas(bool clearMem = true);
void prepareMenuFrame();
void drawCenter(const char* a, const char* b = nullptr);

// ============================================================================
//  Toast (transient bottom-of-screen status message)
//
//  showToast() writes msg + sets an expiry; drawToastIfActive() reads it
//  during the next render and clears it once expired. The reader checks
//  `g_toast.untilMs` to decide between overlaying the toast and drawing the
//  normal status bar.
// ============================================================================
struct ToastState {
  String msg;
  uint32_t untilMs = 0;
};
extern ToastState g_toast;

void showToast(const String& msg);
void drawToastIfActive();
void resetUiEphemeralState();   // clears any pending toast
