// Audio driver — ES8311 codec via I2C + legacy IDF I2S driver (driver/i2s.h).
// ESP_I2S.h fails on this board: OPI PSRAM becomes default heap and the new
// IDF GDMA driver rejects callback contexts that land in PSRAM.
// Legacy driver has no such restriction and confirmed init OK (err=0).
// Pins and ES8311 sequence from socquique/TamaPoke & capsule-radar (same board).
#include "audio_alert.h"
#include "config.h"
#include <Wire.h>
#include <Arduino.h>
#include "driver/i2s.h"
#include <math.h>
#include <esp_heap_caps.h>
#include <AudioOutput.h>
#include <ESP8266SAM.h>

#define ES8311_ADDR  0x18
#define I2S_PORT     I2S_NUM_0
#define SAMPLE_RATE  22050   // SAM native rate; MCLK/BCLK ratio stays 8 so ES8311 regs unchanged

static volatile int8_t    s_pending   = -1;
static bool                s_audio_ok  = false;
static bool                s_muted     = false;
static uint8_t             s_vol_pct   = 75;
static bool                s_voice_mode= true;    // SAM TTS by default

static const uint32_t DEBOUNCE_MS[2] = { 20000, 15000 };
static uint32_t       s_last_alert[2] = {0, 0};

// ─── ES8311 I2C helpers ───────────────────────────────────────────────────────

static void e_wr(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(ES8311_ADDR);
    Wire.write(reg);
    Wire.write(val);
    Wire.endTransmission();
}

static uint8_t e_rd(uint8_t reg) {
    Wire.beginTransmission(ES8311_ADDR);
    Wire.write(reg);
    Wire.endTransmission(false);
    Wire.requestFrom((uint8_t)ES8311_ADDR, (uint8_t)1);
    return Wire.available() ? Wire.read() : 0x00;
}

// Sequence verified against socquique/TamaPoke and capsule-radar (same board).
static bool es8311_init() {
    Wire.beginTransmission(ES8311_ADDR);
    if (Wire.endTransmission() != 0) {
        Serial.printf("[audio] ES8311 not found at 0x%02X\n", ES8311_ADDR);
        return false;
    }

    e_wr(0x0D, 0xFA);
    e_wr(0x44, 0x08);
    e_wr(0x44, 0x08);
    e_wr(0x01, 0x30);
    e_wr(0x02, 0x00);
    e_wr(0x03, 0x10);
    e_wr(0x16, 0x24);
    e_wr(0x04, 0x10);
    e_wr(0x05, 0x00);
    e_wr(0x0B, 0x00);
    e_wr(0x0C, 0x00);
    e_wr(0x10, 0x1F);
    e_wr(0x11, 0x7F);
    e_wr(0x00, 0x80);
    e_wr(0x00, 0x80);
    e_wr(0x01, 0xBF);
    e_wr(0x13, 0x10);
    e_wr(0x1B, 0x0A);
    e_wr(0x1C, 0x6A);
    e_wr(0x44, 0x58);

    // Read-modify-write reg 0x06: clear MCLK divider enable bit
    { uint8_t r = e_rd(0x06); r &= ~0x20; e_wr(0x06, r); }

    e_wr(0x02, 0x18);
    e_wr(0x05, 0x00);
    e_wr(0x03, 0x10);
    e_wr(0x04, 0x20);
    e_wr(0x08, 0xFF);

    // Read-modify-write reg 0x06: set BCLK divider bits to 3
    { uint8_t r = e_rd(0x06); r &= 0xE0; r |= 0x03; e_wr(0x06, r); }

    e_wr(0x09, 0x0C);
    e_wr(0x0A, 0x0C);
    e_wr(0x00, 0x80);
    e_wr(0x01, 0xBF);
    e_wr(0x09, 0x0C);
    e_wr(0x0A, 0x0C);
    e_wr(0x17, 0xBF);
    e_wr(0x0E, 0x02);
    e_wr(0x12, 0x00);
    e_wr(0x14, 0x1A);
    e_wr(0x0D, 0x01);
    e_wr(0x15, 0x40);
    e_wr(0x37, 0x08);
    e_wr(0x45, 0x00);
    e_wr(0x32, 0xBF);   // DAC output volume near-max

    Serial.println("[audio] ES8311 configured");
    return true;
}

// ─── SAM TTS AudioOutput adapter ─────────────────────────────────────────────
// SAM generates 22050 Hz mono 16-bit. We write stereo to match the I2S config.

class SAMOut : public AudioOutput {
public:
    SAMOut() : _pos(0), _gain(4.0f) {}

    bool begin() override { _pos = 0; return true; }
    bool stop()  override { flush(); return true; }

    bool SetRate(int hz)         override { (void)hz; return true; }
    bool SetBitsPerSample(int b) override { (void)b;  return true; }
    bool SetChannels(int c)      override { (void)c;  return true; }
    bool SetGain(float g)        override { _gain = g; return true; }

    bool ConsumeSample(int16_t sample[2]) override {
        int32_t v32 = (int32_t)((float)sample[0] * _gain);
        if (v32 >  32767) v32 =  32767;
        if (v32 < -32768) v32 = -32768;
        int16_t v = (int16_t)v32;
        _buf[_pos * 2]     = v;
        _buf[_pos * 2 + 1] = v;
        if (++_pos >= CHUNK) flush();
        return true;
    }

private:
    static const int CHUNK = 256;
    int16_t _buf[CHUNK * 2];
    int     _pos;
    float   _gain;

    void flush() {
        if (_pos == 0) return;
        size_t w = 0;
        i2s_write(I2S_PORT, _buf, _pos * 4, &w, pdMS_TO_TICKS(1000));
        _pos = 0;
    }
};

static void speak_blocking(const char *text) {
    if (!s_audio_ok || s_muted) return;
    SAMOut out;
    ESP8266SAM sam;
    sam.Say(&out, text);
}

// ─── Tone playback ───────────────────────────────────────────────────────────

static const int BUF_FRAMES = 512;
static int16_t *s_buf = nullptr;

static void play_tone(uint32_t freq_hz, uint32_t dur_ms) {
    if (!s_audio_ok || !s_buf) { delay(dur_ms); return; }
    const uint32_t total = (uint32_t)SAMPLE_RATE * dur_ms / 1000;
    const uint32_t fade  = SAMPLE_RATE / 200;   // 5 ms

    for (uint32_t off = 0; off < total; ) {
        uint32_t chunk = total - off;
        if (chunk > (uint32_t)BUF_FRAMES) chunk = (uint32_t)BUF_FRAMES;
        for (uint32_t j = 0; j < chunk; j++) {
            uint32_t si = off + j;
            float env = 1.0f;
            if (si < fade)             env = (float)si / fade;
            if (si > total - fade)     env = (float)(total - si) / fade;
            int16_t v = (int16_t)(env * 28000.0f *
                        sinf(2.0f * (float)M_PI * (float)freq_hz * si / SAMPLE_RATE));
            s_buf[j * 2]     = v;
            s_buf[j * 2 + 1] = v;
        }
        size_t written = 0;
        i2s_write(I2S_PORT, s_buf, chunk * 4, &written, pdMS_TO_TICKS(500));
        off += chunk;
    }
}

static void alert_ble() {
    if (s_voice_mode) speak_blocking("B L E. DETECTED");
    else { play_tone(1200,160); play_tone(880,240); }
}
static void alert_flock() {
    if (s_voice_mode) speak_blocking("FLOCK. CAMERA. DETECTED");
    else { play_tone(880,110); delay(35); play_tone(1100,110); delay(35); play_tone(1320,220); }
}

// ─── Public API ─────────────────────────────────────────────────────────────

void audio_init() {
    Serial.println("[audio] init start"); Serial.flush();
    pinMode(PIN_AUDIO_PA, OUTPUT);
    digitalWrite(PIN_AUDIO_PA, LOW);

    // Tone buffer in PSRAM to preserve internal RAM
    s_buf = (int16_t *)heap_caps_malloc(BUF_FRAMES * 2 * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    if (!s_buf)
        s_buf = (int16_t *)malloc(BUF_FRAMES * 2 * sizeof(int16_t));
    if (!s_buf) { Serial.println("[audio] buf alloc failed"); return; }

    i2s_config_t cfg = {};
    cfg.mode                = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    cfg.sample_rate         = SAMPLE_RATE;
    cfg.bits_per_sample     = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format      = I2S_CHANNEL_FMT_RIGHT_LEFT;
    cfg.communication_format= I2S_COMM_FORMAT_STAND_I2S;
    cfg.intr_alloc_flags    = ESP_INTR_FLAG_LEVEL1;
    cfg.dma_buf_count       = 4;
    cfg.dma_buf_len         = 512;
    cfg.use_apll            = false;
    cfg.tx_desc_auto_clear  = true;
    cfg.fixed_mclk          = 0;
    cfg.mclk_multiple       = I2S_MCLK_MULTIPLE_256;
    cfg.bits_per_chan        = I2S_BITS_PER_CHAN_16BIT;

    esp_err_t err = i2s_driver_install(I2S_PORT, &cfg, 0, nullptr);
    Serial.printf("[audio] i2s_install=%d\n", (int)err); Serial.flush();
    if (err != ESP_OK) return;

    i2s_pin_config_t pins = {};
    pins.mck_io_num   = PIN_I2S_MCLK;   // GPIO42
    pins.bck_io_num   = PIN_I2S_BCLK;   // GPIO9
    pins.ws_io_num    = PIN_I2S_LRCLK;  // GPIO45
    pins.data_out_num = PIN_I2S_DOUT;   // GPIO8
    pins.data_in_num  = I2S_PIN_NO_CHANGE;

    err = i2s_set_pin(I2S_PORT, &pins);
    Serial.printf("[audio] i2s_pins=%d\n", (int)err); Serial.flush();
    if (err != ESP_OK) { i2s_driver_uninstall(I2S_PORT); return; }

    i2s_zero_dma_buffer(I2S_PORT);

    s_audio_ok = true;
    bool has_codec = es8311_init();
    Serial.printf("[audio] ready codec=%s\n", has_codec ? "ES8311" : "direct-amp");
    Serial.flush();
}

void audio_alert_request(AlertType type) {
    s_pending = (int8_t)type;
}

void audio_set_volume(uint8_t pct) {
    if (pct > 100) pct = 100;
    s_vol_pct = pct;
    e_wr(0x32, (uint8_t)((pct * 255u) / 100u));
}

bool audio_mute_toggle() {
    if (!s_muted) {
        s_muted = true;
        e_wr(0x32, 0x00);
        digitalWrite(PIN_AUDIO_PA, LOW);
    } else {
        s_muted = false;
        e_wr(0x32, (uint8_t)((s_vol_pct * 255u) / 100u));
    }
    return s_muted;
}

bool    audio_is_muted()       { return s_muted; }
uint8_t audio_get_volume_pct() { return s_muted ? 0 : s_vol_pct; }

void audio_set_voice_mode(bool voice) { s_voice_mode = voice; }
bool audio_is_voice_mode()            { return s_voice_mode; }

bool audio_is_ready() { return s_audio_ok; }

void audio_play_test_tone() {
    if (!s_audio_ok) return;
    Serial.println("[audio] test tone start"); Serial.flush();
    digitalWrite(PIN_AUDIO_PA, HIGH);
    delay(8);
    play_tone(660,  200);
    play_tone(880,  200);
    play_tone(1100, 300);
    delay(60);
    digitalWrite(PIN_AUDIO_PA, LOW);
    Serial.println("[audio] test tone done"); Serial.flush();
}

void audio_alert_service() {
    int8_t t = s_pending;
    if (t < 0) return;
    s_pending = -1;

    uint32_t now = millis();
    if (now - s_last_alert[t] < DEBOUNCE_MS[t]) return;
    s_last_alert[t] = now;

    if (s_muted) return;
    digitalWrite(PIN_AUDIO_PA, HIGH);
    delay(8);
    switch ((AlertType)t) {
        case AlertType::BLE_Surveillance: alert_ble();   break;
        case AlertType::FlockCamera:      alert_flock(); break;
        default: break;
    }
    delay(60);
    digitalWrite(PIN_AUDIO_PA, LOW);
}
