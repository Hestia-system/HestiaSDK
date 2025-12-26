#include <Arduino.h>
#include "HestiaNetSDK.h"
#include "HestiaCore.h"     // Required for forwarding incoming messages





// -------------------------------------------------------------
// Global network objects owned by the HestiaNetSDK module
// -------------------------------------------------------------
WiFiClient net;
MQTTClient client(256);


namespace HestiaNet {

  // =============================================================
  //  Runtime configuration (loaded from HestiaConfig)
  // =============================================================

  static const char* g_discoveryJson = nullptr;

  /**
   * @brief Register the Home Assistant Discovery JSON block.
   *
   * The SDK stores only the pointer; the JSON payload may reside in PROGMEM.
   *
   * @param json Pointer to the discovery payload.
   */
  void loadDiscoveryJson(const char* json) {
    g_discoveryJson = json;
    Serial.println(F("=== [HestiaNet] Discovery JSON loaded. ==="));
  }


  // ------------------------------------------------------------------------------------
  // Module-level State
  // ------------------------------------------------------------------------------------
  bool mqttFlush = false;


  /*****************************************************************************************
   *  WiFi Guard — tryWiFiConnectNonBlocking_V2()
   *
   *  Purpose:
   *    Non-blocking Wi-Fi connection routine with:
   *      • exponential backoff,
   *      • SSID presence validation (scan-on-failure),
   *      • stateful retry counters,
   *      • driver resets every 5 seconds,
   *      • hostname assignment from device_id.
   *
   *  Behavior:
   *    This function MUST be called repeatedly from the main communication loop.
   *    It never blocks longer than a few milliseconds.
   *
   *  Returns:
   *    true  → Wi-Fi is fully connected
   *    false → Connection is pending, retrying, or failed
   *****************************************************************************************/
  bool tryWiFiConnectNonBlocking_V2() {

    static unsigned long lastAttempt = 0;
    static unsigned long lastReset   = 0;
    static uint8_t tryCount          = 0;
    static unsigned long delayNext   = 100;
    static bool connecting           = false;

    static bool ssidVisible          = true;
    static unsigned long lastScan    = 0;

    String cfgwifi_ssid = HestiaConfig::getParam("wifi_ssid");
    String cfgwifi_pass = HestiaConfig::getParam("wifi_pass");

    wl_status_t st = WiFi.status();

    // ---------------------------------------------------------------------
    // 1️⃣ Already connected → success
    // ---------------------------------------------------------------------
    if (st == WL_CONNECTED) {
      tryCount  = 0;
      delayNext = 100;
      connecting = false;
      return true;
    }

    // ---------------------------------------------------------------------
    // 2️⃣ Avoid scanning too often if the SSID was previously missing
    // ---------------------------------------------------------------------
    if (!ssidVisible && millis() - lastScan < 30000) {
      return false;
    }

    // ---------------------------------------------------------------------
    // 3️⃣ After repeated failures, perform a diagnostic network scan
    // ---------------------------------------------------------------------
    if (tryCount >= 5 && millis() - lastScan > 30000) {

      Serial.println(F("[HestiaNet | WiFi] 🔍 Scanning networks after repeated failures..."));

      int n = WiFi.scanNetworks();
      lastScan = millis();
      ssidVisible = false;

      for (int i = 0; i < n; i++) {
        if (WiFi.SSID(i) == cfgwifi_ssid.c_str()) {
          ssidVisible = true;
          Serial.printf("[HestiaNet | WiFi] ✓ SSID '%s' found (RSSI=%d dBm, channel=%d)\n",
                        cfgwifi_ssid.c_str(), WiFi.RSSI(i), WiFi.channel(i));
          break;
        }
      }

      if (!ssidVisible) {
        Serial.printf("[HestiaNet | WiFi] ⚠ SSID '%s' not found — retry in 30 s\n",
                      cfgwifi_ssid.c_str());
        return false;
      }

      tryCount = 0;
    }

    // ---------------------------------------------------------------------
    // 4️⃣ Anti-spam protection
    // ---------------------------------------------------------------------
    if (connecting && (millis() - lastAttempt < 8000)) return false;
    if (millis() - lastAttempt < delayNext) return false;

    // ---------------------------------------------------------------------
    // 5️⃣ Low-level Wi-Fi driver reset
    // ---------------------------------------------------------------------
    if (millis() - lastReset > 5000) {
      Serial.printf("[HestiaNet | WiFi] Attempt %u...\n", tryCount + 1);

      WiFi.disconnect(true, true);   // wipe config + connection
      delay(50);
      WiFi.mode(WIFI_STA);

      String cfgdevice_id = HestiaConfig::getParam("device_id");
      WiFi.setHostname(cfgdevice_id.c_str());

      lastReset = millis();
    }

    // ---------------------------------------------------------------------
    // 6️⃣ Start a new connection attempt
    // ---------------------------------------------------------------------
    Serial.println(cfgwifi_ssid);
    WiFi.begin(cfgwifi_ssid.c_str(), cfgwifi_pass.c_str());
    connecting = true;

    // ---------------------------------------------------------------------
    // 7️⃣ Exponential backoff + jitter
    // ---------------------------------------------------------------------
    tryCount++;
    delayNext = (tryCount <= 5)
                  ? (100UL << tryCount) + random(0, 50)
                  : 10000UL;

    lastAttempt = millis();

    // ---------------------------------------------------------------------
    // 8️⃣ Diagnostics
    // ---------------------------------------------------------------------
    switch (st) {
      case WL_NO_SSID_AVAIL:
        Serial.println(F("[HestiaNet | WiFi] ✖ SSID unavailable"));
        break;
      case WL_CONNECT_FAILED:
        Serial.println(F("[HestiaNet | WiFi] ✖ Authentication failed"));
        break;
      case WL_DISCONNECTED:
        Serial.println(F("[HestiaNet | WiFi] 🔌 Disconnected from access point"));
        break;
      case WL_CONNECTION_LOST:
        Serial.println(F("[HestiaNet | WiFi] ⚠ Connection lost"));
        break;
      case WL_IDLE_STATUS:
        Serial.println(F("[HestiaNet | WiFi] ⏳ Interface idle"));
        break;
      default:
        break;
    }

    return false;
  }


  /*****************************************************************************************
   *  Print WiFi Info
   *
   *  Displays detailed information about the current Wi-Fi connection.
   *  The call is ignored if the interface is not connected.
   *****************************************************************************************/
  void doWiFiInfo() {
    if (WiFi.status() != WL_CONNECTED) return;

    Serial.println();
    Serial.println(F("=== [WiFi Info] ======================================="));
    Serial.printf("SSID   : %s\n", WiFi.SSID().c_str());
    Serial.printf("BSSID  : %s\n", WiFi.BSSIDstr().c_str());
    Serial.printf("IP     : %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("GW     : %s\n", WiFi.gatewayIP().toString().c_str());
    Serial.printf("MASK   : %s\n", WiFi.subnetMask().toString().c_str());
    Serial.printf("RSSI   : %d dBm\n", WiFi.RSSI());
    Serial.println(F("========================================================"));
  }





  /*****************************************************************************************
   *  Public Wrapper for WiFi Guard
   *
   *  Currently maps directly to tryWiFiConnectNonBlocking_V2().
   *  Exists to preserve API stability for future internal rewrites.
   *****************************************************************************************/
  bool tryWiFiConnectNonBlocking() {
    return tryWiFiConnectNonBlocking_V2();
  }


  /*****************************************************************************************
   *  MQTT Guard — Non-blocking MQTT connection manager
   *
   *  Purpose:
   *    Maintains the MQTT session using:
   *      • exponential backoff,
   *      • single-shot initialization,
   *      • credential retrieval from HestiaConfig,
   *      • non-blocking reconnect attempts,
   *      • connection state tracking.
   *
   *  Must be called repeatedly from the communication loop.
   *
   *  Returns:
   *    true  → MQTT session is active
   *    false → connection pending or failed
   *****************************************************************************************/
  bool tryMQTTConnectNonBlocking() {

    if (WiFi.status() != WL_CONNECTED) return false;

    static bool initialized = false;
    static bool wasConnected = false;
    static unsigned long lastAttempt = 0;
    static uint8_t tryCount = 0;
    static unsigned long nextDelay = 100;

    String cfgmqtt_ip    = HestiaConfig::getParam("mqtt_ip");
    String cfgdevice_id  = HestiaConfig::getParam("device_id");
    String cfgmqtt_user  = HestiaConfig::getParam("mqtt_user");
    String cfgmqtt_pass  = HestiaConfig::getParam("mqtt_pass");

    // ---------------------------------------------------------------------
    // 1️⃣ Initialize MQTT client only once
    // ---------------------------------------------------------------------
    if (!initialized) {
      Serial.println(F("[HestiaNet | MQTT] Initializing client..."));

      client.setKeepAlive(20);
      client.setCleanSession(true);

      client.begin(cfgmqtt_ip.c_str(),
                  HestiaConfig::getParamObj("mqtt_port")->readInt(),
                  net);

      initialized = true;
      delay(10);
    }

    // ---------------------------------------------------------------------
    // 2️⃣ Already connected → success
    // ---------------------------------------------------------------------
    if (client.connected()) {
      if (!wasConnected) {
        Serial.printf("[HestiaNet | MQTT] ✓ Connected to %s:%u\n",
                      cfgmqtt_ip.c_str(),
                      HestiaConfig::getParamObj("mqtt_port")->readInt());
        wasConnected = true;
        tryCount = 0;
        nextDelay = 100;
      }
      return true;
    }

    wasConnected = false;

    // ---------------------------------------------------------------------
    // 3️⃣ Exponential backoff timing
    // ---------------------------------------------------------------------
    if (millis() - lastAttempt < nextDelay) return false;
    lastAttempt = millis();

    Serial.printf("[HestiaNet | MQTT] Reconnect attempt %u...\n", tryCount + 1);

    // ---------------------------------------------------------------------
    // 4️⃣ Attempt reconnection
    // ---------------------------------------------------------------------
    bool ok = client.connect(cfgdevice_id.c_str(),
                            cfgmqtt_user.c_str(),
                            cfgmqtt_pass.c_str());

    if (ok) {
      Serial.println(F("[HestiaNet | MQTT] ✓ Session established"));
      wasConnected = true;
      tryCount = 0;
      nextDelay = 100;
      return false; // false because caller may need to resubscribe
    }

    Serial.println(F("[HestiaNet | MQTT] ✖ Connection failed"));

    // ---------------------------------------------------------------------
    // 5️⃣ Update backoff state
    // ---------------------------------------------------------------------
    tryCount++;
    nextDelay = (tryCount <= 5)
                ? ((100UL << tryCount) + random(0, 50))
                : 10000UL;

    return false;
  }

  /**************************************************************************************
   * @brief  Gracefully disconnects MQTT without affecting WiFi.
   *
   * Rationale:
   *   - In blocking OTA mode, MQTT must be terminated because:
   *        • no reconnection attempts may occur
   *        • no messages should be processed or published
   *        • network stack must remain stable
   *   - WiFi must remain active because OTA HTTP requires STA connectivity.
   *
   * Behavior:
   *   - If MQTT is connected: perform a clean disconnect.
   *   - Otherwise: do nothing.
   **************************************************************************************/
  void disconnectMQTT() {
      if (client.connected()) {
          client.disconnect();   // Cleanly close the MQTT session
      }
      // IMPORTANT:
      // - Do NOT call WiFi.disconnect()
      // - STA must remain active for OTA HTTP upload
  }


/*****************************************************************************************
 *  MQTT Discovery — Publish HA discovery JSON
 *
 *  Purpose:
 *    Publishes the Home Assistant discovery payload for this device.
 *    The payload is expected to reside in PROGMEM and is injected earlier
 *    via loadDiscoveryJson().
 *
 *  Behavior:
 *    • Skips if MQTT is offline
 *    • Skips if no JSON has been injected
 *    • Converts PROGMEM → RAM string manually
 *    • Publishes to:
 *        homeassistant/device/<device_id>/config
 *
 *****************************************************************************************/
void MQTTDiscovery()
{
    Serial.println(F("\n=== [HestiaNet | MQTT Discovery] Publishing HA device config ==="));

    // ---------------------------------------------------------------------
    // 0) Guards
    // ---------------------------------------------------------------------
    if (!client.connected()) {
        Serial.println(F("[HestiaNet | MQTT Discovery] ✖ MQTT offline, aborting"));
        return;
    }

    if (!g_discoveryJson) {
        Serial.println(F("[HestiaNet | MQTT Discovery] ✖ No injected discovery JSON"));
        return;
    }

    // ---------------------------------------------------------------------
    // 1) Convert PROGMEM → RAM
    // ---------------------------------------------------------------------
    String payload;
    size_t len = strlen_P(g_discoveryJson);
    payload.reserve(len);

    for (size_t i = 0; i < len; i++) {
        payload += (char)pgm_read_byte_near(g_discoveryJson + i);
    }

    // ---------------------------------------------------------------------
    // 2) JSON syntax validation (ONLY syntax)
    // ---------------------------------------------------------------------
    DynamicJsonDocument doc(8192);
    DeserializationError err = deserializeJson(doc, payload);

    if (err) {
        Serial.println(F("[HestiaNet | MQTT Discovery] ✖ Invalid JSON syntax"));
        Serial.print  (F("[HestiaNet | MQTT Discovery] Error: "));
        Serial.println(err.c_str());
        return;
    }

    // ---------------------------------------------------------------------
    // 3) Hestia structural validation (NOT Home Assistant validation)
    // ---------------------------------------------------------------------
    if (!doc.containsKey("device") || !doc["device"].is<JsonObject>()) {
        Serial.println(F("[HestiaNet | MQTT Discovery] ✖ Missing or invalid 'device' object"));
        return;
    }

    if (!doc.containsKey("cmps") || !doc["cmps"].is<JsonObject>()) {
        Serial.println(F("[HestiaNet | MQTT Discovery] ✖ Missing or invalid 'cmps' object"));
        return;
    }

    JsonObject cmps = doc["cmps"].as<JsonObject>();
    if (cmps.size() == 0) {
        Serial.println(F("[HestiaNet | MQTT Discovery] ✖ No components defined (cmps empty)"));
        return;
    }

    // ---------------------------------------------------------------------
    // 4) Publish raw discovery payload
    // NOTE: HA validation is delegated to the generator.
    // ---------------------------------------------------------------------
    String topic = "homeassistant/device/";
    topic += HestiaConfig::getParam("device_id");
    topic += "/config";

    bool ok = client.publish(topic.c_str(), payload.c_str(), true, 1);

    if (ok) {
        Serial.printf("[HestiaNet | MQTT Discovery] ✓ Published (%u components) → %s\n",
                      cmps.size(), topic.c_str());
    } else {
        Serial.printf("[HestiaNet | MQTT Discovery] ✖ Publish error → %s\n", topic.c_str());
    }

    Serial.println(F("=== [HestiaNet | MQTT Discovery] Done ===\n"));
}




/*****************************************************************************************
 *  MQTT Message Callback Registration
 *
 *  Purpose:
 *    Registers the MQTT inbound message callback used by HestiaNet. 
 *    This function MUST be called before any MQTT subscription occurs, so that retained
 *    messages delivered immediately after a SUBSCRIBE are correctly captured.
 *
 *  Behavior:
 *    • Installs the messageReceived(...) handler as the active MQTT callback.
 *    • Ensures that retained messages are not lost during initial connection or reconnect.
 *
 *  Usage Requirements:
 *    • Call startMessageReceived() immediately after client.connect(), 
 *      and strictly BEFORE client.subscribe().
 *

 *****************************************************************************************/
void startMessageReceived() {
    client.onMessage(messageReceived);
}



/*****************************************************************************************
 *  MQTT Incoming Message Callback
 *
 *  Behavior:
 *    • Messages are forwarded to HestiaCore’s dispatch layer.
 *****************************************************************************************/
void messageReceived(String &topic, String &payload) {
  Serial.printf("[MQTT HestiaNet] %s <- %s\n", topic.c_str(), payload.c_str());
  Serial.flush();
  HestiaCore::onMessageReceived(topic, payload);
  // Serial.println("HAIotBridge::messageReceived [flush] " + topic + " - " + payload);
}

} // namespace HestiaNet



/*****************************************************************************************
 *  MQTTrefreshWithDelay — Pump MQTT client loop for N ms
 *
 *  Purpose:
 *    Ensures that MQTT internal processing (loop()) runs for a bounded duration.
 *
 *  Behavior:
 *    • Skips if Wi-Fi or MQTT are offline
 *    • Yields to FreeRTOS/lwIP on each iteration (`delay(0)`)
 *
 *  Notes:
 *    • Used to guarantee timely processing of QoS acknowledgments
 *      and inbound packet handling during publish sequences.
 *****************************************************************************************/
void MQTTrefreshWithDelay(unsigned long ms) {

  if (WiFi.status() != WL_CONNECTED) return;
  if (!client.connected()) return;

  unsigned long until = millis() + ms;

  while ((long)(until - millis()) > 0) {
    client.loop();
    delay(0);  // yield to FreeRTOS / lwIP
  }
}
