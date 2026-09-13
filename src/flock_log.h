#pragma once
#include "wifi_scanner.h"

// Persist Flock camera sightings to /flock_log.json on LittleFS.
// Timestamps are seconds-since-boot until a GPS module provides real epoch.
// Call flock_log_update() each time a FLOCK frame is received — it lazily
// loads the existing log on first call, then upserts and saves.
void flock_log_update(const FlockDetection &d);
void flock_log_tick();
