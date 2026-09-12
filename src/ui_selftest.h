#pragma once

void selftest_begin();    // call once in setup() after display::begin()
void selftest_open();     // slide panel in from right
void selftest_close();    // slide panel back out
bool selftest_is_open();
