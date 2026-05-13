#pragma once

#include "config.h"
#include "state.h"

// ============================================================================
//  Shared drawing helpers used by multiple screens
// ============================================================================
int drawSectionHeader(const char* title);
void drawMenuBulletRow(int yBaseline, const String& label, bool selected,
                       bool boldText = false, int depth = 0, bool systemItem = false);
void splitListLabelForDisplay(const String& in, int maxWidth, String& line1, String& line2);

// Layout constants shared across screen implementations.
static const int UI_LIST_LEFT = MARGIN_X + 4;
static const int UI_DEPTH_INDENT = 10;
