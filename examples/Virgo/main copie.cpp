/*****************************************************************************************
 *  File     : Virgo_Main.ino
 *  Project  : Hestia SDK / Virgo IoT
 *
 *  Summary:
 *  --------
 *  Main entry point for Virgo devices running the Hestia SDK.
 *
 *  Responsibilities:
 *    • Initialize low-level hardware and the watchdog
 *    • Load and validate configuration (HestiaConfig)
 *    • Initialize Hestia Core (entity registry, NVS restore)
 *    • Start network services (Wi-Fi + MQTT via HestiaNetSDK)
 *    • Supervise the communication state machine
 *    • Dispatch system-level maintenance tasks
 *
 *  Design Notes:
 *    - Keep this file strictly minimal: it orchestrates, but implements nothing.
 *    - All firmware logic must reside in module files (HestiaCore, HAIoTBridge, etc.).
 *    - Maintain a clean separation between:
 *         ▸ Communication supervision (CoreComm)
 *         ▸ System maintenance (CoreLoop)
 *         ▸ Application logic (if applicable)
 *
 *  Provisioning Behavior:
 *    - Provisioning MUST run before any Wi-Fi or MQTT attempt.
 *    - HestiaConfig::validateR2() decides whether provisioning is required at boot.
 *
 *****************************************************************************************/

#include <Arduino.h>
#include "main.h"
#include "HestiaProvisioning.h"
#include "HestiaParam.h"
#include "DeviceParams.h"
#include "HestiaOTA.h"
#include "HestiaTempo.h"
using Tempo::literals::operator"" _id;




void setup() 
{
    // 0) Basic hardware init
    HardwareInit::InitHardwareMinimal();

    // 1) Load device parameters (R2 JSON → HestiaParam objects)
    HestiaConfig::loadDeviceParams(HESTIA_PARAM_JSON);
    

    // ---------------------------------------------------------------------
    // 2) Validate configuration and provisioning decision
    // ---------------------------------------------------------------------
    if (!HestiaConfig::validateR2() || HestiaConfig::ForceProvisioning()) {
        Serial.println(F("[MAIN] ⚠ Provisioning mode triggered."));
        Provisioning::StartProvisioning(HESTIA_PARAM_JSON);   // never returns
    }

    // ---------------------------------------------------------------------
    // 3) Now that provisioning is settled → start watchdog
    // ---------------------------------------------------------------------
    HardwareInit::InitHardwareWatchdog(PARAM_WATCHDOG_MS->readInt());

    // ---------------------------------------------------------------------
    // 4) Inject bridge configuration and discovery JSON
    // ---------------------------------------------------------------------
    HestiaCore::loadBridgeConfig(bridge_config, BRIDGE_COUNT);
    HestiaNet::loadDiscoveryJson(config_json);

    // ---------------------------------------------------------------------
    // 5) Create all HAIoTBridge entities
    // ---------------------------------------------------------------------
    HestiaCore::RegisterEntitiesIotBridge();

    // ---------------------------------------------------------------------
    // 6) Load NVS values for CONTROL bridges
    // ---------------------------------------------------------------------
    HestiaCore::InitValueNVS();

    // ---------------------------------------------------------------------
    // 7) Silent mode for diagnostics-only entities
    // ---------------------------------------------------------------------
    HA_iotHeartbeat->setLogWrites(false);
    HA_ip->setLogWrites(false);

    // ---------------------------------------------------------------------
    // 8) TX Heartbeat to HA — signals presence
    // ---------------------------------------------------------------------
    HA_iotHeartbeat->write("TICK");

    // ---------------------------------------------------------------------
    // 9) Onboard LED setup
    // ---------------------------------------------------------------------
    int led = PARAM_LED_ONBOARD->readInt();
    if (led >= 0) {
        pinMode(led, OUTPUT);
        Serial.printf("Led onboard enabled on GPIO %d\n", led);
    } else {
        Serial.println("Led onboard free for user.");
    }

    // ---------------------------------------------------------------------
    // 10) User hardware initialization (optionnel)
    // ---------------------------------------------------------------------
    //   Init capteurs, relais, ADC, I2C etc.
}

/*****************************************************************************************
 *  loop()
 *  ------
 *  Runtime execution loop divided into well-defined operational layers.
 *
 *  Layers:
 *    1) CoreComm   — WiFi/MQTT state machine (non-blocking)
 *    2) SystemYield — cooperative scheduling for FreeRTOS
 *    3) Activation  — detection of new network session (WiFi+MQTT ONLINE)
 *    4) OTA Control — user-triggered OTA (if implemented)
 *    5) UX Feedback — LED indicator + periodic heartbeat to HA
 *    6) HA Refresh  — periodic RSSI/IP update for Home Assistant
 *    7) Provisioning Button Polling — press-and-hold trigger
 *
 *  Notes:
 *    • No blocking calls are allowed in this function.
 *    • All heavy work is pushed into CoreComm, HAIoTBridge, or provisioning code.
 *****************************************************************************************/
void loop()
{
    // =========================================================================
    // 1) CORE COMMUNICATION — WiFi/MQTT state machine
    //    Handles:
    //      • WiFi guard
    //      • MQTT guard
    //      • Retained flush management
    //      • MQTT client.loop()
    //      • Watchdog feeding
    // =========================================================================
    HestiaCore::CoreComm();


    // =========================================================================
    // 2) SYSTEM YIELD — maintain cooperative multitasking
    // =========================================================================
    vTaskDelay(1);   // avoids monopolizing the CPU, keeps lwIP and RTOS healthy


    // =========================================================================
    // 3) ACTIVATION SEQUENCE — detect transition to fully ONLINE state
    // =========================================================================
    if (HestiaCore::newSeqComm()) {

        // Initial publication of all HA entities (sensors, switches, etc.)
        // Restores user-facing state in Home Assistant.
        HestiaCore::HAInit();

        // user section for Home Assistant initialisation
        // your code here ...

        // end user section
        HestiaCore::setHAInitDone();
        Serial.println("Communication and Home Assistant ready!");
    }

    bool InitHAOK = HestiaCore::InitHAOK();

    // =========================================================================
    // 4) OTA CONTROL — user-triggered firmware update
    //    (Placeholder — user must implement OTA_Update())
    // =========================================================================
    if (InitHAOK && HA_OTA_Update->onChange()) {
        String ip = WiFi.localIP().toString();
        HestiaCore::logBook("Entering OTA mode. Go to OTA URL: http://" + ip + "/ota");
        HestiaNet::disconnectMQTT();            // gracefully disconnect MQTT
        HestiaOTA_Web_Start();
    }


    // =========================================================================
    // 5) USER EXPERIENCE FEEDBACK — onboard LED indicator
    // =========================================================================
    if (InitHAOK) { 
        if (PARAM_LED_ONBOARD->readInt() >= 0) {

            // Blink LED only when ONLINE (WiFi+MQTT connected)
            if (Tempo::interval("ledLoop"_id).every(500)) {
                static bool ledState = false;
                ledState = !ledState;
                digitalWrite(PARAM_LED_ONBOARD->readInt(), ledState);
            }
        }
    } 


    // =========================================================================
    // 6) HEARTBEAT — periodic device liveness for Home Assistant
    // =========================================================================
    if (InitHAOK) {
        if (
            Tempo::interval("heartbeat"_id).every(PARAM_IOT_ALIVE_MS->readInt())) {
            HA_iotHeartbeat->write("TICK");
        }
    }


    // =========================================================================
    // 7) NETWORK INFO REFRESH — RSSI + SSID update every 2 minutes
    // =========================================================================
    if (InitHAOK) { 
        if (Tempo::interval("RefreshHA"_id).every(120000)) {
            HA_ip->write((String)WiFi.SSID() + " @ " + WiFi.RSSI() + " dB");
        }
    }


    // =========================================================================
    // 8) PROVISIONING BUTTON — press-and-hold detection
    // =========================================================================
    HestiaConfig::pollProvisioningButton();


    // =========================================================================
    // 9) USER SECTION — event-driven architecture (no idle actions)
    // =========================================================================
}
