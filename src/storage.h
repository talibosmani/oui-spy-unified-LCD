#pragma once
#include <FS.h>
#include <stdint.h>

// Call once in setup(). Tries SD first, falls back to LittleFS.
// Returns true if any filesystem is available.
bool storage_begin();

// The active filesystem — use for all open/exists calls.
fs::FS& storage_fs();

bool     storage_has_sd();
uint64_t storage_total_bytes();
uint64_t storage_used_bytes();
