// PCAP Capture — 802.11 promiscuous sniffer → LittleFS file storage.
// Packet data captured in IRAM callback → FreeRTOS queue → drained in pcap_tick().
// Channel hops 1→6→11→2→7→12→3→8→13→4→9→5→10 every 400ms.
#include "pcap_capture.h"
#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <LittleFS.h>
#include <string.h>
#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

// Per-packet queue entry: fixed buffer to keep IRAM callback allocation-free.
#define PKT_BUF 256   // 256 B covers all mgmt frames; data frames truncated (orig_len preserved)
struct PcapQEntry {
    uint32_t ts_ms;
    uint16_t len;
    uint8_t  data[PKT_BUF];
};

static QueueHandle_t s_queue    = nullptr;
static File          s_file;
static PcapStats     s_stats;
static bool          s_running  = false;
static bool          s_fs_ok    = false;

// Channel-hop table (same as flockscanner for pattern consistency)
static const uint8_t CH_LIST[] = {1,6,11,2,7,12,3,8,13,4,9,5,10};
static int           s_ch_idx  = 0;
static uint32_t      s_last_hop = 0;

// ── PCAP binary format ────────────────────────────────────────────────────────
struct __attribute__((packed)) PcapGlobalHdr {
    uint32_t magic;          // 0xa1b2c3d4
    uint16_t ver_major;      // 2
    uint16_t ver_minor;      // 4
    int32_t  thiszone;       // 0
    uint32_t sigfigs;        // 0
    uint32_t snaplen;        // 65535
    uint32_t network;        // 105 = LINKTYPE_IEEE802_11
};

struct __attribute__((packed)) PcapPktHdr {
    uint32_t ts_sec;
    uint32_t ts_usec;
    uint32_t incl_len;
    uint32_t orig_len;
};

// ── Promiscuous callback (WiFi task, ISR-like) ────────────────────────────────
static void IRAM_ATTR promisc_cb(void *buf, wifi_promiscuous_pkt_type_t type) {
    if (!s_running || !s_queue) return;
    const wifi_promiscuous_pkt_t *pkt = (const wifi_promiscuous_pkt_t *)buf;
    uint32_t plen = pkt->rx_ctrl.sig_len;
    if (plen == 0) return;

    PcapQEntry e;
    e.ts_ms = millis();
    e.len   = (plen <= PKT_BUF) ? (uint16_t)plen : PKT_BUF;
    memcpy(e.data, pkt->payload, e.len);

    // Update counters from ISR (atomic enough for uint32 on Xtensa)
    s_stats.total++;
    if (type == WIFI_PKT_MGMT) s_stats.mgmt++;
    else if (type == WIFI_PKT_CTRL) s_stats.ctrl++;
    else if (type == WIFI_PKT_DATA) s_stats.data++;

    xQueueSendFromISR(s_queue, &e, nullptr);
}

// ── Helpers ───────────────────────────────────────────────────────────────────
static int next_file_index() {
    int idx = 1;
    char path[24];
    while (idx < 1000) {
        snprintf(path, sizeof(path), "/pcap_%03d.pcap", idx);
        if (!LittleFS.exists(path)) break;
        idx++;
    }
    return idx;
}

static void write_global_hdr() {
    PcapGlobalHdr gh;
    gh.magic     = 0xa1b2c3d4;
    gh.ver_major = 2;
    gh.ver_minor = 4;
    gh.thiszone  = 0;
    gh.sigfigs   = 0;
    gh.snaplen   = 65535;
    gh.network   = 105; // LINKTYPE_IEEE802_11
    s_file.write((const uint8_t *)&gh, sizeof(gh));
}

// ── Public API ────────────────────────────────────────────────────────────────
void pcap_start() {
    memset(&s_stats, 0, sizeof(s_stats));
    s_running  = false;
    s_ch_idx   = 0;
    s_last_hop = 0;

    // Mount LittleFS
    if (!LittleFS.begin(true)) {
        Serial.println("[pcap] LittleFS mount failed");
        s_fs_ok = false;
    } else {
        s_fs_ok = true;
        // Open new capture file
        int idx = next_file_index();
        snprintf(s_stats.filename, sizeof(s_stats.filename), "/pcap_%03d.pcap", idx);
        s_file = LittleFS.open(s_stats.filename, "w");
        if (s_file) {
            write_global_hdr();
            s_stats.file_bytes = sizeof(PcapGlobalHdr);
            Serial.printf("[pcap] capturing to %s\n", s_stats.filename);
        } else {
            Serial.println("[pcap] failed to open capture file");
        }
    }

    if (!s_queue)
        s_queue = xQueueCreate(32, sizeof(PcapQEntry));
    Serial.printf("[pcap] queue=%p heap_free=%u sizeof_entry=%u\n",
                  s_queue, esp_get_free_heap_size(), (unsigned)sizeof(PcapQEntry));

    // Start WiFi promiscuous
    esp_err_t e;
    WiFi.mode(WIFI_STA);
    e = esp_wifi_start();
    Serial.printf("[pcap] wifi_start=%d\n", (int)e);
    e = esp_wifi_set_promiscuous(true);
    Serial.printf("[pcap] promisc_on=%d\n", (int)e);
    e = esp_wifi_set_promiscuous_rx_cb(promisc_cb);
    Serial.printf("[pcap] promisc_cb=%d\n", (int)e);
    s_stats.channel = CH_LIST[0];
    e = esp_wifi_set_channel(CH_LIST[0], WIFI_SECOND_CHAN_NONE);
    Serial.printf("[pcap] set_ch=%d  running=true\n", (int)e);

    s_running = true;
}

void pcap_stop() {
    s_running = false;
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(nullptr);
    if (s_file) s_file.close();
    if (s_queue) { vQueueDelete(s_queue); s_queue = nullptr; }
}

void pcap_tick() {
    if (!s_running || !s_queue) return;

    // Channel hop every 400ms
    uint32_t now = millis();
    if (now - s_last_hop >= 400) {
        s_last_hop = now;
        s_ch_idx   = (s_ch_idx + 1) % (int)(sizeof(CH_LIST));
        s_stats.channel = CH_LIST[s_ch_idx];
        esp_wifi_set_channel(CH_LIST[s_ch_idx], WIFI_SECOND_CHAN_NONE);
    }

    // Drain queue → write to file (max 8 packets per tick to avoid blocking LVGL)
    PcapQEntry e;
    int drained = 0;
    while (drained < 8 && xQueueReceive(s_queue, &e, 0) == pdTRUE) {
        drained++;
        if (!s_fs_ok || !s_file) continue;
        PcapPktHdr ph;
        ph.ts_sec  = e.ts_ms / 1000;
        ph.ts_usec = (e.ts_ms % 1000) * 1000;
        ph.incl_len = e.len;
        ph.orig_len = e.len;
        s_file.write((const uint8_t *)&ph, sizeof(ph));
        s_file.write(e.data, e.len);
        s_stats.file_bytes += sizeof(ph) + e.len;
    }
}

void pcap_get_stats(PcapStats *out) {
    if (out) *out = s_stats;
}

uint32_t pcap_fs_used() {
    if (!s_fs_ok) return 0;
    return (uint32_t)LittleFS.usedBytes();
}

uint32_t pcap_fs_total() {
    if (!s_fs_ok) return 1;
    return (uint32_t)LittleFS.totalBytes();
}

bool pcap_is_running() { return s_running; }

int pcap_delete_all() {
    if (!s_fs_ok) return 0;
    int n = 0;
    char path[24];
    for (int i = 1; i < 1000; i++) {
        snprintf(path, sizeof(path), "/pcap_%03d.pcap", i);
        if (!LittleFS.exists(path)) break;
        LittleFS.remove(path);
        n++;
    }
    return n;
}
