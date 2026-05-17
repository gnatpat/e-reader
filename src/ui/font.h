#pragma once

#include "pure/paginator.h"  // LayoutMetrics

// ============================================================================
//  Font module — owns the device-wide font role mapping (which u8g2 font
//  table is "body", "bold", etc.) and the layout-metrics cache derived from
//  the body font + line gap + screen dimensions.
//
//  Roles:
//    Body / Bold   Helvetica regular/bold at the user-chosen body size
//                  (8/10/12/14). Reader text, menu rows, section headers.
//    UiSmall       Fixed 6x10. Toast text + battery percentage.
//    UiTiny        Fixed 5x8.  Page number in the reader status bar.
//
//  Roles, not tables: nothing outside font.cpp references u8g2 font
//  identifiers directly. Adding/changing a font is a one-file change.
// ============================================================================
namespace Font {

// Switch the active u8g2 font to the named role. After one of these, raw
// u8g2 calls (setCursor/print/getUTF8Width) all use the role's table.
void useBody();
void useBold();
void useUiSmall();
void useUiTiny();

// Big-display bold (Helvetica B14). Exposed for the Pala apps API's
// `drawCenteredLarge` — firmware screens use Body/Bold under the
// user-chosen size and don't reach for this directly.
void useAppLarge();

// Layout metrics for the body font under the current line gap and screen
// dimensions. Cached; invalidated automatically by the mutators below.
// Calling this leaves the active u8g2 font set to Body.
const LayoutMetrics& bodyLayout();

// ----------------------------------------------------------------------------
//  Persistence — NVS keys live inside font.cpp; nothing else references them.
// ----------------------------------------------------------------------------

// Read body size + line gap from NVS into Font's internal state and apply
// them. Defaults: bodySize=10 (with input validation falling back to 10),
// lineGap=0 (clamped to [0, 4]). Call once from setup() after `prefs.begin`.
void loadSettings();

// Apply + persist. Out-of-set body sizes fall back to 10; line gap is
// clamped to [0, 4]. Both invalidate the layout cache as needed.
void setBodySize(int sz);
void setLineGap(int gap);

// Current applied values. Page-cache stamping passes the body-size + line-gap
// pair so on-disk caches self-invalidate when either changes.
int currentBodySize();
int currentLineGap();

}  // namespace Font
