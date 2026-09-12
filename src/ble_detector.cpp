// Passive BLE scanner for surveillance device detection.
// Matches OUI prefixes, manufacturer company IDs, service UUIDs, and device names
// against a table of known surveillance hardware vendors.
// Thread model: NimBLE callback runs on BLE stack task (core 0); detections are
// posted to a FreeRTOS queue consumed by the LVGL task (core 1) via ble_detector_poll().
#include "ble_detector.h"
#include "config.h"
#include "debug.h"
#include "audio_alert.h"
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <map>
#include <string>

// ---- Vendor signature table -------------------------------------------------

struct VendorSig {
    const char *name;
    uint32_t    color;
    uint8_t     ouis[40][3];
    uint8_t     oui_count;
    uint16_t    cids[2];    // manufacturer company IDs (little-endian in mfr data)
    uint8_t     cid_count;
    uint16_t    svcs[2];    // 16-bit service UUIDs
    uint8_t     svc_count;
    const char *names[3];   // device name substrings (case-insensitive)
    uint8_t     name_count;
};

static const VendorSig VENDORS[] = {
    {
        "AXON", 0xff4545,
        {{0x00,0x25,0xDF}}, 1,
        {0x034D}, 1,
        {0xFC81}, 1,
        {}, 0,
    },
    {
        "RING", 0xff6622,
        {{0xAC,0x9F,0xC3},{0x18,0x7F,0x88},{0x34,0x3E,0xA4},{0x54,0xE0,0x19},
         {0x5C,0x47,0x5E},{0x64,0x9A,0x63},{0x90,0x48,0x6C},{0x9C,0x76,0x13},
         {0xCC,0x3B,0xFB},{0xC4,0xDB,0xAD},{0x24,0x2B,0xD6},{0x00,0xB4,0x63},
         {0x50,0xE4,0x67}}, 13,
        {}, 0, {}, 0, {}, 0,
    },
    {
        "WYZE", 0x00ccaa,
        {{0x2C,0xAA,0x8E},{0x7C,0x78,0xB2},{0x80,0x48,0x2C},
         {0xA4,0xDA,0x22},{0xD0,0x3F,0x27},{0xF0,0xC8,0x8B}}, 6,
        {}, 0, {}, 0, {}, 0,
    },
    {
        "FLOCK", 0xffd700,
        {{0x3C,0x22,0xFB},{0x00,0x0A,0x35},{0xF8,0xB7,0x68},
         {0x24,0x76,0x25},{0xA8,0x10,0xD7},{0xB0,0xA7,0xB9},
         {0x70,0xC9,0x4E},{0x3C,0x91,0x80},{0xD8,0xF3,0xBC},
         {0x80,0x30,0x49},{0xB8,0x35,0x32},{0x14,0x5A,0xFC},
         {0x74,0x4C,0xA1},{0x08,0x3A,0x88},{0x9C,0x2F,0x9D},
         {0xC0,0x35,0x32},{0x94,0x08,0x53},{0xE4,0xAA,0xEA},
         {0xF4,0x6A,0xDD},{0xE0,0x0A,0xF6},{0x24,0xB2,0xB9},
         {0x00,0xF4,0x8D},{0xD0,0x39,0x57},{0xE8,0xD0,0xFC},
         {0xE0,0x4F,0x43},{0xB8,0x1E,0xA4},{0x70,0x08,0x94},
         {0x58,0x8E,0x81},{0xEC,0x1B,0xBD},{0x3C,0x71,0xBF},
         {0x58,0x00,0xE3},{0x90,0x35,0xEA},{0x5C,0x93,0xA2},
         {0x64,0x6E,0x69},{0x48,0x27,0xEA},{0xA4,0xCF,0x12},
         {0x14,0xB5,0xCD},{0x82,0x6B,0xF2}}, 38,
        {}, 0, {}, 0, {}, 0,
    },
    {
        "DJI", 0x4499ff,
        {{0x60,0x60,0x1F},{0x48,0x1C,0xB9},{0xAC,0x30,0x2C},{0x34,0xD2,0x62}}, 4,
        {}, 0, {}, 0, {}, 0,
    },
    {
        "SKYDIO", 0x44ddaa,
        {{0xD0,0xCF,0x5E}}, 1,
        {}, 0, {}, 0, {}, 0,
    },
    {
        "PARROT", 0xff44aa,
        {{0x90,0x03,0xB7},{0xA0,0x14,0x3D},{0x00,0x12,0x1C}}, 3,
        {}, 0, {}, 0, {}, 0,
    },
    // META / Ray-Ban: rotating MAC so OUI is useless; composite CID+SVC match only.
    {
        "META", 0xff69b4,
        {}, 0,
        {0x0D53}, 1,
        {0xFD5F}, 1,
        {"Ray-Ban","Wayfarer","Oakley Meta"}, 3,
    },
};
static constexpr int VENDOR_COUNT = sizeof(VENDORS) / sizeof(VENDORS[0]);

// ---- Queue & debounce -------------------------------------------------------

static QueueHandle_t s_queue = nullptr;

// MAC string -> last detection millis (debounce)
static std::map<std::string, uint32_t> s_seen;

// ---- NimBLE callback --------------------------------------------------------

class DetectorCB : public NimBLEAdvertisedDeviceCallbacks {
public:
    void onResult(NimBLEAdvertisedDevice *adv) override {
        if (!s_queue) return;

        const NimBLEAddress addr = adv->getAddress();
        const uint8_t *raw = addr.getNative(); // 6 bytes, reversed (LSB first)
        // OUI = raw[5], raw[4], raw[3] (the first 3 bytes of the MAC string)
        const uint8_t oui[3] = { raw[5], raw[4], raw[3] };

        const uint16_t mfr_cid = adv->haveManufacturerData()
            ? (uint8_t)adv->getManufacturerData()[0] |
              ((uint8_t)adv->getManufacturerData()[1] << 8)
            : 0xFFFF;

        const std::string name = adv->haveName() ? adv->getName() : "";

        for (int v = 0; v < VENDOR_COUNT; ++v) {
            const VendorSig &sig = VENDORS[v];
            const char *method = nullptr;

            // 1. OUI prefix match
            for (int o = 0; o < sig.oui_count && !method; ++o)
                if (oui[0]==sig.ouis[o][0] && oui[1]==sig.ouis[o][1] && oui[2]==sig.ouis[o][2])
                    method = "oui";

            // 2. Company ID match
            for (int c = 0; c < sig.cid_count && !method; ++c)
                if (mfr_cid == sig.cids[c])
                    method = "cid";

            // 3. Service UUID match (16-bit UUIDs only for now)
            if (!method && sig.svc_count > 0 && adv->haveServiceUUID()) {
                for (int u = 0; u < (int)adv->getServiceUUIDCount() && !method; ++u) {
                    const NimBLEUUID svc = adv->getServiceUUID(u);
                    if (svc.bitSize() == 16) {
                        const uint16_t svc16 = (uint16_t)svc.getNative()->u16.value;
                        for (int s = 0; s < sig.svc_count && !method; ++s)
                            if (svc16 == sig.svcs[s])
                                method = "svc_uuid";
                    }
                }
            }

            // 4. Device name substring match (case-insensitive)
            if (!method && !name.empty()) {
                std::string name_lc = name;
                for (auto &ch : name_lc) ch = tolower(ch);
                for (int n = 0; n < sig.name_count && !method; ++n) {
                    std::string sub = sig.names[n];
                    for (auto &ch : sub) ch = tolower(ch);
                    if (name_lc.find(sub) != std::string::npos)
                        method = "name";
                }
            }

            if (!method) continue;

            // Debounce: skip if this MAC was seen recently
            const std::string mac_str = addr.toString();
            const uint32_t now = millis();
            auto it = s_seen.find(mac_str);
            if (it != s_seen.end() && (now - it->second) < DET_DEBOUNCE_MS)
                return;
            // Age-based eviction — don't clear entirely (causes burst re-flood)
            if (s_seen.size() > 300) {
                for (auto ei = s_seen.begin(); ei != s_seen.end(); ) {
                    ei = (now - ei->second > (uint32_t)DET_DEBOUNCE_MS)
                         ? s_seen.erase(ei) : std::next(ei);
                }
            }
            s_seen[mac_str] = now;

            Detection d{};
            strncpy(d.mac,    mac_str.c_str(),  sizeof(d.mac) - 1);
            strncpy(d.vendor, sig.name,          sizeof(d.vendor) - 1);
            strncpy(d.method, method,            sizeof(d.method) - 1);
            d.rssi        = adv->getRSSI();
            d.badge_color = sig.color;

            xQueueSend(s_queue, &d, 0);

            // Audio alert — Flock cameras get a distinct triple-beep
            if (strcmp(sig.name, "FLOCK") == 0)
                audio_alert_request(AlertType::FlockCamera);
            else
                audio_alert_request(AlertType::BLE_Surveillance);

            return; // first vendor match wins
        }

        // Debug mode: show every unique BLE device that wasn't matched.
        // Rate-limited to 4 new entries/second to avoid flooding the LVGL list.
        if (g_debug) {
            const std::string mac_str = addr.toString();
            const uint32_t now = millis();
            auto it = s_seen.find(mac_str);
            if (it != s_seen.end() && (now - it->second) < 3000) return;

            // Per-second rate cap — prevents heap spike when entering a dense BLE area
            static uint32_t s_dbg_window = 0;
            static uint8_t  s_dbg_count  = 0;
            if (now - s_dbg_window >= 1000) { s_dbg_window = now; s_dbg_count = 0; }
            if (s_dbg_count >= 4) return;
            ++s_dbg_count;

            // Evict stale entries instead of clearing (clear causes a re-flood burst)
            if (s_seen.size() > 300) {
                for (auto ei = s_seen.begin(); ei != s_seen.end(); ) {
                    ei = (now - ei->second > 4000) ? s_seen.erase(ei) : std::next(ei);
                }
            }
            s_seen[mac_str] = now;

            Detection d{};
            strncpy(d.mac, mac_str.c_str(), sizeof(d.mac) - 1);
            strncpy(d.method, "SCAN", sizeof(d.method) - 1);
            // vendor: device name if available, otherwise MAC address
            if (!name.empty()) {
                strncpy(d.vendor, name.c_str(), sizeof(d.vendor) - 1);
            } else {
                strncpy(d.vendor, mac_str.c_str(), sizeof(d.vendor) - 1);
            }
            d.rssi        = adv->getRSSI();
            d.badge_color = 0x444444;   // grey badge for unmatched devices
            xQueueSend(s_queue, &d, 0);
        }
    }
};

static DetectorCB *s_cb = nullptr;

void ble_detector_start() {
    if (!s_queue)
        s_queue = xQueueCreate(DET_QUEUE_DEPTH, sizeof(Detection));

    // NimBLEDevice::init() is called once in setup() — just grab the scan handle
    NimBLEScan *scan = NimBLEDevice::getScan();
    s_cb = new DetectorCB();
    scan->setAdvertisedDeviceCallbacks(s_cb, true);  // true = want duplicates
    scan->setActiveScan(false);                       // passive — don't reveal ourselves
    scan->setInterval(BLE_SCAN_INTERVAL);
    scan->setWindow(BLE_SCAN_WINDOW);
    scan->start(0, nullptr, false);                   // 0 = scan forever
    Serial.println("[ble] passive scan started");
}

void ble_detector_stop() {
    NimBLEDevice::getScan()->stop();
    if (s_cb) { delete s_cb; s_cb = nullptr; }
    s_seen.clear();
}

bool ble_detector_poll(Detection *out) {
    if (!s_queue) return false;
    return xQueueReceive(s_queue, out, 0) == pdTRUE;
}
