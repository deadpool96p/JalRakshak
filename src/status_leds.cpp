#include "status_leds.h"

// Current System Status (0 = SAFE, 1 = CAUTION, 2 = CONTAMINATED)
static int g_currentStatus = 0;

// Blue LED Pulse State Machine
static uint32_t g_blueOffAt = 0;
static uint8_t  g_doublePulsePhase = 0;
static uint32_t g_nextPhaseAt = 0;

void status_leds_init() {
  pinMode(PIN_LED_GREEN, OUTPUT);
  pinMode(PIN_LED_RED, OUTPUT);
  pinMode(PIN_LED_BLUE, OUTPUT);

  digitalWrite(PIN_LED_GREEN, LOW);
  digitalWrite(PIN_LED_RED, LOW);
  digitalWrite(PIN_LED_BLUE, LOW);
}

void status_leds_set_status(int finalStatus) {
  g_currentStatus = finalStatus;
}

void status_leds_pulse_blue(uint16_t durationMs) {
  digitalWrite(PIN_LED_BLUE, HIGH);
  g_blueOffAt = millis() + durationMs;
  g_doublePulsePhase = 0; // Cancel any active multi-phase pulse
}

void status_leds_double_pulse_blue() {
  // Phase 1: ON for 40ms -> Phase 2: OFF for 40ms -> Phase 3: ON for 40ms -> OFF
  digitalWrite(PIN_LED_BLUE, HIGH);
  g_doublePulsePhase = 1;
  g_nextPhaseAt = millis() + 40;
}

void status_leds_tick() {
  uint32_t now = millis();

  // --------------------------------------------------------------------------
  // 1. GREEN & RED LED STATE MACHINE
  // --------------------------------------------------------------------------
  if (g_currentStatus == 0) {
    // CLASS_SAFE: Solid Green, Red OFF
    digitalWrite(PIN_LED_GREEN, HIGH);
    digitalWrite(PIN_LED_RED, LOW);
  } else if (g_currentStatus == 2) {
    // CLASS_CONTAMINATED: Green OFF, Rapid Emergency Flash on Red (200 ms toggle)
    digitalWrite(PIN_LED_GREEN, LOW);
    bool redState = ((now / 200) % 2 == 0);
    digitalWrite(PIN_LED_RED, redState ? HIGH : LOW);
  } else {
    // CLASS_CAUTION: Slow Alternating Blink between Green and Red (600 ms period)
    bool altPhase = ((now / 600) % 2 == 0);
    digitalWrite(PIN_LED_GREEN, altPhase ? HIGH : LOW);
    digitalWrite(PIN_LED_RED, altPhase ? LOW : HIGH);
  }

  // --------------------------------------------------------------------------
  // 2. BLUE LED ACTIVITY PULSE STATE MACHINE
  // --------------------------------------------------------------------------
  if (g_doublePulsePhase > 0) {
    if (now >= g_nextPhaseAt) {
      if (g_doublePulsePhase == 1) {
        // Turn OFF after 1st pulse
        digitalWrite(PIN_LED_BLUE, LOW);
        g_doublePulsePhase = 2;
        g_nextPhaseAt = now + 40;
      } else if (g_doublePulsePhase == 2) {
        // Turn ON for 2nd pulse
        digitalWrite(PIN_LED_BLUE, HIGH);
        g_doublePulsePhase = 3;
        g_nextPhaseAt = now + 40;
      } else {
        // Turn OFF and finish double pulse
        digitalWrite(PIN_LED_BLUE, LOW);
        g_doublePulsePhase = 0;
        g_blueOffAt = 0;
      }
    }
  } else if (g_blueOffAt > 0) {
    if (now >= g_blueOffAt) {
      digitalWrite(PIN_LED_BLUE, LOW);
      g_blueOffAt = 0;
    }
  }
}
