#pragma once
#include <stdint.h>

enum class AlertType : int8_t {
    BLE_Surveillance = 0,   // any matched surveillance BLE device
    FlockCamera      = 1,   // Flock Safety ALPR camera specifically
};

void audio_init();

// Safe to call from any task / ISR — just sets a flag, never blocks
void audio_alert_request(AlertType type);

// Call from main loop() — plays any pending alert (blocks during speech/tones)
void audio_alert_service();

// Volume: 0=mute, 100=max (writes ES8311 DAC volume register)
void audio_set_volume(uint8_t pct);

// Mute toggle: saves/restores pre-mute volume. Returns new muted state.
bool audio_mute_toggle();
bool audio_is_muted();
uint8_t audio_get_volume_pct();  // returns current % (0 when muted)

// Voice/tone toggle: true = SAM speech, false = beep tones
void audio_set_voice_mode(bool voice);
bool audio_is_voice_mode();

// Returns true if the audio codec initialised successfully
bool audio_is_ready();

// Play a 3-note ascending test tone — call from main loop, blocks ~550 ms
void audio_play_test_tone();
