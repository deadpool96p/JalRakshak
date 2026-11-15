#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

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
    Serial.printf("[WEB] Toggled Simulation Mode: %s\n", g_simSpikeActive ? "SIM SPIKE" : "LIVE SENSOR");
  } else if (action == "lang_cycle") {
    ui_cycle_language();
  } else if (action == "brt_cycle") {
    ui_cycle_brightness();
  } else if (action == "audio_mute_toggle") {
    audio_toggle_mute();
  } else if (action == "audio_test") {
    audio_play_test();
  } else if (action == "cal_zero") {
    if (xSemaphoreTake(g_telemetryMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
      sensors_calibrate_zero(g_sharedTelemetry);
      xSemaphoreGive(g_telemetryMutex);
    }
  } else if (action == "cal_reset") {
    if (xSemaphoreTake(g_telemetryMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
      sensors_reset_offsets(g_sharedTelemetry);
      xSemaphoreGive(g_telemetryMutex);
    }
  }
}

// ============================================================================
// CORE 0 TASK: 20ms BASE SCHEDULER (Web Server, Status LEDs, Audio, Sensor/ML)
// ============================================================================
void sensorTinyMLTask(void *pvParameters) {
  WaterTelemetry localTelemetry;
  memset(&localTelemetry, 0, sizeof(WaterTelemetry));

  TickType_t xLastWakeTime = xTaskGetTickCount();
  const TickType_t xBaseTick = pdMS_TO_TICKS(20); // 20 ms fast base tick for Web & LEDs

  uint32_t tickCounter = 0;

  Serial.println("[CORE 0] Fast 20ms Web/LED Scheduler & 500ms Sensor/ML Task started.");

  for (;;) {
    // 1. Instant 20ms Web Server & DNS Captive Portal Handler
    web_dashboard_tick();

    // 2. Instant 20ms Hardware Status LED & Activity Pulse State Machine
    status_leds_tick();

    // 3. Audio Housekeeping Hook
    audio_tick();

    // 4. Sensor & TinyML Pipeline every 25th tick (25 * 20 ms = 500 ms / 2 Hz)
    tickCounter++;
    if (tickCounter >= 25) {
      tickCounter = 0;
      bool simActive = g_simSpikeActive;

      // Sample ADC1 Channels & 1-Wire DS18B20 with 32-sample noise rejection
      sensors_read(localTelemetry, simActive);

      // Run Edge AI Neural Network Inference & IS 10500 Guardrail Verification
      tinyml_infer(localTelemetry);

      // Update Hardware Status LEDs (Green/Red) & Pulse Blue Activity LED
      status_leds_set_status(localTelemetry.finalStatus);
      status_leds_pulse_blue(40);

      // Update Asynchronous Audio State Engine (Confirmation Chimes & Alarms)
      audio_update_state(localTelemetry.finalStatus);

      // Update Web Dashboard Telemetry Snapshot
      web_dashboard_update_telemetry(localTelemetry, simActive);

      // Thread-Safe Mutex Lock to Update Shared State for Core 1 (UI)
      if (xSemaphoreTake(g_telemetryMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        memcpy(&g_sharedTelemetry, &localTelemetry, sizeof(WaterTelemetry));
        xSemaphoreGive(g_telemetryMutex);
      }

      // Log Periodic Telemetry over Serial
      Serial.printf("[TELEMETRY #%lu] pH: %.2f | TDS: %.0f ppm | Turb: %.1f NTU | Temp: %.1f C | Class: %d | Latency: %lu us\n",
                    (unsigned long)localTelemetry.sampleCount,
                    localTelemetry.pH,
                    localTelemetry.tdsPpm,
                    localTelemetry.turbNTU,
                    localTelemetry.tempC,
                    localTelemetry.finalStatus,
                    (unsigned long)localTelemetry.inferenceTimeUs);
    }

    vTaskDelayUntil(&xLastWakeTime, xBaseTick);
  }
}

// ============================================================================
// SYSTEM SETUP
// ============================================================================
void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("\n==================================================");
  Serial.println("   JALRAKSHAK // ESP32 EDGE AI WATER MONITOR      ");
  Serial.println("   Dual-Core FreeRTOS + TinyML IS 10500 Guardrail ");
  Serial.println("==================================================");

  // Initialize Mutex
  g_telemetryMutex = xSemaphoreCreateMutex();
  if (g_telemetryMutex == NULL) {
    Serial.println("[ERROR] Failed to create FreeRTOS Telemetry Mutex!");
    while (1) delay(1000);
  }

  // Initialize Hardware Status LEDs (GPIO 33 Green, GPIO 32 Red, GPIO 27 Blue)
  status_leds_init();
  Serial.println("[OK] Hardware Status LEDs ready on GPIO 33, 32, 27.");

  // Initialize Sensor Hardware (ADC1 & 1-Wire)
  sensors_init();
  Serial.println("[OK] Sensors initialized on GPIO 4, 34, 36, 35.");

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
// CORE 1 TASK: UI RENDERING & TOUCH EVENT LOOP (~30 FPS / 33ms)
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

  // 2. Poll & Handle Touch Input (Debounced)
  ui_handle_touch(uiSnapshot, simToggle);
  if (simToggle != g_simSpikeActive) {
    g_simSpikeActive = simToggle;
    Serial.printf("[UI] Mode switched: %s\n", g_simSpikeActive ? "SYNTHETIC SPIKE" : "LIVE SENSOR");
  }

  // 3. Render Smooth 30 FPS Industrial Dashboard & Animated Sprite Waveform
  ui_render(uiSnapshot);

  // Maintain ~30 FPS UI refresh rate
  delay(33);
}
