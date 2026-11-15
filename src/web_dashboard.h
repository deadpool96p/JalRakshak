#ifndef WEB_DASHBOARD_H
#define WEB_DASHBOARD_H

#include <Arduino.h>
#include "sensors.h"

// ============================================================================
// PUBLIC FUNCTIONS
// ============================================================================
void web_dashboard_init();
void web_dashboard_tick();
void web_dashboard_update_telemetry(const WaterTelemetry &telemetry, bool simActive);

// Callback hook types for remote control from Web UI
typedef void (*WebControlCallback)(const String &action);
void web_dashboard_set_control_callback(WebControlCallback cb);

#endif // WEB_DASHBOARD_H
