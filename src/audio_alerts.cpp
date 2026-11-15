#include "audio_alerts.h"
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <math.h>

static bool g_audioMuted = false;
static int  g_lastStatus = -1;
static uint32_t g_lastAlertMs = 0;

static QueueHandle_t g_audioQueue = NULL;
static TaskHandle_t  g_audioTaskHandle = NULL;

// 64-sample precomputed sine table (centered at 128, amplitude 120)
static const uint8_t SINE_64[64] = {
  128, 140, 152, 163, 175, 186, 196, 206,
  214, 222, 229, 235, 239, 243, 245, 245,
  245, 243, 239, 235, 229, 222, 214, 206,
  196, 186, 175, 163, 152, 140, 128, 116,
  104,  93,  81,  70,  60,  50,  42,  34,
   27,  21,  17,  13,  11,  11,  11,  13,
   17,  21,  27,  34,  42,  50,  60,  70,
   81,  93, 104, 116, 128, 128, 128, 128
};

// ============================================================================
// HELPER: PLAY SINE TONE VIA HARDWARE DAC1 (GPIO 25)
// ============================================================================
static void play_tone(float freqHz, uint16_t durationMs, uint8_t maxVolume = 230, bool decay = false) {
  if (freqHz <= 10.0f) {
    vTaskDelay(pdMS_TO_TICKS(durationMs));
    return;
  }

  // Sample period in microseconds for 64-sample period:
  // T_period = 1 / freq -> step = 1 / (freq * 64)
  // To keep timing robust on FreeRTOS without high timer jitter, we step through phase
  float sampleRate = 16000.0f; // 16 kHz synthesis loop
  uint32_t stepUs = (uint32_t)(1000000.0f / sampleRate);
  uint32_t totalSamples = (uint32_t)((durationMs / 1000.0f) * sampleRate);
  float phaseInc = (freqHz * 64.0f) / sampleRate;
  float phase = 0.0f;

  for (uint32_t s = 0; s < totalSamples; s++) {
    float volScale = (float)maxVolume / 255.0f;
    if (decay) {
      volScale *= (1.0f - ((float)s / (float)totalSamples));
    }
    int idx = ((int)phase) % 64;
    int centered = (int)SINE_64[idx] - 128;
    int outVal = 128 + (int)(centered * volScale);
    if (outVal < 0) outVal = 0;
    if (outVal > 255) outVal = 255;

    dacWrite(PIN_DAC_AUDIO, (uint8_t)outVal);
    phase += phaseInc;
    if (phase >= 64.0f) phase -= 64.0f;

    delayMicroseconds(stepUs);
  }

  // Smooth ramp-down to zero DC bias
  dacWrite(PIN_DAC_AUDIO, 0);
}

// ============================================================================
// ASYNCHRONOUS AUDIO TASK
// ============================================================================
static void audioTask(void *pvParameters) {
  AudioSoundType sound;
  for (;;) {
    if (xQueueReceive(g_audioQueue, &sound, portMAX_DELAY) == pdTRUE) {
      if (g_audioMuted) {
        dacWrite(PIN_DAC_AUDIO, 0);
        continue;
      }

      switch (sound) {
        case SOUND_STARTUP:
          // Major arpeggio: C5 (523Hz), E5 (659Hz), G5 (784Hz), C6 (1046Hz)
          play_tone(523.25f, 90, 200, true);
          vTaskDelay(pdMS_TO_TICKS(15));
          play_tone(659.25f, 90, 210, true);
          vTaskDelay(pdMS_TO_TICKS(15));
          play_tone(783.99f, 90, 220, true);
          vTaskDelay(pdMS_TO_TICKS(15));
          play_tone(1046.50f, 180, 230, true);
          break;

        case SOUND_SAFE_CHIME:
          // Harmonic confirmation bell: G5 (784Hz) -> C6 (1046Hz)
          play_tone(783.99f, 120, 220, true);
          vTaskDelay(pdMS_TO_TICKS(30));
          play_tone(1046.50f, 300, 240, true);
          break;

        case SOUND_CONTAMINATED_ALARM:
          // 3 bursts of dual-frequency alarm siren (880 Hz <-> 1320 Hz)
          for (int burst = 0; burst < 3; burst++) {
            play_tone(880.0f, 90, 255, false);
            play_tone(1320.0f, 90, 255, false);
            play_tone(880.0f, 90, 255, false);
            play_tone(1320.0f, 120, 255, false);
            vTaskDelay(pdMS_TO_TICKS(60));
          }
          break;

        case SOUND_TEST:
          // Double chirp
          play_tone(987.77f, 70, 220, true);
          vTaskDelay(pdMS_TO_TICKS(40));
          play_tone(1318.51f, 100, 240, true);
          break;

        case SOUND_LOW_BATTERY:
          // Low-battery descending double-beep
          play_tone(783.99f, 80, 220, true);
          vTaskDelay(pdMS_TO_TICKS(50));
          play_tone(523.25f, 120, 220, true);
          break;

        default:
          break;
      }

      // Ensure 0V output to prevent idle hiss and DC heating
      dacWrite(PIN_DAC_AUDIO, 0);
    }
  }
}

// ============================================================================
// PUBLIC FUNCTIONS
// ============================================================================
void audio_init() {
  dacWrite(PIN_DAC_AUDIO, 0); // Start silent at 0V

  // Load Persisted Audio Mute Setting from NVS
  Preferences prefs;
  if (prefs.begin("jalrakshak", true)) {
    if (prefs.isKey("muted")) {
      g_audioMuted = prefs.getBool("muted", false);
    } else {
      g_audioMuted = prefs.getBool("audMute", false);
    }
    prefs.end();
  }

  // Create Queue and Task
  g_audioQueue = xQueueCreate(4, sizeof(AudioSoundType));
  if (g_audioQueue != NULL) {
    xTaskCreatePinnedToCore(
      audioTask,
      "AudioTask",
      3072,
      NULL,
      1,
      &g_audioTaskHandle,
      0 // Core 0
    );
  }

  // Play startup sound
  audio_play_startup();
}

void audio_tick() {
  // Tick handler hook (can be used for additional audio housekeeping if needed)
}

void audio_play_sound(AudioSoundType sound) {
  if (g_audioQueue != NULL && !g_audioMuted) {
    xQueueSend(g_audioQueue, &sound, 0);
  }
}

void audio_play_startup() {
  audio_play_sound(SOUND_STARTUP);
}

void audio_play_safe() {
  audio_play_sound(SOUND_SAFE_CHIME);
}

void audio_play_contaminated() {
  audio_play_sound(SOUND_CONTAMINATED_ALARM);
}

void audio_play_test() {
  audio_play_sound(SOUND_TEST);
}

void audio_play_low_battery() {
  audio_play_sound(SOUND_LOW_BATTERY);
}

void audio_update_state(int newStatus) {
  uint32_t now = millis();

  if (newStatus != g_lastStatus) {
    if (newStatus == 0) { // SAFE
      audio_play_safe();
    } else if (newStatus == 2) { // CONTAMINATED
      if (now - g_lastAlertMs >= 15000 || g_lastStatus == -1) {
        audio_play_contaminated();
        g_lastAlertMs = now;
      }
    }
    g_lastStatus = newStatus;
  } else if (newStatus == 2) {
    // If still contaminated, repeat alarm after anti-spam cooldown of 15 seconds
    if (now - g_lastAlertMs >= 15000) {
      audio_play_contaminated();
      g_lastAlertMs = now;
    }
  }
}

bool audio_toggle_mute() {
  g_audioMuted = !g_audioMuted;
  dacWrite(PIN_DAC_AUDIO, 0);

  Preferences prefs;
  if (prefs.begin("jalrakshak", false)) {
    prefs.putBool("muted", g_audioMuted);
    prefs.putBool("audMute", g_audioMuted);
    prefs.end();
  }
  Serial.printf("[AUDIO] Mute toggled: %s\n", g_audioMuted ? "MUTED" : "UNMUTED (ON)");
  return g_audioMuted;
}

bool audio_is_muted() {
  return g_audioMuted;
}

void audio_set_muted(bool muted) {
  g_audioMuted = muted;
  dacWrite(PIN_DAC_AUDIO, 0);

  Preferences prefs;
  if (prefs.begin("jalrakshak", false)) {
    prefs.putBool("muted", g_audioMuted);
    prefs.putBool("audMute", g_audioMuted);
    prefs.end();
  }
}
