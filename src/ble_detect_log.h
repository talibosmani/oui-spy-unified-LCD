#pragma once
#include "ble_detector.h"

void ble_detect_log_update(const Detection &d);
// Call once per loop() — flushes dirty log to flash at most once per minute.
void ble_detect_log_tick();
