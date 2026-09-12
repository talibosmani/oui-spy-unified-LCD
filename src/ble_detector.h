#pragma once
#include <stdint.h>

struct Detection {
    char    mac[18];         // "AA:BB:CC:DD:EE:FF"
    char    vendor[12];      // "AXON", "FLOCK", "DJI", "META", etc.
    char    method[12];      // "oui", "cid", "svc_uuid", "name"
    int8_t  rssi;
    uint32_t badge_color;   // LVGL hex color for the vendor badge
};

// Start passive BLE scan. Detections are placed in the internal queue.
void ble_detector_start();
void ble_detector_stop();

// Returns true and fills *out if a new (debounced) detection is waiting.
// Safe to call from any task — uses an RTOS queue internally.
bool ble_detector_poll(Detection *out);
