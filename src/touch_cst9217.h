#pragma once
#include <stdint.h>

bool touch_begin();
bool touch_read(uint16_t *x, uint16_t *y);
