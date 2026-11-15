#include <FS.h>
using fs::FS;

#include "web_dashboard.h"
#include "status_leds.h"
#include "audio_alerts.h"
#include "ui_manager.h"
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>

// DNS Server for Captive Portal
static DNSServer dnsServer;
static const byte DNS_PORT = 53;

// WebServer Instance on Port 80
static WebServer server(80);

// Cached Telemetry Snapshot for Web Clients
static WaterTelemetry g_webTelemetry;
static bool g_webSimActive = false;
static WebControlCallback g_controlCallback = NULL;

// ============================================================================
// EMBEDDED SINGLE-PAGE PROGMEM WEB APP (HTML5 + CSS3 + Canvas + JS)
// ============================================================================
static const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>JalRakshak // Edge AI Node</title>
<style>
  :root {
    --bg-dark: #070D1F;
    --card-bg: #0E1A38;
    --card-border: #1E305C;
    --accent-cyan: #38BDF8;
    --safe-green: #10B981;
    --caution-yellow: #F59E0B;
    --alert-red: #EF4444;
    --text-white: #F8FAFC;
    --text-dim: #94A3B8;
  }
  * { box-sizing: border-box; margin: 0; padding: 0; font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; }
  body { background: var(--bg-dark); color: var(--text-white); padding: 14px; min-height: 100vh; }
  .container { max-width: 900px; margin: 0 auto; }
  header { display: flex; justify-content: space-between; align-items: center; padding-bottom: 14px; border-bottom: 1px solid var(--card-border); margin-bottom: 14px; flex-wrap: wrap; gap: 8px; }
  .logo { font-size: 20px; font-weight: 800; color: var(--accent-cyan); letter-spacing: 0.5px; display: flex; align-items: center; gap: 8px; }
  .pulse { width: 10px; height: 10px; border-radius: 50%; background: var(--safe-green); box-shadow: 0 0 10px var(--safe-green); animation: pulse 1.5s infinite; }
  @keyframes pulse { 0% { opacity: 0.4; } 50% { opacity: 1; } 100% { opacity: 0.4; } }
  .net-badge { font-size: 12px; background: var(--card-bg); padding: 4px 10px; border-radius: 6px; border: 1px solid var(--card-border); color: var(--text-dim); }

  .status-hero { border-radius: 10px; padding: 18px; margin-bottom: 14px; display: flex; justify-content: space-between; align-items: center; flex-wrap: wrap; gap: 12px; }
  .hero-safe { background: rgba(16, 185, 129, 0.15); border: 1px solid var(--safe-green); color: var(--safe-green); }
  .hero-caution { background: rgba(245, 158, 11, 0.15); border: 1px solid var(--caution-yellow); color: var(--caution-yellow); }
  .hero-alert { background: rgba(239, 68, 68, 0.18); border: 1px solid var(--alert-red); color: var(--alert-red); }
  .hero-title { font-size: 24px; font-weight: 800; letter-spacing: 0.5px; }
  .hero-sub { font-size: 13px; color: var(--text-white); margin-top: 4px; opacity: 0.9; }

  .grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(200px, 1fr)); gap: 12px; margin-bottom: 14px; }
  .card { background: var(--card-bg); border: 1px solid var(--card-border); border-radius: 8px; padding: 14px; transition: transform 0.15s ease; }
  .card-label { font-size: 11px; text-transform: uppercase; color: var(--text-dim); letter-spacing: 0.5px; display: flex; justify-content: space-between; }
  .card-val { font-size: 26px; font-weight: 700; color: var(--text-white); margin: 6px 0; }
  .card-sub { font-size: 12px; font-weight: 500; }

  .section { background: var(--card-bg); border: 1px solid var(--card-border); border-radius: 8px; padding: 14px; margin-bottom: 14px; }
  .sec-title { font-size: 13px; font-weight: 700; color: var(--accent-cyan); text-transform: uppercase; letter-spacing: 0.5px; margin-bottom: 10px; display: flex; justify-content: space-between; }

  .bar-container { height: 12px; background: #060b17; border-radius: 6px; overflow: hidden; margin: 8px 0; border: 1px solid var(--card-border); }
  .bar-fill { height: 100%; transition: width 0.4s ease, background-color 0.4s ease; width: 0%; }

  .chart-box { width: 100%; height: 180px; position: relative; margin-top: 6px; }
  canvas { width: 100%; height: 100%; background: #060B17; border-radius: 6px; border: 1px solid var(--card-border); }

  .btn-bar { display: grid; grid-template-columns: repeat(auto-fit, minmax(130px, 1fr)); gap: 8px; }
  button { background: #132247; border: 1px solid var(--card-border); color: var(--text-white); padding: 10px; border-radius: 6px; font-size: 12px; font-weight: 600; cursor: pointer; transition: all 0.15s ease; }
  button:hover { background: #1D3268; border-color: var(--accent-cyan); }
  button:active { transform: scale(0.97); }
  .btn-accent { border-color: var(--accent-cyan); color: var(--accent-cyan); }
  .btn-warn { border-color: var(--alert-red); color: var(--alert-red); }
  .btn-safe { border-color: var(--safe-green); color: var(--safe-green); }

  .footer-meta { font-size: 11px; color: var(--text-dim); text-align: center; margin-top: 14px; padding-top: 10px; border-top: 1px solid var(--card-border); }
</style>
</head>
<body>
<div class="container">
  <header>
    <div class="logo"><span class="pulse"></span> JALRAKSHAK // EDGE AI</div>
    <div style="display:flex;gap:8px;align-items:center;flex-wrap:wrap;">
      <div id="batBadge" class="net-badge" style="color:var(--safe-green)">PWR: DETECTING...</div>
      <div class="net-badge">AP: JalRakshak-Node | 192.168.4.1</div>
    </div>
  </header>

  <div id="heroBox" class="status-hero hero-safe">
    <div>
      <div id="heroStatus" class="hero-title">SAFE / POTABLE</div>
      <div id="heroReason" class="hero-sub">All metrics compliant with IS 10500:2012</div>
    </div>
    <div id="heroProb" style="font-size: 24px; font-weight: 800;">Risk: 2%</div>
  </div>

  <div class="grid">
    <div class="card">
      <div class="card-label"><span>pH Level</span><span id="phVolt">2.50V</span></div>
      <div id="valPh" class="card-val">7.24</div>
      <div id="subPh" class="card-sub" style="color:var(--safe-green)">Ideal (6.5-8.5)</div>
    </div>
    <div class="card">
      <div class="card-label"><span>Total Dissolved Solids</span><span id="tdsVolt">0.32V</span></div>
      <div id="valTds" class="card-val">142 <span style="font-size:14px;color:var(--text-dim)">ppm</span></div>
      <div id="subTds" class="card-sub" style="color:var(--safe-green)">Optimal (&lt; 500)</div>
    </div>
    <div class="card">
      <div class="card-label"><span>Turbidity</span><span id="turbVolt">4.05V</span></div>
      <div id="valTurb" class="card-val">0.8 <span style="font-size:14px;color:var(--text-dim)">NTU</span></div>
      <div id="subTurb" class="card-sub" style="color:var(--safe-green)">Clear (&lt; 5.0)</div>
    </div>
    <div class="card">
      <div class="card-label"><span>Water Temp</span><span id="tempStatus">Probe OK</span></div>
      <div id="valTemp" class="card-val">24.8 <span style="font-size:14px;color:var(--text-dim)">&deg;C</span></div>
      <div id="subTemp" class="card-sub" style="color:var(--accent-cyan)">Ref: 25.0&deg;C</div>
    </div>
  </div>

  <div class="section">
    <div class="sec-title">
      <span>TinyML Neural Classifier (4&rarr;16&rarr;12&rarr;3)</span>
      <span id="inferTime" style="color:var(--text-dim)">Latency: 210 &mu;s</span>
    </div>
    <div class="bar-container">
      <div id="riskBar" class="bar-fill" style="width: 2%; background: var(--safe-green);"></div>
    </div>
    <div style="display:flex; justify-content:space-between; font-size:12px; margin-top:6px; color:var(--text-dim)">
      <div>Safe: <b id="probSafe" style="color:var(--safe-green)">95%</b></div>
      <div>Caution: <b id="probCaution" style="color:var(--caution-yellow)">4%</b></div>
      <div>Contaminated: <b id="probContam" style="color:var(--alert-red)">1%</b></div>
    </div>
  </div>

  <div class="section">
    <div class="sec-title">
      <span>Live 60-Second Telemetry Waveform</span>
      <button onclick="downloadCSV()" style="padding:4px 8px;font-size:11px;">Download CSV Log</button>
    </div>
    <div class="chart-box">
      <canvas id="liveChart" width="850" height="180"></canvas>
    </div>
  </div>

  <div class="section">
    <div class="sec-title">Hardware & Mode Controls (Synchronized with TFT)</div>
    <div class="btn-bar">
      <button id="btnMode" onclick="sendAction('sim_toggle')" class="btn-accent">MODE: LIVE</button>
      <button id="btnLang" onclick="sendAction('lang_cycle')">LANG: EN</button>
      <button id="btnBrt" onclick="sendAction('brt_cycle')">BRIGHTNESS: HIGH</button>
      <button id="btnAudio" onclick="sendAction('audio_mute_toggle')">AUDIO: ON</button>
      <button onclick="sendAction('audio_test')">TEST SOUND</button>
      <button onclick="sendAction('cal_zero')" class="btn-safe">CAL / ZERO</button>
      <button onclick="sendAction('cal_reset')" class="btn-warn">RESET OFFSETS</button>
    </div>
  </div>

  <div class="footer-meta">
    JalRakshak Firmware v2.2.0 &bull; Dual-Core FreeRTOS SMP &bull; Bureau of Indian Standards IS 10500:2012 Safe Guardrails
  </div>
</div>

<script>
const historyData = [];
const maxPoints = 60;
const canvas = document.getElementById('liveChart');
const ctx = canvas.getContext('2d');

function sendAction(action) {
  fetch('/api/control?action=' + action, { method: 'POST' })
    .then(r => r.json())
    .then(() => fetchTelemetry());
}

function fetchTelemetry() {
  fetch('/api/telemetry')
    .then(r => r.json())
    .then(data => {
      updateUI(data);
      recordHistory(data);
      renderChart();
    })
    .catch(err => console.log('Poll error:', err));
}

function updateUI(d) {
  document.getElementById('valPh').innerText = d.pH.toFixed(2);
  document.getElementById('phVolt').innerText = d.rawPhV.toFixed(2) + 'V';
  document.getElementById('subPh').innerText = d.phStandby ? 'Standby (Unplugged)' : (d.pH < 6.5 ? 'Acidic' : d.pH > 8.5 ? 'Alkaline' : 'Normal (6.5-8.5)');
  document.getElementById('subPh').style.color = d.phStandby ? 'var(--text-dim)' : (d.pH < 6.5 || d.pH > 8.5 ? 'var(--alert-red)' : 'var(--safe-green)');

  document.getElementById('valTds').innerHTML = Math.round(d.tdsPpm) + ' <span style="font-size:14px;color:var(--text-dim)">ppm</span>';
  document.getElementById('tdsVolt').innerText = d.rawTdsV.toFixed(2) + 'V';
  document.getElementById('subTds').innerText = d.tdsPpm > 500 ? 'High Solids (>500)' : 'Optimal (<500 ppm)';
  document.getElementById('subTds').style.color = d.tdsPpm > 500 ? 'var(--alert-red)' : 'var(--safe-green)';

  document.getElementById('valTurb').innerHTML = d.turbNTU.toFixed(1) + ' <span style="font-size:14px;color:var(--text-dim)">NTU</span>';
  document.getElementById('turbVolt').innerText = d.rawTurbV.toFixed(2) + 'V';
  document.getElementById('subTurb').innerText = d.turbStandby ? 'Standby (Unplugged)' : (d.turbNTU > 5.0 ? 'Turbid (>5 NTU)' : 'Clear (<5 NTU)');
  document.getElementById('subTurb').style.color = d.turbStandby ? 'var(--text-dim)' : (d.turbNTU > 5.0 ? 'var(--alert-red)' : 'var(--safe-green)');

  document.getElementById('valTemp').innerHTML = d.tempC.toFixed(1) + ' <span style="font-size:14px;color:var(--text-dim)">&deg;C</span>';
  document.getElementById('tempStatus').innerText = d.tempConnected ? 'Probe OK' : 'Simulated';

  // Battery status badge
  const batBadge = document.getElementById('batBadge');
  if (batBadge) {
    if (d.batConnected) {
      batBadge.innerText = 'BAT: ' + Math.round(d.batPercent) + '% (' + d.batVolts.toFixed(2) + 'V)';
      batBadge.style.color = d.batPercent <= 15 ? 'var(--alert-red)' : d.batPercent <= 30 ? 'var(--caution-yellow)' : 'var(--safe-green)';
    } else {
      batBadge.innerText = 'PWR: USB (5V)';
      batBadge.style.color = 'var(--accent-cyan)';
    }
  }

  // Hero status box
  const hero = document.getElementById('heroBox');
  const heroTitle = document.getElementById('heroStatus');
  const heroReason = document.getElementById('heroReason');
  const heroProb = document.getElementById('heroProb');
  const riskPct = Math.round(d.probContam * 100);

  heroProb.innerText = 'Risk: ' + riskPct + '%';
  heroReason.innerText = d.alertReason;

  if (d.finalStatus === 2) {
    hero.className = 'status-hero hero-alert';
    heroTitle.innerText = 'CONTAMINATED / HAZARD';
  } else if (d.finalStatus === 1) {
    hero.className = 'status-hero hero-caution';
    heroTitle.innerText = 'CAUTION / DRIFT';
  } else {
    hero.className = 'status-hero hero-safe';
    heroTitle.innerText = 'SAFE / POTABLE';
  }

  // TinyML stats
  document.getElementById('inferTime').innerText = 'Latency: ' + d.inferLatencyUs + ' \u03BCs';
  const riskBar = document.getElementById('riskBar');
  riskBar.style.width = Math.min(riskPct, 100) + '%';
  riskBar.style.backgroundColor = (d.finalStatus === 2) ? 'var(--alert-red)' : (d.finalStatus === 1) ? 'var(--caution-yellow)' : 'var(--safe-green)';

  document.getElementById('probSafe').innerText = Math.round(d.probSafe * 100) + '%';
  document.getElementById('probCaution').innerText = Math.round(d.probCaution * 100) + '%';
  document.getElementById('probContam').innerText = riskPct + '%';

  // Control button labels
  document.getElementById('btnMode').innerText = d.simMode ? 'MODE: SIM SPIKE' : 'MODE: LIVE';
  document.getElementById('btnLang').innerText = 'LANG: ' + (d.lang === 1 ? 'HI' : d.lang === 2 ? 'MR' : 'EN');
  document.getElementById('btnBrt').innerText = 'BRT: ' + (d.brightness === 0 ? 'LOW' : d.brightness === 1 ? 'MED' : 'HIGH');
  document.getElementById('btnAudio').innerText = 'AUDIO: ' + (d.audioMuted ? 'MUTED' : 'ON');
  document.getElementById('btnAudio').style.borderColor = d.audioMuted ? 'var(--alert-red)' : 'var(--accent-cyan)';
}

function recordHistory(d) {
  const ts = new Date().toLocaleTimeString();
  historyData.push({
    time: ts,
    pH: d.pH,
    tds: d.tdsPpm,
    turb: d.turbNTU,
    risk: d.probContam * 100,
    status: d.finalStatus
  });
  if (historyData.length > maxPoints) historyData.shift();
}

function renderChart() {
  const w = canvas.width;
  const h = canvas.height;
  ctx.clearRect(0, 0, w, h);

  // Draw grid
  ctx.strokeStyle = '#15254A';
  ctx.lineWidth = 1;
  for (let y = 30; y < h; y += 35) {
    ctx.beginPath();
    ctx.moveTo(0, y);
    ctx.lineTo(w, y);
    ctx.stroke();
  }

  if (historyData.length < 2) return;

  const step = w / (maxPoints - 1);
  const startX = w - (historyData.length - 1) * step;

  // Plot Risk (0-100%)
  ctx.beginPath();
  ctx.lineWidth = 2.5;
  ctx.strokeStyle = '#EF4444';
  historyData.forEach((pt, i) => {
    const x = startX + i * step;
    const y = h - 10 - (pt.risk / 100) * (h - 25);
    if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
  });
  ctx.stroke();

  // Plot TDS (0-800 ppm mapped to height)
  ctx.beginPath();
  ctx.lineWidth = 1.5;
  ctx.strokeStyle = '#38BDF8';
  historyData.forEach((pt, i) => {
    const x = startX + i * step;
    const y = h - 10 - (Math.min(pt.tds, 800) / 800) * (h - 25);
    if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
  });
  ctx.stroke();

  // Legend
  ctx.fillStyle = '#EF4444'; ctx.fillRect(10, 8, 12, 4);
  ctx.fillStyle = '#94A3B8'; ctx.font = '10px sans-serif'; ctx.fillText('Contam Risk %', 26, 12);
  ctx.fillStyle = '#38BDF8'; ctx.fillRect(130, 8, 12, 4);
  ctx.fillStyle = '#94A3B8'; ctx.fillText('TDS (ppm / 800)', 146, 12);
}

function downloadCSV() {
  let csv = 'Timestamp,pH,TDS_ppm,Turbidity_NTU,Contam_Risk_Pct,Status_Code\n';
  historyData.forEach(r => {
    csv += `${r.time},${r.pH.toFixed(2)},${r.tds.toFixed(1)},${r.turb.toFixed(2)},${r.risk.toFixed(1)},${r.status}\n`;
  });
  const blob = new Blob([csv], { type: 'text/csv' });
  const url = window.URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = 'jalrakshak_telemetry_' + Date.now() + '.csv';
  a.click();
}

setInterval(fetchTelemetry, 1000);
fetchTelemetry();
</script>
</body>
</html>
)rawliteral";

// ============================================================================
// API HANDLERS
// ============================================================================
static void handleRoot() {
  server.send(200, "text/html", INDEX_HTML);
  status_leds_pulse_blue(40);
}

static void handleCaptiveRedirect() {
  server.sendHeader("Location", "http://192.168.4.1/", true);
  server.send(302, "text/plain", "");
}

static void handleApiTelemetry() {
  char json[640];
  snprintf(json, sizeof(json),
    "{\"pH\":%.2f,\"tdsPpm\":%.1f,\"turbNTU\":%.2f,\"tempC\":%.1f,"
    "\"rawTdsV\":%.3f,\"rawTurbV\":%.3f,\"rawPhV\":%.3f,"
    "\"batVolts\":%.2f,\"batPercent\":%.0f,\"batConnected\":%s,\"rawBatPinV\":%.2f,"
    "\"probSafe\":%.3f,\"probCaution\":%.3f,\"probContam\":%.3f,"
    "\"predictedClass\":%d,\"finalStatus\":%d,\"alertReason\":\"%s\","
    "\"inferLatencyUs\":%lu,\"failSafe\":%s,\"turbStandby\":%s,\"phStandby\":%s,"
    "\"simMode\":%s,\"lang\":%d,\"brightness\":%d,\"audioMuted\":%s,"
    "\"tdsOffset\":%.1f,\"turbOffset\":%.2f}",
    g_webTelemetry.pH,
    g_webTelemetry.tdsPpm,
    g_webTelemetry.turbNTU,
    g_webTelemetry.tempC,
    g_webTelemetry.rawTdsV,
    g_webTelemetry.rawTurbV,
    g_webTelemetry.rawPhV,
    g_webTelemetry.batVolts,
    g_webTelemetry.batPercent,
    g_webTelemetry.batConnected ? "true" : "false",
    g_webTelemetry.rawBatPinV,
    g_webTelemetry.probSafe,
    g_webTelemetry.probCaution,
    g_webTelemetry.probContaminated,
    g_webTelemetry.predictedClass,
    g_webTelemetry.finalStatus,
    g_webTelemetry.alertReason,
    (unsigned long)g_webTelemetry.inferenceTimeUs,
    g_webTelemetry.failSafeTriggered ? "true" : "false",
    g_webTelemetry.turbStandby ? "true" : "false",
    g_webTelemetry.phStandby ? "true" : "false",
    g_webSimActive ? "true" : "false",
    (int)ui_get_language(),
    (int)ui_get_brightness(),
    audio_is_muted() ? "true" : "false",
    g_webTelemetry.tdsOffset,
    g_webTelemetry.turbOffset
  );

  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", json);
}

static void handleApiControl() {
  String action = "";
  if (server.hasArg("action")) {
    action = server.arg("action");
  }

  if (g_controlCallback && action.length() > 0) {
    g_controlCallback(action);
  }

  status_leds_double_pulse_blue();

  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", "{\"status\":\"ok\"}");
}

// ============================================================================
// INITIALIZATION & TICK
// ============================================================================
void web_dashboard_init() {
  // 1. Configure SoftAP with 11dBm low RF power to eliminate ADC1 crosstalk
  WiFi.mode(WIFI_AP);
  WiFi.softAP("JalRakshak-Node", "jalrakshak123");
  WiFi.setTxPower(WIFI_POWER_11dBm);

  IPAddress apIP = WiFi.softAPIP();
  Serial.printf("[WIFI AP] SoftAP Started. SSID: JalRakshak-Node | IP: %s\n", apIP.toString().c_str());

  // 2. Start DNS Server for Captive Portal
  dnsServer.start(DNS_PORT, "*", apIP);

  // 3. Start mDNS (http://jalrakshak.local)
  if (MDNS.begin("jalrakshak")) {
    Serial.println("[MDNS] Responder started: http://jalrakshak.local");
    MDNS.addService("http", "tcp", 80);
  }

  // 4. Register Web Endpoints
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/telemetry", HTTP_GET, handleApiTelemetry);
  server.on("/api/control", HTTP_POST, handleApiControl);

  // Captive Portal Detection Endpoints
  server.on("/generate_204", HTTP_GET, handleCaptiveRedirect);
  server.on("/hotspot-detect.html", HTTP_GET, handleCaptiveRedirect);
  server.on("/fwlink", HTTP_GET, handleCaptiveRedirect);
  server.on("/connecttest.txt", HTTP_GET, handleCaptiveRedirect);
  server.onNotFound(handleCaptiveRedirect);

  server.begin();
  Serial.println("[HTTP] Web Dashboard server listening on port 80.");
}

void web_dashboard_tick() {
  dnsServer.processNextRequest();
  server.handleClient();
}

void web_dashboard_update_telemetry(const WaterTelemetry &telemetry, bool simActive) {
  g_webTelemetry = telemetry;
  g_webSimActive = simActive;
}

void web_dashboard_set_control_callback(WebControlCallback cb) {
  g_controlCallback = cb;
}
