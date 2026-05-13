#pragma once

#include "ui/screen.h"

class AboutScreen : public Screen {
public:
  void onEnter() override;
  void onButton(const ButtonEvent& e) override;
  void draw() override;
};

extern AboutScreen g_aboutScreen;
