// Storage abstraction — SD card (SPI) with LittleFS fallback.
// Call storage_begin() once in setup(). All log modules then call
// storage_fs() to get the active fs::FS reference.
#include "storage.h"
#include "config.h"
#include <SD.h>
#include <SPI.h>
#include <LittleFS.h>
#include <Arduino.h>

static bool s_has_sd = false;

bool storage_begin() {
    // --- Try SD card first ---
    SPI.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
    if (SD.begin(PIN_SD_CS, SPI, 4000000)) {
        s_has_sd = true;
        Serial.printf("[storage] SD mounted  size=%llu MB  type=%d\n",
                      SD.cardSize() / (1024ULL * 1024ULL), (int)SD.cardType());
        return true;
    }

    // --- Fall back to LittleFS ---
    Serial.println("[storage] SD not found — falling back to LittleFS");
    s_has_sd = false;
    if (!LittleFS.begin(true)) {
        Serial.println("[storage] LittleFS mount failed");
        return false;
    }
    Serial.printf("[storage] LittleFS mounted  free=%u KB\n",
                  (unsigned)(LittleFS.totalBytes() - LittleFS.usedBytes()) / 1024);
    return true;
}

fs::FS& storage_fs() {
    return s_has_sd ? (fs::FS&)SD : (fs::FS&)LittleFS;
}

bool storage_has_sd() { return s_has_sd; }

bool storage_format_sd() {
    if (s_has_sd) SD.end(); // unmount if already mounted
    SPI.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
    // format_if_empty=true tells the library to write a fresh FAT32
    // filesystem if the existing one can't be read.
    if (SD.begin(PIN_SD_CS, SPI, 4000000, "/sd", 5, true)) {
        s_has_sd = true;
        Serial.printf("[storage] SD formatted+mounted  size=%llu MB\n",
                      SD.cardSize() / (1024ULL * 1024ULL));
        return true;
    }
    Serial.println("[storage] format failed — card not present or wiring issue");
    return false;
}

uint64_t storage_total_bytes() {
    return s_has_sd ? SD.totalBytes() : (uint64_t)LittleFS.totalBytes();
}

uint64_t storage_used_bytes() {
    return s_has_sd ? SD.usedBytes() : (uint64_t)LittleFS.usedBytes();
}
