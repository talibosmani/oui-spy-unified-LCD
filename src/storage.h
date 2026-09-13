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

enum class SdState {
    None,      // no card answers on the SDMMC bus
    NotFat32,  // card answers but the filesystem can't be mounted
    Ready      // mounted, FAT32
};
SdState  storage_sd_state();

// Format the SD card as FAT32 then re-mount it.
// Returns true if the card is now mounted as SD; else see storage_last_error().
bool        storage_format_sd();
const char* storage_last_error();

// Unmount SD (if mounted) and switch all logging to LittleFS.
void storage_use_internal();
