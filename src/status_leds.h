#ifndef STATUS_LEDS_H
#define STATUS_LEDS_H

#include <Arduino.h>

// ============================================================================
// HARDWARE STATUS LED PIN DEFINITIONS
// ============================================================================
#define PIN_LED_GREEN  33  // D33: Green LED (Drinkable / Safe Status)
#define PIN_LED_RED    32  // D32: Red LED (Not Drinkable / Alert Status)
#define PIN_LED_BLUE   27  // D27: Blue LED (TinyML & Wi-Fi Activity Pulse)

// ============================================================================
// PUBLIC FUNCTIONS
// ============================================================================
void status_leds_init();
void status_leds_set_status(int finalStatus);
void status_leds_pulse_blue(uint16_t durationMs = 40);
void status_leds_double_pulse_blue();
void status_leds_tick();

#endif // STATUS_LEDS_H
