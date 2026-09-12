// Passive 802.11 promiscuous scanner for the Flock-You mode.
// Captures probe requests (device → network intent) and beacon frames (APs).
// Thread model: WiFi RX callback (WiFi driver task) → FreeRTOS queue → main loop.
#include "wifi_scanner.h"
#include "config.h"
#include "debug.h"
#include "audio_alert.h"
#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <cstring>
#include <cstdio>

#define MGMT_PROBE_REQ  0x04
#define MGMT_PROBE_RESP 0x05
#define MGMT_BEACON     0x08
#define SCAN_QUEUE_DEPTH 128   // larger buffer for debug-mode burst

static QueueHandle_t s_queue   = nullptr;
static uint8_t       s_channel = 1;
static uint32_t      s_last_hop = 0;
static int           s_ch_idx   = 0;

// Debounce table — plain C array, safe in IRAM_ATTR callback context.
#define SEEN_MAX 128
struct SeenEntry { uint8_t mac[6]; uint32_t ts; };
static SeenEntry s_seen_table[SEEN_MAX];

// Returns true and updates timestamp if the device should be reported (not debounced).
// Returns false if within the debounce window. Safe to call from IRAM_ATTR.
static bool IRAM_ATTR seen_check_update(const uint8_t *mac6, uint32_t now, uint32_t debounce_ms) {
    int free_slot  = -1;
    int oldest_idx = 0;
    for (int i = 0; i < SEEN_MAX; i++) {
        if (s_seen_table[i].ts == 0) {
            if (free_slot < 0) free_slot = i;
            continue;
        }
        if (memcmp(s_seen_table[i].mac, mac6, 6) == 0) {
            if ((now - s_seen_table[i].ts) < debounce_ms) return false;
            s_seen_table[i].ts = now;
            return true;
        }
        if (s_seen_table[i].ts < s_seen_table[oldest_idx].ts) oldest_idx = i;
    }
    int slot = (free_slot >= 0) ? free_slot : oldest_idx;
    memcpy(s_seen_table[slot].mac, mac6, 6);
    s_seen_table[slot].ts = now;
    return true;
}

// IRAM-safe MAC formatter — no string literals, no library calls.
static void IRAM_ATTR fmt_mac(const uint8_t *m, char *out) {
    for (int i = 0; i < 6; i++) {
        uint8_t hi = m[i] >> 4, lo = m[i] & 0xF;
        out[i*3]   = hi < 10 ? (char)('0'+hi) : (char)('A'+hi-10);
        out[i*3+1] = lo < 10 ? (char)('0'+lo) : (char)('A'+lo-10);
        out[i*3+2] = (i < 5) ? ':' : '\0';
    }
    out[17] = '\0';
}

static const uint8_t CH_LIST[] = {1, 6, 11, 2, 7, 12, 3, 8, 13, 4, 9, 5, 10};
static const int     CH_COUNT  = (int)sizeof(CH_LIST);

// Surveillance camera WiFi vendor OUI table.
// type_badge replaces "AP"/"PROBE" in the badge for matched rows.
struct WifiVendorSig {
    char    type_badge[8];
    uint32_t color;
    uint8_t  ouis[8][3];
    uint8_t  oui_count;
    bool     flock_ssid_fallback;  // also match if SSID contains "flock"
};
static const WifiVendorSig WIFI_VENDORS[] = {
    { "FLOCK", 0xffd700,
      {{0x3C,0x22,0xFB},{0x00,0x0A,0x35},{0xF8,0xB7,0x68},
       {0x24,0x76,0x25},{0xA8,0x10,0xD7},{0xB0,0xA7,0xB9}}, 6, true },
    { "RING",  0xff6622,
      {{0xFC,0xAA,0x81},{0x78,0x7B,0x8A},{0xB0,0xC5,0xCA},{0x34,0x27,0x92}}, 4, false },
    { "HIKV",  0xffcc44,
      {{0xBC,0x0F,0xF3},{0xC0,0x56,0xE3},{0x44,0x19,0xB6},
       {0xE8,0xB4,0xC8},{0x08,0xA1,0x89},{0x48,0xEA,0x63}}, 6, false },
    { "DAHUA", 0xff8800,
      {{0x90,0xD3,0x82},{0xE0,0x50,0x8B},{0x14,0x40,0xE8},{0x14,0x60,0xCB}}, 4, false },
    { "AXIS",  0x4499ff,
      {{0x00,0x40,0x8C},{0xAC,0xCC,0x8E},{0xB8,0xA4,0x4F}}, 3, false },
};
static constexpr int WIFI_VENDOR_COUNT = (int)(sizeof(WIFI_VENDORS)/sizeof(WIFI_VENDORS[0]));

// --- SSID tagged-param parser -------------------------------------------------

static int parse_ssid(const uint8_t *body, int body_len, char *out, int max_out) {
    for (int i = 0; i + 2 <= body_len; ) {
        uint8_t tag = body[i];
        uint8_t len = body[i + 1];
        if (i + 2 + len > body_len) break;
        if (tag == 0) {
            if (len == 0) { out[0] = '\0'; return 0; }
            int n = (len < max_out - 1) ? len : max_out - 1;
            memcpy(out, &body[i + 2], n);
            out[n] = '\0';
            return n;
        }
        i += 2 + len;
    }
    out[0] = '\0';
    return -1;
}

// --- Promiscuous callback (WiFi task, ISR-like) --------------------------------

static void IRAM_ATTR wifi_sniffer_cb(void *buf, wifi_promiscuous_pkt_type_t type) {
    if (!s_queue) return;
    if (type != WIFI_PKT_MGMT) return;

    const wifi_promiscuous_pkt_t *ppkt = (const wifi_promiscuous_pkt_t *)buf;
    const uint8_t *frame     = ppkt->payload;
    const int      frame_len = (int)ppkt->rx_ctrl.sig_len;

    if (frame_len < 24) return;

    uint16_t fc   = (uint16_t)frame[0] | ((uint16_t)frame[1] << 8);
    uint8_t  fsub = (fc >> 4) & 0x0F;
    if (fsub != MGMT_PROBE_REQ && fsub != MGMT_BEACON && fsub != MGMT_PROBE_RESP) return;

    // Source MAC at bytes 10-15 (addr2)
    const uint8_t *src = &frame[10];
    const uint32_t now = millis();
    const uint32_t debounce_ms = g_debug ? 5000u
                                         : (fsub == MGMT_BEACON ? 30000u : 15000u);

    if (!seen_check_update(src, now, debounce_ms)) return;

    FlockDetection d{};
    fmt_mac(src, d.mac);
    d.rssi    = ppkt->rx_ctrl.rssi;
    d.channel = ppkt->rx_ctrl.channel;

    if (fsub == MGMT_PROBE_REQ) {
        memcpy(d.type, "PROBE", 6);
        d.badge_color = 0x4499ff;
        char ssid[33]{};
        if (parse_ssid(&frame[24], frame_len - 24, ssid, sizeof(ssid)) > 0)
            memcpy(d.ssid, ssid, sizeof(d.ssid));
        else
            memcpy(d.ssid, "(wildcard)", 11);
    } else {
        memcpy(d.type, "AP", 3);
        d.badge_color = 0x44dd88;
        if (frame_len >= 36) {
            char ssid[33]{};
            int slen = parse_ssid(&frame[36], frame_len - 36, ssid, sizeof(ssid));
            if (slen > 0)       memcpy(d.ssid, ssid, sizeof(d.ssid));
            else if (slen == 0) memcpy(d.ssid, "(hidden)", 9);
        }
    }

    // OUI vendor match
    const WifiVendorSig *vendor = nullptr;
    for (int vi = 0; vi < WIFI_VENDOR_COUNT && !vendor; ++vi) {
        for (int oi = 0; oi < WIFI_VENDORS[vi].oui_count; ++oi) {
            if (src[0]==WIFI_VENDORS[vi].ouis[oi][0] &&
                src[1]==WIFI_VENDORS[vi].ouis[oi][1] &&
                src[2]==WIFI_VENDORS[vi].ouis[oi][2]) {
                vendor = &WIFI_VENDORS[vi]; break;
            }
        }
    }
    // SSID "flock" keyword fallback (catches Flock cameras regardless of OUI)
    if (!vendor && d.ssid[0]) {
        for (int i = 0; d.ssid[i]; i++) {
            if ((d.ssid[i]  |0x20)=='f' && (d.ssid[i+1]|0x20)=='l' &&
                (d.ssid[i+2]|0x20)=='o' && (d.ssid[i+3]|0x20)=='c' &&
                (d.ssid[i+4]|0x20)=='k') {
                vendor = &WIFI_VENDORS[0]; break;   // Flock is index 0
            }
        }
    }

    // Filter: drop non-matching frames in normal mode.
    // Debug mode mirrors BLE detector: shows everything, grey badge for unmatched.
    if (!vendor) {
        if (!g_debug) return;
        d.badge_color = 0x333333;   // grey — unmatched in debug mode
        xQueueSend(s_queue, &d, 0);
        return;
    }

    // Vendor matched — set badge label, color, and audio alert
    memcpy(d.type, vendor->type_badge, sizeof(d.type));
    d.badge_color = vendor->color;
    if (vendor == &WIFI_VENDORS[0])
        audio_alert_request(AlertType::FlockCamera);
    else
        audio_alert_request(AlertType::BLE_Surveillance);

    xQueueSend(s_queue, &d, 0);
}

// --- Public API ---------------------------------------------------------------

void flockscanner_init() {
    // Pre-init WiFi stack alongside BLE so mode switches are instant
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true);
    Serial.println("[wifi] stack ready");
}

void flockscanner_start() {
    if (!s_queue)
        s_queue = xQueueCreate(SCAN_QUEUE_DEPTH, sizeof(FlockDetection));

    memset(s_seen_table, 0, sizeof(s_seen_table));
    s_ch_idx  = 0;
    s_channel = CH_LIST[0];
    s_last_hop = 0;

    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(wifi_sniffer_cb);
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(s_channel, WIFI_SECOND_CHAN_NONE);

    Serial.printf("[wifi] promiscuous on ch%d\n", s_channel);
}

void flockscanner_stop() {
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(nullptr);
    memset(s_seen_table, 0, sizeof(s_seen_table));
    Serial.println("[wifi] promiscuous off");
}

bool flockscanner_poll(FlockDetection *out) {
    if (!s_queue) return false;
    return xQueueReceive(s_queue, out, 0) == pdTRUE;
}

uint8_t flockscanner_tick() {
    uint32_t now = millis();
    uint32_t hop_ms = g_debug ? 150u : 300u;  // faster channel hop in debug mode
    if (now - s_last_hop >= hop_ms) {
        s_ch_idx = (s_ch_idx + 1) % CH_COUNT;
        s_channel = CH_LIST[s_ch_idx];
        esp_wifi_set_channel(s_channel, WIFI_SECOND_CHAN_NONE);
        s_last_hop = now;
    }
    return s_channel;
}
