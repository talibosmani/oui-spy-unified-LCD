#pragma once
#include <stdint.h>

using ChromeExitCb = void(*)();

// Call once after display::begin() and before any screen is loaded.
void ui_chrome_begin();

// Update the status bar with fresh battery data.
void ui_chrome_update_battery(uint8_t pct, bool charging);

// Update the storage indicator ("SD" or "LFS").
void ui_chrome_update_storage(bool has_sd);

// Boot-time storage chooser (full screen, lives on lv_layer_top()).
//   status:    line under the title, e.g. "FAT32 card ready - 29.7 GB"
//   hint:      smaller grey explanation, may be nullptr
//   primary:   label for the green button; nullptr hides it
//   on_primary: runs on green tap; return nullptr to close, or an error string
//               which is shown in red and keeps the dialog open
//   secondary: label for the grey button
//   on_secondary: runs on grey tap, dialog then closes
using SdPrimaryCb   = const char* (*)();
using SdSecondaryCb = void (*)();
void ui_chrome_show_sd_dialog(const char *status, const char *hint,
                              const char *primary,   SdPrimaryCb   on_primary,
                              const char *secondary, SdSecondaryCb on_secondary);

// Show/hide the bottom exit button (hidden on the menu screen).
void ui_chrome_show_exit(bool show);

// Set the callback invoked when the exit button is tapped.
// Pass nullptr to disconnect.
void ui_chrome_set_exit_cb(ChromeExitCb cb);

