#pragma once

#include <Preferences.h>
#include <ArduinoJson.h>
#include <Arduino.h>   // Required for uint8_t and String

// ============================================================================
//  File   : HAIoTBridge.h
//  Project: Hestia SDK / Virgo Template
//  Version: 1.2
//  Purpose: Unified management of MQTT-linked entities acting as a bridge
//           between the Virgo device and Home Assistant.
// ============================================================================

// ============================================================================
// TypeHA — Behavior model for Home Assistant entities
// ============================================================================
enum class TypeHA : uint8_t {
  HA_CONTROL = 0,   // Read/write (switch, number, select)
  HA_INDICATOR,     // Read-only (sensor)
  HA_BUTTON,        // Stateless trigger
  HA_ENTITIES       // Internal entities managed by HAIoTBridge
};

inline const char* typeHA_to_string(TypeHA type) {
  switch(type) {
    case TypeHA::HA_CONTROL:   return "CONTROL";
    case TypeHA::HA_INDICATOR: return "INDICATOR";
    case TypeHA::HA_BUTTON:    return "BUTTON";
    case TypeHA::HA_ENTITIES:  return "ENTITIES";
    default:                   return "UNKNOWN";
  }
}

// ============================================================================
// BridgeConfig — Static configuration describing an entity
// ============================================================================
struct BridgeConfig {
  const char* name;         // Stable internal name
  TypeHA      type;         // Entity behavior type
  const char* topicTo;      // MQTT state topic (device → HA)
  const char* topicFrom;    // MQTT command topic (HA → device)
  const char* resolution;   // Optional numeric resolution
  const char* defaultValue; // Default applied if no NVS entry exists
};

// Forward declaration
class HAIoTBridge;

namespace HestiaCore {
  void publishToMQTT(const String& topic, const String& payload);
}

// ============================================================================
//  Class : HAIoTBridge
// ----------------------------------------------------------------------------
// Represents a Home Assistant entity (sensor, switch, button, etc.).
// Each instance corresponds to one entry in bridge_config[].
//
// Responsibilities:
//   • Local persistence of values through NVS
//   • MQTT publish/subscribe handling
//   • Normalization of numeric/string/boolean formats
//   • Providing a stable unique identifier per entity
// ============================================================================
class HAIoTBridge {
public:

  // -------------------------------------------------------------------------
  // Constructor
  // -------------------------------------------------------------------------
  /**
   * @brief Construct a bridge entity from a BridgeConfig descriptor.
   *
   * @param cfg Configuration entry describing the entity.
   */
  HAIoTBridge(const BridgeConfig& cfg);

  // -------------------------------------------------------------------------
  // Initialization
  // -------------------------------------------------------------------------
  /**
   * @brief Initialize the entity value.
   *
   * Behavior:
   *   • HA_CONTROL: restore value from NVS if present, otherwise use default.
   *   • Others: always use the default value.
   *
   * After initialization:
   *   • The bridge is marked initialized.
   *   • The current value is immediately published to MQTT.
   */
  void init();

  // -------------------------------------------------------------------------
  // Local write (internal logic or sensor updates)
  // -------------------------------------------------------------------------
  /**
   * @brief Write a new value coming from local logic (not MQTT).
   *
   * Value is normalized and:
   *   • HA_CONTROL → persisted to NVS and published
   *   • Other types → published only
   */
  void write(const String& v);

  void write(const char* v);     ///< Convenience overload
  void write(float v);           ///< Convenience overload
  void write(int v);             ///< Convenience overload

  /**
   * @brief Boolean overload.
   * Maps true → "ON", false → "OFF".
   */
  void write(bool v);

  // -------------------------------------------------------------------------
  // Change detection
  // -------------------------------------------------------------------------
  /**
   * @brief Detect if the current value differs from the last known value.
   *
   * Behavior:
   *   • HA_BUTTON → any non-empty value triggers true once, then resets.
   *   • Others    → returns true if _value ≠ _valueMem.
   *
   * @return true if a change is detected.
   */
  bool onChange();

  // -------------------------------------------------------------------------
  // MQTT read handler
  // -------------------------------------------------------------------------
  /**
   * @brief Process an incoming MQTT message.
   *
   * The message is consumed only if:
   *   • the entity has an input topic (_topicFrom),
   *   • the entity is not an indicator,
   *   • the topic matches.
   *
   * For HA_CONTROL, the value is persisted after normalization.
   *
   * @return true if this bridge handled the message.
   */
  bool readMQTT(String& topic, String& payload, bool flushMode);

  // -------------------------------------------------------------------------
// Read accessors
// -------------------------------------------------------------------------
/**
 * @brief Retrieve the current value as a String.
 * @return The current stored value.
 */
String read() const;

/**
 * @brief Retrieve the current value as an integer.
 * @return Value converted using String::toInt().
 */
int readInt() const;

/**
 * @brief Retrieve the current value as a float.
 * @return Value converted using String::toFloat().
 */
float readFloat() const;

/**
 * @brief Retrieve the current value as a boolean.
 *
 * Evaluation rules:
 *   • "ON" (case-insensitive) → true
 *   • "1"                     → true
 *   • anything else           → false
 *
 * @return Boolean interpretation of the current value.
 */
bool readBool() const;


// -------------------------------------------------------------------------
// NVS reset
// -------------------------------------------------------------------------
/**
 * @brief Remove the NVS entry associated with this entity.
 *
 * Also clears the in-memory value and its cached mirror.
 * Next initialization will reload the default value.
 */
void reset();

// -------------------------------------------------------------------------
// publishStateToHA
// -------------------------------------------------------------------------
/**
   * @brief Publish the value to MQTT using the configured state topic.
   *
   * No action is taken if the entity has no outbound topic.
   */
  void publishValueToHA();

// -------------------------------------------------------------------------
// Accessors
// -------------------------------------------------------------------------
/**
 * @brief Check whether the entity has completed its initialization sequence.
 * @return true if init() has been executed.
 */
bool isInitialized() const;

/**
 * @brief Get the logical identifier of the entity.
 * @return Internal name reference.
 */
const String& name() const;

/**
 * @brief Get the MQTT state-publish topic (device → Home Assistant).
 * @return Reference to the outbound topic.
 */
const String& topicTo() const;

/**
 * @brief Get the MQTT command topic (Home Assistant → device).
 * @return Reference to the inbound topic.
 */
const String& topicFrom() const;

/**
 * @brief Get the entity's behavioral type within the HA bridge model.
 * @return TypeHA enumeration value.
 */
TypeHA type() const;

/**
 * @brief Get the decimal precision used when normalizing numeric values.
 * @return Number of decimals.
 */
uint8_t decimals() const;

/**
 * @brief Enable or disable logging for outgoing publish operations.
 * @param enable True → log writes ; False → silent mode.
 */
void setLogWrites(bool enable);


private:
  // ========================================================================
  // Internal data
  // ========================================================================
  String   _name;          // Logical entity name
  TypeHA   _type;          // Behavior model (CONTROL, INDICATOR, etc.)
  String   _topicTo;       // MQTT → HA (state)
  String   _topicFrom;     // MQTT ← HA (commands)
  String   _resolution;    // Optional numeric resolution
  String   _defaultValue;  // Default applied when no NVS entry exists
  String   _deviceId;      // Reserved / not used in R2
  String   _uniqueId;      // Reserved / not used in R2
  String   _nvsKey;        // Compact NVS identifier (<=15 chars)

  uint8_t  _decimals;      // Decimal precision derived from resolution

  Preferences preferences; // NVS handler

  String   _value;         // Current value
  String   _valueMem;      // Last published / acknowledged value

  bool     _initialized;   // Set once init() completes
  bool     _logWrites = true; // Enable/disable publish logging


  // ========================================================================
  // Internal helpers
  // ========================================================================
  /**
   * @brief Compute the number of decimals implied by a resolution string.
   *
   * Examples:
   *   "1"    → 0
   *   "0.1"  → 1
   *   "0.01" → 2
   */
  static uint8_t computeDecimals(const String& res);

  /**
   * @brief Determine whether a string represents a valid float.
   *
   * Rules:
   *   • optional leading '-'
   *   • at most one '.'
   *   • must contain at least one digit
   */
  static bool isFloatLike(const String& s);

  /**
   * @brief Normalize a value based on decimal precision.
   *
   * If the input is float-like, it is rendered using @p dec digits.
   * Otherwise, the value is returned unchanged.
   */
  static String normalize(uint8_t dec, const String& s);

  /**
   * @brief Produce a compact NVS-compliant key (≤15 characters).
   *
   * If the full key is too long:
   *   • keep the last 14 characters,
   *   • append a checksum digit (sum modulo 10).
   *
   * Guarantees stability while reducing collision probability.
   */
  static String shortenKey(const String& full);

  /**
   * @brief Persist the value to NVS (if applicable) and publish it to MQTT.
   *
   * HA_CONTROL entities store the value before publishing.
   */
  void saveAndPublish(const String& val);

  /**
   * @brief Publish the value to MQTT using the configured state topic.
   *
   * No action is taken if the entity has no outbound topic.
   */
  void publish(const String& val);
};
// ============================================================================