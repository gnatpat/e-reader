#include "state.h"

#include <U8g2_for_Adafruit_GFX.h>

// ============================================================================
//  Display + adapter (definitions; adapter class lives in display.h)
// ============================================================================
EInkDisplay_WirelessPaperV1_2 display;

// ============================================================================
//  Font pointers (declared extern in config.h)
// ============================================================================
const uint8_t* PAGE_FONT = u8g2_font_5x8_tf;
const uint8_t* MAIN_FONT = u8g2_font_helvR08_te;
const uint8_t* BOLD_FONT = u8g2_font_helvB08_te;

// ============================================================================
//  Globals — instances of the shared state structs declared in state.h.
//  Pure definitions; no logic lives in this file.
// ============================================================================
WebServer server(80);
Preferences prefs;

char AP_SSID[24] = "PALA-";
const char* AP_PASS = "palaread";
