// OUI Spy — Waveshare ESP32-S3-Touch-AMOLED-1.75
// Boot: mode selector → tap a mode → full screen for that mode.
// Persistent chrome: battery status bar (top) + circle-X exit button (bottom).
#include <Arduino.h>
#include "config.h"
#include "display.h"
#include "ui_menu.h"
#include "ui_detector.h"
#include "ui_flockyou.h"
#include "ui_blesniff.h"
#include "ui_foxhunter.h"
#include "ui_chrome.h"
#include "ble_detector.h"
#include "ble_sniffer.h"
#include "foxhunter.h"
#include "wifi_scanner.h"
#include "axp2101.h"
#include "audio_alert.h"
#include "ui_quicksettings.h"
#include "debug.h"
#include "skyspy.h"
#include "ui_skyspy.h"
#include "pcap_capture.h"
#include "pcap_download.h"
#include "ui_pcap.h"
#include "ble_detect_log.h"
#include "ble_sniff_log.h"
#include "skyspy_log.h"
#include "flock_log.h"
#include "storage.h"
#include "ui_selftest.h"
#include <NimBLEDevice.h>

// ---- App state --------------------------------------------------------------

enum class State { Menu, Detector, FlockYou, BLESniff, Foxhunter, SkySpy, PCAP };
static State s_state = State::Menu;

static void on_mode_selected(AppMode mode);

// ---- Common exit path -------------------------------------------------------

static void return_to_menu() {
    switch (s_state) {
        case State::Detector:
            ble_detector_stop();
            ui_detector_destroy();
            break;
        case State::FlockYou:
            flockscanner_stop();
            ui_flockyou_destroy();
            break;
        case State::BLESniff:
            blesniff_stop();
            ui_blesniff_destroy();
            break;
        case State::Foxhunter:
            foxhunter_stop();
            ui_foxhunter_destroy();
            break;
        case State::SkySpy:
            skyspy_stop();
            ui_skyspy_destroy();
            break;
        case State::PCAP:
            if (pcap_download_active()) pcap_download_stop();
            if (pcap_is_running()) pcap_stop();
            ui_pcap_destroy();
            break;
        default:
            break;
    }
    s_state = State::Menu;
    ui_chrome_show_exit(false);
    ui_chrome_set_exit_cb(nullptr);
    ui_menu_create(on_mode_selected);
}

// ---- Boot-button long-press (>2s → menu) ------------------------------------

static uint32_t s_boot_held_ms = 0;

static void check_boot_button() {
    if (digitalRead(PIN_BOOT_BUTTON) == LOW) {
        if (s_boot_held_ms == 0) s_boot_held_ms = millis();
        if ((millis() - s_boot_held_ms) > 2000 && s_state != State::Menu)
            return_to_menu();
    } else {
        s_boot_held_ms = 0;
    }
}

// ---- Mode-selection callback ------------------------------------------------

static void on_mode_selected(AppMode mode) {
    ui_menu_destroy();

    ui_chrome_show_exit(true);
    ui_chrome_set_exit_cb(return_to_menu);

    switch (mode) {
        case AppMode::Detector:
            s_state = State::Detector;
            ui_detector_create(return_to_menu);
            ble_detector_start();
            break;

        case AppMode::FlockYou:
            s_state = State::FlockYou;
            ui_flockyou_create(return_to_menu);
            flockscanner_start();
            break;

        case AppMode::BLESniff:
            s_state = State::BLESniff;
            ui_blesniff_create(return_to_menu);
            blesniff_start();
            break;

        case AppMode::Foxhunter:
            s_state = State::Foxhunter;
            ui_foxhunter_create(return_to_menu);
            foxhunter_start();
            break;

        case AppMode::SkySpy:
            s_state = State::SkySpy;
            ui_skyspy_create(return_to_menu);
            skyspy_start();
            break;

        case AppMode::PCAP:
            s_state = State::PCAP;
            ui_pcap_create(return_to_menu);
            // Capture is NOT started automatically — user taps START CAPTURE
            break;

        default:
            ui_chrome_show_exit(false);
            ui_chrome_set_exit_cb(nullptr);
            s_state = State::Menu;
            ui_menu_create(on_mode_selected);
            break;
    }
}

// ---- Boot-time storage chooser ----------------------------------------------

static void use_internal() {
    storage_use_internal();
    ui_chrome_update_storage(false);
}

static void show_storage_chooser() {
    static char status[64];
    switch (storage_sd_state()) {
        case SdState::Ready: {
            double gb = storage_total_bytes() / (1024.0 * 1024.0 * 1024.0);
            snprintf(status, sizeof(status), "FAT32 card ready - %.1f GB", gb);
            ui_chrome_show_sd_dialog(
                status,
                "Detections and PCAP captures will be saved to the card.",
                "Continue with SD card", []() -> const char* { return nullptr; },
                "Use internal storage instead", use_internal);
            break;
        }
        case SdState::NotFat32:
            ui_chrome_show_sd_dialog(
                "Card detected but it is not FAT32",
                "Cards over 32 GB ship as exFAT, which this board cannot read.\n"
                "Formatting erases everything on the card.",
                "Format card to FAT32", []() -> const char* {
                    if (!storage_format_sd()) return storage_last_error();
                    ui_chrome_update_storage(true);
                    return nullptr;
                },
                "Use internal storage", nullptr);
            break;
        case SdState::None:
            ui_chrome_show_sd_dialog(
                "No SD card detected",
                "Insert a FAT32 microSD into the slot on the back and reboot to use it.",
                nullptr, nullptr,
                "Continue with internal storage", nullptr);
            break;
    }
}

// ---- Arduino entry points ---------------------------------------------------

void setup() {
    Serial.begin(115200);
    delay(400);
    Serial.println("[main] OUI Spy starting...");

    pinMode(PIN_BOOT_BUTTON, INPUT_PULLUP);

    storage_begin();

    if (!display::begin()) {
        Serial.println("[main] display init FAILED — halting");
        while (true) delay(1000);
    }

    // Init BLE stack once (scan started per-mode, not here)
    NimBLEDevice::init("oui-spy");
    Serial.println("[main] NimBLE ok");

    // Pre-init WiFi stack so Flock-You mode starts instantly
    flockscanner_init();

    // Persistent chrome (status bar + exit button on lv_layer_top)
    ui_chrome_begin();

    // Quick-settings panel (swipe-down from top to open)
    qs_begin();

    // Self-test overlay (swipe-left from menu to open)
    selftest_begin();

    axp_begin();
    auto bat = axp_read();
    ui_chrome_update_battery(bat.pct, bat.charging);

    ui_chrome_update_storage(storage_has_sd());

    // Audio init last — WiFi/BLE clocks must be stable before I2S starts
    audio_init();

    ui_menu_create(on_mode_selected);

    // Must come after ui_menu_create(): the dialog lives on lv_layer_top() and
    // is shown over the loaded menu screen.
    show_storage_chooser();
    Serial.println("[main] ready");
}

void loop() {
    display::loop();
    check_boot_button();

    // ---- Quick-settings panel gesture ----------------------------------------
    if (display::consume_swipe_from_top()) qs_open();
    if (display::consume_swipe_up()       ) qs_close();

    // ---- Self-test overlay gesture (menu only) --------------------------------
    if (display::consume_swipe_left() && s_state == State::Menu)
        selftest_open();

    // ---- Per-mode work -------------------------------------------------------
    // Drain at most 3 entries per loop tick — creating many LVGL rows at once
    // spikes the heap, especially in debug mode where every device is shown.
    if (s_state == State::Detector) {
        Detection det;
        int n = 0;
        while (n < 3 && ble_detector_poll(&det)) {
            ble_detect_log_update(det);
            ui_detector_add(det);
            ++n;
        }
        ble_detect_log_tick(); // flush dirty log at most once per minute
    }

    if (s_state == State::FlockYou) {
        uint8_t ch = flockscanner_tick();
        ui_flockyou_set_channel(ch);

        FlockDetection fd;
        int n = 0;
        while (n < 3 && flockscanner_poll(&fd)) {
            ui_flockyou_add(fd);
            ++n;
        }
        ui_flockyou_tick();
        flock_log_tick();
    }

    if (s_state == State::BLESniff) {
        SniffEntry se;
        int n = 0;
        while (n < 3 && blesniff_poll(&se)) {
            ble_sniff_log_update(se);
            ui_blesniff_add(se);
            ++n;
        }
        ble_sniff_log_tick();
    }

    if (s_state == State::Foxhunter) {
        // 300 ms in track mode (smooth gauge), 600 ms in scan mode (list refresh)
        static uint32_t s_fox_tick = 0;
        uint32_t interval = ui_foxhunter_is_tracking() ? 300u : 600u;
        if (millis() - s_fox_tick >= interval) {
            s_fox_tick = millis();
            FoxDevice devs[40];
            int cnt = foxhunter_get_devices(devs, 40);
            ui_foxhunter_update(devs, cnt);
        }
    }

    if (s_state == State::SkySpy) {
        DroneEntry de;
        int n = 0;
        while (n < 3 && skyspy_poll(&de)) {
            skyspy_log_update(de);
            ui_skyspy_add(de);
            ++n;
        }
        skyspy_log_tick();
    }

    if (s_state == State::PCAP) {
        pcap_tick(); // drain capture queue + channel hop

        // Refresh UI every 500ms
        static uint32_t s_pcap_ui_tick = 0;
        if (millis() - s_pcap_ui_tick >= 500) {
            s_pcap_ui_tick = millis();
            ui_pcap_update();

            // Storage warning when >80% full
            uint64_t total = pcap_fs_total();
            uint64_t used  = pcap_fs_used();
            if (total > 0 && used > total * 4 / 5)
                ui_pcap_show_storage_warning(true);
        }
    }

    // ---- Audio alert service (plays any pending alert, brief blocking) ------
    audio_alert_service();

    // ---- Debug: heap report every 10 s in debug mode ------------------------
    if (g_debug) {
        static uint32_t s_heap_tick = 0;
        if (millis() - s_heap_tick >= 10000) {
            s_heap_tick = millis();
            Serial.printf("[dbg] heap free=%u  min=%u  state=%d\n",
                          esp_get_free_heap_size(),
                          esp_get_minimum_free_heap_size(),
                          (int)s_state);
        }
    }

    // ---- Battery refresh every 30 s -----------------------------------------
    static uint32_t s_bat_tick = 0;
    if (millis() - s_bat_tick >= 30000) {
        s_bat_tick = millis();
        auto bat = axp_read();
        ui_chrome_update_battery(bat.pct, bat.charging);
    }

    delay(5);
}
