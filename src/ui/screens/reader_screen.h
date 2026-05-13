#pragma once

#include "ui/screen.h"

class ReaderScreen : public Screen {
public:
  void onEnter() override;
  void onButton(const ButtonEvent& e) override;
  void draw() override;
  void onIdleTick() override;
  void onSleep() override;
};

extern ReaderScreen g_readerScreen;
