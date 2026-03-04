#include <Arduino.h>
#include "HardwareInit.h"
#include "esp_idf_version.h"

/*****************************************************************************************
 *  File     : HardwareInit.cpp
 *  Purpose  : Implementation of low-level hardware initialization utilities.
 *
 *  Notes:
 *    - This layer is intentionally minimal and hardware-centric.
 *    - Do not place WiFi/MQTT logic here — belongs to HestiaNetSDK.
 *
 *****************************************************************************************/

namespace HardwareInit {

   // Internal watchdog state (private)
  static bool watchdogInitialized = false;

  /*****************************************************************************************
   *  InitHardwareMinimal()
   *  ----------------------
   *  Bring up the Serial interface and print a diagnostic banner.
   *****************************************************************************************/
  void InitHardwareMinimal() {

    Serial.begin(115200);
    delay(2000);    // Allow USB CDC to stabilize

    Serial.println();
    Serial.println(F("=========================================================="));
    Serial.println(F(" 🔥  Virgo / Hestia SDK  —  Hardware Initialization"));
    Serial.println(F("=========================================================="));
    Serial.println(F("Firmware Info:"));
    Serial.printf("Build   : %s - %s\n", __DATE__, __TIME__);

    Serial.println(F("🧩 Minimal hardware initialization complete."));
    Serial.println(F("----------------------------------------------------------"));
  }


  /*****************************************************************************************
   *  InitHardwareWatchdog()
   *  -----------------------
   *  Configure ESP-IDF task watchdog.
   *
   *  Behavior:
   *    • Delete any previous watchdog instance
   *    • Reinitialize watchdog with new timeout
   *    • Register the current task
   *    • Reset watchdog once at startup
   */
  void InitHardwareWatchdog(int timeoutMs) {

    watchdogInitialized = false;

    // Vérifier si la tâche est déjà enregistrée AVANT de delete
    if (esp_task_wdt_status(NULL) == ESP_OK) {
        // C3 / C6 → ne faire delete QUE si la tâche est réellement enregistrée
        esp_task_wdt_delete(NULL);
    }
    esp_task_wdt_deinit();

    esp_err_t err = ESP_OK;

    // Select API based on actual ESP-IDF version, not build system.
#if defined(ESP_IDF_VERSION_MAJOR) && (ESP_IDF_VERSION_MAJOR >= 5)
    esp_task_wdt_config_t wdt_config{};
    wdt_config.timeout_ms     = static_cast<uint32_t>(timeoutMs);
    wdt_config.idle_core_mask = (1U << portNUM_PROCESSORS) - 1U;
    wdt_config.trigger_panic  = true;

    err = esp_task_wdt_init(&wdt_config);
#else
    // Legacy API (IDF 4.x)
    int timeout_s = timeoutMs / 1000;
    if (timeout_s < 1) timeout_s = 1;
    err = esp_task_wdt_init(timeout_s, true);
#endif

    // Error handling
    if (err != ESP_OK) {
        Serial.printf("[Watchdog] ✖ Init error (%d)\n", err);
        return;
    }

    err = esp_task_wdt_add(NULL);
    if (err != ESP_OK) {
        Serial.printf("[Watchdog] ✖ Task registration failed (%d)\n", err);
        return;
    }

    esp_task_wdt_reset();

    Serial.printf("[Watchdog] ✓ Started with timeout: %d ms\n", timeoutMs);
    watchdogInitialized = true;
 }


  /*****************************************************************************************
   *  watchdogKick()
   *  ---------------
   *  Safe watchdog reset (ignored if not initialized).
   *****************************************************************************************/
  void watchdogKick() {
    if (!watchdogInitialized) return;
    esp_task_wdt_reset();
  }

} // namespace HardwareInit
