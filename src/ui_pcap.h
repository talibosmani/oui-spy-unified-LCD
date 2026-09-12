#pragma once

using PcapBackCb = void(*)();

void ui_pcap_create(PcapBackCb on_back);
void ui_pcap_destroy();
// Refresh stats display + handle download button state. Call every ~500ms.
void ui_pcap_update();
// Show storage-full warning overlay (call when usage > 80%).
void ui_pcap_show_storage_warning(bool show);
