

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include "HestiaConfig.h"
#include "HestiaProvisioning.h"
#include "HardwareInit.h"


/*****************************************************************************************
 *  File     : HestiaProvisioning.cpp
 *  Project  : Hestia SDK / Virgo IoT
 *
 *  Summary:
 *  --------
 *  Implementation of the provisioning-mode subsystem.
 *
 *  High-level behavior:
 *    • Start an ESP32 SoftAP using DEVICE_ID as access point name
 *    • Serve a captive-portal configuration form dynamically generated from the
 *      HestiaConfigSchema (master JSON definition)
 *    • Validate submitted fields through HestiaConfig::validateAll()
 *    • Save only valid key/value pairs into NVS (namespace "HConfig")
 *    • Automatically restart the device after a successful save
 *
 *  Notes:
 *    • This module *intentionally* blocks execution inside StartProvisioning().
 *    • Provisioning mode must start BEFORE any Wi-Fi or MQTT operation.
 *    • The watchdog is actively serviced to avoid resets during UI interaction.
 *
 *****************************************************************************************/

using namespace HestiaConfig;

namespace Provisioning {

  // --------------------------------------------------------------------------------------
  // Internal module state (file-local)
  // --------------------------------------------------------------------------------------
  static DNSServer dnsServer;        ///< Captive-portal DNS redirector
  static WebServer server(80);       ///< HTTP provisioning server
  static bool formSaved = false;     ///< Set to true when /save completes successfully
  static JsonArray schemaParams;     ///< R2: DeviceParams schema array (used by handleSave)


  // ======================================================================================
  //  buildHtmlForm() — HTML Form Generator
  // ======================================================================================
  /**
   * @brief Dynamically generate the provisioning UI.
   *
   * The generator iterates over all parameters returned by HestiaConfig::allParams(),
   * creating:
   *    • <select>   for option-based parameters
   *    • <input>    for scalar types (string, number, etc.)
   *
   * All existing values are pre-filled.
   *
   * @return The full HTML page as a String.
   */
  String buildHtmlForm() {
    String html = R"(
      <html><head>
        <meta name='viewport' content='width=device-width, initial-scale=1.0'/>
        <title>Provisioning</title>
        <style>
          body { font-family: sans-serif; margin: 20px; }

          /* iOS Captive Portal: prevent automatic zoom */
          input, select, textarea, button {
            font-size: 16px;
          }

          h2 { margin-bottom: 16px; }
          label { font-weight: bold; display: block; margin-top: 8px; }

          input, select {
            width: 100%;
            padding: 8px;
            margin-bottom: 12px;
            box-sizing: border-box;
          }

          input:invalid {
            border: 1px solid #cc0000;
            background: #ffe6e6;
          }
          input:valid {
            border: 1px solid #33aa33;
            background: #eaffea;
          }

          .status-badge {
            font-size: 14px;
            padding: 4px 10px;
            border-radius: 999px;
            display: inline-block;
            margin: 8px 0 12px 0;
          }
          .status-ok {
            background: #e6f6e6;
            color: #006600;
          }
          .status-bad {
            background: #fde0e0;
            color: #990000;
          }

          button {
            padding: 12px;
            width: 100%;
            font-size: 16px;   /* important */
            border: none;
            border-radius: 6px;
          }
          #saveBtn.save-normal {
            background: #009900;
            color: #ffffff;
          }
          #saveBtn.save-force {
            background: #cc0000;
            color: #ffffff;
          }
        </style>

      </head><body>
      <h2>Device configuration</h2>
      <form id='provForm' method='POST'>
    )";

    // ----------------------------------------------------------------------
    // Generate form fields from DeviceParams JSON (R2)
    // ----------------------------------------------------------------------
    for (JsonObject meta : schemaParams) {

        const char* key   = meta["key"];
        const char* label = meta["label"] | key;
        const char* type  = meta["type"]  | "string";

        // Retrieve current value through the HestiaParam pipeline
        HestiaParam* p = HestiaConfig::getParamObj(key);
        String value = p ? p->read() : "";

        // --- LABEL ---
        html += "<label>";
        html += label;
        html += "</label>";

        // --- SELECT (if options exist) ---
        if (meta.containsKey("options")) {
            html += "<select name='";
            html += key;
            html += "'>";

            JsonArray opts = meta["options"].as<JsonArray>();
            for (JsonVariant ov : opts) {
                const char* opt = ov.as<const char*>();
                bool selected = (value == opt);

                html += "<option value='";
                html += opt;
                html += "'";
                if (selected) html += " selected";
                html += ">";
                html += opt;
                html += "</option>";
            }

            html += "</select>";
            continue;
        }

        // --- INPUT ---
        String htmlType = "text";
        if (strcmp(type, "int") == 0)   htmlType = "number";
        if (strcmp(type, "float") == 0) htmlType = "text";  // floats in <input type="number"> behave poorly

        html += "<input type='";
        html += htmlType;
        html += "' name='";
        html += key;
        html += "' value='";
        html += value;
        html += "'";

        // Required attribute
        bool required = meta["required"] | false;
        if (required) html += " required";

        // MIN/MAX validators
        if (meta.containsKey("validate")) {
            JsonObject val = meta["validate"];

            if (val.containsKey("min")) {
                html += " min='";
                html += val["min"].as<int>();
                html += "'";
            }
            if (val.containsKey("max")) {
                html += " max='";
                html += val["max"].as<int>();
                html += "'";
            }
            if (val.containsKey("minLen")) {
                html += " minlength='";
                html += val["minLen"].as<int>();
                html += "'";
            }
            if (val.containsKey("maxLen")) {
                html += " maxlength='";
                html += val["maxLen"].as<int>();
                html += "'";
            }
        }

        // Pattern constraints (e.g., IP address)
        const char* pattern = meta["pattern"] | nullptr;
        if (pattern && strcmp(pattern, "ip") == 0) {
            html += " pattern='^([0-9]{1,3}\\.){3}[0-9]{1,3}$'";
        }

        html += ">";
    }



    html += R"(
        <div id='cfgStatus' class='status-badge'></div>
        <button type='button' id='saveBtn'>Save configuration</button>
      </form>

      <script>
        function updateProvisioningStatus() {
          var form = document.getElementById('provForm');
          var fields = form.querySelectorAll('input, select');
          var allValid = true;
          for (var i = 0; i < fields.length; i++) {
            if (!fields[i].checkValidity()) {
              allValid = false;
              break;
            }
          }
          var status = document.getElementById('cfgStatus');
          var btn = document.getElementById('saveBtn');
          if (allValid) {
            status.textContent = 'Valid configuration';
            status.className = 'status-badge status-ok';
            btn.textContent = 'Save configuration';
            btn.className = 'save-normal';
            btn.dataset.mode = 'normal';
          } else {
            status.textContent = 'Invalid configuration';
            status.className = 'status-badge status-bad';
            btn.textContent = 'Save invalid configuration';
            btn.className = 'save-force';
            btn.dataset.mode = 'force';
          }
        }

        function submitProvisioningForm(ev) {
          ev.preventDefault();
          var form = document.getElementById('provForm');
          var btn = document.getElementById('saveBtn');
          var mode = btn.dataset.mode || 'normal';
          if (mode === 'force') {
            form.action = '/forceSave';
          } else {
            form.action = '/save';
          }
          form.submit();
        }

        document.addEventListener('DOMContentLoaded', function() {
          var form = document.getElementById('provForm');
          var btn = document.getElementById('saveBtn');
          form.addEventListener('input', updateProvisioningStatus);
          btn.addEventListener('click', submitProvisioningForm);
          updateProvisioningStatus();
        });
      </script>

      </body></html>
    )";

    return html;
  }

  // ======================================================================================
  //  handleRoot() — Serve configuration form
  // ======================================================================================
  /**
   * @brief HTTP handler for GET "/".
   *
   * Returns the fully generated configuration form.
   */
  void handleRoot() {
      server.send(200, "text/html", buildHtmlForm());
  }


  // ======================================================================================
  //  handleSave() — Form processing + validation + NVS write
  // ======================================================================================
  /**
   * @brief Process POST "/save" (or "/forceSave" when force=true).
   *
   * Steps:
   *   1) Transfer all incoming form fields into HestiaParam objects (RAM)
   *   2) In normal mode:
   *        • Validation will occur on next boot through validateR2()
   *   3) Write each updated value into NVS ("HConfig" namespace)
   *   4) Mark formSaved = true
   *   5) Return an HTML confirmation page
   *   6) Restart the ESP automatically
   *
   * Notes:
   *   • Provisioning R2 decouples 'write' from immediate validation.
   *     Invalid configurations may be saved intentionally via '/forceSave'.
   */
  void handleSave(bool force) {

      // Iterate over DeviceParams (R2 JSON schema)
      for (JsonObject meta : schemaParams) {

          const char* key = meta["key"];

          if (!server.hasArg(key))
              continue;

          String v = server.arg(key);

          // R2 pipeline: store → persist, regardless of validity
          HestiaParam* hp = HestiaConfig::getParamObj(key);
          if (hp) {
              hp->write(v);       // always write to RAM
              hp->saveToNVS();    // persist value immediately
          }
      }

      // Final user response
      if (force) {
          HestiaConfig::SetForceProvisioning(true);
          server.send(200, "text/html",
              "<h3>Forced configuration saved.</h3>"
              "<p>The device will reboot into provisioning mode.</p>");
          formSaved = true;
          delay(2000);
          ESP.restart();
      } 
      else {
          HestiaConfig::SetForceProvisioning(false);
          server.send(200, "text/html",
              "<h3>Configuration saved successfully.</h3>"
              "<p>The device will reboot automatically.</p>");
          formSaved = true;
          delay(2000);
          ESP.restart();
      }
  }


  // ---------------------------------------------------------------------------
  // HTTP Wrappers
  // ---------------------------------------------------------------------------
  void handleSaveRouter()       { handleSave(false); }
  void handleForceSaveRouter()  { handleSave(true); }


  // ======================================================================================
  //  StartProvisioning() — Public API
  // ======================================================================================
  /**
   * @brief Start provisioning mode.
   *
   * This function NEVER RETURNS.
   *
   * Behavior:
   *   • Initialize SoftAP named after DEVICE_ID
   *   • Configure captive DNS redirect (all domains → 192.168.4.1)
   *   • Expose GET "/" and POST "/save" and "/forceSave"
   *   • Enter a loop:
   *         dnsServer.processNextRequest()
   *         server.handleClient()
   *         watchdogKick()
   *   • Exit only after a successful save
   *   • Automatically restart ESP
   *
   * @param jsonSchema  Pointer to the DeviceParams (R2) JSON schema in RAM/PROGMEM
   */
  void StartProvisioning(const char* jsonSchema) {

      Serial.println("=== PROVISIONING MODE ===");

      // Parse the R2 JSON schema
      DynamicJsonDocument doc(8192);
      deserializeJson(doc, jsonSchema);

      // Make schema globally available
      JsonArray arr = doc.as<JsonArray>();
      schemaParams = arr;

      // --- Wi-Fi Access Point ------------------------------------------------------------
      WiFi.mode(WIFI_AP);

      HestiaParam* dev = HestiaConfig::getParamObj("device_id");
      WiFi.softAP(dev ? dev->read().c_str() : "HestiaDevice");

      delay(200);

      IPAddress apIP(192,168,4,1);
      WiFi.softAPConfig(apIP, apIP, IPAddress(255,255,255,0));

      Serial.print("AP started: ");
      Serial.println(dev ? dev->read() : "UNKNOWN_DEVICE");

      Serial.print("IP: ");
      Serial.println(WiFi.softAPIP());

      // --- Captive Portal DNS ------------------------------------------------------------
      dnsServer.start(53, "*", apIP);

      // --- WebServer routes --------------------------------------------------------------
      server.on("/", handleRoot);
      server.on("/save", HTTP_POST, handleSaveRouter);
      server.on("/forceSave", HTTP_POST, handleForceSaveRouter);
      // --------------------------------------------------------------------------------------
      // Captive Portal Support (iOS / Android / Windows / ChromeOS)
      // --------------------------------------------------------------------------------------

      // iOS / macOS
      server.on("/hotspot-detect.html", []() {
          server.sendHeader("Location", "/", true);
          server.send(302, "text/plain", "");
      });

      // Android
      server.on("/generate_204", []() {
          // Many implementations return 204, but redirecting works better for captive portals
          server.sendHeader("Location", "/", true);
          server.send(302, "text/plain", "");
      });

      // Windows (NCSI)
      server.on("/ncsi.txt", []() {
          server.sendHeader("Location", "/", true);
          server.send(302, "text/plain", "");
      });

      // Windows fallback
      server.on("/fwlink", []() {
          server.sendHeader("Location", "/", true);
          server.send(302, "text/plain", "");
      });

      // ChromeOS
      server.on("/connecttest.txt", []() {
          server.sendHeader("Location", "/", true);
          server.send(302, "text/plain", "");
      });

      // Fallback for ANY unknown path (critical!)
      server.onNotFound([]() {
          server.sendHeader("Location", "/", true);
          server.send(302, "text/plain", "");
      });

      server.begin();

      Serial.println("Provisioning portal ready");

      // --- Blocking loop until formSaved is set -------------------------------------------
      while (!formSaved) {
          dnsServer.processNextRequest();
          server.handleClient();
          HardwareInit::watchdogKick();
          delay(10);
      }

      // Should never reach here
      Serial.println("Exiting provisioning mode");
  }

} // namespace Provisioning
