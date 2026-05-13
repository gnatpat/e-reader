#pragma once

#include "config.h"
#include "state.h"

#if HAS_BATTERY
void adcSetupOnce();
void updateBatteryCached(bool force = false);
void drawBatteryTopRight();
#endif
