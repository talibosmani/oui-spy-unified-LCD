#pragma once
#include "foxhunter.h"

using FoxBackCb = void(*)();

void ui_foxhunter_create(FoxBackCb on_back);
void ui_foxhunter_destroy();

// Single update call: rebuilds list in scan mode, updates gauge in track mode.
void ui_foxhunter_update(const FoxDevice *devs, int count);

bool ui_foxhunter_is_tracking();
