#include "ble_sniff_log.h"
#include "storage.h"
#include <ArduinoJson.h>
#include <Arduino.h>
#include <string.h>

#define LOG_PATH    "/ble_sniff_log.json"
#define MAX_ENTRIES  200

struct SniffLogEntry {
    char     mac[18];
    char     name[18];
    uint32_t first_seen;
    uint32_t last_seen;
    uint16_t times_seen;
    int8_t   rssi_peak;
};

static SniffLogEntry s_table[MAX_ENTRIES];
static int           s_count     = 0;
static bool          s_inited    = false;
static bool          s_dirty     = false;
static uint32_t      s_last_save = 0;
static bool        s_urgent    = false;
static const uint32_t SAVE_INTERVAL_MS = 60000;
static const uint32_t NEW_ENTRY_FLUSH_MS = 2000;

static void save() {
    File f = storage_fs().open(LOG_PATH, "w");
    if (!f) { Serial.println("[ble_sniff_log] write failed"); return; }
    JsonDocument doc;
    doc["note"] = "first_seen/last_seen = uptime seconds";
    JsonArray arr = doc["devices"].to<JsonArray>();
    for (int i = 0; i < s_count; i++) {
        JsonObject o = arr.add<JsonObject>();
        o["mac"]        = s_table[i].mac;
        o["name"]       = s_table[i].name;
        o["first_seen"] = s_table[i].first_seen;
        o["last_seen"]  = s_table[i].last_seen;
        o["times_seen"] = s_table[i].times_seen;
        o["rssi_peak"]  = s_table[i].rssi_peak;
    }
    serializeJson(doc, f);
    f.close();
    s_last_save = millis();
    s_dirty     = false;
    s_urgent     = false;
}

static void load() {
    File f = storage_fs().open(LOG_PATH, "r");
    if (!f) return;
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) return;
    JsonArray arr = doc["devices"].as<JsonArray>();
    for (JsonObject o : arr) {
        if (s_count >= MAX_ENTRIES) break;
        auto &e = s_table[s_count++];
        strncpy(e.mac,  o["mac"]  | "", 17); e.mac[17]  = '\0';
        strncpy(e.name, o["name"] | "", 17); e.name[17] = '\0';
        e.first_seen = o["first_seen"] | 0u;
        e.last_seen  = o["last_seen"]  | 0u;
        e.times_seen = o["times_seen"] | 1u;
        e.rssi_peak  = (int8_t)(o["rssi_peak"] | -127);
    }
    Serial.printf("[ble_sniff_log] loaded %d entries\n", s_count);
}

void ble_sniff_log_update(const SniffEntry &se) {
    if (!s_inited) { load(); s_inited = true; }
    const uint32_t now = millis() / 1000;

    for (int i = 0; i < s_count; i++) {
        if (strncmp(s_table[i].mac, se.mac, 17) == 0) {
            s_table[i].last_seen = now;
            s_table[i].times_seen++;
            if (se.rssi > s_table[i].rssi_peak) s_table[i].rssi_peak = se.rssi;
            if (se.name[0] != '\0' && s_table[i].name[0] == '\0')
                strncpy(s_table[i].name, se.name, 17);
            s_dirty = true;
            return;
        }
    }

    int slot;
    if (s_count < MAX_ENTRIES) {
        slot = s_count++;
    } else {
        slot = 0;
        for (int i = 1; i < MAX_ENTRIES; i++)
            if (s_table[i].first_seen < s_table[slot].first_seen) slot = i;
    }

    strncpy(s_table[slot].mac,  se.mac,  17); s_table[slot].mac[17]  = '\0';
    strncpy(s_table[slot].name, se.name, 17); s_table[slot].name[17] = '\0';
    s_table[slot].first_seen = now;
    s_table[slot].last_seen  = now;
    s_table[slot].times_seen = 1;
    s_table[slot].rssi_peak  = se.rssi;
    s_dirty = s_urgent = true; // new entry — flushed by tick() within NEW_ENTRY_FLUSH_MS
    Serial.printf("[ble_sniff_log] new %s total=%d\n", se.mac, s_count);
}

void ble_sniff_log_tick() {
    uint32_t since = millis() - s_last_save;
    if (s_dirty && (since >= SAVE_INTERVAL_MS || (s_urgent && since >= NEW_ENTRY_FLUSH_MS))) {
        save();
    }
}
