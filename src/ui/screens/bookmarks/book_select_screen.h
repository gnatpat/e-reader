#pragma once

#include "ui/screen.h"

class BookmarkBookSelectScreen : public Screen {
public:
  void onEnter() override;
  void onButton(const ButtonEvent& e) override;
  void draw() override;
};

extern BookmarkBookSelectScreen g_bmBookSelectScreen;
