#pragma once
#include <stdint.h>

struct SniffEntry {
    char    mac[18];     // "AA:BB:CC:DD:EE:FF"
    char    name[18];    // device name (truncated), empty if none
    int8_t  rssi;
    uint32_t first_seen; // millis()
};

void blesniff_start();
void blesniff_stop();

// Returns true and fills *out if a new unique device was seen.
bool blesniff_poll(SniffEntry *out);
