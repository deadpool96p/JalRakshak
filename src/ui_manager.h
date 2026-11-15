#ifndef UI_MANAGER_H
#define UI_MANAGER_H

#include <Arduino.h>
#include <TFT_eSPI.h>
#include "sensors.h"

// ============================================================================
// HARDWARE DEFINITION: BACKLIGHT PWM (GPIO 22 / D22)
// ============================================================================
#define PIN_TFT_BACKLIGHT 22

// ============================================================================
// UI THEME COLORS (16-bit RGB565)
// ============================================================================
#define UI_COLOR_BG          0x08A5  // Dark Navy Medical / Industrial (#0B132B)
#define UI_COLOR_CARD        0x112B  // Elevated Card Background
#define UI_COLOR_CARD_BORDER 0x2236  // Subtle Border
#define UI_COLOR_CYAN        0x06FD  // Tech Cyan Accent (#00DFFF)
#define UI_COLOR_WHITE       0xFFFF  // Pure White
#define UI_COLOR_GRAY        0x7BEF  // Secondary Text
#define UI_COLOR_DARK_GRAY   0x31A6  // Grid & Track Line
#define UI_COLOR_SAFE        0x07E0  // Green (#00FF00)
#define UI_COLOR_CAUTION     0xFDE0  // Amber / Yellow (#FFD700)
#define UI_COLOR_ALERT       0xF800  // Red (#FF0000)
#define UI_COLOR_DARK_SAFE   0x03E0
#define UI_COLOR_DARK_ALERT  0x8000

// ============================================================================
// ENUMS FOR SCREENS, LANGUAGES & BRIGHTNESS
// ============================================================================
enum ScreenView {
  VIEW_DASHBOARD = 0,     // 4 Parameter Cards + Live Risk Graph + Badge
  VIEW_RAW_DIAGNOSTICS,  // Raw ADC Voltages, 1-Wire & Calibration Baseline
  VIEW_TINYML_METRICS    // Neural Network Architecture & Class Probabilities
};

enum LanguageMode {
  LANG_EN = 0,            // English
  LANG_HI = 1,            // Hindi (हिंदी)
  LANG_MR = 2             // Marathi (मराठी)
};

enum BrightnessLevel {
  BRIGHTNESS_LOW = 0,     // 30%  (Duty 75)
  BRIGHTNESS_MED = 1,     // 65%  (Duty 165)
  BRIGHTNESS_HIGH = 2     // 100% (Duty 255)
};

// ============================================================================
// PUBLIC FUNCTIONS
// ============================================================================
void ui_init();
void ui_render(const WaterTelemetry &telemetry);
void ui_handle_touch(WaterTelemetry &telemetry, bool &simSpikeActive);
void ui_run_touch_calibration();

void ui_cycle_language();
void ui_cycle_brightness();
void ui_request_language_cycle();
void ui_request_brightness_cycle();
void ui_request_force_redraw();
LanguageMode ui_get_language();
BrightnessLevel ui_get_brightness();
ScreenView ui_get_current_view();

#endif // UI_MANAGER_H
