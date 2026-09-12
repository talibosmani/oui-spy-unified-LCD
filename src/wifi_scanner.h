#pragma once
#include <stdint.h>

struct FlockDetection {
    char     mac[18];
    char     ssid[33];
    char     type[8];      // "PROBE", "AP", "DATA"
    int8_t   rssi;
    uint8_t  channel;
    uint32_t badge_color;
};

// Call from setup() to pre-init the WiFi stack alongside BLE
void flockscanner_init();

// Start / stop promiscuous capture
void flockscanner_start();
void flockscanner_stop();

// Drain one detection from the FreeRTOS queue; returns false when empty
bool flockscanner_poll(FlockDetection *out);

// Call from loop() while Flock-You is active — hops channels, returns current channel
uint8_t flockscanner_tick();
