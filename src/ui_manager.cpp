#include "ui_manager.h"
#include "lang_assets.h"
#include "audio_alerts.h"
#include <SPI.h>
#include <Preferences.h>
#include <esp_arduino_version.h>

// Display & Sprite Instances
static TFT_eSPI tft = TFT_eSPI();
// Compact Animated Graph Sprite (120x86 px * 2 bytes = 20.6 KB - strictly < 25 KB constraint)
static TFT_eSprite graphSprite = TFT_eSprite(&tft);

// Preferences (Flash NVS Storage)
static Preferences prefs;

// Application Settings (Persisted in NVS)
static LanguageMode currentLang = LANG_EN;
static BrightnessLevel currentBrt = BRIGHTNESS_HIGH;

// Screen State
static ScreenView currentView = VIEW_DASHBOARD;
static ScreenView lastView = (ScreenView)-1;
static uint32_t lastTouchMs = 0;
static uint32_t animFrame = 0;
static bool isTouching = false; // Edge-triggered debounce flag

// Thread-safe async UI request flags (dispatched safely from Core 0 to Core 1)
static volatile bool g_reqLangCycle = false;
static volatile bool g_reqBrtCycle = false;
static volatile bool g_uiForceRedraw = false;

void ui_request_language_cycle() {
  g_reqLangCycle = true;
}

void ui_request_brightness_cycle() {
  g_reqBrtCycle = true;
}

void ui_request_force_redraw() {
  g_uiForceRedraw = true;
}

// Banner Overlay Timer & Mode
enum BannerType { BANNER_NONE, BANNER_BASELINE_SAVED, BANNER_OFFSETS_RESET };
static BannerType currentBanner = BANNER_NONE;
static uint32_t bannerActiveUntil = 0;

// Long-Press Tracking for Offset Reset on CAL/ZERO
static uint32_t calPressStartMs = 0;
static bool calPressActive = false;

// Rolling Graph History Buffer
#define GRAPH_WIDTH 120
#define GRAPH_HEIGHT 86
static float graphHistory[GRAPH_WIDTH];
static int graphHead = 0;

// Reliable Default Landscape Calibration for 2.8" ILI9341 (setRotation(1))
static uint16_t g_calData[5] = { 275, 3620, 264, 3532, 7 };

// Forward declarations
static void renderHeader(const WaterTelemetry &telemetry);
static void renderTouchBar(const WaterTelemetry &telemetry);
static void renderRawView(const WaterTelemetry &telemetry);

// ============================================================================
// HELPER: HARDWARE PWM BACKLIGHT CONTROL (GPIO 22 / D22)
// ============================================================================
static void setBacklightDuty(uint8_t duty) {
#if defined(ESP_ARDUINO_VERSION) && ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
  ledcWrite(PIN_TFT_BACKLIGHT, duty);
#else
  ledcWrite(0, duty);
#endif
}

static void applyBrightness(BrightnessLevel level) {
  uint8_t duty = 255;
  if (level == BRIGHTNESS_LOW)       duty = 75;   // ~30%
  else if (level == BRIGHTNESS_MED)  duty = 165;  // ~65%
  else                               duty = 255;  // 100%
  setBacklightDuty(duty);
}

void ui_cycle_brightness() {
  int next = (int)currentBrt + 1;
  if (next > BRIGHTNESS_HIGH) next = BRIGHTNESS_LOW;
  currentBrt = (BrightnessLevel)next;
  applyBrightness(currentBrt);
  prefs.putUChar("brt", (uint8_t)currentBrt);
  Serial.printf("[BRIGHTNESS] Set to %s (Duty %d)\n", 
                (currentBrt == BRIGHTNESS_LOW) ? "LOW (30%)" : 
                (currentBrt == BRIGHTNESS_MED) ? "MED (65%)" : "HIGH (100%)",
                (currentBrt == BRIGHTNESS_LOW) ? 75 : (currentBrt == BRIGHTNESS_MED) ? 165 : 255);
}

BrightnessLevel ui_get_brightness() {
  return currentBrt;
}

// ============================================================================
// HELPER: LANGUAGE CYCLING & PERSISTENCE
// ============================================================================
void ui_cycle_language() {
  int next = (int)currentLang + 1;
  if (next > LANG_MR) next = LANG_EN;
  currentLang = (LanguageMode)next;
  prefs.putUChar("lang", (uint8_t)currentLang);
  Serial.printf("[LANGUAGE] Switched to %s\n", 
                (currentLang == LANG_EN) ? "ENGLISH" : 
                (currentLang == LANG_HI) ? "HINDI" : "MARATHI");
  lastView = (ScreenView)-1; // Invalidate view to trigger full re-render
  tft.fillScreen(UI_COLOR_BG);
}

LanguageMode ui_get_language() {
  return currentLang;
}

// ============================================================================
// HELPER: PARAMETER COLOR CODING (IS 10500 Compliant)
// ============================================================================
static uint16_t getPhColor(float ph) {
  if (ph < 6.0f || ph > 9.0f) return UI_COLOR_ALERT;
  if (ph < 6.5f || ph > 8.5f) return UI_COLOR_CAUTION;
  return UI_COLOR_SAFE;
}

static uint16_t getTdsColor(float tds) {
  if (tds > 600.0f) return UI_COLOR_ALERT;
  if (tds > 450.0f) return UI_COLOR_CAUTION;
  return UI_COLOR_SAFE;
}

static uint16_t getTurbColor(float turb) {
  if (turb > 8.0f) return UI_COLOR_ALERT;
  if (turb > 4.5f) return UI_COLOR_CAUTION;
  return UI_COLOR_SAFE;
}

static uint16_t getStatusColor(int status) {
  if (status == 2) return UI_COLOR_ALERT;
  if (status == 1) return UI_COLOR_CAUTION;
  return UI_COLOR_SAFE;
}

// ============================================================================
// TOUCH CALIBRATION ROUTINE & NVS PERSISTENCE
// ============================================================================
void ui_run_touch_calibration() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(1);
  tft.setCursor(20, 35);
  tft.println("========================================");
  tft.setCursor(20, 50);
  tft.println("   XPT2046 TOUCH CALIBRATION MODE       ");
  tft.setCursor(20, 65);
  tft.println("========================================");
  tft.setTextColor(UI_COLOR_CYAN, TFT_BLACK);
  tft.setCursor(20, 95);
  tft.println("Touch the MAGENTA corners as they appear...");

  uint16_t newCal[5];
  tft.calibrateTouch(newCal, TFT_MAGENTA, TFT_BLACK, 15);
  memcpy(g_calData, newCal, sizeof(newCal));
  tft.setTouch(g_calData);

  // Permanently save calibrated array to NVS Flash
  prefs.putBytes("calData", g_calData, sizeof(g_calData));

  Serial.println("\n[TOUCH CALIBRATION COMPLETE & PERSISTED]");
  Serial.printf("// Saved to Preferences NVS:\n");
  Serial.printf("uint16_t calData[5] = { %u, %u, %u, %u, %u };\n\n",
                newCal[0], newCal[1], newCal[2], newCal[3], newCal[4]);

  tft.fillScreen(UI_COLOR_SAFE);
  tft.setTextColor(UI_COLOR_BG, UI_COLOR_SAFE);
  tft.setTextSize(2);
  tft.setCursor(30, 110);
  tft.println("CALIBRATION SAVED!");
  delay(1200);

  tft.fillScreen(UI_COLOR_BG);
  lastView = (ScreenView)-1; // Force full screen refresh
}

// ============================================================================
// INITIALIZATION
// ============================================================================
void ui_init() {
  // 1. Configure Hardware PWM Backlight on GPIO 22
#if defined(ESP_ARDUINO_VERSION) && ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
  ledcAttach(PIN_TFT_BACKLIGHT, 5000, 8);
#else
  ledcSetup(0, 5000, 8);
  ledcAttachPin(PIN_TFT_BACKLIGHT, 0);
#endif

  // 2. Open Preferences NVS Storage
  prefs.begin("jalrakshak", false);

  // Load Persisted Brightness
  if (prefs.isKey("brt")) {
    currentBrt = (BrightnessLevel)prefs.getUChar("brt", (uint8_t)BRIGHTNESS_HIGH);
  }
  applyBrightness(currentBrt);

  // Load Persisted Language
  if (prefs.isKey("lang")) {
    currentLang = (LanguageMode)prefs.getUChar("lang", (uint8_t)LANG_EN);
  }

  // 3. Explicitly drive both chip-select lines HIGH before display initialization
  // so the XPT2046 touch controller stays idle during ILI9341 startup commands
  pinMode(5, OUTPUT);  digitalWrite(5, HIGH);  // TFT_CS idle HIGH
  pinMode(21, OUTPUT); digitalWrite(21, HIGH); // TOUCH_CS idle HIGH
  delay(10);

  tft.init();
  tft.setRotation(1); // Locked 320x240 Landscape
  tft.fillScreen(UI_COLOR_BG);

  // 4. Load Persisted Touch Calibration from NVS or apply reliable landscape default
  if (prefs.isKey("calData")) {
    prefs.getBytes("calData", g_calData, sizeof(g_calData));
    tft.setTouch(g_calData);
    Serial.printf("[NVS] Loaded touch calibration: {%u, %u, %u, %u, %u}\n",
                  g_calData[0], g_calData[1], g_calData[2], g_calData[3], g_calData[4]);
  } else {
    tft.setTouch(g_calData);
    Serial.printf("[NVS] Applied default landscape calibration: {%u, %u, %u, %u, %u}\n",
                  g_calData[0], g_calData[1], g_calData[2], g_calData[3], g_calData[4]);
  }

  // 5. Allocate compact Sprite buffer in internal SRAM (< 25 KB)
  graphSprite.setColorDepth(16);
  graphSprite.createSprite(GRAPH_WIDTH, GRAPH_HEIGHT);
  graphSprite.fillSprite(UI_COLOR_CARD);

  // Initialize rolling graph with baseline
  for (int i = 0; i < GRAPH_WIDTH; i++) {
    graphHistory[i] = 0.05f;
  }

  // 6. Boot Touch Calibration Trigger:
  // If screen pressure is held (getTouchRawZ() > 300) during first 3 seconds, run calibration
  tft.setTextColor(UI_COLOR_CYAN, UI_COLOR_BG);
  tft.setTextSize(1);
  tft.setCursor(20, 110);
  tft.print("Hold screen (3s) for Touch Calibration...");
  
  uint32_t bootCheckStart = millis();
  bool triggerCal = false;
  while (millis() - bootCheckStart < 3000) {
    if (tft.getTouchRawZ() > 300) {
      triggerCal = true;
      break;
    }
    delay(20);
  }

  if (triggerCal) {
    ui_run_touch_calibration();
  } else {
    tft.fillScreen(UI_COLOR_BG);
  }
}

// ============================================================================
// HEADER BAR RENDERING
// ============================================================================
static void renderHeader(const WaterTelemetry &telemetry) {
  tft.fillRect(0, 0, 320, 28, UI_COLOR_BG);
  tft.drawFastHLine(0, 28, 320, UI_COLOR_CARD_BORDER);

  // Title (English or Devanagari Bitmap)
  if (currentLang == LANG_EN) {
    tft.setTextColor(UI_COLOR_WHITE, UI_COLOR_BG);
    tft.setTextSize(1);
    tft.setCursor(6, 10);
    tft.print("JALRAKSHAK");
  } else {
    tft.drawBitmap(6, 5, BMP_TITLE_JALRAKSHAK, BMP_TITLE_JALRAKSHAK_W, BMP_TITLE_JALRAKSHAK_H, UI_COLOR_CYAN, UI_COLOR_BG);
  }

  // Pulsing Heartbeat Dot (x: 82)
  animFrame++;
  int pulseRadius = (animFrame % 16 < 8) ? 3 : 2;
  uint16_t pulseColor = (animFrame % 16 < 8) ? UI_COLOR_CYAN : UI_COLOR_DARK_GRAY;
  tft.fillCircle(82, 14, 4, UI_COLOR_BG);
  tft.fillCircle(82, 14, pulseRadius, pulseColor);

  // 18650 Battery / USB Power Badge (x: 94, w: 46, h: 18)
  tft.fillRoundRect(94, 5, 46, 18, 3, UI_COLOR_CARD);
  if (telemetry.batConnected) {
    uint16_t batCol = (telemetry.batPercent <= 15.0f) ? UI_COLOR_ALERT : 
                      (telemetry.batPercent <= 30.0f) ? UI_COLOR_CAUTION : UI_COLOR_SAFE;
    tft.drawRoundRect(94, 5, 46, 18, 3, batCol);
    tft.setTextColor(batCol, UI_COLOR_CARD);
    tft.setTextSize(1);
    tft.setCursor(98, 10);
    tft.printf("%3.0f%%", telemetry.batPercent);
  } else {
    tft.drawRoundRect(94, 5, 46, 18, 3, UI_COLOR_CYAN);
    tft.setTextColor(UI_COLOR_CYAN, UI_COLOR_CARD);
    tft.setTextSize(1);
    tft.setCursor(104, 10);
    tft.print("USB");
  }

  // Interactive Brightness Badge (x: 144, w: 48, h: 18)
  tft.fillRoundRect(144, 5, 48, 18, 3, UI_COLOR_CARD);
  tft.drawRoundRect(144, 5, 48, 18, 3, UI_COLOR_CARD_BORDER);
  tft.setTextColor(UI_COLOR_WHITE, UI_COLOR_CARD);
  tft.setTextSize(1);
  tft.setCursor(148, 10);
  tft.printf("BRT:%s", (currentBrt == BRIGHTNESS_LOW) ? "L" : (currentBrt == BRIGHTNESS_MED) ? "M" : "H");

  // Interactive Language Badge (x: 196, w: 56, h: 18)
  tft.fillRoundRect(196, 5, 56, 18, 3, UI_COLOR_CARD);
  tft.drawRoundRect(196, 5, 56, 18, 3, UI_COLOR_CYAN);
  tft.setTextColor(UI_COLOR_CYAN, UI_COLOR_CARD);
  tft.setTextSize(1);
  tft.setCursor(200, 10);
  tft.print((currentLang == LANG_EN) ? "LNG:EN" : (currentLang == LANG_HI) ? "LNG:HI" : "LNG:MR");

  // Mode Badge ([ LIVE ] vs [ SPIKE ]) (x: 256, w: 60, h: 18)
  if (telemetry.isSimSpike) {
    tft.fillRoundRect(256, 5, 60, 18, 3, UI_COLOR_DARK_ALERT);
    tft.drawRoundRect(256, 5, 60, 18, 3, UI_COLOR_ALERT);
    tft.setTextColor(UI_COLOR_WHITE, UI_COLOR_DARK_ALERT);
    tft.setTextSize(1);
    tft.setCursor(266, 10);
    tft.print("SPIKE");
  } else {
    tft.fillRoundRect(256, 5, 60, 18, 3, UI_COLOR_CARD);
    tft.drawRoundRect(256, 5, 60, 18, 3, UI_COLOR_SAFE);
    tft.setTextColor(UI_COLOR_SAFE, UI_COLOR_CARD);
    tft.setTextSize(1);
    tft.setCursor(270, 10);
    tft.print("LIVE");
  }
}

// ============================================================================
// 4 PARAMETER CARDS (Clean Clapped Padding, No Overflow, Zero Ghost Pixels)
// ============================================================================
static void renderCardHeader(int x, int y, int cardIdx) {
  tft.fillRect(x + 6, y + 6, 76, 17, UI_COLOR_CARD);

  if (currentLang == LANG_EN) {
    tft.setTextColor(UI_COLOR_GRAY, UI_COLOR_CARD);
    tft.setTextSize(1);
    tft.setCursor(x + 8, y + 8);
    if (cardIdx == 0)      tft.print("pH LEVEL");
    else if (cardIdx == 1) tft.print("TDS (PPM)");
    else if (cardIdx == 2) tft.print("TURBIDITY");
    else if (cardIdx == 3) tft.print("TEMP (C)");
  } else {
    if (cardIdx == 0) {
      tft.drawBitmap(x + 8, y + 7, BMP_LABEL_PH, BMP_LABEL_PH_W, BMP_LABEL_PH_H, UI_COLOR_GRAY, UI_COLOR_CARD);
    } else if (cardIdx == 1) {
      tft.drawBitmap(x + 8, y + 7, BMP_LABEL_TDS, BMP_LABEL_TDS_W, BMP_LABEL_TDS_H, UI_COLOR_GRAY, UI_COLOR_CARD);
    } else if (cardIdx == 2) {
      if (currentLang == LANG_HI)
        tft.drawBitmap(x + 8, y + 7, BMP_LABEL_TURB_HI, BMP_LABEL_TURB_HI_W, BMP_LABEL_TURB_HI_H, UI_COLOR_GRAY, UI_COLOR_CARD);
      else
        tft.drawBitmap(x + 8, y + 7, BMP_LABEL_TURB_MR, BMP_LABEL_TURB_MR_W, BMP_LABEL_TURB_MR_H, UI_COLOR_GRAY, UI_COLOR_CARD);
    } else if (cardIdx == 3) {
      tft.drawBitmap(x + 8, y + 7, BMP_LABEL_TEMP, BMP_LABEL_TEMP_W, BMP_LABEL_TEMP_H, UI_COLOR_GRAY, UI_COLOR_CARD);
    }
  }
}

static void renderParameterCard(int x, int y, int w, int h, int cardIdx, 
                               const char* valStr, const char* unitStr, uint16_t color) {
  tft.fillRoundRect(x, y, w, h, 4, UI_COLOR_CARD);
  tft.drawRoundRect(x, y, w, h, 4, color);

  renderCardHeader(x, y, cardIdx);

  // Clear value region to avoid ghost digits
  tft.fillRect(x + 6, y + 26, w - 12, 22, UI_COLOR_CARD);
  tft.setTextColor(UI_COLOR_WHITE, UI_COLOR_CARD);
  tft.setTextSize(2);
  tft.setTextPadding(w - 14);
  tft.setCursor(x + 8, y + 28);
  tft.print(valStr);
  tft.setTextPadding(0);

  // Clear subtext region & draw unit/status
  tft.fillRect(x + 6, y + 54, w - 12, 16, UI_COLOR_CARD);
  tft.setTextColor(color, UI_COLOR_CARD);
  tft.setTextSize(1);
  tft.setTextPadding(w - 14);
  tft.setCursor(x + 8, y + 55);
  tft.print(unitStr);
  tft.setTextPadding(0);
}

// ============================================================================
// SPRITE-BASED ROLLING WAVEFORM & RISK GAUGE (Right Panel)
// ============================================================================
static void renderGraphSprite(const WaterTelemetry &telemetry) {
  graphHistory[graphHead] = telemetry.probContaminated;
  graphHead = (graphHead + 1) % GRAPH_WIDTH;

  graphSprite.fillSprite(UI_COLOR_CARD);

  graphSprite.drawFastHLine(0, 21, GRAPH_WIDTH, UI_COLOR_DARK_GRAY);
  graphSprite.drawFastHLine(0, 43, GRAPH_WIDTH, UI_COLOR_DARK_GRAY);
  graphSprite.drawFastHLine(0, 64, GRAPH_WIDTH, UI_COLOR_DARK_GRAY);

  uint16_t waveColor = (telemetry.probContaminated > 0.5f) ? UI_COLOR_ALERT : 
                       (telemetry.probCaution > 0.4f) ? UI_COLOR_CAUTION : UI_COLOR_SAFE;

  for (int x = 0; x < GRAPH_WIDTH - 1; x++) {
    int idx1 = (graphHead + x) % GRAPH_WIDTH;
    int idx2 = (graphHead + x + 1) % GRAPH_WIDTH;

    int y1 = GRAPH_HEIGHT - 4 - (int)(graphHistory[idx1] * (GRAPH_HEIGHT - 8));
    int y2 = GRAPH_HEIGHT - 4 - (int)(graphHistory[idx2] * (GRAPH_HEIGHT - 8));
    if (y1 < 2) y1 = 2;
    if (y1 >= GRAPH_HEIGHT - 2) y1 = GRAPH_HEIGHT - 2;
    if (y2 < 2) y2 = 2;
    if (y2 >= GRAPH_HEIGHT - 2) y2 = GRAPH_HEIGHT - 2;

    graphSprite.drawLine(x, y1, x + 1, y2, waveColor);
  }

  graphSprite.drawRect(0, 0, GRAPH_WIDTH, GRAPH_HEIGHT, UI_COLOR_CARD_BORDER);
  graphSprite.setTextColor(UI_COLOR_WHITE, UI_COLOR_CARD);
  graphSprite.setTextSize(1);
  graphSprite.setCursor(6, 6);
  graphSprite.printf("P(Contam): %.0f%%", telemetry.probContaminated * 100.0f);

  graphSprite.setTextColor(UI_COLOR_GRAY, UI_COLOR_CARD);
  graphSprite.setCursor(6, GRAPH_HEIGHT - 14);
  graphSprite.printf("Infer: %lu us", (unsigned long)telemetry.inferenceTimeUs);

  graphSprite.pushSprite(192, 70);
}

// ============================================================================
// SCREEN 1: MAIN DASHBOARD VIEW
// ============================================================================
static void renderDashboardView(const WaterTelemetry &telemetry) {
  char bufVal[16];
  char bufSub[16];

  // Card 0: pH
  snprintf(bufVal, sizeof(bufVal), "%.2f", telemetry.pH);
  if (telemetry.phStandby) {
    snprintf(bufSub, sizeof(bufSub), "Standby");
  } else {
    snprintf(bufSub, sizeof(bufSub), (telemetry.pH < 6.5f) ? "Acidic" : (telemetry.pH > 8.5f) ? "Alkali" : "Normal");
  }
  uint16_t phColor = telemetry.phStandby ? UI_COLOR_GRAY : getPhColor(telemetry.pH);
  renderParameterCard(6, 34, 88, 76, 0, bufVal, bufSub, phColor);

  // Card 1: TDS
  snprintf(bufVal, sizeof(bufVal), "%.0f", telemetry.tdsPpm);
  snprintf(bufSub, sizeof(bufSub), (telemetry.tdsPpm > 500) ? "High ppm" : "ppm Normal");
  renderParameterCard(98, 34, 88, 76, 1, bufVal, bufSub, getTdsColor(telemetry.tdsPpm));

  // Card 2: Turbidity
  snprintf(bufVal, sizeof(bufVal), "%.1f", telemetry.turbNTU);
  if (telemetry.turbStandby) {
    snprintf(bufSub, sizeof(bufSub), "Standby");
  } else {
    snprintf(bufSub, sizeof(bufSub), (telemetry.turbNTU > 5.0f) ? "Turbid NTU" : "Clear NTU");
  }
  uint16_t turbColor = telemetry.turbStandby ? UI_COLOR_GRAY : getTurbColor(telemetry.turbNTU);
  renderParameterCard(6, 116, 88, 76, 2, bufVal, bufSub, turbColor);

  // Card 3: Temperature (Interactive Quick-Adjust)
  snprintf(bufVal, sizeof(bufVal), "%.1f C", telemetry.tempC);
  snprintf(bufSub, sizeof(bufSub), telemetry.tempConnected ? "Probe OK" : "Tap Cycle");
  renderParameterCard(98, 116, 88, 76, 3, bufVal, bufSub, telemetry.tempConnected ? UI_COLOR_CYAN : UI_COLOR_CAUTION);

  // Right Top Status Badge
  uint16_t statCol = getStatusColor(telemetry.finalStatus);
  tft.fillRoundRect(192, 34, 120, 30, 4, statCol);

  if (currentLang == LANG_EN) {
    const char* statText = (telemetry.finalStatus == 2) ? "CONTAMINATED" : 
                           (telemetry.finalStatus == 1) ? "CAUTION" : "SAFE / POTABLE";
    tft.setTextColor((telemetry.finalStatus == 1) ? UI_COLOR_BG : UI_COLOR_WHITE, statCol);
    tft.setTextSize(1);
    tft.setCursor(196, 44);
    tft.print(statText);
  } else {
    uint16_t txtCol = (telemetry.finalStatus == 1) ? UI_COLOR_BG : UI_COLOR_WHITE;
    if (telemetry.finalStatus == 2) {
      tft.drawBitmap(218, 41, BMP_STATUS_DUSHIT, BMP_STATUS_DUSHIT_W, BMP_STATUS_DUSHIT_H, txtCol, statCol);
    } else if (telemetry.finalStatus == 1) {
      if (currentLang == LANG_HI)
        tft.drawBitmap(228, 43, BMP_STATUS_CAUTION_HI, BMP_STATUS_CAUTION_HI_W, BMP_STATUS_CAUTION_HI_H, txtCol, statCol);
      else
        tft.drawBitmap(232, 43, BMP_STATUS_CAUTION_MR, BMP_STATUS_CAUTION_MR_W, BMP_STATUS_CAUTION_MR_H, txtCol, statCol);
    } else {
      tft.drawBitmap(210, 41, BMP_STATUS_SAFE, BMP_STATUS_SAFE_W, BMP_STATUS_SAFE_H, txtCol, statCol);
    }
  }

  // Animated Waveform Sprite
  renderGraphSprite(telemetry);

  // Status Subtext below graph
  tft.fillRect(192, 162, 120, 30, UI_COLOR_CARD);
  tft.drawRect(192, 162, 120, 30, UI_COLOR_CARD_BORDER);
  tft.setTextColor(telemetry.failSafeTriggered ? UI_COLOR_ALERT : UI_COLOR_GRAY, UI_COLOR_CARD);
  tft.setTextSize(1);
  tft.setCursor(196, 168);
  tft.print(telemetry.failSafeTriggered ? "IS10500 ALERT" : "IS10500 PASS");
  tft.setCursor(196, 178);
  tft.printf("ML: %s", (telemetry.predictedClass == 0) ? "SAFE" : (telemetry.predictedClass == 1) ? "WARN" : "HAZ");
}

// ============================================================================
// SCREEN 2: RAW VOLTAGES, CALIBRATION & HARDWARE SETTINGS VIEW
// ============================================================================
static void renderRawView(const WaterTelemetry &telemetry) {
  tft.fillRoundRect(6, 34, 308, 158, 4, UI_COLOR_CARD);
  tft.drawRoundRect(6, 34, 308, 158, 4, UI_COLOR_CYAN);

  tft.setTextColor(UI_COLOR_CYAN, UI_COLOR_CARD);
  tft.setTextSize(1);
  tft.setCursor(14, 40);
  tft.print("HARDWARE VOLTAGES & PERSISTED SETTINGS");
  tft.drawFastHLine(14, 50, 290, UI_COLOR_CARD_BORDER);

  tft.setTextColor(UI_COLOR_WHITE, UI_COLOR_CARD);
  tft.setCursor(14, 55);
  tft.printf("TDS Sensor (GPIO 34):      %.3f V  -->  %.0f ppm", telemetry.rawTdsV, telemetry.tdsPpm);

  tft.setCursor(14, 67);
  tft.printf("Turbidity TS-300B (VP/36):  %.3f V  -->  %.1f NTU %s", 
             telemetry.rawTurbV, telemetry.turbNTU, telemetry.turbStandby ? "[STBY]" : "");

  tft.setCursor(14, 79);
  tft.printf("pH Probe (GPIO 35):        %.3f V  -->  %.2f pH %s", 
             telemetry.rawPhV, telemetry.pH, telemetry.phStandby ? "[STBY]" : "");

  float compFactor = 1.0f + 0.02f * (telemetry.tempC - 25.0f);
  tft.setCursor(14, 91);
  tft.printf("DS18B20 Temp (GPIO 4):     %.2f C  (Comp: x%.3f)", telemetry.tempC, compFactor);

  tft.setCursor(14, 103);
  if (telemetry.batConnected) {
    tft.printf("18650 Battery (VN/39):     %.2f V (%.0f%%) [Pin: %.2fV]", 
               telemetry.batVolts, telemetry.batPercent, telemetry.rawBatPinV);
  } else {
    tft.printf("18650 Battery (VN/39):     USB POWER DETECTED [Pin: %.2fV]", telemetry.rawBatPinV);
  }

  // Settings Buttons inside RAW view
  // 1. Backlight Setting Button (x: 14, y: 116, w: 138, h: 22)
  tft.fillRoundRect(14, 116, 138, 22, 3, UI_COLOR_BG);
  tft.drawRoundRect(14, 116, 138, 22, 3, UI_COLOR_CYAN);
  tft.setTextColor(UI_COLOR_CYAN, UI_COLOR_BG);
  tft.setCursor(20, 122);
  tft.printf("BRT: %s (Tap)", (currentBrt == BRIGHTNESS_LOW) ? "LOW (30%)" : (currentBrt == BRIGHTNESS_MED) ? "MED (65%)" : "HIGH (100%)");

  // 2. Language Setting Button (x: 160, y: 116, w: 144, h: 22)
  tft.fillRoundRect(160, 116, 144, 22, 3, UI_COLOR_BG);
  tft.drawRoundRect(160, 116, 144, 22, 3, UI_COLOR_CYAN);
  tft.setTextColor(UI_COLOR_CYAN, UI_COLOR_BG);
  tft.setCursor(166, 122);
  tft.printf("LANG: %s (Tap)", (currentLang == LANG_EN) ? "ENGLISH" : (currentLang == LANG_HI) ? "HINDI" : "MARATHI");

  // 3. Dedicated Recalibrate Touch Button (x: 14 to 196, y: 142 to 164)
  tft.fillRoundRect(14, 142, 182, 22, 3, UI_COLOR_BG);
  tft.drawRoundRect(14, 142, 182, 22, 3, UI_COLOR_ALERT);
  tft.setTextColor(UI_COLOR_ALERT, UI_COLOR_BG);
  tft.setCursor(20, 148);
  tft.print("RECAL TOUCH (Tap)");

  // 4. Audio Alert Mute Toggle Button (x: 202 to 304, y: 142 to 164)
  bool muted = audio_is_muted();
  tft.fillRoundRect(202, 142, 102, 22, 3, UI_COLOR_BG);
  tft.drawRoundRect(202, 142, 102, 22, 3, muted ? UI_COLOR_ALERT : UI_COLOR_CYAN);
  tft.setTextColor(muted ? UI_COLOR_ALERT : UI_COLOR_CYAN, UI_COLOR_BG);
  tft.setCursor(210, 148);
  tft.printf("AUD: %s", muted ? "MUTE" : "ON (Tap)");

  // 5. Calibration Offsets & Reset Button (x: 14 to 304, y: 168 to 188)
  tft.setTextColor(UI_COLOR_SAFE, UI_COLOR_CARD);
  tft.setCursor(14, 173);
  tft.printf("Offsets: TDS=%.0f | Turb=%.1f", telemetry.tdsOffset, telemetry.turbOffset);

  // [ RESET ] touch button in RAW view
  tft.fillRoundRect(232, 168, 72, 20, 3, UI_COLOR_BG);
  tft.drawRoundRect(232, 168, 72, 20, 3, UI_COLOR_ALERT);
  tft.setTextColor(UI_COLOR_ALERT, UI_COLOR_BG);
  tft.setTextSize(1);
  tft.setCursor(244, 174);
  tft.print("RESET");
}

// ============================================================================
// SCREEN 3: TINYML MODEL DIAGNOSTICS & RISK GAUGE
// ============================================================================
static void renderTinyMLView(const WaterTelemetry &telemetry) {
  tft.fillRoundRect(6, 34, 308, 158, 4, UI_COLOR_CARD);
  tft.drawRoundRect(6, 34, 308, 158, 4, UI_COLOR_CYAN);

  tft.setTextColor(UI_COLOR_CYAN, UI_COLOR_CARD);
  tft.setTextSize(1);
  tft.setCursor(14, 40);
  tft.print("TINYML EDGE NEURAL NETWORK & RISK GAUGE");
  tft.drawFastHLine(14, 52, 290, UI_COLOR_CARD_BORDER);

  tft.setTextColor(UI_COLOR_WHITE, UI_COLOR_CARD);
  tft.setCursor(14, 60);
  tft.printf("Overall Contamination Risk: %.1f%%", telemetry.probContaminated * 100.0f);

  int gaugeX = 14;
  int gaugeY = 74;
  int gaugeW = 290;
  int gaugeH = 12;
  tft.fillRect(gaugeX, gaugeY, gaugeW, gaugeH, UI_COLOR_DARK_GRAY);
  
  int fillW = (int)(telemetry.probContaminated * gaugeW);
  if (fillW > gaugeW) fillW = gaugeW;
  uint16_t gaugeCol = (telemetry.probContaminated > 0.5f) ? UI_COLOR_ALERT : 
                      (telemetry.probCaution > 0.4f) ? UI_COLOR_CAUTION : UI_COLOR_SAFE;
  tft.fillRect(gaugeX, gaugeY, fillW, gaugeH, gaugeCol);
  tft.drawRect(gaugeX, gaugeY, gaugeW, gaugeH, UI_COLOR_WHITE);

  int barX = 160;
  int barMaxW = 135;

  // SAFE Bar
  tft.setCursor(14, 96);
  tft.setTextColor(UI_COLOR_SAFE, UI_COLOR_CARD);
  tft.printf("CLASS 0 (SAFE):    %4.1f%%", telemetry.probSafe * 100.0f);
  tft.fillRect(barX, 96, barMaxW, 8, UI_COLOR_DARK_GRAY);
  tft.fillRect(barX, 96, (int)(telemetry.probSafe * barMaxW), 8, UI_COLOR_SAFE);

  // CAUTION Bar
  tft.setCursor(14, 112);
  tft.setTextColor(UI_COLOR_CAUTION, UI_COLOR_CARD);
  tft.printf("CLASS 1 (CAUTION): %4.1f%%", telemetry.probCaution * 100.0f);
  tft.fillRect(barX, 112, barMaxW, 8, UI_COLOR_DARK_GRAY);
  tft.fillRect(barX, 112, (int)(telemetry.probCaution * barMaxW), 8, UI_COLOR_CAUTION);

  // CONTAMINATED Bar
  tft.setCursor(14, 128);
  tft.setTextColor(UI_COLOR_ALERT, UI_COLOR_CARD);
  tft.printf("CLASS 2 (CONTAM):  %4.1f%%", telemetry.probContaminated * 100.0f);
  tft.fillRect(barX, 128, barMaxW, 8, UI_COLOR_DARK_GRAY);
  tft.fillRect(barX, 128, (int)(telemetry.probContaminated * barMaxW), 8, UI_COLOR_ALERT);

  tft.setTextColor(UI_COLOR_WHITE, UI_COLOR_CARD);
  tft.setCursor(14, 148);
  tft.printf("Latency: %lu us (%.2f ms) | Pred: %s", 
             (unsigned long)telemetry.inferenceTimeUs, 
             telemetry.inferenceTimeUs / 1000.0f,
             (telemetry.predictedClass == 0) ? "SAFE" : (telemetry.predictedClass == 1) ? "CAUTION" : "CONTAM");

  tft.setCursor(14, 162);
  tft.setTextColor(telemetry.failSafeTriggered ? UI_COLOR_ALERT : UI_COLOR_SAFE, UI_COLOR_CARD);
  tft.printf("IS 10500 Guardrail: %s", telemetry.failSafeTriggered ? "BREACH DETECTED" : "ALL METRICS COMPLIANT");
}

// ============================================================================
// BOTTOM INTERACTIVE TOUCH BAR (4 Buttons: y = 202 to 238)
// ============================================================================
static void renderTouchBar(const WaterTelemetry &telemetry) {
  int y = 202;
  int h = 34;

  // Button 1: [ LIVE / SIM ] (x: 4, w: 74)
  int b1_x = 4;
  int b1_w = 74;
  uint16_t b1_bg = telemetry.isSimSpike ? UI_COLOR_DARK_ALERT : UI_COLOR_CARD;
  uint16_t b1_border = telemetry.isSimSpike ? UI_COLOR_ALERT : UI_COLOR_CYAN;
  tft.fillRoundRect(b1_x, y, b1_w, h, 4, b1_bg);
  tft.drawRoundRect(b1_x, y, b1_w, h, 4, b1_border);
  tft.setTextColor(telemetry.isSimSpike ? UI_COLOR_WHITE : UI_COLOR_CYAN, b1_bg);
  tft.setTextSize(1);
  tft.setCursor(b1_x + 10, y + 13);
  tft.print(telemetry.isSimSpike ? "SIM ON" : "LIVE/SIM");

  // Button 2: [ VIEW ] (x: 82, w: 76)
  int b2_x = 82;
  int b2_w = 76;
  tft.fillRoundRect(b2_x, y, b2_w, h, 4, UI_COLOR_CARD);
  tft.drawRoundRect(b2_x, y, b2_w, h, 4, UI_COLOR_CYAN);
  tft.setTextColor(UI_COLOR_WHITE, UI_COLOR_CARD);
  tft.setTextSize(1);
  tft.setCursor(b2_x + 8, y + 13);
  const char* viewLabel = (currentView == VIEW_DASHBOARD) ? "VW:DASH" : 
                          (currentView == VIEW_RAW_DIAGNOSTICS) ? "VW:RAW" : "VW:ML";
  tft.print(viewLabel);

  // Button 3: [ CAL / ZERO ] (x: 162, w: 76)
  int b3_x = 162;
  int b3_w = 76;
  tft.fillRoundRect(b3_x, y, b3_w, h, 4, UI_COLOR_CARD);
  tft.drawRoundRect(b3_x, y, b3_w, h, 4, UI_COLOR_CYAN);
  tft.setTextColor(UI_COLOR_CYAN, UI_COLOR_CARD);
  tft.setTextSize(1);
  tft.setCursor(b3_x + 12, y + 13);
  tft.print("CAL/ZERO");

  // Button 4: [ LANG: EN / HI / MR ] (x: 242, w: 74)
  int b4_x = 242;
  int b4_w = 74;
  tft.fillRoundRect(b4_x, y, b4_w, h, 4, UI_COLOR_CARD);
  tft.drawRoundRect(b4_x, y, b4_w, h, 4, UI_COLOR_CYAN);
  tft.setTextColor(UI_COLOR_WHITE, UI_COLOR_CARD);
  tft.setTextSize(1);
  tft.setCursor(b4_x + 8, y + 13);
  tft.print((currentLang == LANG_EN) ? "LNG:EN" : (currentLang == LANG_HI) ? "LNG:HI" : "LNG:MR");
}

// ============================================================================
// MAIN UI RENDER LOOP
// ============================================================================
void ui_render(const WaterTelemetry &telemetry) {
  // Service remote thread-safe UI actions dispatched from Core 0
  if (g_reqLangCycle) {
    g_reqLangCycle = false;
    ui_cycle_language();
  }
  if (g_reqBrtCycle) {
    g_reqBrtCycle = false;
    ui_cycle_brightness();
  }
  if (g_uiForceRedraw) {
    g_uiForceRedraw = false;
    lastView = (ScreenView)-1;
  }

  if (currentView != lastView) {
    tft.fillScreen(UI_COLOR_BG);
    lastView = currentView;
  }

  renderHeader(telemetry);

  switch (currentView) {
    case VIEW_DASHBOARD:
      renderDashboardView(telemetry);
      break;
    case VIEW_RAW_DIAGNOSTICS:
      renderRawView(telemetry);
      break;
    case VIEW_TINYML_METRICS:
      renderTinyMLView(telemetry);
      break;
  }

  renderTouchBar(telemetry);

  // Render Confirmation Banner Overlay if active
  if (millis() < bannerActiveUntil) {
    if (currentBanner == BANNER_OFFSETS_RESET) {
      tft.fillRoundRect(35, 92, 250, 48, 6, UI_COLOR_ALERT);
      tft.drawRoundRect(35, 92, 250, 48, 6, UI_COLOR_WHITE);
      tft.setTextColor(UI_COLOR_WHITE, UI_COLOR_ALERT);
      tft.setTextSize(2);
      tft.setCursor(55, 98);
      tft.print("OFFSETS RESET!");
      tft.setTextSize(1);
      tft.setCursor(55, 122);
      tft.print("Offsets Cleared: TDS=0 | Turb=0");
    } else {
      tft.fillRoundRect(35, 92, 250, 48, 6, UI_COLOR_SAFE);
      tft.drawRoundRect(35, 92, 250, 48, 6, UI_COLOR_WHITE);
      tft.setTextColor(UI_COLOR_BG, UI_COLOR_SAFE);
      tft.setTextSize(2);
      tft.setCursor(55, 98);
      tft.print("BASELINE SAVED!");
      tft.setTextSize(1);
      tft.setCursor(55, 122);
      tft.printf("Offsets Stored: TDS=%.0f | Turb=%.1f", telemetry.tdsOffset, telemetry.turbOffset);
    }
  }
}

// ============================================================================
// EDGE-TRIGGERED TOUCH HANDLER WITH CLEAN BUTTON INVERT/HIGHLIGHT FEEDBACK
// (Zero tap artifacts, lower threshold 220, edge-triggered debounce)
// ============================================================================
void ui_handle_touch(WaterTelemetry &telemetry, bool &simSpikeActive) {
  uint16_t tx = 0, ty = 0;
  // Lower pressure threshold (220) to capture light finger touches reliably
  bool touched = tft.getTouch(&tx, &ty, 220);

  if (touched) {
    // Reject out-of-bounds readings immediately
    if (tx >= 320 || ty >= 240) {
      calPressActive = false;
      return;
    }

    uint32_t now = millis();

    // Check if user is holding Button 3 (CAL / ZERO) for long-press offset reset (> 1.5 sec)
    if (ty >= 195 && ty <= 240 && tx >= 162 && tx <= 238) {
      if (!calPressActive) {
        calPressActive = true;
        calPressStartMs = now;
      } else if (now - calPressStartMs >= 1500) {
        calPressActive = false;
        isTouching = true;
        sensors_reset_offsets(telemetry);
        currentBanner = BANNER_OFFSETS_RESET;
        bannerActiveUntil = millis() + 1800;
        tft.fillRoundRect(162, 202, 76, 34, 4, UI_COLOR_ALERT);
        tft.setTextColor(UI_COLOR_WHITE, UI_COLOR_ALERT);
        tft.setTextSize(1);
        tft.setCursor(174, 215);
        tft.print("RESET!");
        delay(100);
        renderTouchBar(telemetry);
        return;
      }
    } else {
      calPressActive = false;
    }

    // Edge-triggered debounce: execute once on rising edge, ignore hold
    if (isTouching) {
      return;
    }
    if (now - lastTouchMs < 250) {
      return;
    }
    isTouching = true;
    lastTouchMs = now;

    Serial.printf("[TOUCH] Button Event at X=%d, Y=%d\n", tx, ty);

    // ------------------------------------------------------------------------
    // A. Header Bar Touches (y: 0 to 28)
    // ------------------------------------------------------------------------
    if (ty <= 28) {
      // Tap Brightness Badge (x: 142 to 194)
      if (tx >= 142 && tx <= 194) {
        tft.fillRoundRect(144, 5, 48, 18, 3, UI_COLOR_WHITE);
        tft.setTextColor(UI_COLOR_BG, UI_COLOR_WHITE);
        tft.setTextSize(1);
        tft.setCursor(150, 10);
        tft.print("BRT");
        delay(80);
        ui_cycle_brightness();
        renderHeader(telemetry);
        return;
      }
      // Tap Language Badge (x: 195 to 254)
      else if (tx >= 195 && tx <= 254) {
        tft.fillRoundRect(196, 5, 56, 18, 3, UI_COLOR_WHITE);
        tft.setTextColor(UI_COLOR_BG, UI_COLOR_WHITE);
        tft.setTextSize(1);
        tft.setCursor(202, 10);
        tft.print("LANG");
        delay(80);
        ui_cycle_language();
        return;
      }
      // Tap Mode Badge (x: 255 to 318)
      else if (tx >= 255 && tx <= 318) {
        simSpikeActive = !simSpikeActive;
        tft.fillRoundRect(256, 5, 60, 18, 3, UI_COLOR_WHITE);
        tft.setTextColor(UI_COLOR_BG, UI_COLOR_WHITE);
        tft.setTextSize(1);
        tft.setCursor(266, 10);
        tft.print("MODE");
        delay(80);
        renderHeader(telemetry);
        return;
      }
    }

    // ------------------------------------------------------------------------
    // B. Settings View Touches (VIEW_RAW_DIAGNOSTICS: y 114 to 175)
    // ------------------------------------------------------------------------
    if (currentView == VIEW_RAW_DIAGNOSTICS && ty >= 114 && ty <= 175) {
      // Brightness Row (x: 14 to 152)
      if (tx >= 14 && tx <= 152 && ty <= 138) {
        tft.fillRoundRect(14, 116, 138, 22, 3, UI_COLOR_CYAN);
        tft.setTextColor(UI_COLOR_BG, UI_COLOR_CYAN);
        tft.setCursor(20, 122);
        tft.print("CYCLING BRT...");
        delay(80);
        ui_cycle_brightness();
        renderRawView(telemetry);
        return;
      } 
      // Language Row (x: 160 to 304)
      else if (tx >= 160 && tx <= 304 && ty <= 138) {
        tft.fillRoundRect(160, 116, 144, 22, 3, UI_COLOR_CYAN);
        tft.setTextColor(UI_COLOR_BG, UI_COLOR_CYAN);
        tft.setCursor(166, 122);
        tft.print("CYCLING LANG...");
        delay(80);
        ui_cycle_language();
        return;
      }
      // Recalibrate Touch Button (x: 14 to 196, y: 140 to 166)
      else if (tx >= 14 && tx <= 196 && ty >= 140 && ty <= 166) {
        tft.fillRoundRect(14, 142, 182, 22, 3, UI_COLOR_ALERT);
        tft.setTextColor(UI_COLOR_WHITE, UI_COLOR_ALERT);
        tft.setCursor(20, 148);
        tft.print("CALIBRATING...");
        delay(120);
        ui_run_touch_calibration();
        return;
      }
      // Audio Alert Mute Toggle Button (x: 200 to 306, y: 140 to 166)
      else if (tx >= 200 && tx <= 306 && ty >= 140 && ty <= 166) {
        audio_toggle_mute();
        renderRawView(telemetry);
        return;
      }
      // Reset Offsets Button in RAW view (x: 230 to 306, y: 166 to 190)
      else if (tx >= 230 && tx <= 306 && ty >= 166 && ty <= 190) {
        sensors_reset_offsets(telemetry);
        currentBanner = BANNER_OFFSETS_RESET;
        bannerActiveUntil = millis() + 1500;
        tft.fillRoundRect(232, 168, 72, 20, 3, UI_COLOR_ALERT);
        tft.setTextColor(UI_COLOR_WHITE, UI_COLOR_ALERT);
        tft.setTextSize(1);
        tft.setCursor(244, 174);
        tft.print("RESET!");
        delay(90);
        renderRawView(telemetry);
        return;
      }
    }

    // ------------------------------------------------------------------------
    // C. Bottom Button Bar Touches (y: 195 to 240)
    // ------------------------------------------------------------------------
    if (ty >= 195 && ty <= 240) {
      // Button 1: Toggle Live / Sim Spike (x: 4 to 78)
      if (tx >= 4 && tx <= 78) {
        simSpikeActive = !simSpikeActive;
        tft.fillRoundRect(4, 202, 74, 34, 4, UI_COLOR_WHITE);
        tft.setTextColor(UI_COLOR_BG, UI_COLOR_WHITE);
        tft.setTextSize(1);
        tft.setCursor(14, 215);
        tft.print(simSpikeActive ? "SIM ON" : "LIVE ON");
        delay(90);
        renderTouchBar(telemetry);
        renderHeader(telemetry);
      } 
      // Button 2: Cycle Views (x: 82 to 158)
      else if (tx >= 82 && tx <= 158) {
        int next = (int)currentView + 1;
        if (next > VIEW_TINYML_METRICS) next = VIEW_DASHBOARD;
        currentView = (ScreenView)next;
        tft.fillRoundRect(82, 202, 76, 34, 4, UI_COLOR_CYAN);
        tft.setTextColor(UI_COLOR_BG, UI_COLOR_CYAN);
        tft.setTextSize(1);
        tft.setCursor(94, 215);
        tft.print("SWITCH");
        delay(90);
        tft.fillScreen(UI_COLOR_BG);
        lastView = (ScreenView)-1;
      } 
      // Button 3: Calibrate Zero (Short tap) (x: 162 to 238)
      else if (tx >= 162 && tx <= 238) {
        sensors_calibrate_zero(telemetry);
        currentBanner = BANNER_BASELINE_SAVED;
        bannerActiveUntil = millis() + 1500;
        tft.fillRoundRect(162, 202, 76, 34, 4, UI_COLOR_SAFE);
        tft.setTextColor(UI_COLOR_BG, UI_COLOR_SAFE);
        tft.setTextSize(1);
        tft.setCursor(176, 215);
        tft.print("SAVED!");
        delay(90);
        renderTouchBar(telemetry);
      }
      // Button 4: Cycle Language (x: 242 to 318)
      else if (tx >= 242 && tx <= 318) {
        tft.fillRoundRect(242, 202, 74, 34, 4, UI_COLOR_WHITE);
        tft.setTextColor(UI_COLOR_BG, UI_COLOR_WHITE);
        tft.setTextSize(1);
        tft.setCursor(252, 215);
        tft.print("LANG");
        delay(90);
        ui_cycle_language();
      }
    }
    // ------------------------------------------------------------------------
    // D. Interactive Parameter Card 3: Quick-Adjust Temp (x: 98..188, y: 116..194)
    // ------------------------------------------------------------------------
    else if (currentView == VIEW_DASHBOARD && tx >= 98 && tx <= 188 && ty >= 116 && ty <= 194) {
      if (!telemetry.tempConnected) {
        sensors_cycle_sim_temp();
        tft.fillRoundRect(98, 116, 88, 76, 4, UI_COLOR_CYAN);
        tft.setTextColor(UI_COLOR_BG, UI_COLOR_CYAN);
        tft.setTextSize(2);
        tft.setCursor(106, 145);
        tft.printf("->%.0fC", sensors_get_sim_temp());
        delay(100);
        Serial.printf("[UI] Quick-adjust Temp cycled to %.1f C\n", sensors_get_sim_temp());
      }
    }
  } else {
    // Touch released: reset state flags
    isTouching = false;
    calPressActive = false;
  }
}

ScreenView ui_get_current_view() {
  return currentView;
}
