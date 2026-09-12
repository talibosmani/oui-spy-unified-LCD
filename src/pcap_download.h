#pragma once
#include <stdint.h>

// Start AP "Download_WIFI" + HTTP file browser on 192.168.4.1:80.
// Call after pcap_stop() — cannot run alongside the sniffer.
void pcap_download_start();

// Handle incoming HTTP clients. Call every loop tick while in download mode.
void pcap_download_tick();

// Tear down the web server and AP.
void pcap_download_stop();

// True between start() and stop().
bool pcap_download_active();
