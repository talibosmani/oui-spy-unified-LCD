#pragma once
#include "wifi_scanner.h"

using FlockBackCb = void(*)();

void ui_flockyou_create(FlockBackCb on_back);
void ui_flockyou_destroy();
void ui_flockyou_add(const FlockDetection &d);
void ui_flockyou_set_channel(uint8_t ch);
void ui_flockyou_tick();   // call every loop() — drives alert RED→AMBER→dismiss
