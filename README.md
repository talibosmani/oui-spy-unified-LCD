# OUI Spy — Waveshare ESP32-S3-Touch-AMOLED-1.75 Port

A passive BLE surveillance detector running on the Waveshare ESP32-S3-Touch-AMOLED-1.75 (466×466 round AMOLED). It listens to Bluetooth Low Energy advertisements, identifies surveillance hardware by OUI prefix and other signatures, and announces matches out loud using a software speech synthesizer.

**Based on [OUI Spy Unified Blue](https://colonelpanichacks.github.io/oui-spy-unified-blue/) by Colonel Panic.**
This project ports and extends that work onto a different hardware platform with audio, an expanded detection table, and additional operating modes.

---

## Hardware

| Component | Detail |
|-----------|--------|
| Board | Waveshare ESP32-S3-Touch-AMOLED-1.75 |
| Display | 466×466 round AMOLED (CO5300, QSPI) |
| MCU | ESP32-S3R8 — dual-core 240 MHz |
| PSRAM | 8 MB OPI embedded |
| Touch | CST9217 (I2C) |
| Audio | ES8311 codec (I2C + I2S) + PA (GPIO46) |
| PMIC | AXP2101 (I2C) |
| Flash | 16 MB |

### Pin map

| Function | GPIO |
|----------|------|
| I2S MCLK | 42 |
| I2S BCLK | 9 |
| I2S LRCLK | 45 |
| I2S DOUT | 8 |
| Audio PA enable | 46 |
| I2C SDA | 15 |
| I2C SCL | 14 |
| SD CMD (onboard slot) | 1 |
| SD CLK (onboard slot) | 2 |
| SD D0 (onboard slot) | 3 |

The onboard microSD slot is driven in SDMMC 1-bit mode. No external wiring is required. Detection logs and PCAP captures are written to the card when present, otherwise to internal LittleFS. If the card is unformatted or exFAT, a boot-time dialog offers to format it as FAT32.

---

## Differences from the original project

The original [OUI Spy Unified Blue](https://colonelpanichacks.github.io/oui-spy-unified-blue/) by Colonel Panic established the core concept and detection approach. This port makes the following changes and additions:

### Hardware target

The original targets a different display and MCU combination. This build is written entirely for the Waveshare ESP32-S3-Touch-AMOLED-1.75, which means:

- A custom LVGL 8.4.0 driver for the CO5300 QSPI round display (466×466, 6-pixel x-offset)
- CST9217 I2C touch driver with axis mirroring
- AXP2101 PMIC integration for battery and power management
- OPI PSRAM heap layout — this breaks the newer IDF GDMA I2S driver, requiring the legacy `driver/i2s.h` path (see audio section)

### Audio alerts (new)

The original project has no audio output. This build adds:

- **ES8311 I2C codec** driver with a verified initialization sequence (sourced from Colonel Panic's own [capsule-radar](https://github.com/socquique/capsule-radar) and [TamaPoke](https://socquique.github.io/TamaPoke/) projects, same board)
- **ESP8266SAM text-to-speech** synthesizer for spoken alerts
- Flock Safety cameras speak `FLOCK. CAMERA. DETECTED`
- All other matched vendors speak `B L E. DETECTED`
- Tone-mode fallback (two-tone / triple-beep) when voice is disabled
- Volume control and mute toggle via quick-settings panel

**Key hardware detail:** MCLK must be GPIO 42, not GPIO 3. The newer `ESP_I2S.h` / GDMA driver fails on this board because OPI PSRAM is the default heap and the GDMA driver rejects callback contexts in PSRAM. The legacy `driver/i2s.h` works correctly.

### Expanded OUI and signature table

| Vendor | Signatures | Notes |
|--------|-----------|-------|
| Flock Safety | 38 OUI prefixes | ALPR fixed cameras |
| Ring | 13 OUI prefixes | All registered Ring LLC MAC blocks |
| Wyze | 6 OUI prefixes | |
| DJI | 4 OUI prefixes | Drones |
| Skydio | 1 OUI prefix | Drones |
| Parrot | 3 OUI prefixes | Drones |
| AXON | Company ID 0x034D + service UUID 0xFC81 | |
| Meta Ray-Ban | Company ID 0x0D53 + service UUID 0xFD5F + name match | Rotating MAC — OUI alone is insufficient |

### Additional operating modes

| Mode | Description |
|------|-------------|
| **DETECTOR** | Primary mode. Passive BLE scan with OUI matching, badge display, voice alert, and JSON logging |
| **BLE SNIFF** | Unfiltered advertisement log — every advertising device in range, timestamped |
| **FOX HUNT** | WiFi RSSI-based transmitter direction finding |
| **SKY SPY** | Drone-focused detection (DJI, Skydio, Parrot) with dedicated log |
| **PCAP** | Raw 802.11 packet capture to the SD card (or LittleFS), downloadable over WiFi |
| **SELF TEST** | Swipe-left panel to verify touch, audio, and system health before field use |

### Matching rules and false-positive control

- **OUI prefixes are only compared on public BLE addresses.** Phones rotate random (private) addresses every ~15 minutes; those carry no vendor OUI, so comparing them against the prefix table is pure chance and is skipped.
- **Meta Ray-Ban requires company ID and service UUID together** (or a device-name match). Company ID alone would flag any Meta product.
- **One device is one detection.** A re-alert of a MAC already on screen refreshes its RSSI and moves it to the top instead of adding a duplicate row. The footer shows the number of distinct devices seen this session; the repeat count lives in the log file as `times_seen`.

### JSON detection logging

Every match is written to `/ble_detect_log.json` on the SD card, or LittleFS if no card is present:

```json
{
  "mac": "AC:9F:C3:xx:xx:xx",
  "vendor": "RING",
  "method": "oui",
  "first_seen": 12345,
  "last_seen": 67890,
  "times_seen": 3,
  "rssi_peak": -62
}
```

Writes are deferred: a new entry is flushed within 2 seconds, updates to existing entries at most once a minute. The same policy applies to the BLE Sniff, Sky Spy and Flock-You logs. Debug-mode scan rows are never written to the detection log.

### Debug mode

The **DBG** toggle in the quick-settings panel (swipe down from the top) shows every unmatched BLE device in the detector list with a grey badge, rate-limited to 4 new entries per second. In debug mode the footer separates real hits from scan noise: `0 hits · 37 scanned`. Scan rows are shown but not logged.

---

## Flashing a release

Each [release](https://github.com/talibosmani/oui-spy-unified-LCD/releases) ships a merged image for offset `0x0`, so no PlatformIO is needed:

```bash
esptool.py --chip esp32s3 --port /dev/cu.usbmodemXXXX write_flash 0x0 oui-spy-vX.Y.Z-esp32s3-amoled-1.75.bin
```

## Build

Requires [PlatformIO](https://platformio.org/).

```bash
# Clone
git clone https://github.com/talibosmani/oui-spy-unified-LCD.git
cd oui-spy-unified-LCD

# Build and flash
pio run -e oui-spy-175 -t upload

# Serial monitor
pio device monitor -b 115200
```

### Dependencies (managed by PlatformIO)

- `lvgl/lvgl @ 8.4.0`
- `moononournation/GFX Library for Arduino @ 1.6.4`
- `h2zero/NimBLE-Arduino @ ^1.4.0`
- `bblanchon/ArduinoJson @ ^7.0.4`
- `earlephilhower/ESP8266Audio @ ^1.9.7`
- `earlephilhower/ESP8266SAM @ ^1.0.0`

---

## Credits

- **[Colonel Panic](https://colonelpanichacks.github.io/oui-spy-unified-blue/)** — original OUI Spy Unified Blue concept, detection architecture, and OUI database
- **[socquique/capsule-radar](https://github.com/socquique/capsule-radar)** and **[TamaPoke](https://socquique.github.io/TamaPoke/)** — ES8311 audio initialization sequence and I2S pin mapping for this board

---

## License

This project inherits the license of the upstream OUI Spy Unified Blue. Check the original repository for terms. All additions made in this port are released under the same terms.
