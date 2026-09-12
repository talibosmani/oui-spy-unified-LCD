// Fox Hunter — passive BLE RSSI tracker.
// Tracks all nearby BLE devices; UI lets user lock onto one and shows live
// signal strength on an arc gauge (great for the round 466×466 display).
#include "foxhunter.h"
#include "config.h"
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <string.h>
#include <stdlib.h>

#define FOX_MAX     40      // max simultaneous devices tracked
#define FOX_TTL_MS  8000   // drop device if not seen for 8 s

static FoxDevice s_table[FOX_MAX];
static int       s_count = 0;

static FoxDevice *find_or_alloc(const char *mac) {
    for (int i = 0; i < s_count; i++)
        if (strncmp(s_table[i].mac, mac, 17) == 0) return &s_table[i];
    if (s_count < FOX_MAX) return &s_table[s_count++];
    // Replace oldest (evict)
    int oldest = 0;
    for (int i = 1; i < s_count; i++)
        if (s_table[i].last_seen < s_table[oldest].last_seen) oldest = i;
    return &s_table[oldest];
}

class FoxCB : public NimBLEAdvertisedDeviceCallbacks {
public:
    void onResult(NimBLEAdvertisedDevice *adv) override {
        const std::string mac = adv->getAddress().toString();
        FoxDevice *d = find_or_alloc(mac.c_str());
        strncpy(d->mac, mac.c_str(), sizeof(d->mac) - 1);
        d->mac[sizeof(d->mac)-1] = '\0';
        if (adv->haveName()) {
            std::string n = adv->getName();
            strncpy(d->name, n.c_str(), sizeof(d->name) - 1);
            d->name[sizeof(d->name)-1] = '\0';
        }
        d->rssi      = adv->getRSSI();
        d->rssi_peak = (d->last_seen == 0) ? d->rssi : (d->rssi > d->rssi_peak ? d->rssi : d->rssi_peak);
        d->last_seen = millis();
    }
};

static FoxCB *s_cb = nullptr;

void foxhunter_start() {
    memset(s_table, 0, sizeof(s_table));
    s_count = 0;
    NimBLEScan *scan = NimBLEDevice::getScan();
    s_cb = new FoxCB();
    scan->setAdvertisedDeviceCallbacks(s_cb, true);
    scan->setActiveScan(false);
    scan->setInterval(BLE_SCAN_INTERVAL);
    scan->setWindow(BLE_SCAN_WINDOW);
    scan->start(0, nullptr, false);
    Serial.println("[foxhunter] passive scan started");
}

void foxhunter_stop() {
    NimBLEDevice::getScan()->stop();
    if (s_cb) { delete s_cb; s_cb = nullptr; }
    s_count = 0;
}

static int cmp_rssi(const void *a, const void *b) {
    return ((const FoxDevice *)b)->rssi - ((const FoxDevice *)a)->rssi;
}

int foxhunter_get_devices(FoxDevice *out, int max) {
    // Copy active (non-stale) devices into a temporary buffer, then sort
    FoxDevice tmp[FOX_MAX];
    int n = 0;
    uint32_t now = millis();
    for (int i = 0; i < s_count && n < FOX_MAX; i++) {
        if (s_table[i].last_seen == 0) continue;
        if ((now - s_table[i].last_seen) > FOX_TTL_MS) continue;
        tmp[n++] = s_table[i];
    }
    qsort(tmp, n, sizeof(FoxDevice), cmp_rssi);
    int ret = n < max ? n : max;
    memcpy(out, tmp, ret * sizeof(FoxDevice));
    return ret;
}

int8_t foxhunter_rssi(const char *mac) {
    uint32_t now = millis();
    for (int i = 0; i < s_count; i++) {
        if (strncmp(s_table[i].mac, mac, 17) == 0) {
            if ((now - s_table[i].last_seen) > FOX_TTL_MS) return -128;
            return s_table[i].rssi;
        }
    }
    return -128;
}
