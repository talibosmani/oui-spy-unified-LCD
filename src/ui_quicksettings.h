#pragma once

void qs_begin();      // call once in setup() — creates panel off-screen
void qs_open();       // slide panel down from top
void qs_close();      // slide panel back up
bool qs_is_open();
