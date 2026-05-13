#pragma once

#include <Arduino.h>
#include <heltec-eink-modules.h>
#include <WebServer.h>
#include <Preferences.h>
#include <LittleFS.h>

#include "config.h"
#include "pure/paginator.h"     // LayoutMetrics
#include "pure/offset_cache.h"  // OffsetCache

// Use LittleFS as the project's filesystem. Defined AFTER FS.h has declared
// `class FS`, so the macro doesn't collide with that class name.
#define FS LittleFS

// ============================================================================
//  Globals (definitions live in state.cpp)
// ============================================================================
extern WebServer server;
extern Preferences prefs;

extern char AP_SSID[24];
extern const char* AP_PASS;

extern EInkDisplay_WirelessPaperV1_2 display;
