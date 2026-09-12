#include "flock_log.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <Arduino.h>
#include <string.h>

#define LOG_PATH    "/flock_log.json"
#define MAX_ENTRIES  100

struct FlockEntry {
    char     mac[18];
    uint32_t first_seen;   // seconds since boot (replaced by UTC epoch when GPS added)
    uint32_t last_seen;
    uint16_t times_seen;
    int8_t   rssi_peak;
};

static FlockEntry s_table[MAX_ENTRIES];
static int        s_count  = 0;
static bool       s_inited = false;

static void load() {
    if (!LittleFS.begin(false)) return;
    File f = LittleFS.open(LOG_PATH, "r");
    if (!f) return;
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) return;
    JsonArray arr = doc["cameras"].as<JsonArray>();
    for (JsonObject o : arr) {
        if (s_count >= MAX_ENTRIES) break;
        auto &e = s_table[s_count++];
        strncpy(e.mac, o["mac"] | "", 17);
        e.mac[17]    = '\0';
        e.first_seen = o["first_seen"] | 0u;
        e.last_seen  = o["last_seen"]  | 0u;
        e.times_seen = o["times_seen"] | 1u;
        e.rssi_peak  = (int8_t)(o["rssi_peak"] | -127);
    }
    Serial.printf("[flock_log] loaded %d entries from flash\n", s_count);
}

static void save() {
    File f = LittleFS.open(LOG_PATH, "w");
    if (!f) { Serial.println("[flock_log] open for write failed"); return; }
    JsonDocument doc;
    doc["note"] = "first_seen/last_seen = uptime seconds; will switch to UTC epoch when GPS module is added";
    JsonArray arr = doc["cameras"].to<JsonArray>();
    for (int i = 0; i < s_count; i++) {
        JsonObject o = arr.add<JsonObject>();
        o["mac"]        = s_table[i].mac;
        o["first_seen"] = s_table[i].first_seen;
        o["last_seen"]  = s_table[i].last_seen;
        o["times_seen"] = s_table[i].times_seen;
        o["rssi_peak"]  = s_table[i].rssi_peak;
    }
    serializeJsonPretty(doc, f);
    f.close();
}

void flock_log_update(const FlockDetection &d) {
    if (!s_inited) { load(); s_inited = true; }

    const uint32_t now = millis() / 1000;

    // Update existing entry for this MAC
    for (int i = 0; i < s_count; i++) {
        if (strncmp(s_table[i].mac, d.mac, 17) == 0) {
            s_table[i].last_seen = now;
            s_table[i].times_seen++;
            if (d.rssi > s_table[i].rssi_peak) s_table[i].rssi_peak = d.rssi;
            save();
            Serial.printf("[flock_log] updated %s  seen=%d  rssi_peak=%d\n",
                          d.mac, s_table[i].times_seen, s_table[i].rssi_peak);
            return;
        }
    }

    // New entry — evict oldest if table is full
    int slot;
    if (s_count < MAX_ENTRIES) {
        slot = s_count++;
    } else {
        slot = 0;
        for (int i = 1; i < MAX_ENTRIES; i++) {
            if (s_table[i].first_seen < s_table[slot].first_seen) slot = i;
        }
    }

    strncpy(s_table[slot].mac, d.mac, 17);
    s_table[slot].mac[17]    = '\0';
    s_table[slot].first_seen = now;
    s_table[slot].last_seen  = now;
    s_table[slot].times_seen = 1;
    s_table[slot].rssi_peak  = d.rssi;
    save();
    Serial.printf("[flock_log] new entry %s  rssi=%d  total=%d\n",
                  d.mac, d.rssi, s_count);
}
