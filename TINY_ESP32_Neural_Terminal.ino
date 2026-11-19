/* TINY ESP32 Neural Terminal
 * Port of the TNY2 inference format/algorithm from WollyMonjac/Tiny-2.1.
 * Original project: https://github.com/WollyMonjac/Tiny-2.1 (MIT)
 * Target: ESP32-S3 with PSRAM; Arduino-ESP32 core 3.x
 */
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include "src/TinyEngine.h"
#include "src/WebUI.h"

#ifndef BOARD_HAS_PSRAM
#warning "Select an ESP32-S3 board with PSRAM enabled (8 MB recommended)."
#endif

static const char *AP_SSID = "TINY-NEURAL";
static const char *AP_PASS = "tinyterminal"; // Change before deployment.
WebServer server(80);
TinyEngine tiny;

static SemaphoreHandle_t stateMutex;
static volatile bool inferenceRunning = false;
static volatile bool inferenceDone = false;
static volatile int progressPct = 0;
static volatile int generatedChars = 0;
static volatile float layerActivity[8] = {0};
static String currentQuestion, currentAnswer, lastError;
static uint32_t inferenceMs = 0;
static uint32_t inferenceStartedMs = 0;
static int creativity = 5;
static TaskHandle_t inferenceTaskHandle = nullptr;

static void onProgress(int pos, int total) { progressPct = total ? (int)(pos * 100.0f / total) : 0; }
static void onLayer(int layer, float activity) { if (layer >= 0 && layer < 8) layerActivity[layer] = activity; }
static void onCharacter(char c) {
  if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
    currentAnswer += c; generatedChars = currentAnswer.length();
    xSemaphoreGive(stateMutex);
  }
}
static void inferenceTask(void *) {
  uint32_t start = millis();
  char output[129] = {0};
  bool ok = tiny.generate(currentQuestion.c_str(), output, sizeof(output), creativity,
                           onProgress, onCharacter, onLayer);
  inferenceMs = millis() - start;
  if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    if (!ok) lastError = tiny.error();
    inferenceRunning = false; inferenceDone = true; progressPct = 100;
    xSemaphoreGive(stateMutex);
  }
  inferenceTaskHandle = nullptr;
  vTaskDelete(nullptr);
}

static String jsonEscape(const String &input) {
  String out;
  out.reserve(input.length() + 8);
  for (size_t i = 0; i < input.length(); ++i) {
    const char c = input[i];
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if ((uint8_t)c >= 0x20) out += c;
        break;
    }
  }
  return out;
}

static void sendJsonStatus() {
  const uint32_t elapsed = inferenceRunning ? (millis() - inferenceStartedMs) : inferenceMs;
  const float cps = elapsed ? (generatedChars * 1000.0f / elapsed) : 0.0f;
  const uint32_t heapFree = ESP.getFreeHeap();
  const uint32_t heapTotal = ESP.getHeapSize();
  const uint32_t psramFree = ESP.getFreePsram();
  const uint32_t psramTotal = ESP.getPsramSize();
  const uint32_t fsTotal = LittleFS.totalBytes();
  const uint32_t fsUsed = LittleFS.usedBytes();
  const float tempC = temperatureRead();

  String json;
  json.reserve(900 + currentAnswer.length() + lastError.length());
  json = "{\"running\":" + String(inferenceRunning ? "true" : "false") +
    ",\"done\":" + String(inferenceDone ? "true" : "false") +
    ",\"progress\":" + String(progressPct) +
    ",\"chars\":" + String(generatedChars) +
    ",\"elapsed_ms\":" + String(elapsed) +
    ",\"chars_per_sec\":" + String(cps, 2) +
    ",\"heap_free\":" + String(heapFree) +
    ",\"heap_total\":" + String(heapTotal) +
    ",\"heap_min\":" + String(ESP.getMinFreeHeap()) +
    ",\"psram_free\":" + String(psramFree) +
    ",\"psram_total\":" + String(psramTotal) +
    ",\"flash_size\":" + String(ESP.getFlashChipSize()) +
    ",\"fs_total\":" + String(fsTotal) +
    ",\"fs_used\":" + String(fsUsed) +
    ",\"fs_free\":" + String(fsTotal >= fsUsed ? fsTotal - fsUsed : 0) +
    ",\"cpu_mhz\":" + String(ESP.getCpuFreqMHz()) +
    ",\"uptime_ms\":" + String(millis()) +
    ",\"temperature_c\":";
  json += isnan(tempC) ? "null" : String(tempC, 1);
  json += ",\"model_loaded\":" + String(tiny.loaded() ? "true" : "false") +
    ",\"layers\":" + String(tiny.layers()) +
    ",\"width\":" + String(tiny.width()) + ",\"activity\":[";
  for (int i = 0; i < 8; ++i) {
    if (i) json += ',';
    json += String(layerActivity[i], 4);
  }
  String answer;
  if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(30)) == pdTRUE) {
    answer = currentAnswer;
    xSemaphoreGive(stateMutex);
  }
  json += "],\"answer\":\"" + jsonEscape(answer) + "\"";
  if (lastError.length()) json += ",\"error\":\"" + jsonEscape(lastError) + "\"";
  json += "}";
  server.send(200, "application/json", json);
}

void setup() {
  Serial.begin(115200); delay(250);
  stateMutex = xSemaphoreCreateMutex();
  Serial.println("\n[TINY] Booting Neural Terminal...");
  if (!LittleFS.begin(false)) Serial.println("[TINY] LittleFS mount failed (no auto-format; check partition table/upload).");
  else {
    Serial.printf("[TINY] LittleFS: %u bytes total, %u used\n", (unsigned)LittleFS.totalBytes(), (unsigned)LittleFS.usedBytes());
    if (LittleFS.exists("/TINY.BIN")) {
      Serial.println("[TINY] Loading /TINY.BIN into PSRAM...");
      if (!tiny.load(LittleFS, "/TINY.BIN")) Serial.printf("[TINY] Model error: %s\n", tiny.error().c_str());
      else Serial.printf("[TINY] Model online: %d layers, width %d\n", tiny.layers(), tiny.width());
    } else Serial.println("[TINY] TINY.BIN not found. Upload it to LittleFS data/.");
  }
  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.printf("[WIFI] SSID: %s | PASS: %s | URL: http://%s\n", AP_SSID, AP_PASS, WiFi.softAPIP().toString().c_str());
  server.on("/", HTTP_GET, [](){ server.send_P(200, "text/html; charset=utf-8", TINY_INDEX_HTML); });
  server.on("/api/status", HTTP_GET, sendJsonStatus);
  server.on("/api/config", HTTP_POST, [](){
    if (server.hasArg("creativity")) creativity = constrain(server.arg("creativity").toInt(), 0, 9);
    server.send(200, "application/json", "{\"ok\":true,\"creativity\":"+String(creativity)+"}");
  });
  server.on("/api/chat", HTTP_POST, [](){
    if (!tiny.loaded()) { server.send(503, "application/json", "{\"error\":\"Modelo TINY.BIN no cargado.\"}"); return; }
    if (inferenceRunning) { server.send(409, "application/json", "{\"error\":\"El motor está ocupado.\"}"); return; }
    String q = server.arg("plain"); q.trim();
    if (!q.length()) { server.send(400, "application/json", "{\"error\":\"Pregunta vacía.\"}"); return; }
    if (q.length() > 72) q.remove(72);
    for (size_t i=0;i<q.length();i++) if ((uint8_t)q[i] < 32 || (uint8_t)q[i] > 126) q.setCharAt(i, ' ');
    if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(50)) != pdTRUE) {
      server.send(503, "application/json", "{\"error\":\"No se pudo bloquear el estado del motor.\"}");
      return;
    }
    currentQuestion=q; currentAnswer=""; currentAnswer.reserve(128); lastError=""; generatedChars=0; progressPct=0; inferenceDone=false; inferenceRunning=true; inferenceStartedMs=millis();
    xSemaphoreGive(stateMutex);
    BaseType_t created = xTaskCreatePinnedToCore(inferenceTask, "tiny_infer", 12288, nullptr, 1, &inferenceTaskHandle, 0);
    if (created != pdPASS) { inferenceRunning=false; server.send(503,"application/json","{\"error\":\"No se pudo crear la tarea de inferencia.\"}"); return; }
    server.send(202, "application/json", "{\"accepted\":true}");
  });
  server.onNotFound([](){ server.send(404,"text/plain","Not found"); });
  server.begin();
  Serial.println("[HTTP] Neural Terminal ready.");
}
void loop() { server.handleClient(); delay(2); }
