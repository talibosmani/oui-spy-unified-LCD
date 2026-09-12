#pragma once
#include <stdint.h>
#include <stdbool.h>

struct FoxDevice {
    char    mac[18];         // "AA:BB:CC:DD:EE:FF"
    char    name[20];        // device name, may be empty
    int8_t  rssi;            // latest RSSI
    int8_t  rssi_peak;       // strongest ever seen
    uint32_t last_seen;      // millis()
};

void foxhunter_start();
void foxhunter_stop();

// Fills out[] with active devices (seen < 8s ago), sorted by RSSI descending.
// Returns number of devices written (at most max).
int foxhunter_get_devices(FoxDevice *out, int max);

// Live RSSI for a specific MAC (-128 if not found / stale).
int8_t foxhunter_rssi(const char *mac);
