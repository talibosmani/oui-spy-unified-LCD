#pragma once
#include "ble_detector.h"

using DetBackCb = void(*)();  // called when user taps the back button

void ui_detector_create(DetBackCb on_back);
void ui_detector_destroy();
void ui_detector_add(const Detection &d);   // call from main loop after ble_detector_poll()
void ui_detector_set_scanning(bool active); // pulse the status dot
