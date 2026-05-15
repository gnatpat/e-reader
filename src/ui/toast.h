#pragma once

#include "pure/arduino_compat.h"  // String

// ============================================================================
//  Toast — transient bottom-of-screen status message.
//
//  showToast() writes msg + sets an expiry; drawToastIfActive() reads it
//  during the next render and clears it once expired. The reader peeks at
//  `g_toast.untilMs` to decide between overlaying the toast and drawing the
//  normal status bar.
// ============================================================================
struct ToastState {
  String   msg;
  uint32_t untilMs = 0;
};
extern ToastState g_toast;

void showToast(const String& msg);
void drawToastIfActive();
void resetUiEphemeralState();   // clears any pending toast
