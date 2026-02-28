



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
      "HA/restartLog/toHESTIASDK",
      "",
      "",
      "false"
    },

    { "IotBridge_iotHeartbeat",
      TypeHA::HA_INDICATOR,
      "Virgo/iotHeartbeat/toHA",
      "Virgo/iotHeartbeat/fromHA",
      "",
      ""
    },

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

    { "IotBridge_OTA",
      TypeHA::HA_BUTTON,
      "",
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
    "model": "Hestia_SDK",
    "sw_version": "1.0.1"
  },
  "o": {
    "name": "Virgo"
  },
  "cmps": {
    "ha_log_topic": {
      "p": "sensor",
      "name": "ha_log_topic",
      "unique_id": "Virgo_ha_log_topic",
      "stat_t": "Virgo/log/toHA",
      "icon": "mdi:notebook-edit"
    },
    "iotHeartbeat": {
      "p": "sensor",
      "name": "iotHeartbeat",
      "unique_id": "Virgo_iotHeartbeat",
      "stat_t": "Virgo/iotHeartbeat/toHA",
      "icon": "mdi:heart-pulse"
    },
    "SW_version": {
      "p": "sensor",
      "name": "SW_version",
      "unique_id": "Virgo_SW_version",
      "stat_t": "Virgo/SW_version/toHA",
      "icon": "mdi:language-cpp",
      "availability": [
        {
          "topic": "Virgo/availability"
        }
      ]
    },
    "ip": {
      "p": "sensor",
      "name": "ip",
      "unique_id": "Virgo_ip",
      "stat_t": "Virgo/ip/toHA",
      "icon": "mdi:wifi-arrow-up",
      "availability": [
        {
          "topic": "Virgo/availability"
        }
      ]
    },
    "OTA": {
      "p": "button",
      "name": "OTA",
      "unique_id": "Virgo_OTA",
      "cmd_t": "Virgo/OTA/fromHA",
      "device_class": "update",
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

#define HA_online       HA("HA_online")
#define HA_heartbeat    HA("HA_heartbeat")
#define HA_restartLog   HA("restartLog")
#define HA_iotHeartbeat HA("iotHeartbeat")
#define HA_ip           HA("ip")
#define HA_SW_version   HA("SW_version")
#define HA_OTA          HA("OTA")

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