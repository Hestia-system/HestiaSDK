#pragma once
#include <vector>
#include "HAIotBridge.h"     // MUST be included before config.h
#include "HestiaNetSDK.h"
#include "HardwareInit.h"
#include "HestiaConfig.h"

/*****************************************************************************************
 *  File     : HestiaCore.h (v2.0)
 *  Project  : Hestia SDK / Virgo Template
 *  Author   : Ji & GPT-5
 *  Date     : November 2025
 *
 *  Summary:
 *  --------
 *  HestiaCore is responsible for orchestrating all high-level runtime logic:
 *
 *    • Instantiating all HAIoTBridge entities declared in the BridgeConfig table
 *    • Managing the BridgeRegistry (dynamic list of active entities)
 *    • Running the Core Communication State Machine (Wi-Fi → mDNS → MQTT)
 *    • Handling MQTT discovery, subscriptions, retained-message flushing
 *    • Centralizing MQTT publication
 *    • Dispatching inbound MQTT messages to the correct bridge
 *    • Detecting when full communication becomes operational
 *
 *  Design Notes:
 *  -------------
 *    • Entity creation is automatic: one HAIoTBridge instance per BridgeConfig entry.
 *    • All value handling (NVS restore, formatting, publish/read) is encapsulated
 *      directly in HAIoTBridge.
 *    • HestiaCore focuses strictly on orchestration and communication flow.
 *
 *****************************************************************************************/

namespace HestiaCore {

  // =====================================================================================
  //  Active Bridge Registry
  // =====================================================================================
  extern std::vector<HAIoTBridge*> BridgeRegistry;

  // =====================================================================================
  //  Core API — High-Level Operations
  // =====================================================================================

  /**
   * @brief Print a structured summary of all registered bridge entities.
   */
  void logSummary();

  /**
   * @brief Restore values from NVS and initialize all bridge entities.
   *
   * CONTROL-type bridges load their persisted state from NVS.
   * Other bridge types adopt their configured default value.
   */
  bool InitValueNVS();

  /**
   * @brief Publish all HAIoTBridge-controlled values to HA.
   *
   * Behavior:
   *   • Iterates through the registry and invokes each entity's init() method.
   *   • Publish MQTT to HA for each entity.
   */
  bool publishValuesToHA();

    /**
   * @brief HAInitDone setter — called from main after HAInit()
   *
  * @param v New value for the HA initialization done flag.
   */
  void setHAInitDone();

  /**
   * @brief Instantiate all entities from the active BridgeConfig table and
   *        register them in BridgeRegistry.
   */
  void RegisterEntitiesIotBridge();

  /**
   * @brief Perform core runtime initialization:
   *        - Minimal hardware initialization
   *        - Instantiation of all HAIoTBridge entities
   *
   * Should be called early in setup().
   */
  void InitCore();

  /**
   * @brief Execute the full Communication State Machine.
   *
   * Responsibilities:
   *   • Wi-Fi Guard
   *   • mDNS registration (optional)
   *   • MQTT Guard
   *   • MQTT Discovery
   *   • MQTT Subscriptions
   *   • Retained-message flush window
   *
   * Must be called continuously in loop().
   */
  void CoreComm();

  // =====================================================================================
  //  Communication State Indicators
  // =====================================================================================
  /**
   * @brief True when the *entire* communication pipeline has completed.
   *
   * Meaning:
   *   • Wi-Fi connected
   *   • MQTT connected
   *   • MQTT Discovery published
   *   • MQTT subscriptions established
   *   • Retained-message flush window completed
   *   • HAInit() fully executed (ha_init_done == true)
   *
   * This flag represents the final, stable, fully-synchronized operational state.
   */
  extern bool comm_state_ok;      
  extern bool comm_state_okmem;   ///< One-shot transition detector (new online session)

  // =====================================================================================
  //  Utility Functions
  // =====================================================================================

  /**
   * @brief Check whether MQTT communication with Home Assistant is available.
   *
   * Definition:
   *    commOK() == (mqttOK == true) && (HA_online == true)
   *
   * Meaning:
   *   • MQTT transport is operational
   *   • Home Assistant has reported HA_STATUS = ON
   *
   * This is the minimal condition required to allow MQTT publications.
   * It does NOT imply discovery completion, subscription readiness, or HAInit completion.
   */
  bool commOK();

  /**
   * @brief Check whether the entire communication pipeline is fully operational.
   *
   * Definition:
   *    InitHAOK() == comm_state_ok
   *
   * Meaning:
   *   • Full MQTT + HA pipeline is complete
   *   • All retained messages have been flushed
   *   • All MQTT discovery and subscriptions are active
   *   • HAInit() has completed successfully
   *
   * This is the highest-level readiness indicator.
   */
  bool InitHAOK();


  /**
   * @brief Detect the beginning of a new communication session.
   *
   * Returns true exactly once when Wi-Fi + MQTT become operational.
   */
  bool newSeqComm();

  /**
   * @brief Retrieve a bridge instance by its internal name.
   */
  HAIoTBridge* get(const String& name);

  /**
   * @brief Initialize all registered entities (NVS restore + initial publish).
   */
  void initAll();

  /**
   * @brief Clear NVS entries for all CONTROL-type entities.
   */
  void resetAll();

  // =====================================================================================
  //  MQTT Mediation Layer
  // =====================================================================================

  /**
   * @brief Dispatch incoming MQTT messages to the appropriate bridge entity.
   *
   * Used internally by HestiaNetSDK when a message is received.
   */
  void onMessageReceived(String &topic, String &payload);

  /**
   * @brief Centralized MQTT publication function.
   *
   * Publication Rules:
   *   • publishToMQTT() is allowed as soon as commOK() == true
   *     (MQTT connected + HA online).
   *
   * Notes:
   *   • Full pipeline completion (InitHAOK) is NOT required for publication.
   *   • Using commOK() avoids deadlocks during HAInit() publishing.
   *   • This function MUST NOT wait for comm_state_ok.
   */
  void publishToMQTT(const String &topic, const String &payload, bool logIt);

  // =====================================================================================
  //  logBook — Centralized logger
  // =====================================================================================

  /**
   * @brief Unified logging helper.
   *
   * Prints a message on the Serial console and publishes it to the
   * Home Assistant logging topic (HA_LOG).
   */
  void logBook(const String& msg);

  // =====================================================================================
  //  Bridge Configuration Injection (Architecture S-2)
  // =====================================================================================

  /**
   * @brief Inject an external BridgeConfig table into HestiaCore.
   *
   * Overview:
   *    Enables applications (e.g., user firmware) to supply their own
   *    entity definitions, decoupling HestiaCore from static compiled-in
   *    tables.
   *
   * Behavior (current stage):
   *    - Stores the pointer + count internally
   *    - Does NOT yet replace the instantiation path
   *      RegisterEntitiesIotBridge() still falls back to the internal table
   *      unless the injection occurs before InitCore().
   *
   * Purpose:
   *    - Support for device-specific entity sets
   *    - Support for PROGMEM-based large tables
   *    - Preparation for dynamic bridge generation
   *
   * @param table Pointer to the external BridgeConfig array
   * @param count Number of entries in the table
   *
   * @note Safe to call before InitCore(). No validation is performed at this stage.
   */
  void loadBridgeConfig(const BridgeConfig* table, size_t count);

} // namespace HestiaCore
// ============================================================================
