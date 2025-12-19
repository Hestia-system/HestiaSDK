#pragma once
#include <Arduino.h>
#include <vector>
#include "HestiaParam.h"

/*****************************************************************************************
 *  File     : HestiaConfig.h
 *  Project  : Hestia SDK / Virgo Template
 *
 *  Summary
 *  -------
 *  HestiaConfig V2 — Metadata-driven configuration system.
 *
 *  Section 1 responsibilities:
 *    • Define enums and structures for parameter metadata and runtime values.
 *    • Declare the public API for schema parsing and metadata access.
 *
 *  Section 3 responsibilities:
 *    • Declare the generic NVS load/save API.
 *
 *  Future sections (4..6) will introduce:
 *    • config.h fallback rules
 *    • validation logic
 *    • loadBAD() provisioning decision
 *    • typed getters (WiFi, MQTT, Timezone, LED, OTA, etc.)
 *
 *****************************************************************************************/

namespace HestiaConfig {

  // Global parameter registry (populated by loadDeviceParams and provisioning)
  extern std::vector<HestiaParam*> _params;

  // Safe fallback parameter (used when a key lookup fails)
  extern HestiaParam _nullParam;

  // Core API
  bool   loadDeviceParams(const char* json);
  String getParam(const String& key);
  bool   setParam(const String& key, const String& value);
  HestiaParam* getParamObj(const String& key);



  // ============================================================================
  //  Enums — Parameter types and validation patterns
  // ============================================================================

  enum class ParamType {
    STRING,
    INT,
    BOOL,
    FLOAT
  };

  enum class PatternType {
    ANYTHING,
    IP,
    HOSTNAME,
    URL,
    EMAIL
  };



  // ============================================================================
  //  Validation Rules
  // ============================================================================

  struct ValidationRules {
    bool   hasMin    = false;
    bool   hasMax    = false;
    float  minVal    = 0.0f;
    float  maxVal    = 0.0f;

    bool   hasMinLen = false;
    bool   hasMaxLen = false;
    uint16_t minLen  = 0;
    uint16_t maxLen  = 0;

    PatternType pattern = PatternType::ANYTHING;
  };



  // ============================================================================
  //  ParamMeta — Static metadata associated with a parameter
  // ============================================================================
  struct ParamMeta {
    String      key;         ///< Logical + NVS key (e.g. "wifi_ssid")
    ParamType   type;        ///< STRING | INT | BOOL | FLOAT
    String      label;       ///< Human-readable label ID (i18n-compatible)
    bool        required;    ///< Provisioning must request user input
    bool        critical;    ///< Failure leads to provisioning mode (loadBAD)
    String      defaultVal;  ///< Default used if both NVS and config.h are empty
    uint8_t     decimals;    ///< Float formatting precision
    ValidationRules rules;   ///< Validation constraints
    std::vector<String> options; ///< Optional list of allowed values
  };



  // ============================================================================
  //  ParamInstance — Static metadata + current runtime value
  // ============================================================================
  struct ParamInstance {
    ParamMeta meta;
    String    value;  ///< Current value stored as a String
  };



  // ============================================================================
  //  Public API — Metadata & schema management
  // ============================================================================

  /**
   * @brief Parse the metadata schema from HestiaConfigSchema.h if it has not
   *        already been initialized.
   */
  void initSchemaIfNeeded();

  /**
   * @brief Dump the full parameter schema (metadata only) to the Serial console.
   *
   * Useful when inspecting provisioning metadata, validation rules,
   * or debugging schema generation.
   */
  void debugDumpSchema();

  /**
   * @brief Return the total number of parameters defined by the schema.
   */
  size_t paramCount();

  /**
   * @brief Access the full read-only list of parameters (metadata + value).
   */
  const std::vector<ParamInstance>& allParams();

  /**
   * @brief Access the full read-write list of parameters (metadata + value).
   *
   * Intended for provisioning modules or debugging tools.
   */
  std::vector<ParamInstance>& allParamsMutable();



  /**
   * @brief Retrieve the current value of a parameter by its key.
   *
   * This function provides safe read-only access to the configuration store.
   * It returns the parameter's in-RAM value after schema initialization,
   * provisioning loading, and NVS restore.
   *
   * @param key  Unique parameter identifier ("wifi_ssid", "mqtt_ip", etc.).
   * @return     The stored value, or an empty String if the key does not exist.
   *
   * Usage example:
   *    String ssid   = HestiaConfig::get("wifi_ssid");
   *    String broker = HestiaConfig::get("mqtt_ip");
   */
  String get(const String& key);

// ============================================================================
//  Public API — Section 3: Generic NVS access
// ============================================================================

/**
 * @brief Load all configuration values from NVS.
 *
 * Behavior:
 *   • For each parameter in the metadata registry (schema),
 *     attempt to load its persisted value from the NVS namespace "HConfig".
 *   • Empty or missing entries are left untouched.
 *
 * @return true if at least one non-empty value was found in NVS.
 */
bool loadFromNVS();

/**
 * @brief Save all configuration values to NVS (overwrite mode).
 *
 * Behavior:
 *   • Writes every parameter's current value to the NVS namespace "HConfig".
 *   • Overwrites existing entries unconditionally.
 *
 * @return true (Preferences API does not expose error codes).
 */
bool saveToNVS();


/**
 * @brief Apply fallback values from config.h to empty parameters.
 *
 * Behavior:
 *   • Only parameters whose value is currently empty are updated.
 *   • Does NOT overwrite:
 *        - values restored from NVS
 *        - provisioning-time values
 *
 * Intended for:
 *   • Legacy compatibility before full schema migration is complete.
 */
void loadFromConfigH();



// ============================================================================
//  Public API — Section 5: Validation runtime
// ============================================================================

/**
 * @brief Validate a single parameter according to its metadata rules.
 *
 * @param p         Parameter instance (metadata + runtime value)
 * @param errorMsg  Human-readable error message (currently in French)
 *
 * @return true  → value is valid according to metadata
 * @return false → validation failed
 */
bool validateParam(const ParamInstance& p, String& errorMsg);

/**
 * @brief Validate all parameters and aggregate errors into a report.
 *
 * Behavior:
 *   • Runs validateParam() on each parameter.
 *   • Collects human-readable messages into a multi-line string.
 *
 * @param report Output string containing aggregated validation errors.
 *               Empty if all parameters are valid.
 *
 * @return true  → all parameters valid
 * @return false → at least one failure
 */
bool validateAll(String& report);



// ============================================================================
//  Placeholder for Section 4..6 functionality
// ============================================================================

/**
 * @brief Placeholder for loadBAD() logic — NOT USED YET.
 *
 * This function will eventually implement the R2 boot decision policy:
 *   • merging config.h defaults
 *   • validation of critical parameters
 *   • determination of provisioning necessity
 *
 * For now, this is a stub.
 */
bool loadBAD();



// ============================================================================
//  Public API — Provisioning control (manual trigger)
// ============================================================================

/**
 * @brief Check if provisioning has been explicitly requested.
 *
 * Behavior:
 *   • Reads boolean flag "force_prov" from NVS namespace "HConfig".
 *   • If true → caller should enter provisioning mode.
 *
 * Notes:
 *   • This function does NOT automatically clear the flag (R2 semantics).
 *
 * @return true  → provisioning must be forced.
 * @return false → normal boot can proceed.
 */
bool ForceProvisioning();

/**
 * @brief Set or clear the NVS provisioning request flag ("force_prov").
 *
 * Intended usage:
 *   • Hardware button long-press (>5s)
 *   • UI actions (LCD menu, serial console, OTA commands, etc.)
 *
 * @param enable
 *        true  → next boot enters provisioning mode
 *        false → clear provisioning request
 */
void SetForceProvisioning(bool enable);



// ============================================================================
//  Public API — Provisioning button handling
// ============================================================================

/**
 * @brief Monitor the provisioning button (PIN_PROVISIONING).
 *
 * Behavior:
 *   • Pin is expected to be configured as INPUT_PULLUP (HIGH = idle).
 *   • If held LOW for at least PROV_HOLD_MS:
 *        - set NVS flag "force_prov" = true
 *        - wait for release
 *        - restart device (ESP.restart)
 *
 * Notes:
 *   • Should be called regularly from loop().
   *   • One-shot operation: requires a complete press→release cycle
 *     between provisioning triggers.
 */
void pollProvisioningButton();



// ============================================================================
//  Public API — R2 Critical Validation Pass
// ============================================================================

/**
 * @brief Perform an R2-compliant validation pass for all parameters.
 *
 * Validation policy:
 *   • Only parameters with `"critical": true` are evaluated.
 *   • For each such parameter, validateValue() is executed.
 *   • If ANY critical parameter fails, the function returns false.
 *     → Device must fall back to provisioning.
 *   • If all critical parameters pass, returns true.
 *
 * Notes:
 *   • Non-critical parameters do not affect boot validity.
 *   • Provisioning-time writes are not validated — validation occurs at boot.
 *   • This is the R2 replacement for the R1 validateAll() pipeline.
 *
 * @return true  → All critical parameters valid
 * @return false → At least one critical parameter invalid
 */
bool validateR2();

} // namespace HestiaConfig

