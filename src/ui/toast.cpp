#include "ui/toast.h"

#include "config.h"        // TOAST_MS, MARGIN_X, SCREEN_H, SCREEN_W, STATUS_H
#include "hal/display.h"   // u8g2 + gfx
#include "ui/font.h"

ToastState g_toast;

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

  Font::useUiSmall();
  int textY = SCREEN_H - 1;
  u8g2.setCursor(MARGIN_X, textY);
  u8g2.print(g_toast.msg.c_str());
  Font::useBody();
}
