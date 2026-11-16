#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <Preferences.h>

#include "sensors.h"
#include "tinyml_engine.h"
#include "ui_manager.h"
#include "status_leds.h"
#include "audio_alerts.h"
#include "web_dashboard.h"

// ============================================================================
// FREERTOS THREAD-SAFE SHARED MEMORY & SYNCHRONIZATION
// ============================================================================
static WaterTelemetry g_sharedTelemetry;
static SemaphoreHandle_t g_telemetryMutex = NULL;
static bool g_simSpikeActive = false;

// Task Handles
static TaskHandle_t sensorMLTaskHandle = NULL;

// ============================================================================
// REMOTE WEB CONTROL CALLBACK (Dispatched from Web Dashboard on Port 80)
// ============================================================================
static void handleWebControlAction(const String &action) {
  Serial.printf("[WEB CONTROL] Action dispatched: %s\n", action.c_str());
  if (action == "sim_toggle") {
    g_simSpikeActive = !g_simSpikeActive;
    ui_request_force_redraw();
    Serial.printf("[WEB] Toggled Simulation Mode: %s\n", g_simSpikeActive ? "SIM SPIKE" : "LIVE SENSOR");
  } else if (action == "lang_cycle") {
    ui_request_language_cycle();
  } else if (action == "brt_cycle") {
    ui_request_brightness_cycle();
  } else if (action == "audio_mute_toggle") {
    audio_toggle_mute();
    ui_request_force_redraw();
  } else if (action == "audio_test") {
    audio_play_test();
  } else if (action == "cal_zero") {
    if (xSemaphoreTake(g_telemetryMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
      sensors_calibrate_zero(g_sharedTelemetry);
      xSemaphoreGive(g_telemetryMutex);
    }
    ui_request_force_redraw();
  } else if (action == "cal_reset") {
    if (xSemaphoreTake(g_telemetryMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
      sensors_reset_offsets(g_sharedTelemetry);
      xSemaphoreGive(g_telemetryMutex);
    }
    ui_request_force_redraw();
  }
}

// ============================================================================
// CORE 0 TASK: 20ms BASE SCHEDULER (Web Server, Audio Engine, Sensor/ML)
// ============================================================================
void sensorTinyMLTask(void *pvParameters) {
  WaterTelemetry localTelemetry;
  memset(&localTelemetry, 0, sizeof(WaterTelemetry));

  TickType_t xLastWakeTime = xTaskGetTickCount();
  const TickType_t xBaseTick = pdMS_TO_TICKS(20); // 20 ms fast base tick for Web & Audio

  uint32_t tickCounter = 0;

  Serial.println("[CORE 0] Fast 20ms Web Scheduler & 500ms Sensor/ML Task started.");

  for (;;) {
    // 1. Instant 20ms Web Server & DNS Captive Portal Handler
    web_dashboard_tick();

    // 2. Audio Housekeeping Hook
    audio_tick();

    // 3. Sensor & TinyML Pipeline every 25th tick (25 * 20 ms = 500 ms / 2 Hz)
    tickCounter++;
    if (tickCounter >= 25) {
      tickCounter = 0;
      bool simActive = g_simSpikeActive;

      // Sample ADC1 Channels & 1-Wire DS18B20 with 32-sample noise rejection
      sensors_read(localTelemetry, simActive);

      // Run Edge AI Neural Network Inference & IS 10500 Guardrail Verification
      tinyml_infer(localTelemetry);

      // Update Asynchronous Audio State Engine (Confirmation Chimes & Alarms)
      audio_update_state(localTelemetry.finalStatus);

      // Low Battery Alert Chime (triggered once upon crossing < 15% threshold)
      if (localTelemetry.lowBatAlertTriggered) {
        audio_play_low_battery();
      }

      // Update Web Dashboard Telemetry Snapshot
      web_dashboard_update_telemetry(localTelemetry, simActive);

      // Thread-Safe Mutex Lock to Update Shared State for Core 1 (UI & Status LEDs)
      if (xSemaphoreTake(g_telemetryMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        memcpy(&g_sharedTelemetry, &localTelemetry, sizeof(WaterTelemetry));
        xSemaphoreGive(g_telemetryMutex);
      }
    }

    vTaskDelayUntil(&xLastWakeTime, xBaseTick);
  }
}

// ============================================================================
// SYSTEM SETUP
// ============================================================================
void setup() {
  // 1. Explicitly initialize Status LED GPIOs as OUTPUT at the very start of setup()
  pinMode(PIN_LED_GREEN, OUTPUT); // GPIO 33 (Green LED: SAFE / Drinkable)
  pinMode(PIN_LED_RED, OUTPUT);   // GPIO 32 (Red LED: CONTAMINATED / Alert)
  pinMode(PIN_LED_BLUE, OUTPUT);  // GPIO 27 (Blue LED: Heartbeat / TinyML / Wi-Fi)

  // 2. Run 1-second Boot Self-Test: Red (32), Green (33), Blue (27) sequentially for 250ms each,
  // then all HIGH together for 250ms so hardware operation is visually confirmed on every boot
  digitalWrite(PIN_LED_RED, HIGH);   digitalWrite(PIN_LED_GREEN, LOW);  digitalWrite(PIN_LED_BLUE, LOW);  delay(250);
  digitalWrite(PIN_LED_RED, LOW);    digitalWrite(PIN_LED_GREEN, HIGH); digitalWrite(PIN_LED_BLUE, LOW);  delay(250);
  digitalWrite(PIN_LED_RED, LOW);    digitalWrite(PIN_LED_GREEN, LOW);  digitalWrite(PIN_LED_BLUE, HIGH); delay(250);
  digitalWrite(PIN_LED_RED, HIGH);   digitalWrite(PIN_LED_GREEN, HIGH); digitalWrite(PIN_LED_BLUE, HIGH); delay(250);
  digitalWrite(PIN_LED_RED, LOW);    digitalWrite(PIN_LED_GREEN, LOW);  digitalWrite(PIN_LED_BLUE, LOW);

  // 3. Configure ADC1 Resolution (12-bit) and Attenuation (11dB)
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_TDS, ADC_11db);        // GPIO 34 (D34 / ADC1_CH6)
  analogSetPinAttenuation(PIN_PH, ADC_11db);         // GPIO 35 (D35 / ADC1_CH7)
  analogSetPinAttenuation(PIN_TURBIDITY, ADC_11db);  // GPIO 36 (VP / ADC1_CH0)
  analogSetPinAttenuation(PIN_BATTERY, ADC_11db);    // GPIO 39 (VN / ADC1_CH3)

  Serial.begin(115200);
  delay(50);
  Serial.println("\n==================================================");
  Serial.println("   JALRAKSHAK // ESP32 EDGE AI WATER MONITOR      ");
  Serial.println("   Dual-Core FreeRTOS + TinyML IS 10500 Guardrail ");
  Serial.println("==================================================");

  // Initialize FreeRTOS Mutex
  g_telemetryMutex = xSemaphoreCreateMutex();
  if (g_telemetryMutex == NULL) {
    Serial.println("[ERROR] Failed to create FreeRTOS Telemetry Mutex!");
    while (1) delay(1000);
  }

  // Initialize Sensor Hardware (ADC1 & 1-Wire)
  sensors_init();
  Serial.println("[OK] Sensors initialized on GPIO 4, 34, 36, 35, 39.");

  // Check & configure dynamic clean-water turbidity baseline ("jal_cal", key "turb_base")
  Preferences calPrefs;
  calPrefs.begin("jal_cal", false);
  float savedTurbBase = 0.0f;
  if (calPrefs.isKey("turb_base")) {
    savedTurbBase = calPrefs.getFloat("turb_base", 0.0f);
  }

  // Sample initial voltage on GPIO 36 (VP)
  float initPin36V = sensors_read_adc_voltage(PIN_TURBIDITY, 32);
  float initTurbV = initPin36V * TURB_DIVIDER_RATIO;

  if (savedTurbBase >= 1.00f && savedTurbBase <= 4.80f) {
    v_turb_clean = savedTurbBase;
    Serial.printf("[NVS] Loaded saved clean turbidity baseline: %.3f V\n", v_turb_clean);
  } else {
    // If no saved NVS value exists (or if initial clean water voltage is in range 1.80f - 4.60f)
    if (initTurbV >= 1.80f && initTurbV <= 4.60f) {
      v_turb_clean = initTurbV;
    } else {
      v_turb_clean = (initTurbV > 0.5f) ? initTurbV : 4.00f;
    }
    calPrefs.putFloat("turb_base", v_turb_clean);
    Serial.printf("[NVS] Initialized dynamic clean turbidity baseline: %.3f V\n", v_turb_clean);
  }
  calPrefs.end();
  sensors_set_turb_clean(v_turb_clean);

  // Initialize TinyML Neural Network Engine
  tinyml_init();
  Serial.println("[OK] TinyML 4->16->12->3 Neural Network Engine ready.");

  // Initialize PAM8403 DAC Audio Alert Engine (Hardware DAC1 on GPIO 25)
  audio_init();
  Serial.println("[OK] PAM8403 Hardware DAC1 Audio Engine ready on GPIO 25.");

  // Initialize Offline SoftAP & Web Dashboard
  web_dashboard_init();
  web_dashboard_set_control_callback(handleWebControlAction);
  Serial.println("[OK] Wi-Fi SoftAP (JalRakshak-Node) & Web Dashboard ready.");

  // Initialize 2.8" ILI9341 SPI TFT + XPT2046 UI
  ui_init();
  Serial.println("[OK] UI Manager & 120x86 Animated Sprite ready.");

  // Spawn Core 0 Sensor + TinyML Task (10KB stack on Core 0)
  xTaskCreatePinnedToCore(
    sensorTinyMLTask,      // Task Function
    "SensorMLTask",        // Task Name
    10240,                 // Stack Size (bytes)
    NULL,                  // Parameter
    1,                     // Priority
    &sensorMLTaskHandle,   // Task Handle
    0                      // Pin to Core 0
  );

  Serial.println("[OK] Core 0 Task pinned. Entering Core 1 UI loop (~30 FPS).");
}

// ============================================================================
// CORE 1 TASK: UI RENDERING, STATUS LEDS & TOUCH EVENT LOOP (~30 FPS / 33ms)
// Arduino loop() automatically executes on Core 1
// ============================================================================
void loop() {
  WaterTelemetry uiSnapshot;
  bool simToggle = g_simSpikeActive;

  // 1. Thread-Safe Mutex Lock to Retrieve Latest Snapshot from Core 0
  if (xSemaphoreTake(g_telemetryMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
    memcpy(&uiSnapshot, &g_sharedTelemetry, sizeof(WaterTelemetry));
    xSemaphoreGive(g_telemetryMutex);
  }

  uint32_t now = millis();

  // 2. Toggle Blue LED (GPIO 27) every 500 ms using a non-blocking millis() timer
  int ledB = ((now / 500) % 2 == 0) ? 1 : 0;
  digitalWrite(PIN_LED_BLUE, ledB ? HIGH : LOW);

  // 3. Status LEDs State Machine (GPIO 33 Green, GPIO 32 Red)
  // Always drive either Green (SAFE / POTABLE) or Red (WARNING / CONTAMINATED).
  // Never leave both Green and Red LOW during normal or standby operation.
  // Blink Red at 4 Hz (125ms HIGH / 125ms LOW) on critical contamination.
  int ledG = 0;
  int ledR = 0;
  if (uiSnapshot.finalStatus == 0) {
    // SAFE / POTABLE -> Green HIGH, Red LOW
    ledG = 1;
    ledR = 0;
    digitalWrite(PIN_LED_GREEN, HIGH);
    digitalWrite(PIN_LED_RED, LOW);
  } else if (uiSnapshot.finalStatus == 2) {
    // CRITICAL CONTAMINATION -> Green LOW, Blink Red at 4 Hz
    ledG = 0;
    ledR = ((now / 125) % 2 == 0) ? 1 : 0;
    digitalWrite(PIN_LED_GREEN, LOW);
    digitalWrite(PIN_LED_RED, ledR ? HIGH : LOW);
  } else {
    // WARNING / CAUTION -> Green LOW, Red HIGH
    ledG = 0;
    ledR = 1;
    digitalWrite(PIN_LED_GREEN, LOW);
    digitalWrite(PIN_LED_RED, HIGH);
  }

  // 4. 1-Second Serial Diagnostic Output (115200 Baud)
  // Prints both live v_turb and clean baseline v_turb_clean
  static uint32_t lastDiagMs = 0;
  if (now - lastDiagMs >= 1000) {
    lastDiagMs = now;
    Serial.printf("[DIAG] Temp: %.1fC | TDS(34): %.2fV (%.0f ppm) | Turb(36): pin=%.2fV->sens=%.2fV (base=%.2fV -> %.1f NTU) | pH(35): pin=%.2fV->sens=%.2fV (%.2f pH) | Bat(39): pin=%.2fV->bat=%.2fV (%.0f%%) | LEDs: R=%d G=%d B=%d\n",
      uiSnapshot.tempC,
      uiSnapshot.pinTdsV,
      uiSnapshot.tdsPpm,
      uiSnapshot.pinTurbV,
      uiSnapshot.rawTurbV,
      uiSnapshot.vTurbClean,
      uiSnapshot.turbNTU,
      uiSnapshot.pinPhV,
      uiSnapshot.rawPhV,
      uiSnapshot.pH,
      uiSnapshot.rawBatPinV,
      uiSnapshot.batVolts,
      uiSnapshot.batPercent,
      ledR, ledG, ledB
    );
  }

  // 5. Poll & Handle Touch Input (Debounced)
  ui_handle_touch(uiSnapshot, simToggle);
  if (simToggle != g_simSpikeActive) {
    g_simSpikeActive = simToggle;
    Serial.printf("[UI] Mode switched: %s\n", g_simSpikeActive ? "SYNTHETIC SPIKE" : "LIVE SENSOR");
  }

  // 6. Render Smooth 30 FPS Industrial Dashboard & Animated Sprite Waveform
  ui_render(uiSnapshot);

  // Maintain ~30 FPS UI refresh rate
  delay(33);
}
