#pragma once

#include "ui/screen.h"

class BookmarkListScreen : public Screen {
public:
  void onEnter() override;
  void onButton(const ButtonEvent& e) override;
  void draw() override;
};

extern BookmarkListScreen g_bmListScreen;
