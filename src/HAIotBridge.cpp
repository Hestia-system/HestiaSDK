#include <Arduino.h>
#include "HAIotBridge.h"
#include "HestiaCore.h"

// ============================================================================
// HAIoTBridge — Implementation
// ============================================================================

// -----------------------------------------------------------------------------
// Constructor
// -----------------------------------------------------------------------------
// Initializes the bridge from a static BridgeConfig structure.
// All null pointers in cfg are safely replaced with empty strings.
// Computes decimal precision from the resolution string and prepares
// the shortened NVS key used to persist HA_CONTROL values.
//
HAIoTBridge::HAIoTBridge(const BridgeConfig& cfg)
: _name(cfg.name),
  _type(cfg.type),
  _topicTo(cfg.topicTo ? cfg.topicTo : ""),
  _topicFrom(cfg.topicFrom ? cfg.topicFrom : ""),
  _resolution(cfg.resolution ? cfg.resolution : ""),
  _defaultValue(cfg.defaultValue ? cfg.defaultValue : ""),
  _decimals(0),
  _value(""),
  _valueMem(""),
  _initialized(false),
  _logWrites(true)
{
  _decimals = computeDecimals(_resolution);
  _nvsKey = shortenKey(_name);

  Serial.printf("[HAIoTBridge] %-28s → NVS key: %s\n",
                _name.c_str(), _nvsKey.c_str());
}

// -----------------------------------------------------------------------------
// Initialization
// -----------------------------------------------------------------------------
// Restores the persisted value from NVS when the bridge type is HA_CONTROL.
// If no value is found and a default exists, the default value is used.
// For non-control types, only the default value is applied.
// Once initialized, the bridge automatically publishes its current state.
//
void HAIoTBridge::init() {
  if (_type == TypeHA::HA_CONTROL) {
    preferences.begin("Pref", true);
    String val = preferences.getString(_nvsKey.c_str(), "");
    preferences.end();

    if (val.isEmpty() && !_defaultValue.isEmpty()) {
      Serial.printf("  ↳ No NVS value for %s, using default value: %s\n",
                    _name.c_str(), _defaultValue.c_str());
      _value = _defaultValue;
      _valueMem = _defaultValue;
    } else {
      _value = normalize(_decimals, val);
      _valueMem = _value;
      Serial.printf("  ↳ %s restored from NVS, value: %s\n",
                    _name.c_str(), val.c_str());
    }
  } else {
    Serial.printf("  ↳ No NVS restore for: %s\n", _name.c_str());
    _value = _defaultValue;
    _valueMem = _defaultValue;
  }

  _initialized = true;

}

// -------------------------------------------------------------------------
// publishValueToHA
// -------------------------------------------------------------------------

void HAIoTBridge::publishValueToHA(){
  if (_type == TypeHA::HA_CONTROL) {
    publish(_value);
  }
}

// Local write
// -----------------------------------------------------------------------------
// Updates the internal value and publishes it.
// If the bridge is HA_CONTROL, the value is saved to NVS before publishing.
//
void HAIoTBridge::write(const String& v) { 
  _value = v;
  _valueMem = _value;
  if (_type == TypeHA::HA_CONTROL) {
    saveAndPublish(_value);
  } else {
    publish(_value);
  }
}

void HAIoTBridge::write(const char* v) { write(String(v)); }
void HAIoTBridge::write(float v) { write(String(v, (unsigned int)_decimals)); }
void HAIoTBridge::write(int v) { write(String(v)); }
void HAIoTBridge::write(bool v) { write(v ? "ON" : "OFF"); }

// -----------------------------------------------------------------------------
// Change detection
// -----------------------------------------------------------------------------
// Returns true when the current value differs from the last published value.
// - HA_BUTTON fields always trigger a change (stateless behavior).
// - Empty values are ignored.
// -----------------------------------------------------------------------------
bool HAIoTBridge::onChange() {
  if (_value.isEmpty()) return false;

  if (_type == TypeHA::HA_BUTTON) {
    _value = "";
    _valueMem = "";
    return true;
  }

  if (_value == _valueMem) return false;
  _valueMem = _value;
  return true;
}

// -----------------------------------------------------------------------------
// MQTT message handling
// -----------------------------------------------------------------------------
// Processes an incoming MQTT message if the topic matches the bridge's
// input topic (_topicFrom). Returns true only when the message was handled.
//
// Behavior:
//   • Indicators never consume incoming topics.
//   • If the topic matches, the payload is normalized and applied.
//   • HA_CONTROL types persist the value and re-publish.
// -----------------------------------------------------------------------------
bool HAIoTBridge::readMQTT(String &topic, String &payload, bool flushMode) {
  // 1) Check input channel eligibility
  if (_topicFrom.isEmpty() || _type == TypeHA::HA_INDICATOR) {
    return false;
  }
  if (flushMode && _type != TypeHA::HA_ENTITIES) {
    Serial.println("HAIotBridge::messageReceived [flush] " + topic + " - " + payload);
    return false;
  }

  // 2) Check topic match
  if (topic != _topicFrom) {
    return false;   // Not our topic
  }

  // 3) Process message
  // Serial.printf("[MQTT] %s <- %s\n", _name.c_str(), payload.c_str());
  _value = payload;
  
  if (_type == TypeHA::HA_CONTROL) {
    saveAndPublish(_value);
  }
  
  return true;  // Message consumed
}

// -----------------------------------------------------------------------------
// Read operations
// -----------------------------------------------------------------------------
// Lightweight accessors providing the internal value in various formats.
// -----------------------------------------------------------------------------
String HAIoTBridge::read() const { 
  return _value; 
}

int HAIoTBridge::readInt() const { 
  return _value.toInt(); 
}

float HAIoTBridge::readFloat() const { 
  return _value.toFloat(); 
}

bool HAIoTBridge::readBool() const { 
  return _value.equalsIgnoreCase("true")
        || _value.equalsIgnoreCase("on")
        || _value == "1";
}
// -----------------------------------------------------------------------------
// NVS reset
// -----------------------------------------------------------------------------
// Removes the stored value from NVS for this bridge key. This resets the
// HA_CONTROL state so that the next initialization will reload defaults.
// -----------------------------------------------------------------------------
void HAIoTBridge::reset() {
  preferences.begin("Pref", false);
  preferences.remove(_nvsKey.c_str());
  preferences.end();
  _value.clear();
  _valueMem.clear();
}

// -----------------------------------------------------------------------------
// Accessors
// -----------------------------------------------------------------------------
bool HAIoTBridge::isInitialized() const { 
  return _initialized; 
}

const String& HAIoTBridge::name() const { 
  return _name; 
}

const String& HAIoTBridge::topicTo() const { 
  return _topicTo; 
}

const String& HAIoTBridge::topicFrom() const { 
  return _topicFrom; 
}

TypeHA HAIoTBridge::type() const { 
  return _type; 
}

uint8_t HAIoTBridge::decimals() const { 
  return _decimals; 
}

void HAIoTBridge::setLogWrites(bool enable) {
    _logWrites = enable;
}

// ============================================================================
// Internal helpers (private static methods)
// ============================================================================

// -----------------------------------------------------------------------------
// computeDecimals
// -----------------------------------------------------------------------------
// Determines the decimal precision based on a resolution string.
// Example:
//    "0.01" → 2 decimals
//    "1"    → 0 decimals
// -----------------------------------------------------------------------------
uint8_t HAIoTBridge::computeDecimals(const String& res) {
  int p = res.indexOf('.');
  return (p < 0) ? 0 : (uint8_t)(res.length() - p - 1);
}

// -----------------------------------------------------------------------------
// isFloatLike
// -----------------------------------------------------------------------------
// Returns true if the string can be interpreted as a float.
// Rules:
//   • Optional leading minus
//   • At most one decimal point
//   • Must contain at least one digit
// -----------------------------------------------------------------------------
bool HAIoTBridge::isFloatLike(const String& s) {
  if (s.isEmpty()) return false;
  bool pointSeen = false, signSeen = false, digitSeen = false;
  for (size_t i = 0; i < s.length(); ++i) {
    char c = s[i];
    if (c == '-' && i == 0 && !signSeen) { 
      signSeen = true; 
      continue; 
    }
    if (c == '.') { 
      if (pointSeen) return false; 
      pointSeen = true; 
      continue; 
    }
    if (c < '0' || c > '9') return false;
    digitSeen = true;
  }
  return digitSeen;
}

// -----------------------------------------------------------------------------
// normalize
// -----------------------------------------------------------------------------
// Normalizes numeric values according to the decimal precision.
// For float-like strings, the value is formatted with the given number of
// decimals; otherwise, the string is returned untouched.
// -----------------------------------------------------------------------------
String HAIoTBridge::normalize(uint8_t dec, const String& s) {
  if (isFloatLike(s)) {
    return String(s.toFloat(), (unsigned int)dec);
  }
  return s;
}

// -----------------------------------------------------------------------------
// shortenKey
// -----------------------------------------------------------------------------
// Produces a compact NVS key (max 15 chars).
// If the full name exceeds this length, the function keeps the last 14
// characters and appends a checksum digit to avoid collisions.
// -----------------------------------------------------------------------------
String HAIoTBridge::shortenKey(const String& full) {
  if (full.length() <= 15) return full;

  uint8_t sum = 0;
  for (size_t i = 0; i < full.length(); ++i) sum += full[i];

  String suffix = String(sum % 10);
  return full.substring(full.length() - 14) + suffix;
}

// -----------------------------------------------------------------------------
// saveAndPublish
// -----------------------------------------------------------------------------
// Persists the value to NVS for HA_CONTROL bridges (using the shortened key),
// then publishes the value to MQTT via HestiaCore.
// -----------------------------------------------------------------------------
void HAIoTBridge::saveAndPublish(const String& val) {
  if (_nvsKey.length() <= 15 && _type == TypeHA::HA_CONTROL) {
    preferences.begin("Pref", false);
    preferences.putString(_nvsKey.c_str(), val);
    preferences.end();
  }

  publish(val);
}

// -----------------------------------------------------------------------------
// publish
// -----------------------------------------------------------------------------
// Publishes the value to the configured output MQTT topic.
// Logging is optional and controlled by setLogWrites().
// -----------------------------------------------------------------------------
void HAIoTBridge::publish(const String& val) {

  if (_topicTo.length() == 0) return;
    // Serial.printf("[HAIoTBridge::publish] %s -> %s\n", _topicTo.c_str(), val.c_str());
    HestiaCore::publishToMQTT(_topicTo, val, _logWrites);
  }


