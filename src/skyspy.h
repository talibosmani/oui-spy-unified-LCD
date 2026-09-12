#pragma once
#include <stdint.h>

struct DroneEntry {
    char    mac[18];      // "AA:BB:CC:DD:EE:FF"
    char    id[21];       // OpenDroneID Basic ID string (max 20 chars)
    uint8_t ua_type;      // Unmanned Aircraft type (0-15)
    int8_t  rssi;
    uint32_t last_seen;   // millis()
};

void skyspy_start();
void skyspy_stop();

// Returns true and fills *out if a new/updated drone was seen.
bool skyspy_poll(DroneEntry *out);
