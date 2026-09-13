#include "skyspy_log.h"
#include "storage.h"
#include <ArduinoJson.h>
#include <Arduino.h>
#include <string.h>

#define LOG_PATH    "/skyspy_log.json"
#define MAX_ENTRIES  100

struct DroneLogEntry {
    char     mac[18];
    char     drone_id[21];
    uint8_t  ua_type;
    uint32_t first_seen;
    uint32_t last_seen;
    uint16_t times_seen;
    int8_t   rssi_peak;
};

static DroneLogEntry s_table[MAX_ENTRIES];
static int           s_count     = 0;
static bool          s_inited    = false;
static bool          s_dirty     = false;
static uint32_t      s_last_save = 0;
static const uint32_t SAVE_INTERVAL_MS = 60000;

static void save() {
    File f = storage_fs().open(LOG_PATH, "w");
    if (!f) { Serial.println("[skyspy_log] write failed"); return; }
    JsonDocument doc;
    doc["note"] = "first_seen/last_seen = uptime seconds; ua_type per OpenDroneID spec";
    JsonArray arr = doc["drones"].to<JsonArray>();
    for (int i = 0; i < s_count; i++) {
        JsonObject o = arr.add<JsonObject>();
        o["mac"]        = s_table[i].mac;
        o["drone_id"]   = s_table[i].drone_id;
        o["ua_type"]    = s_table[i].ua_type;
        o["first_seen"] = s_table[i].first_seen;
        o["last_seen"]  = s_table[i].last_seen;
        o["times_seen"] = s_table[i].times_seen;
        o["rssi_peak"]  = s_table[i].rssi_peak;
    }
    serializeJson(doc, f);
    f.close();
    s_last_save = millis();
    s_dirty     = false;
}

static void load() {
    File f = storage_fs().open(LOG_PATH, "r");
    if (!f) return;
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) return;
    JsonArray arr = doc["drones"].as<JsonArray>();
    for (JsonObject o : arr) {
        if (s_count >= MAX_ENTRIES) break;
        auto &e = s_table[s_count++];
        strncpy(e.mac,      o["mac"]      | "", 17); e.mac[17]      = '\0';
        strncpy(e.drone_id, o["drone_id"] | "", 20); e.drone_id[20] = '\0';
        e.ua_type    = o["ua_type"]    | 0u;
        e.first_seen = o["first_seen"] | 0u;
        e.last_seen  = o["last_seen"]  | 0u;
        e.times_seen = o["times_seen"] | 1u;
        e.rssi_peak  = (int8_t)(o["rssi_peak"] | -127);
    }
    Serial.printf("[skyspy_log] loaded %d entries\n", s_count);
}

void skyspy_log_update(const DroneEntry &de) {
    if (!s_inited) { load(); s_inited = true; }
    const uint32_t now = millis() / 1000;

    for (int i = 0; i < s_count; i++) {
        if (strncmp(s_table[i].mac, de.mac, 17) == 0) {
            s_table[i].last_seen = now;
            s_table[i].times_seen++;
            if (de.rssi > s_table[i].rssi_peak) s_table[i].rssi_peak = de.rssi;
            if (de.id[0] != '\0' && s_table[i].drone_id[0] == '\0')
                strncpy(s_table[i].drone_id, de.id, 20);
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

    strncpy(s_table[slot].mac,      de.mac, 17); s_table[slot].mac[17]      = '\0';
    strncpy(s_table[slot].drone_id, de.id,  20); s_table[slot].drone_id[20] = '\0';
    s_table[slot].ua_type    = de.ua_type;
    s_table[slot].first_seen = now;
    s_table[slot].last_seen  = now;
    s_table[slot].times_seen = 1;
    s_table[slot].rssi_peak  = de.rssi;
    save();
    Serial.printf("[skyspy_log] new drone %s  id=%s  total=%d\n", de.mac, de.id, s_count);
}

void skyspy_log_tick() {
    if (s_dirty && (millis() - s_last_save >= SAVE_INTERVAL_MS)) {
        save();
    }
}
