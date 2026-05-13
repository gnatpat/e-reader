#pragma once

#include "config.h"
#include "state.h"

// LittleFS mount + size accounting + directory helpers. Firmware-only.

bool   fsBegin();
size_t fsTotalBytesSafe();
size_t fsUsedBytesSafe();
size_t fsFreeBytesSafe();
void   ensureBooksDir();
bool   ensureDirRecursive(const String& path);
bool   isDirEmpty(const String& path);
