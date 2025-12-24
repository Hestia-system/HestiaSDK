#include "HestiaParam.h"
#include <Preferences.h>

// NVS namespace used for all configuration parameters.
static constexpr const char* NAMESPACE = "HConfig";


/**
 * ============================================================================
 *  Constructor — Build a parameter from a JSON schema entry
 * ============================================================================
 *
 * The JSON object must define:
 *    - key           (string)
 *    - type          (string: "string", "int", "float", "bool")
 *    - label         (string, optional)
 *    - provisioning  (bool)
 *    - required      (bool)
 *    - critical      (bool)
 *    - default       (string)
 *    - decimals      (int, optional)
 *    - pattern       (string: ip, hostname, anything…)
 *
 * Optional validator object:
 *    "validate": {
 *        "minLen": n,
 *        "maxLen": n,
 *        "min"   : n,
 *        "max"   : n
 *    }
 */
HestiaParam::HestiaParam(const JsonObject& obj)
{
    key          = obj["key"]          | "";
    type         = obj["type"]         | "";
    label        = obj["label"]        | key;
    provisioning = obj["provisioning"] | false;
    required     = obj["required"]     | false;
    critical     = obj["critical"]     | false;

    defaultValue = obj["default"] | "";
    _value       = defaultValue;

    decimals     = obj["decimals"] | 0;
    pattern      = obj["pattern"]  | "anything";

    // Parse optional validation rules
    if (obj.containsKey("validate")) {
        JsonObject v = obj["validate"];

        if (v.containsKey("minLen")) validators.minLen = v["minLen"];
        if (v.containsKey("maxLen")) validators.maxLen = v["maxLen"];

        if (v.containsKey("min")) {
            validators.min    = v["min"];
            validators.hasMin = true;
        }
        if (v.containsKey("max")) {
            validators.max    = v["max"];
            validators.hasMax = true;
        }
    }
}


/**
 * ============================================================================
 *  loadFromNVS()
 * ============================================================================
 *
 * Loads (or initializes) the parameter from NVS.
 *
 * Behavior:
 *   provisioning == false  → bypass (device-level constants)
 *
 *   provisioning == true:
 *      • If NVS contains a value → load into _value
 *      • If no value exists AND lazyInit == true → write defaultValue to NVS
 *
 * This allows runtime defaults to be applied without requiring provisioning.
 */
void HestiaParam::loadFromNVS(bool lazyInit)
{
    if (!provisioning) return;

    Preferences prefs;
    prefs.begin(NAMESPACE, false);

    String k = HestiaParam::nvsKey(key);

    if (prefs.isKey(k.c_str())) {
        _value = prefs.getString(k.c_str(), _value);
    }
    else if (lazyInit) {
        prefs.putString(k.c_str(), _value);
    }

    prefs.end();
}


/**
 * ============================================================================
 *  saveToNVS()
 * ============================================================================
 *
 * Writes the current value (_value) to the NVS store.
 * Used when provisioning form is submitted.
 */
void HestiaParam::saveToNVS()
{
    Preferences prefs;
    prefs.begin(NAMESPACE, false);
    String k = HestiaParam::nvsKey(key);
    prefs.putString(k.c_str(), _value);
    prefs.end();
}


/**
 * ============================================================================
 *  WRITE API — Core method
 * ============================================================================
 */
bool HestiaParam::write(const String& v)
{
    String x = v;
    x.trim();

    // -----------------------------------------------------
    // 1) Normalisation booléenne (optionnelle mais utile)
    // -----------------------------------------------------
    if (type == "bool") {
        String low = x;
        low.toLowerCase();

        if (low == "true" || low == "on" || low == "1") {
            _value = "true";
            return true;
        }

        if (low == "false" || low == "off" || low == "0") {
            _value = "false";
            return true;
        }

        // toute autre valeur bool est acceptée telle quelle
        // la validation décidera
        _value = x;
        return true;
    }

    // -----------------------------------------------------
    // 2) Tous les autres types : stockage brut
    // -----------------------------------------------------
    _value = x;
    return true;
}




/**
 * ============================================================================
 *  WRITE overloads — forward to core String write()
 * ============================================================================
 */
bool HestiaParam::write(int v){ return write(String(v));}
bool HestiaParam::write(long v){ return write(String(v));}
bool HestiaParam::write(float v){ return write(formatNumber(v)); }
bool HestiaParam::write(double v){ return write(formatNumber(v)); }
bool HestiaParam::write(bool v){ return write(v ? "true" : "false"); }

/**
 * ============================================================================
 *  formatNumber() — format a number according to decimal precision
 * ============================================================================
 */
String HestiaParam::formatNumber(double v) const
{
    if (decimals == 0) {
        return String((long)v);   // no decimals
    }
    return String(v, (unsigned int)decimals);
}


/**
 * ============================================================================
 *  READ API — typed accessors
 * ============================================================================
 */
int HestiaParam::readInt() const { return _value.toInt();}
// Arduino String has no toLong(), so reuse toInt().
long HestiaParam::readLong() const { return (long)_value.toInt();} 
float HestiaParam::readFloat() const { return _value.toFloat(); }
double HestiaParam::readDouble() const { return atof(_value.c_str()); }

bool HestiaParam::readBool() const
{
    String v = _value;
    v.toLowerCase();
    v.trim();

    if (v == "true" || v == "1" || v == "on")
        return true;

    if (v == "false" || v == "0" || v == "off")
        return false;

    // fallback : toute autre valeur est considérée false
    return false;
}


/**
 * ============================================================================
 *  VALIDATION: Pattern rules
 * ============================================================================
 */
bool HestiaParam::validatePattern(const String& candidate) const
{
    if (pattern == "anything")
        return true;

    if (pattern == "bool")
        return (candidate == "true" || candidate == "false");

    if (pattern == "ip") {
        int a,b,c,d;
        if (sscanf(candidate.c_str(), "%d.%d.%d.%d", &a,&b,&c,&d) != 4)
            return false;
        return (a>=1&&a<=255 &&
                b>=1&&b<=255 &&
                c>=1&&c<=255 &&
                d>=1&&d<=255);
    }

    if (pattern == "hostname") {
        if (candidate.length() < 1 || candidate.length() > 64)
            return false;
        for (char c : candidate) {
            if (!(isalnum(c) || c=='-' || c=='.'))
                return false;
        }
        return true;
    }

    return true;
}

/**
 * ============================================================================
 *  VALIDATION: Range rules (min / max / minLen / maxLen)
 * ============================================================================
 */
bool HestiaParam::validateRange(const String& candidate) const
{
    if (type == "string") {
        if (validators.minLen >= 0 &&
            candidate.length() < (size_t)validators.minLen)
            return false;

        if (validators.maxLen >= 0 &&
            candidate.length() > (size_t)validators.maxLen)
            return false;

        return true;
    }

    if (type == "number") {
        double v = candidate.toFloat();

        if (validators.hasMin && v < validators.min) return false;
        if (validators.hasMax && v > validators.max) return false;

        return true;
    }

    return true;
}

/**
 * ============================================================================
 *  VALIDATION: Full rule set
 * ============================================================================
 */
bool HestiaParam::validate(const String& candidate) const
{
    if (required && candidate.length() == 0)
        return false;

    if (!validatePattern(candidate))
        return false;

    return validateRange(candidate);
}

/**
 * ============================================================================
 *  VALIDATION: Full rule set validate _value
 * ============================================================================
 */
bool HestiaParam::validateValue() const
{
    return validate(_value);
}

/**
 * ============================================================================
 *  NVSKEY: max 15 characters
 * ============================================================================
 */
String HestiaParam::nvsKey(const String& jsonKey)
{
    // Limite NVS = 15 caractères (ESP-IDF)
    if (jsonKey.length() <= 15)
        return jsonKey;

    // Compatibilité R1 : garder les 15 derniers caractères
    return jsonKey.substring(jsonKey.length() - 15);
}
