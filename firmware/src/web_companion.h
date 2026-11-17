#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include "config.h"

// Extern hooks implemented in main .ino
extern bool sdMounted;
extern int nextFileIndex;
extern volatile uint32_t measuredSampleRate;
extern volatile int g_vuPeak;
extern volatile int g_freq1KHz;
extern volatile int g_micGain;
extern volatile int g_volume;
extern volatile float g_ultraPeak;
extern float g_peakFreqKHz;
extern bool autoModeEnabled;
extern float g_trigThreshold;
extern WavEntry wavList[MAX_WAV_LIST];
extern int wavListCount;

void scanSDWavFiles();
void saveRingBufferToSD(bool triggeredByAuto);
void stopPlayback();
bool streamWavFileToClient(WiFiClient& client, const char* filename, WebServer& srv);
bool deleteWavFileFromSD(const char* filename);

WebServer webServer(80);
bool wifiActive = false;

static const char HTML_DASHBOARD_PROGMEM[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Esperdyne Bat Node</title>
<style>
  body { font-family: system-ui, sans-serif; background:#0d1117; color:#e6edf3; margin:0; padding:16px; }
  h2 { color:#38bdf8; margin:0 0 12px 0; }
  .card { background:#161b22; border:1px solid #30363d; border-radius:10px; padding:14px; margin-bottom:14px; }
  .grid { display:grid; grid-template-columns:1fr 1fr; gap:10px; }
  .stat { background:#0d1117; padding:10px; border-radius:8px; text-align:center; }
  .val { font-size:20px; font-weight:700; color:#58a6ff; }
  .lbl { font-size:11px; color:#8b949e; text-transform:uppercase; }
  .btn { display:inline-block; background:#238636; color:#fff; border:none; padding:10px 14px; border-radius:6px; font-weight:600; text-decoration:none; cursor:pointer; font-size:14px; margin:4px 2px; }
  .btn-blue { background:#1f6feb; }
  .btn-amber { background:#d29922; color:#000; }
  .btn-red { background:#da3633; padding:6px 10px; font-size:12px; }
  .file-row { display:flex; justify-content:space-between; align-items:center; padding:8px 0; border-bottom:1px solid #21262d; }
  .file-row:last-child { border-bottom:none; }
</style>
</head>
<body>
  <h2>&#129415; Esperdyne PAM Node</h2>
  <div class="card">
    <div class="grid">
      <div class="stat"><div class="val" id="pk">-- kHz</div><div class="lbl">Peak Ultrasound</div></div>
      <div class="stat"><div class="val" id="vu">-- %</div><div class="lbl">Live VU Level</div></div>
      <div class="stat"><div class="val" id="sr">-- Hz</div><div class="lbl">ADC Rate</div></div>
      <div class="stat"><div class="val" id="am">--</div><div class="lbl">PAM Trigger</div></div>
    </div>
    <div style="margin-top:12px; text-align:center;">
      <a class="btn" href="/save">&#9210; Save 5s WAV</a>
      <a class="btn btn-amber" href="/toggle_auto">&#127769; Toggle AUTO</a>
      <a class="btn btn-blue" href="/">&#8635; Refresh Files</a>
    </div>
  </div>
  <div class="card">
    <h3 style="margin-top:0;">SD Card Recordings</h3>
    <div id="files">%FILES%</div>
  </div>
<script>
setInterval(async () => {
  try {
    const r = await fetch('/status');
    const d = await r.json();
    document.getElementById('pk').innerText = d.peakKHz > 0 ? d.peakKHz.toFixed(1) + ' kHz' : '-- kHz';
    document.getElementById('vu').innerText = d.vu + '%';
    document.getElementById('sr').innerText = d.sr + ' Hz';
    document.getElementById('am').innerText = d.auto ? 'AUTO ON' : 'MANUAL';
  } catch(e){}
}, 900);
</script>
</body>
</html>
)rawliteral";

static String buildFileRowsHtml() {
  scanSDWavFiles();
  if (!sdMounted) return "<p style='color:#f85149'>No MicroSD Card Detected</p>";
  if (wavListCount == 0) return "<p style='color:#8b949e'>No .WAV recordings yet.</p>";
  String out = "";
  for (int i = 0; i < wavListCount; i++) {
    float mb = (float)wavList[i].sizeBytes / (1024.0f * 1024.0f);
    out += "<div class='file-row'><div><strong>" + String(wavList[i].name) + "</strong><br><small style='color:#8b949e'>" + String(mb, 2) + " MB</small></div>";
    out += "<div><a class='btn btn-blue' style='padding:6px 10px;font-size:12px;' href='/dl?f=" + String(wavList[i].name) + "'>Download</a> ";
    out += "<a class='btn btn-red' href='/del?f=" + String(wavList[i].name) + "' onclick='return confirm(\"Delete?\")'>Del</a></div></div>";
  }
  return out;
}

inline void startWifiCompanion() {
  if (wifiActive) return;
  WiFi.mode(WIFI_AP);
  WiFi.softAP("Esperdyne-BatNode"); // Open AP -> connect & browse 192.168.4.1
  delay(100);

  webServer.on("/", []() {
    String page = FPSTR(HTML_DASHBOARD_PROGMEM);
    page.replace("%FILES%", buildFileRowsHtml());
    webServer.send(200, "text/html", page);
  });

  webServer.on("/status", []() {
    String json = "{\"peakKHz\":" + String(g_peakFreqKHz, 1) +
                  ",\"vu\":" + String(g_vuPeak) +
                  ",\"sr\":" + String(measuredSampleRate) +
                  ",\"auto\":" + String(autoModeEnabled ? "true" : "false") + "}";
    webServer.send(200, "application/json", json);
  });

  webServer.on("/save", []() {
    saveRingBufferToSD(false);
    webServer.sendHeader("Location", "/");
    webServer.send(302, "text/plain", "");
  });

  webServer.on("/toggle_auto", []() {
    autoModeEnabled = !autoModeEnabled;
    webServer.sendHeader("Location", "/");
    webServer.send(302, "text/plain", "");
  });

  webServer.on("/dl", []() {
    if (!webServer.hasArg("f")) { webServer.send(400, "text/plain", "Missing file"); return; }
    String fname = webServer.arg("f");
    if (!fname.startsWith("/")) fname = "/" + fname;
    stopPlayback();
    WiFiClient client = webServer.client();
    if (!streamWavFileToClient(client, fname.c_str(), webServer)) {
      webServer.send(404, "text/plain", "Not Found");
    }
  });

  webServer.on("/del", []() {
    if (webServer.hasArg("f")) {
      String fname = webServer.arg("f");
      if (!fname.startsWith("/")) fname = "/" + fname;
      stopPlayback();
      deleteWavFileFromSD(fname.c_str());
    }
    webServer.sendHeader("Location", "/");
    webServer.send(302, "text/plain", "");
  });

  webServer.begin();
  wifiActive = true;
}

inline void stopWifiCompanion() {
  if (!wifiActive) return;
  webServer.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  wifiActive = false;
}

