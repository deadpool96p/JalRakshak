#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

#include "sensors.h"
#include "tinyml_engine.h"
#include "ui_manager.h"

// ============================================================================
// FREERTOS THREAD-SAFE SHARED MEMORY & SYNCHRONIZATION
// ============================================================================
static WaterTelemetry g_sharedTelemetry;
static SemaphoreHandle_t g_telemetryMutex = NULL;
static bool g_simSpikeActive = false;

// Task Handles
static TaskHandle_t sensorMLTaskHandle = NULL;

// ============================================================================
// CORE 0 TASK: SENSOR SAMPLING, TINYML INFERENCE & IS 10500 SAFETY FUSION
// Runs every 500ms on Core 0
// ============================================================================
void sensorTinyMLTask(void *pvParameters) {
  WaterTelemetry localTelemetry;
  memset(&localTelemetry, 0, sizeof(WaterTelemetry));

  TickType_t xLastWakeTime = xTaskGetTickCount();
  const TickType_t xFrequency = pdMS_TO_TICKS(500); // 500 ms sampling rate (2 Hz)

  Serial.println("[CORE 0] Sensor & TinyML Inference Task started.");

  for (;;) {
    bool simActive = g_simSpikeActive;

    // 1. Sample ADC1 Channels & 1-Wire DS18B20 with 32-sample noise rejection
    sensors_read(localTelemetry, simActive);

    // 2. Run Edge AI Neural Network Inference & IS 10500 Guardrail Verification
    tinyml_infer(localTelemetry);

    // 3. Thread-Safe Mutex Lock to Update Shared State for Core 1
    if (xSemaphoreTake(g_telemetryMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
      // Preserve calibration offsets across loops
      localTelemetry.tdsOffset = g_sharedTelemetry.tdsOffset;
      localTelemetry.turbOffset = g_sharedTelemetry.turbOffset;
      memcpy(&g_sharedTelemetry, &localTelemetry, sizeof(WaterTelemetry));
      xSemaphoreGive(g_telemetryMutex);
    }

    // 4. Log Periodic Telemetry over Serial
    Serial.printf("[TELEMETRY #%lu] pH: %.2f | TDS: %.0f ppm | Turb: %.1f NTU | Temp: %.1f C | Class: %d | Latency: %lu us\n",
                  (unsigned long)localTelemetry.sampleCount,
                  localTelemetry.pH,
                  localTelemetry.tdsPpm,
                  localTelemetry.turbNTU,
                  localTelemetry.tempC,
                  localTelemetry.finalStatus,
                  (unsigned long)localTelemetry.inferenceTimeUs);

    vTaskDelayUntil(&xLastWakeTime, xFrequency);
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

  // Initialize Sensor Hardware (ADC1 & 1-Wire)
  sensors_init();
  Serial.println("[OK] Sensors initialized on GPIO 4, 34, 36, 35.");

  // Initialize TinyML Neural Network Engine
  tinyml_init();
  Serial.println("[OK] TinyML 4->16->12->3 Neural Network Engine ready.");

  // Initialize 2.8" ILI9341 SPI TFT + XPT2046 UI
  ui_init();
  Serial.println("[OK] UI Manager & 120x86 Animated Sprite ready.");

  // Spawn Core 0 Sensor + TinyML Task (8KB stack on Core 0)
  xTaskCreatePinnedToCore(
    sensorTinyMLTask,      // Task Function
    "SensorMLTask",        // Task Name
    8192,                  // Stack Size (bytes)
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
