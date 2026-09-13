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
#include <driver/sdmmc_host.h>
#include <sdmmc_cmd.h>

static bool    s_has_sd    = false;
static SdState s_state     = SdState::None;
static char    s_last_err[96] = "";

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

// Talk to the card at the SDMMC protocol level without touching FATFS.
// True means a card answered CMD0/ACMD41 — i.e. it's physically present and
// alive, regardless of what filesystem is on it.
static bool sd_card_answers() {
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.flags = SDMMC_HOST_FLAG_1BIT;
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 1;
    slot.clk = (gpio_num_t)PIN_SD_CLK;
    slot.cmd = (gpio_num_t)PIN_SD_CMD;
    slot.d0  = (gpio_num_t)PIN_SD_D0;

    if (sdmmc_host_init() != ESP_OK) return false;
    bool ok = false;
    if (sdmmc_host_init_slot(host.slot, &slot) == ESP_OK) {
        sdmmc_card_t card;
        ok = (sdmmc_card_init(&host, &card) == ESP_OK);
    }
    sdmmc_host_deinit();
    return ok;
}

static bool littlefs_mount() {
    if (!LittleFS.begin(true)) {
        Serial.println("[storage] LittleFS mount failed");
        return false;
    }
    Serial.printf("[storage] LittleFS mounted  free=%u KB\n",
                  (unsigned)(LittleFS.totalBytes() - LittleFS.usedBytes()) / 1024);
    return true;
}

bool storage_begin() {
    if (sd_mount(false)) {
        s_has_sd = true;
        s_state  = SdState::Ready;
        Serial.printf("[storage] SD mounted  size=%llu MB  type=%d\n",
                      SD_MMC.cardSize() / (1024ULL * 1024ULL), (int)SD_MMC.cardType());
        return true;
    }

    s_state = sd_card_answers() ? SdState::NotFat32 : SdState::None;
    Serial.printf("[storage] SD not mounted (%s) — using LittleFS\n",
                  s_state == SdState::NotFat32 ? "card present, bad filesystem" : "no card");
    s_has_sd = false;
    return littlefs_mount();
}

fs::FS& storage_fs() {
    return s_has_sd ? (fs::FS&)SD_MMC : (fs::FS&)LittleFS;
}

bool    storage_has_sd()   { return s_has_sd; }
SdState storage_sd_state() { return s_state; }

bool storage_format_sd() {
    if (s_has_sd) SD_MMC.end();
    if (sd_mount(true)) {
        s_has_sd = true;
        s_state  = SdState::Ready;
        Serial.printf("[storage] SD formatted+mounted  size=%llu MB\n",
                      SD_MMC.cardSize() / (1024ULL * 1024ULL));
        return true;
    }
    snprintf(s_last_err, sizeof(s_last_err), "Format failed - reseat the card and reboot");
    Serial.printf("[storage] %s\n", s_last_err);
    return false;
}

const char* storage_last_error() { return s_last_err; }

void storage_use_internal() {
    if (s_has_sd) SD_MMC.end();
    s_has_sd = false;
    littlefs_mount();
    Serial.println("[storage] switched to LittleFS by user");
}

uint64_t storage_total_bytes() {
    return s_has_sd ? SD_MMC.totalBytes() : (uint64_t)LittleFS.totalBytes();
}

uint64_t storage_used_bytes() {
    return s_has_sd ? SD_MMC.usedBytes() : (uint64_t)LittleFS.usedBytes();
}
