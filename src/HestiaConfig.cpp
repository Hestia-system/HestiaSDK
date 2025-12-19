#include <Arduino.h>
#include "HestiaConfig.h"
#include <ArduinoJson.h>
#include <Preferences.h>
#include "HestiaTempo.h"
using Tempo::literals::operator"" _id;

namespace {

  // ============================================================================
  //  JSON schema stored in PROGMEM (see HestiaConfigSchema.h)
  //
  //  Expected top-level structure:
  //    {
  //      "version": <uint>,
  //      "params": [ { ... }, { ... }, ... ]
  //    }
  // ============================================================================

  // Schema initialization state
  bool schemaInitialized = false;

  // Global parameter table (raw metadata + values)
  std::vector<HestiaConfig::ParamInstance> g_params;

  // NVS
  const char* NVS_NAMESPACE = "HConfig";
  Preferences prefs;

  // ============================================================================
  //  Helpers — string → ParamType / PatternType
  // ============================================================================

  HestiaConfig::ParamType parseParamType(const char* s) {
    if (!s) return HestiaConfig::ParamType::STRING;
    if (strcmp(s, "string") == 0) return HestiaConfig::ParamType::STRING;
    if (strcmp(s, "int")    == 0) return HestiaConfig::ParamType::INT;
    if (strcmp(s, "bool")   == 0) return HestiaConfig::ParamType::BOOL;
    if (strcmp(s, "float")  == 0) return HestiaConfig::ParamType::FLOAT;
    return HestiaConfig::ParamType::STRING;
  }

  HestiaConfig::PatternType parsePatternType(const char* s) {
    if (!s) return HestiaConfig::PatternType::ANYTHING;
    if (strcmp(s, "ip")       == 0) return HestiaConfig::PatternType::IP;
    if (strcmp(s, "hostname") == 0) return HestiaConfig::PatternType::HOSTNAME;
    if (strcmp(s, "url")      == 0) return HestiaConfig::PatternType::URL;
    if (strcmp(s, "email")    == 0) return HestiaConfig::PatternType::EMAIL;
    if (strcmp(s, "anything") == 0) return HestiaConfig::PatternType::ANYTHING;
    return HestiaConfig::PatternType::ANYTHING;
  }

  const __FlashStringHelper* patternToFString(HestiaConfig::PatternType p) {
    switch (p) {
      case HestiaConfig::PatternType::IP:        return F("ip");
      case HestiaConfig::PatternType::HOSTNAME:  return F("hostname");
      case HestiaConfig::PatternType::URL:       return F("url");
      case HestiaConfig::PatternType::EMAIL:     return F("email");
      case HestiaConfig::PatternType::ANYTHING:
      default:                                   return F("anything");
    }
  }

  const __FlashStringHelper* typeToFString(HestiaConfig::ParamType t) {
    switch (t) {
      case HestiaConfig::ParamType::STRING: return F("string");
      case HestiaConfig::ParamType::INT:    return F("int");
      case HestiaConfig::ParamType::BOOL:   return F("bool");
      case HestiaConfig::ParamType::FLOAT:  return F("float");
      default:                              return F("string");
    }
  }

} // anonymous namespace



namespace HestiaConfig {

  // Global list of instantiated parameters from DeviceParams + provisioning schema
  std::vector<HestiaParam*> _params;


  // ============================================================================
  //  ForceProvisioning — check whether provisioning must be forced
  // ============================================================================
  //
  //  Behavior:
  //    • Reads boolean flag "force_prov" from the NVS namespace "HConfig".
  //    • If true → value is kept and returned.
  //    • Otherwise → returns false.
  //
  //  Note:
  //    The caller may clear the flag afterward if needed.
  // ============================================================================

  bool ForceProvisioning() {
      Preferences prefs;
      prefs.begin("HConfig", false);
      bool v = prefs.getBool("force_prov", false);
      prefs.end();
      return v;
  }


  // ============================================================================
  //  SetForceProvisioning — explicitly set provisioning-flag in NVS
  // ============================================================================
  //
  //  Behavior:
  //    • Stores boolean flag under "force_prov"
  //    • Small delay ensures safe flash write commit
  // ============================================================================

  void SetForceProvisioning(bool enable) {
      Preferences prefs;
      prefs.begin("HConfig", false);
      prefs.putBool("force_prov", enable);
      delay(30);
      prefs.end();
  }


  // ============================================================================
  //  pollProvisioningButton — long-press provisioning trigger
  // ============================================================================
  //
  //  Conditions:
  //    • If pin_provisioning < 0 → feature disabled
  //    • Pull-up logic:
  //         HIGH = idle
  //         LOW  = pressed
  //
  //  Sequence:
  //    • Detect press start
  //    • If held ≥ prov_hold_ms → set force_prov flag
  //    • On release (and if validated) → restart the ESP
  //
  //  This allows hardware-based reset into provisioning mode.
  // ============================================================================

  void pollProvisioningButton() {
    if (HestiaConfig::getParamObj("pin_provisioning")->readInt() < 0) return;

    int pin = HestiaConfig::getParamObj("pin_provisioning")->readInt();
    int level = digitalRead(pin);
    bool pressed = (level == LOW);

    static bool wasPressed = false;
    static bool holdValidated = false;


    // Press start
    if (pressed && !wasPressed) {
        wasPressed = true;
        Tempo::oneShot("PROV_BUT_DELAY"_id).start(HestiaConfig::getParamObj("prov_hold_ms")->readInt()); 
        holdValidated = false;
        return;
    }

    // Button held
    if (pressed && wasPressed && !holdValidated) {
        int holdMs = HestiaConfig::getParamObj("prov_hold_ms")->readInt();
        if (Tempo::oneShot("PROV_BUT_DELAY"_id).done()) {
            Serial.println(F("[HestiaConfig] Long press detected, force provisioning enabled."));
            SetForceProvisioning(true);
            holdValidated = true;
        }
        return;
    }

    // Release
    if (!pressed && wasPressed) {
        wasPressed = false;

        if (holdValidated) {
            Serial.println(F("[HestiaConfig] Button released, restarting..."));
            holdValidated = false;
            delay(100);
            ESP.restart();
        }
        return;
    }
  }



  // ============================================================================
//  loadDeviceParams — Load device-level parameters from JSON definition.
// ---------------------------------------------------------------------------
//  This function parses the static JSON schema embedded in DeviceParams.h,
//  instantiates one HestiaParam object per entry, and populates the internal
//  registry (_params). All previously allocated parameters are destroyed.
//
//  Notes:
//    • This stage handles ONLY firmware-level parameters ("provisioning": false).
//    • User-facing provisioning parameters are still handled by the legacy
//      ParamInstance / ParamMeta mechanism until full migration is completed.
// ============================================================================
bool loadDeviceParams(const char* json)
{
    Serial.println(F("[HestiaConfig] === Start loadDeviceParams ===\n"));

    // ----------------------------------------------------------------------
    // 1) Guard: empty or null JSON
    // ----------------------------------------------------------------------
    if (!json || json[0] == '\0') {
        Serial.println(F("[HestiaConfig] ERROR: DeviceParams JSON is empty."));
        Serial.println(F("[HestiaConfig] === End loadDeviceParams (ABORT) ==="));
        return false;
    }

    // ----------------------------------------------------------------------
    // 2) Parse JSON document
    // ----------------------------------------------------------------------
    DynamicJsonDocument doc(8192);
    DeserializationError err = deserializeJson(doc, json);
    if (err) {
        Serial.printf("[HestiaConfig] ERROR: JSON parse failure: %s\n", err.c_str());
        Serial.println(F("[HestiaConfig] === End loadDeviceParams (ABORT) ==="));
        return false;
    }

    JsonArray arr = doc["params"].as<JsonArray>();
    if (arr.isNull()) {
        Serial.println(F("[HestiaConfig] ERROR: 'params' array not found."));
        Serial.println(F("[HestiaConfig] === End loadDeviceParams (ABORT) ==="));
        return false;
    }

    // ----------------------------------------------------------------------
    // 3) Clear previous parameters
    // ----------------------------------------------------------------------
    for (auto* p : _params) {
        delete p;
    }
    _params.clear();

    Serial.println(F("[HestiaConfig] Instantiating parameters..."));

    // ----------------------------------------------------------------------
    // 4) Instantiate each HestiaParam object
    // ----------------------------------------------------------------------
    for (JsonObject obj : arr) {
        HestiaParam* p = new HestiaParam(obj);

        // Allow provisioning parameters (if any) to load their stored value
        p->loadFromNVS(true);   // lazy-init enabled

        _params.push_back(p);

        Serial.printf("  %s → %s : %s\n",
                      p->provisioning ? "NVS" : "json",
                      p->key.c_str(),
                      p->read().c_str());
    }

    // ----------------------------------------------------------------------
    // 5) Final summary
    // ----------------------------------------------------------------------
    Serial.printf("[HestiaConfig] %u device parameters loaded.\n",
                  (unsigned)_params.size());
    Serial.println(F("[HestiaConfig] === End loadDeviceParams ===\n"));

    return true;
}



// ============================================================================
//  getParam — Retrieve a parameter value by key
// ============================================================================
String getParam(const String& key)
{
    for (auto* p : _params) {
        if (p->key == key)
            return p->read();
    }
    return "";
}



// ============================================================================
//  setParam — Assign a value to a parameter by key
// ============================================================================
bool setParam(const String& key, const String& value)
{
    for (auto* p : _params) {
        if (p->key == key) {
            return p->write(value);
        }
    }
    return false;
}



// ============================================================================
//  getParamObj — Retrieve the HestiaParam object associated with a key
// ============================================================================
HestiaParam* getParamObj(const String& key)
{
    for (auto* p : _params) {
        if (p->key == key)
            return p;
    }

    Serial.printf("[HestiaConfig][ERROR] Param '%s' not found. Using NULL param.\n",
                  key.c_str());

    return nullptr;
}



// ============================================================================
//  validateR2 — Validate all CRITICAL parameters
// ---------------------------------------------------------------------------
//  Only parameters marked as "critical" are validated here.
//
//  Behavior:
//    • If any critical parameter fails validation → return false
//    • Otherwise → return true
//
//  Notes:
//    • Non-critical parameters are ignored by R2 validation.
//    • Detailed validation logic resides inside HestiaParam::validateValue().
// ============================================================================
bool validateR2()
{
    for (HestiaParam* p : _params) {
        if (!p) continue;

        if (p->critical && !p->validateValue()) {
            Serial.printf("[HestiaConfig] R2 validation failed: %s → %s\n",
                          p->key.c_str(), p->read().c_str());
            return false;
        }
    }

    return true;
}


} // namespace HestiaConfig
// ============================================================================
