#include <Arduino.h>
#include <esp32-hal.h>
#include <math.h>

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("\n=== TINY / ESP32-S3 N16R8 DIAGNOSTIC ===");
  Serial.printf("Chip: %s\n", ESP.getChipModel());
  Serial.printf("Revision: %u | cores: %u\n", ESP.getChipRevision(), ESP.getChipCores());
  Serial.printf("CPU: %u MHz\n", ESP.getCpuFreqMHz());
  Serial.printf("Flash: %lu bytes (%.2f MB)\n", (unsigned long)ESP.getFlashChipSize(), ESP.getFlashChipSize()/1048576.0);
  Serial.printf("PSRAM found: %s\n", psramFound() ? "YES" : "NO");
  Serial.printf("PSRAM total: %lu bytes\n", (unsigned long)ESP.getPsramSize());
  Serial.printf("PSRAM free: %lu bytes\n", (unsigned long)ESP.getFreePsram());
  Serial.printf("Heap total: %lu bytes\n", (unsigned long)ESP.getHeapSize());
  Serial.printf("Heap free: %lu bytes\n", (unsigned long)ESP.getFreeHeap());
  Serial.printf("Heap minimum free: %lu bytes\n", (unsigned long)ESP.getMinFreeHeap());
  float t = temperatureRead();
  if (isnan(t)) Serial.println("Temperature: unavailable");
  else Serial.printf("Chip temperature: %.1f C (not ambient temperature)\n", t);
  Serial.println("========================================");
}
void loop() { delay(1000); }
