#pragma once
#include <stdint.h>

struct PcapStats {
    uint32_t total;
    uint32_t mgmt;
    uint32_t ctrl;
    uint32_t data;
    uint8_t  channel;
    uint32_t file_bytes;   // bytes written to current capture file
    char     filename[24]; // current file name, e.g. "/pcap_001.pcap"
};

// Mounts LittleFS (formats if needed), creates a new capture file, starts WiFi sniffer.
void pcap_start();

// Stops the sniffer and closes the capture file.
void pcap_stop();

// Drain the capture queue and hop channels. Call every loop tick.
void pcap_tick();

// Get current stats snapshot.
void pcap_get_stats(PcapStats *out);

// Returns total bytes used across all /pcap_*.pcap files.
uint64_t pcap_fs_used();

// Returns total LittleFS capacity in bytes.
uint64_t pcap_fs_total();

// Delete all pcap files. Returns number deleted.
int pcap_delete_all();

// True between pcap_start() and pcap_stop().
bool pcap_is_running();
