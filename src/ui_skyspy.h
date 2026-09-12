#pragma once
#include "skyspy.h"

using SkySpy_BackCb = void(*)();

void ui_skyspy_create(SkySpy_BackCb on_back);
void ui_skyspy_destroy();
void ui_skyspy_add(const DroneEntry &d);
