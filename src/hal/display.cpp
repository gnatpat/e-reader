#include "hal/display.h"

#include "storage/settings_store.h"  // g_settings (lineGap, fontSize)

U8G2_FOR_ADAFRUIT_GFX u8g2;
HeltecGFXAdapter gfx(display);

// Transient bottom-of-screen status message. Written by showToast(),
// cleared by drawToastIfActive() on expiry; the reader peeks at
// `g_toast.untilMs` to decide between toast vs. status bar.
ToastState g_toast;

// ============================================================================
//  Fonts / metrics — owned here; nothing else needs direct access. External
//  callers go through invalidateMetrics() and getMetrics().
// ============================================================================
static LayoutMetrics s_metrics;
static bool s_metricsValid = false;

void invalidateMetrics() {
  s_metricsValid = false;
}

const LayoutMetrics& getMetrics() {
  if (!s_metricsValid) {
    u8g2.setFont(MAIN_FONT);
    s_metrics.ascent = u8g2.getFontAscent();
    s_metrics.descent = u8g2.getFontDescent();
    s_metrics.lineH = (s_metrics.ascent - s_metrics.descent) + g_settings.lineGap;

    int w = SCREEN_W - (MARGIN_X * 2);
    if (w < 50) w = 50;
    s_metrics.maxWidth = w;

    int maxHeight = SCREEN_H - TOP_PAD - BOT_PAD;
    if (SHOW_PROGRESS_BAR || SHOW_PAGE_NUMBER) maxHeight -= STATUS_H;

    s_metrics.maxLines = maxHeight / s_metrics.lineH;
    if (s_metrics.maxLines < 1) s_metrics.maxLines = 1;
    s_metricsValid = true;
  }
  return s_metrics;
}

void applyFontSize(int sz) {
  switch (sz) {
    case 8:  MAIN_FONT = u8g2_font_helvR08_te; BOLD_FONT = u8g2_font_helvB08_te; break;
    case 10: MAIN_FONT = u8g2_font_helvR10_te; BOLD_FONT = u8g2_font_helvB10_te; break;
    case 12: MAIN_FONT = u8g2_font_helvR12_te; BOLD_FONT = u8g2_font_helvB12_te; break;
    case 14: MAIN_FONT = u8g2_font_helvR14_te; BOLD_FONT = u8g2_font_helvB14_te; break;
    default: MAIN_FONT = u8g2_font_helvR10_te; BOLD_FONT = u8g2_font_helvB10_te; sz = 10; break;
  }
  g_settings.fontSize = sz;
  invalidateMetrics();
}

// ============================================================================
//  Drawing primitives
// ============================================================================
void beginPageCanvas(bool clearMem) {
  if (clearMem) display.clearMemory();
  display.landscape();
  u8g2.setFontMode(1);
  u8g2.setForegroundColor(1);
  u8g2.setBackgroundColor(0);
}

// Counter that drives the periodic full refresh of menu screens (clears
// ghosting). Owned here; nothing outside `prepareMenuFrame` needs to see it.
static int s_menuDrawsSinceFull = 0;

void prepareMenuFrame() {
  bool doFull = (s_menuDrawsSinceFull >= MENU_FULL_REFRESH_EVERY);
  if (doFull) {
    display.fastmodeOff();
    display.clear();
    s_menuDrawsSinceFull = 0;
  } else {
    display.fastmodeOn();
  }
  beginPageCanvas();
  s_menuDrawsSinceFull++;
}

void drawCenter(const char* a, const char* b) {
  display.fastmodeOff();
  display.clear();
  beginPageCanvas();
  u8g2.setFont(MAIN_FONT);

  const int lineH = 16;
  int y = (SCREEN_H / 2) - lineH / 2;
  if (b) y -= lineH / 2;

  int wA = u8g2.getUTF8Width(a);
  u8g2.setCursor((SCREEN_W - wA) / 2, y);
  u8g2.print(a);

  if (b) {
    y += lineH;
    int wB = u8g2.getUTF8Width(b);
    u8g2.setCursor((SCREEN_W - wB) / 2, y);
    u8g2.print(b);
  }
  display.update();
}

void showToast(const String& msg) {
  g_toast.msg = msg;
  g_toast.untilMs = millis() + TOAST_MS;
}

void resetUiEphemeralState() {
  g_toast.msg = "";
  g_toast.untilMs = 0;
}

void drawToastIfActive() {
  if (g_toast.untilMs == 0) return;
  if ((int32_t)(millis() - g_toast.untilMs) > 0) {
    g_toast.untilMs = 0;
    g_toast.msg = "";
    return;
  }

  const int yTop = SCREEN_H - STATUS_H;
  gfx.fillRect(0, yTop, SCREEN_W, STATUS_H, 0);

  u8g2.setFont(u8g2_font_6x10_tf);
  int textY = SCREEN_H - 1;
  u8g2.setCursor(MARGIN_X, textY);
  u8g2.print(g_toast.msg.c_str());
  u8g2.setFont(MAIN_FONT);
}
