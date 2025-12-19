/*****************************************************************************************
 *  File     : HestiaProvisioning.h
 *  Project  : Hestia SDK / Virgo IoT
 *
 *  Summary:
 *  --------
 *  Provisioning-mode implementation for Virgo devices.
 *
 *  Responsibilities:
 *    • Start a self-contained Wi-Fi Access Point using the device identifier
 *    • Host a captive-portal HTTP server on 192.168.4.1
 *    • Dynamically generate a full configuration form from the DeviceParams
 *      JSON schema (HESTIA_PARAM_JSON)
 *    • Persist values via HestiaParam / NVS
 *    • Defer final validation to HestiaConfig::validateR2() at next boot
 *
 *  Design Principles:
 *    • Single public entry point: StartProvisioning()
 *    • Fully blocking by design (the function never returns)
 *    • Deterministic: the device cannot silently fall back to old configuration
 *    • No dependency on HestiaCore or HAIoTBridge
 *    • Watchdog is kicked automatically in the provisioning loop
 *
 *  Usage:
 *    if (HestiaConfig::loadBAD() || HestiaConfig::ForceProvisioning()) {
 *        Provisioning::StartProvisioning(HESTIA_PARAM_JSON);
 *    }
 *
 *****************************************************************************************/

#pragma once

namespace Provisioning {

  /**
   * @brief Start provisioning mode using the provided JSON schema.
   *
   * Behavior:
   *   • Configures DNS redirection for captive-portal behavior
   *   • Starts an HTTP server exposing:
   *         GET  "/"         → dynamic configuration form
   *         POST "/save"     → normal save (clears force_prov)
   *         POST "/forceSave"→ forced save (sets force_prov = true)
   *   • Blocks in a loop until the form is submitted successfully
   *   • Triggers ESP.restart() to apply new configuration
   *
   * Notes:
   *   • This function NEVER returns.
   *   • Field-level correctness (HTML5 validation) is handled by the dynamic form.
   *   • Firmware accepts any submitted value.
   *   • Final authoritative validation is performed at next boot via
   *     HestiaConfig::validateR2(), which may trigger provisioning again.
   *
   * @param jsonSchema  Pointer to a PROGMEM string containing the full
   *                    DeviceParams JSON schema.
   */
  void StartProvisioning(const char* jsonSchema);

} // namespace Provisioning
