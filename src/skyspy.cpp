// Sky Spy — Drone Remote ID (OpenDroneID / ASTM F3411) BLE scanner.
// Listens for BLE advertisements with service UUID 0xFFFA (ASTM International).
// Parses Basic ID message (type 0) to extract drone ID and UA category.
#include "skyspy.h"
#include <NimBLEDevice.h>
#include <NimBLEScan.h>
#include <Arduino.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#define ODID_SVC_UUID 0xFFFA

static QueueHandle_t s_queue = nullptr;
static NimBLEScan   *s_scan  = nullptr;

class ODIDCallback : public NimBLEAdvertisedDeviceCallbacks {
    void onResult(NimBLEAdvertisedDevice *dev) override {
        if (!dev->haveServiceData()) return;
        // Find service data entry with UUID 0xFFFA
        NimBLEUUID target((uint16_t)ODID_SVC_UUID);
        std::string svcData;
        uint8_t cnt = dev->getServiceDataCount();
        bool found = false;
        for (uint8_t i = 0; i < cnt; i++) {
            if (dev->getServiceDataUUID(i) == target) {
                svcData = dev->getServiceData(i);
                found = true;
                break;
            }
        }
        if (!found || svcData.size() < 3) return;

        const uint8_t *d = (const uint8_t *)svcData.data();
        // Byte 0 upper nibble = message type; 0 = Basic ID
        if (((d[0] >> 4) & 0x0F) != 0) return;

        DroneEntry e{};
        strncpy(e.mac, dev->getAddress().toString().c_str(), 17);
        e.mac[17] = '\0';
        e.ua_type = d[1] & 0x0F;
        // Bytes 2-21: ID string (null-padded, 20 chars max)
        for (int i = 0; i < 20 && (2 + i) < (int)svcData.size(); i++) {
            char c = (char)d[2 + i];
            e.id[i] = (c >= 0x20 && c < 0x7F) ? c : '\0';
            if (!c) break;
        }
        e.id[20]   = '\0';
        e.rssi     = dev->getRSSI();
        e.last_seen = millis();
        if (s_queue) xQueueSend(s_queue, &e, 0);
    }
};

static ODIDCallback s_cb;

void skyspy_start() {
    if (!s_queue)
        s_queue = xQueueCreate(32, sizeof(DroneEntry));

    s_scan = NimBLEDevice::getScan();
    s_scan->setAdvertisedDeviceCallbacks(&s_cb, /*duplicates=*/true);
    s_scan->setActiveScan(false);
    s_scan->setInterval(80);
    s_scan->setWindow(60);
    s_scan->start(0, nullptr, false); // continuous, keep callbacks from prior start
}

void skyspy_stop() {
    if (s_scan) { NimBLEDevice::getScan()->stop(); s_scan = nullptr; }
    if (s_queue) { vQueueDelete(s_queue); s_queue = nullptr; }
}

bool skyspy_poll(DroneEntry *out) {
    if (!s_queue) return false;
    return xQueueReceive(s_queue, out, 0) == pdTRUE;
}
