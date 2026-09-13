#pragma once
#include <stdint.h>

using ChromeExitCb = void(*)();

// Call once after display::begin() and before any screen is loaded.
void ui_chrome_begin();

// Update the status bar with fresh battery data.
void ui_chrome_update_battery(uint8_t pct, bool charging);

// Update the storage indicator ("SD" or "LFS").
void ui_chrome_update_storage(bool has_sd);

// Show/hide the bottom exit button (hidden on the menu screen).
void ui_chrome_show_exit(bool show);

// Set the callback invoked when the exit button is tapped.
// Pass nullptr to disconnect.
void ui_chrome_set_exit_cb(ChromeExitCb cb);

