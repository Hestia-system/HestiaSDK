



#pragma once
#include <Arduino.h>
#include "HAIotBridge.h"
#include "HestiaCore.h"
#include "HestiaNetSDK.h"
#include "HardwareInit.h"
#include "HestiaConfig.h"

/*****************************************************************************************
 *  File     : main.h
 *  Project  : Hestia SDK / Virgo IoT
 *
 *  Summary:
 *  --------
 *  Central definitions for:
 *    • The bridge_config[] table (static list of HAIoTBridge entities)
 *    • Compile-time HA Discovery JSON (published once, retained)
 *    • Constants used by main.cpp
 *
 *  Notes:
 *    • bridge_config[] is injected into the SDK via HestiaCore::loadBridgeConfig()
 *    • Each entry corresponds to one HAIoTBridge instance created at startup
 *    • All parameters in this file must be PROGMEM-safe — no heap allocation
 *****************************************************************************************/


// ============================================================================
//  Bridge Entity Table — Static HAIoTBridge configuration
//  ----------------------------------------------------------------------------
//  Each entry describes a single Home Assistant entity exposed by the device.
//  Format: { internalName, typeHA, topicTo, topicFrom, resolution, defaultValue }
//
//  All fields are passed verbatim to the HAIoTBridge constructor.
//
//  This table must remain PROGMEM-resident: entities are instantiated once and
//  never modified at runtime.
// ============================================================================

static const BridgeConfig bridge_config[] PROGMEM = {

    // ------------------------------------------------------------------------
    //  System Diagnostics Entities
    // ------------------------------------------------------------------------
    { "IotBridge_HA_online",
      TypeHA::HA_ENTITIES,
      "",
      "HA/domotique/online",
      "",
      "false"
    },

    { "IotBridge_HA_heartbeat",
      TypeHA::HA_ENTITIES,
      "",
      "HA/Heartbeat/fromHA",
      "",
      "0"
    },
    
    { "IotBridge_restartLog",
      TypeHA::HA_ENTITIES,
      "VIot/restartLog/toHESTIASDK",
      "",
      "",
      "false"
    },

    { "IotBridge_iotHeartbeat",
      TypeHA::HA_ENTITIES,
      "Virgo/iotHeartbeat/toHA",
      "",
      "",
      ""
    },


    // ------------------------------------------------------------------------
    //  Indicators (read-only from HA perspective)
    // ------------------------------------------------------------------------
    { "IotBridge_ip",
      TypeHA::HA_INDICATOR,
      "Virgo/ip/toHA",
      "",
      "",
      "0.0.0.0"
    },

    { "IotBridge_SW_version",
      TypeHA::HA_INDICATOR,
      "Virgo/SW_version/toHA",
      "",
      "",
      "v"
    },

    // ------------------------------------------------------------------------
    //  Controls (writeable via HA MQTT)
    // ------------------------------------------------------------------------
    { "IotBridge_OTA",
      TypeHA::HA_BUTTON,
      "Virgo/OTA/toHA",
      "Virgo/OTA/fromHA",
      "",
      ""
    }
};

// Number of bridge entries (static, compile-time)
static const size_t BRIDGE_COUNT =
    sizeof(bridge_config) / sizeof(BridgeConfig);


// ============================================================================
//  Home Assistant Discovery JSON
//  ----------------------------------------------------------------------------
//  This JSON block is published ONCE at boot via MQTT Discovery.
//  It informs Home Assistant how to auto-create entities and device metadata.
//
//  The definition must be stored as:
//    • PROGMEM
//    • const char[]
//    • Null-terminated
//
//  The actual JSON structure is injected through HestiaNet::loadDiscoveryJson()
//  in main.cpp.
// ============================================================================


static const char config_json[] PROGMEM = R"rawliteral(
{
  "device": {
    "identifiers": "Virgo",
    "name": "Virgo",
    "manufacturer": "Jacques Bherer",
    "model": "Hestia SDK Device",
    "sw_version": "1.0.0"
  },
  "o": { "name": "Virgo" },
  "cmps": {
    "ip": {
      "p": "sensor",
      "name": "ip",
      "unique_id": "Virgo_IP Address",
      "stat_t": "Virgo/ip/toHA"
    },
    "log": {
      "p": "sensor",
      "name": "log",
      "unique_id": "Virgo_log",
      "stat_t": "Virgo/log/toHA"
    },
    "iotHeartbeat": {
      "p": "sensor",
      "name": "iotHeartbeat",
      "unique_id": "Virgo_iotHeartbeat",
      "stat_t": "Virgo/iotHeartbeat/toHA"
    },
    "SW_version": {
      "p": "sensor",
      "name": "SW_version",
      "unique_id": "Virgo_SW_version",
      "stat_t": "Virgo/SW_version/toHA",
        "availability": [
        {
          "topic": "Virgo/availability"
        }
      ]
    },
    "OTA": {
      "p": "button",
      "name": "OTA update",
      "icon": "mdi:cellphone-arrow-down",
      "unique_id": "Virgo_OTA2",
      "stat_t": "Virgo/OTA/toHA",
      "cmd_t": "Virgo/OTA/fromHA",
        "availability": [
        {
          "topic": "Virgo/availability"
        }
      ]
    }
  }
}
)rawliteral";



// ============================================================================
// Forward Declarations
// ============================================================================
class HAIoTBridge;

namespace HestiaCore {
  HAIoTBridge* get(const String& name);
}

// ============================================================================
// Macros — Simplified Entity Access
// ============================================================================
#define HA(name) HestiaCore::get("IotBridge_" name)

#define HA_restartLog     HA("restartLog")
#define HA_iotHeartbeat   HA("iotHeartbeat")

#define HA_ip             HA("ip")
#define HA_SW_version     HA("SW_version")

#define HA_OTA            HA("OTA")         // Standard alias
#define HA_OTA_Update     HA("OTA")         // Human-readable alias

/***************************************************************************************
 * Usage Example:
 *
 *    SW_version->write("V1.0.3");
 *    if (OTA->onChange()) { startOTA(); }
 *
 *    log->write("System boot OK");
 *    ip->write(WiFi.localIP().toString());
 *
 ***************************************************************************************/