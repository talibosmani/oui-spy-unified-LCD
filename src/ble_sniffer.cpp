// Passive BLE sniffer — shows ALL nearby BLE advertisements (no vendor filter).
// Thread model: NimBLE callback → FreeRTOS queue → main loop via blesniff_poll().
#include "ble_sniffer.h"
#include "config.h"
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <map>
#include <string>

#define SNIFF_QUEUE_DEPTH 64
#define SNIFF_DEBOUNCE_MS 30000   // suppress re-reporting the same MAC for 30s

static QueueHandle_t s_queue = nullptr;
static std::map<std::string, uint32_t> s_seen;

class SniffCB : public NimBLEAdvertisedDeviceCallbacks {
public:
    void onResult(NimBLEAdvertisedDevice *adv) override {
        if (!s_queue) return;
        const std::string mac = adv->getAddress().toString();
        const uint32_t now = millis();
        auto it = s_seen.find(mac);
        if (it != s_seen.end() && (now - it->second) < SNIFF_DEBOUNCE_MS) return;
        // Evict stale entries when table grows large
        if (s_seen.size() > 400) {
            for (auto ei = s_seen.begin(); ei != s_seen.end(); ) {
                ei = (now - ei->second > (uint32_t)SNIFF_DEBOUNCE_MS)
                     ? s_seen.erase(ei) : std::next(ei);
            }
        }
        s_seen[mac] = now;
        SniffEntry e{};
        strncpy(e.mac, mac.c_str(), sizeof(e.mac) - 1);
        if (adv->haveName()) {
            std::string n = adv->getName();
            strncpy(e.name, n.c_str(), sizeof(e.name) - 1);
        }
        e.rssi       = adv->getRSSI();
        e.first_seen = now;
        xQueueSend(s_queue, &e, 0);
    }
};

static SniffCB *s_cb = nullptr;

void blesniff_start() {
    if (!s_queue) s_queue = xQueueCreate(SNIFF_QUEUE_DEPTH, sizeof(SniffEntry));
    s_seen.clear();
    NimBLEScan *scan = NimBLEDevice::getScan();
    s_cb = new SniffCB();
    scan->setAdvertisedDeviceCallbacks(s_cb, true);
    scan->setActiveScan(false);
    scan->setInterval(BLE_SCAN_INTERVAL);
    scan->setWindow(BLE_SCAN_WINDOW);
    scan->start(0, nullptr, false);
    Serial.println("[blesniff] passive scan started");
}

void blesniff_stop() {
    NimBLEDevice::getScan()->stop();
    if (s_cb) { delete s_cb; s_cb = nullptr; }
    s_seen.clear();
}

bool blesniff_poll(SniffEntry *out) {
    if (!s_queue) return false;
    return xQueueReceive(s_queue, out, 0) == pdTRUE;
}
