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

// ***** OBJECTS INITIALISATION  **********************************************************


// ***** SECTION USER FONCTIONS  **********************************************************


// ***** VARIABLES   **********************************************************************

int ledOnBoard = 8;
int pinProvisioning = 9;

// ***** SETUP SETUP  SETUP SETUP  SETUP SETUP  SETUP SETUP  ******************************
void setup() 
{
    HestiaCore::initCore(HESTIA_PARAM_JSON, bridge_config, BRIDGE_COUNT, config_json);
 
    // 1) INPUT / OUTPUT SETUP
    // ---------------------------------------------------------------------
    pinMode(ledOnBoard, OUTPUT);
    pinMode(pinProvisioning, INPUT);

    // 2) SENSORS SETUP
    // ---------------------------------------------------------------------


}


/* ***** LOOP  LOOP  LOOP  LOOP  LOOP  LOOP  LOOP  LOOP    ********************************
 *  ------
 *  Runtime execution loop divided into well-defined operational layers.
 *
 *  Notes:
 *    • No blocking calls are allowed in this function.
 *    • All heavy work is pushed into CoreComm, HAIoTBridge, or provisioning code.
 *****************************************************************************************/
void loop()
{
    // 1) CORE COMMUNICATION — WiFi/MQTT state machine
    // =========================================================================
    HestiaCore::CoreComm();

    // 2) SYSTEM YIELD — maintain cooperative multitasking
    // =========================================================================
    vTaskDelay(1);   // avoids monopolizing the CPU, keeps lwIP and RTOS healthy

    // 3) ACTIVATION SEQUENCE — detect transition to fully ONLINE state
    // =========================================================================
    if (HestiaCore::newSeqComm()) {

        // Restores user-facing state in Home Assistant.
        HestiaCore::HAInit();

        // user section for Home Assistant initialisation
        // your code here ...

        // end user section
        HestiaCore::setHAInitDone();
        Serial.println("Communication and Home Assistant ready!");
        HA_iotHeartbeat->write("TICK");
    }
    bool InitHAOK = HestiaCore::InitHAOK();

    // 4) OTA CONTROL — user-triggered firmware update
    // =========================================================================
    if (InitHAOK && HA_OTA_Update->onChange()) {
        String ip = WiFi.localIP().toString();
        HestiaCore::logBook("Entering OTA mode. Go to OTA URL: http://" + ip + "/ota");
        HestiaNet::disconnectMQTT();            // gracefully disconnect MQTT
        HestiaOTA_Web_Start();
    }

    // 5) IF WiFi + MQTT + HA CONNECTED ...
    // =========================================================================
    if (InitHAOK) { 
        // 5.1 Blink ledOnBoard every 0,5 sec
        if (Tempo::interval("ledLoop"_id).every(500)) {
            digitalWrite(ledOnBoard, !digitalRead(ledOnBoard));
        }
        // 5.2 HEARTBEAT — periodic device liveness for Home Assistant
        if (
            Tempo::interval("heartbeat"_id).every(PARAM_IOT_ALIVE_MS->readInt())) {
            HA_iotHeartbeat->write("TICK");
        }
        // 5.3 NETWORK INFO REFRESH — RSSI + SSID update every 2 minutes
        if (Tempo::interval("RefreshHA"_id).every(120000)) {
            HA_ip->write((String)WiFi.SSID() + " @ " + WiFi.RSSI() + " dB");
        }
    } 

    // 6) PROVISIONING BUTTON — press-and-hold detection for 5 sec
    // =========================================================================
    HestiaConfig::pollProvisioningButton(pinProvisioning, 5000);

    // 7) USER SECTION — event-driven architecture (no idle actions)
    // =========================================================================
}
