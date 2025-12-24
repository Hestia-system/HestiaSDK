#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>

/**
 * ============================================================================
 *  HestiaParam R2 — Self-contained configuration parameter with NVS awareness
 * ============================================================================
 *
 *  Overview:
 *    Represents a single configuration parameter as defined by the device’s
 *    JSON schema (DeviceParams.json). Each instance contains:
 *      • Static metadata  (key, type, rules, defaults)
 *      • Runtime value    (always stored as a String)
 *      • Optional NVS persistence for provisioning parameters
 *
 *  Responsibilities:
 *    • Hold and manage the parameter’s current value
 *    • Load and save provisioning parameters to NVS
 *    • Perform type-aware formatting (float precision)
 *    • Enforce validation rules (required, length, range, pattern)
 *
 *  Notes:
 *    • R2 separates “write” from “validation”. A parameter may hold an invalid
 *      value during provisioning; validation occurs only at boot via R2 logic.
 *    • All values are stored as String for uniform handling across types.
 */
class HestiaParam {
public:

    // ---- Construct from JSON schema entry ----
    HestiaParam(const JsonObject& obj);

    // ---- Load value from NVS (lazyInit writes default if missing) ----
    void loadFromNVS(bool lazyInit);

    // ---- Persist current value into NVS ----
    void saveToNVS();

    // ---- Write API (all supported types) ----
    bool write(const String& v);
    bool write(const char* v)      { return write(String(v)); }
    bool write(int v);
    bool write(long v);
    bool write(float v);
    bool write(double v);
    bool write(bool v);

    // ---- Read API ----
    String read()    const { return _value; }
    int    readInt() const;
    long   readLong() const;
    float  readFloat() const;
    double readDouble() const;
    bool   readBool() const;

    // ---- Validation API ----
    bool validate(const String& candidate) const;
    bool validateValue() const;        // validate current internal value

    // ---- Public metadata extracted from schema ----
    String key;            ///< Unique identifier (NVS key)
    String type;           ///< Raw schema type ("string", "int", ...)
    String label;          ///< Human-readable label (i18n-ready)
    bool   provisioning = false;  ///< true → persisted via NVS
    bool   required     = false;  ///< missing value must be filled in provisioning
    bool   critical     = false;  ///< invalid → force device provisioning
    String defaultValue;          ///< schema default

private:

    // ---- Runtime storage ----
    String _value;

    // ---- Float formatting ----
    int decimals = 0;              // configured precision for numeric types
    String formatNumber(double v) const;

    // ---- Pattern and range validation helpers ----
    bool validatePattern(const String& candidate) const;
    bool validateRange(const String& candidate) const;

    // ---- Validation rules extracted from schema ----
    struct {
        int     minLen = -1;
        int     maxLen = -1;
        double  min    = 0;
        double  max    = 0;
        bool    hasMin = false;
        bool    hasMax = false;
    } validators;

    String pattern = "anything";   // schema pattern (ip, hostname, bool, …)

    // ---- max 15 characters for nvskey ----
    static String nvsKey(const String &jsonKey);
};
