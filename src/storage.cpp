// Storage abstraction — onboard microSD (SDMMC 1-bit) with LittleFS fallback.
// Call storage_begin() once in setup(). All log modules then call
// storage_fs() to get the active fs::FS reference.
//
// The board's TF slot is wired to the ESP32-S3's native SDMMC host
// (CMD=GPIO1, CLK=GPIO2, D0=GPIO3), so no SPI bus is involved and there is
// no contention with the CO5300 display's QSPI on SPI2_HOST.
#include "storage.h"
#include "config.h"
#include <SD_MMC.h>
#include <LittleFS.h>
#include <Arduino.h>

static bool s_has_sd = false;
static char s_last_err[96] = "";

static bool sd_mount(bool format_if_failed) {
    SD_MMC.setPins(PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0);
    if (!SD_MMC.begin("/sdcard", /*mode1bit=*/true, format_if_failed, SDMMC_FREQ_DEFAULT)) {
        return false;
    }
    if (SD_MMC.cardType() == CARD_NONE) {
        SD_MMC.end();
        return false;
    }
    return true;
}

bool storage_begin() {
    if (sd_mount(false)) {
        s_has_sd = true;
        Serial.printf("[storage] SD mounted  size=%llu MB  type=%d\n",
                      SD_MMC.cardSize() / (1024ULL * 1024ULL), (int)SD_MMC.cardType());
        return true;
    }
    Serial.println("[storage] SD not mounted — falling back to LittleFS");

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
    return s_has_sd ? (fs::FS&)SD_MMC : (fs::FS&)LittleFS;
}

bool storage_has_sd() { return s_has_sd; }

const char* storage_sd_probe() {
    // SDMMC gives no cheap pre-mount probe; begin() failing already means the
    // host got no valid response on CMD/D0. Distinguish "no card" from "bad FS"
    // by attempting a mount and reading the card type.
    if (SD_MMC.begin("/sdcard", true, false, SDMMC_FREQ_DEFAULT)) {
        SD_MMC.end();
        return "Card detected but not mounted";
    }
    return "No card detected in onboard slot";
}

bool storage_format_sd() {
    if (s_has_sd) SD_MMC.end();
    if (sd_mount(true)) {
        s_has_sd = true;
        Serial.printf("[storage] SD formatted+mounted  size=%llu MB\n",
                      SD_MMC.cardSize() / (1024ULL * 1024ULL));
        return true;
    }
    snprintf(s_last_err, sizeof(s_last_err), "No card responding - reseat card");
    Serial.printf("[storage] %s\n", s_last_err);
    return false;
}

const char* storage_last_error() { return s_last_err; }

uint64_t storage_total_bytes() {
    return s_has_sd ? SD_MMC.totalBytes() : (uint64_t)LittleFS.totalBytes();
}

uint64_t storage_used_bytes() {
    return s_has_sd ? SD_MMC.usedBytes() : (uint64_t)LittleFS.usedBytes();
}
