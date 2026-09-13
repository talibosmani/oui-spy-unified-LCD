#pragma once
#include <FS.h>
#include <stdint.h>

// Call once in setup(). Tries SD first, falls back to LittleFS.
bool     storage_begin();

// The active filesystem — use for all open/exists calls.
fs::FS&  storage_fs();

bool     storage_has_sd();
uint64_t storage_total_bytes();
uint64_t storage_used_bytes();

// Format the SD card as FAT32 then re-mount it.
// Returns true if the card is now mounted as SD.
bool storage_format_sd();
