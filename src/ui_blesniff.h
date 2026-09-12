#pragma once
#include "ble_sniffer.h"

using BLESniffBackCb = void(*)();

void ui_blesniff_create(BLESniffBackCb on_back);
void ui_blesniff_destroy();
void ui_blesniff_add(const SniffEntry &e);
