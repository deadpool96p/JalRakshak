#ifndef AUDIO_ALERTS_H
#define AUDIO_ALERTS_H

#include <Arduino.h>

// ============================================================================
// HARDWARE DAC PIN DEFINITION (PAM8403 Amplifier Input)
// ============================================================================
#define PIN_DAC_AUDIO 25  // D25: ESP32 Hardware DAC1 Output (Channel 1)

// Audio Sound Types
enum AudioSoundType {
  SOUND_NONE = 0,
  SOUND_STARTUP,
  SOUND_SAFE_CHIME,
  SOUND_CONTAMINATED_ALARM,
  SOUND_TEST,
  SOUND_LOW_BATTERY
};

// ============================================================================
// PUBLIC FUNCTIONS
// ============================================================================
void audio_init();
void audio_tick();
void audio_play_sound(AudioSoundType sound);
void audio_play_startup();
void audio_play_safe();
void audio_play_contaminated();
void audio_play_test();
void audio_play_low_battery();
void audio_update_state(int newStatus);
bool audio_toggle_mute();
bool audio_is_muted();
void audio_set_muted(bool muted);

#endif // AUDIO_ALERTS_H
