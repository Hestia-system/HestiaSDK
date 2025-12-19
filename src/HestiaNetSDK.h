/*****************************************************************************************
 *  File     : HestiaNetSDK.h
 *  Project  : Hestia SDK / Virgo IoT
 *
 *  Summary:
 *  --------
 *  HestiaNetSDK provides core networking capabilities for the Hestia runtime:
 *
 *    • Robust non-blocking Wi-Fi connection manager (“Wi-Fi Guard”)
 *    • Robust non-blocking MQTT connection manager (“MQTT Guard”)
 *    • mDNS hostname registration
 *    • Home Assistant MQTT Discovery publishing
 *    • Retained-message flush mode for startup cleanup
 *    • Central dispatch of MQTT payloads to HestiaCore
 *
 *  Design Philosophy:
 *    - Never block the main loop
 *    - Always recover from network instability without reboot
 *    - Use deterministic state machinery for Wi-Fi and MQTT
 *
 *****************************************************************************************/

#pragma once
#include <WiFi.h>
#include <MQTT.h>

// ========================================================================================
//  Global network objects (declared in HestiaNetSDK.cpp)
// ========================================================================================
extern WiFiClient net;
extern MQTTClient client;

// ========================================================================================
//  Forward Declarations — MQTT → HestiaCore Routing
// ========================================================================================
namespace HestiaCore {
  void onMessageReceived(String &topic, String &payload);
}

// ========================================================================================
//  Namespace: HestiaNet
// ========================================================================================
namespace HestiaNet {

  /**
   * @brief Load runtime Wi-Fi and MQTT parameters from HestiaConfig.
   *
   * This must be called once during system boot (after configuration validation).
   *
   * Rationale:
   *   • Prevent repeated O(n) lookups in HestiaConfig
   *   • Ensure networking always uses the freshest provisioning values
   *   • Separate firmware defaults from provisioning/runtime configuration
   */
  void loadConfig();


  // ====================================================================================
  //  Wi-Fi Guard — Non-blocking, self-recovering STA connection
  // ====================================================================================

  /**
   * @brief Non-blocking, fault-tolerant Wi-Fi STA connection manager.
   *
   * Features:
   *   • Radio-level Wi-Fi stack reset when needed
   *   • Exponential backoff + random jitter
   *   • Optional SSID presence scan after multiple failures
   *   • No blocking delays — designed for call-every-loop()
   *
   * @return true if WL_CONNECTED, false otherwise.
   */
  bool tryWiFiConnectNonBlocking_V2();

  /**
   * @brief Compatibility wrapper, forwards to tryWiFiConnectNonBlocking_V2().
   */
  bool tryWiFiConnectNonBlocking();

  /**
   * @brief Print detailed Wi-Fi diagnostics to Serial (only when connected).
   */
  void doWiFiInfo();

  /**
   * @brief Register mDNS hostname for LAN-based autodiscovery.
   */
  void registerMDNS();


  // ====================================================================================
  //  MQTT Guard — Non-blocking, self-recovering MQTT client
  // ====================================================================================

  /**
   * @brief Attempt to establish or repair an MQTT connection.
   *
   * Behavior:
   *   • Initializes client.begin() once
   *   • Uses exponential backoff on failure
   *   • Returns true once MQTT is fully connected
   */
  bool tryMQTTConnectNonBlocking();

  /**************************************************************************************
   * @brief  Gracefully stops all MQTT communications before entering OTA or other
   *         exclusive modes. 
   *
   * Purpose:
   *   - Ensures that the MQTT client is cleanly disconnected
   *   - Prevents any further reconnection attempts by CoreComm()
   *   - Does NOT disconnect WiFi (STA must remain active for HTTP OTA)
   *
   * Usage:
   *   Called once before transitioning into a blocking OTA or other isolated mode.
   *
   * Notes:
   *   - WiFi remains connected so the embedded HTTP server can continue running.
   *   - CoreComm() must check HestiaCore::commSuspended to avoid reconnections.
 **************************************************************************************/

  void disconnectMQTT();


  // ====================================================================================
  //  MQTT Discovery (Home Assistant)
  // ====================================================================================

  /**
   * @brief Publish the Home Assistant Discovery JSON block.
   *
   * The JSON must be injected via loadDiscoveryJson() and stored in PROGMEM.
   *
   * Publishes to:
   *     homeassistant/device/<device_id>/config
   *
   * Retained = true to persist device attributes on the broker.
   */
  void MQTTDiscovery();


  // ====================================================================================
  //  MQTT Message Callback Registration
  // ====================================================================================

  /**
   * @brief Register the MQTT inbound callback.
   *
   * This function installs the messageReceived() handler as the active
   * MQTT callback. It MUST be called after each successful MQTT connection
   * and strictly BEFORE any call to client.subscribe().
   *
   * Rationale:
   *   Retained messages are delivered immediately after SUBSCRIBE.
   *   If no callback is installed at that moment, retained messages are lost.
   *
   * Notes:
   *   • This function does not toggle flush mode.
   *   • It only ensures that the MQTT client is prepared to receive messages.
   *   • Flush control is handled separately via mqttFlush and HestiaCore logic.
   */
  void startMessageReceived();


  // ====================================================================================
  //  MQTT → HestiaCore Message Routing
  // ====================================================================================

  /**
   * @brief Central MQTT message callback.
   *
    * Behavior:
   *   • Forwards messages to HestiaCore::onMessageReceived()
   */
  void messageReceived(String &topic, String &payload);


  // ====================================================================================
  //  Discovery JSON Injection
  // ====================================================================================

  /**
   * @brief Store a pointer to the Home Assistant Discovery JSON (in PROGMEM).
   *
   * The JSON is not parsed immediately; it will be published later by
   * MQTTDiscovery() when the network stack becomes operational.
   *
   * @param json Pointer to PROGMEM string containing the discovery payload.
   */
  void loadDiscoveryJson(const char* json);

} // namespace HestiaNet



// ========================================================================================
//  Global Utility — MQTTrefreshWithDelay()
// ========================================================================================

/**
 * @brief Pump the MQTT client loop for ~ms milliseconds.
 *
 * Intended to guarantee delivery of outgoing packets (QoS, retained, etc.).
 * Automatically yields to FreeRTOS/lwIP on each cycle.
 *
 * @param ms Duration to pump the loop.
 */
void MQTTrefreshWithDelay(unsigned long ms);
// ========================================================================================