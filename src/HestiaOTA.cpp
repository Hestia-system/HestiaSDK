#include <Arduino.h>
#include <WebServer.h>
#include <WiFi.h>
#include <Update.h>

#include "HestiaConfig.h"    // pour getParam()
#include "HardwareInit.h"    // pour watchdogKick
#include "HestiaOTA.h"   // header minimal fourni plus tard si nécessaire

// ---------------------------------------------------------------------------
// INTERNAL STATE
// ---------------------------------------------------------------------------
static WebServer server(80);
static int loginAttempts = 0;
static const int MAX_ATTEMPTS = 5;
static uint32_t lastActivity = 0;
static const uint32_t OTA_TIMEOUT_MS = 10UL * 60UL * 1000UL;   // 10 minutes

// ---------------------------------------------------------------------------
// HELPERS
// ---------------------------------------------------------------------------

static String makeTitle()
{
    String dev = HestiaConfig::getParam("device_id");
    String ver = HestiaConfig::getParam("version_prog");
    return dev + " - " + ver;
}

static void rebootDevice()
{
    server.send(200, "text/html; charset=utf-8",
        "<html><body><h2>Rebooting...</h2></body></html>");
    delay(1500);
    ESP.restart();
}

// ---------------------------------------------------------------------------
// LOGIN PAGE (if user/pass configured)
// ---------------------------------------------------------------------------

static void handleLoginPage(bool invalid = false)
{
    String html;
    html += "<html><body>";
    html += "<h2>" + makeTitle() + "</h2>";

    if (invalid)
        html += "<p style='color:red;'>Invalid login or password</p>";

    html += "<form method='POST' action='/login'>";
    html += "Login:<br><input name='user'><br><br>";
    html += "Password:<br><input name='pass' type='password'><br><br>";
    html += "<button type='submit'>Login</button>";
    html += "</form><br>";

    html += "<form method='POST' action='/cancel'>";
    html += "<button type='submit'>Cancel</button>";
    html += "</form>";

    html += "</body></html>";

    server.send(200, "text/html; charset=utf-8", html);
}

static void handleLoginPost()
{
    if (loginAttempts >= MAX_ATTEMPTS)
    {
        rebootDevice();
        return;
    }

    String u = server.arg("user");
    String p = server.arg("pass");

    String cu = HestiaConfig::getParam("iot_user");
    String cp = HestiaConfig::getParam("iot_pass");

    if (u == cu && p == cp)
    {
        // LOGIN OK → go to OTA main page
        loginAttempts = 0;
        server.sendHeader("Location", "/ota");
        server.send(302);
        return;
    }

    loginAttempts++;
    handleLoginPage(true);
}

// ---------------------------------------------------------------------------
// OTA FILE SELECTION PAGE
// ---------------------------------------------------------------------------

static void handleOtaPage()
{
    String title = makeTitle();

    String html;
    html += "<html><head><style>";
    html += "body { font-family: sans-serif; text-align:center; margin-top:40px; }";
    html += ".row { margin: 15px; }";
    html += ".btn { padding:10px 22px; margin:0 10px; }";
    html += "#progress { width:80%; height:20px; background:#ddd; margin:auto; }";
    html += "#bar { width:0%; height:100%; background:#4CAF50; }";
    html += "</style></head><body>";

    // TITRES
    html += "<h3>Update firmware by over the air (OTA)</h3>";
    html += "<h2>" + title + "</h2>";

    // COMPTE À REBOURS
    html += "<p id='countdown' style='font-size:18px; margin-bottom:20px;'>Time remaining: 10m 00s</p>";

    // INPUT FICHIER
    html += "<div class='row'><input id='file' type='file'></div>";

    // BOUTONS
    html += "<div class='row'>";
    html += "<button class='btn' onclick='startUpload()'>Update</button>";
    html += "<form method=\"POST\" action=\"/cancel\" style=\"display:inline;\">";
    html += "<button class='btn' type='submit'>Cancel</button>";
    html += "</form>";
    html += "</div>";

    // BARRE PROGRESSION
    html += "<div id='progress'><div id='bar'></div></div>";
    html += "<p id='status'></p>";

    // JAVASCRIPT
    html += "<script>";

    // ---------------------------------------------------------------
    // TIMER 10 minutes (600 secondes)
    // ---------------------------------------------------------------
    html += "var timeoutSec = 600;";
    html += "function updateCountdown(){";
    html += "  var m = Math.floor(timeoutSec/60);";
    html += "  var s = timeoutSec % 60;";
    html += "  document.getElementById('countdown').innerText = "
            "'Time remaining: ' + m + 'm ' + (s<10?'0':'') + s + 's';";
    html += "  timeoutSec--;";
    html += "}";
    html += "setInterval(updateCountdown, 1000);";

    // RESET TIMER FRONTEND
    html += "function resetTimer(){ timeoutSec = 600; }";
    html += "document.getElementById('file').addEventListener('change', resetTimer);";

    // ---------------------------------------------------------------
    // UPLOAD + PROGRESSION
    // ---------------------------------------------------------------
    html += "function startUpload(){";
    html += " var f = document.getElementById('file').files[0];";
    html += " if(!f){ alert('Select a file first'); return; }";

    html += " resetTimer();"; // activité détectée

    html += " var xhr = new XMLHttpRequest();";
    html += " xhr.open('POST', '/upload', true);";

    // PROGRESSION
    html += " xhr.upload.onprogress = function(e){";
    html += "   resetTimer();"; // activité → reset
    html += "   if(e.lengthComputable){";
    html += "     var p = Math.round((e.loaded / e.total) * 100);";
    html += "     document.getElementById('bar').style.width = p + '%';";
    html += "     document.getElementById('status').innerText = p + '%';";
    html += "   }";
    html += " };";

    // FIN UPLOAD
    html += " xhr.onload = function(){";
    html += "   document.getElementById('status').innerHTML = 'Upload complete. Device rebooting…';";
    html += " };";

  

    html += " var form = new FormData();";
    html += " form.append('firmware', f);";
    html += " xhr.send(form);";
    html += "}";

    html += "</script>";
    html += "</body></html>";

    lastActivity = millis();   // reset timer serveur
    server.send(200, "text/html; charset=utf-8", html);
}



// ---------------------------------------------------------------------------
// HANDLE UPLOAD STREAMING
// ---------------------------------------------------------------------------

static void handleUpload()
{
    HTTPUpload& up = server.upload();

    if (up.status == UPLOAD_FILE_START)
    {
      lastActivity = millis();  
      loginAttempts = 0;
        Update.begin();   // no size limit specified, allows auto-detection
    }
    else if (up.status == UPLOAD_FILE_WRITE)
    {
      lastActivity = millis();  
      Update.write(up.buf, up.currentSize);
    }
    else if (up.status == UPLOAD_FILE_END)
    {
      lastActivity = millis();
      if (Update.end(true))
        {
            server.send(200, "text/html; charset=utf-8",
                "<html><body><h2>Firmware updated successfully. Rebooting…</h2></body></html>");
            delay(1500);
            ESP.restart();
        }
        else
        {
            server.send(500, "text/html; charset=utf-8",
                "<html><body><h2>Update failed.</h2></body></html>");
        }
    }

}

// ---------------------------------------------------------------------------
// CANCEL → reboot
// ---------------------------------------------------------------------------

static void handleCancel()
{
    rebootDevice();
}

// ---------------------------------------------------------------------------
// ROUTER
// ---------------------------------------------------------------------------

static void configureRoutes()
{
    server.on("/", []() {
        String u = HestiaConfig::getParam("iot_user");
        String p = HestiaConfig::getParam("iot_pass");

        if (u == "" && p == "")
            handleOtaPage();
        else
            handleLoginPage(false);
    });

    server.on("/login", HTTP_POST, handleLoginPost);
    server.on("/ota", HTTP_GET, handleOtaPage);
    server.on("/cancel", HTTP_POST, handleCancel);

    server.on(
        "/upload",
        HTTP_POST,
        []() {},   // finished by handler
        handleUpload
    );

    server.onNotFound([]() {
        server.send(404, "text/plain", "Not found");
    });
}

// ---------------------------------------------------------------------------
// PUBLIC ENTRYPOINT
// ---------------------------------------------------------------------------

void HestiaOTA_Web_Start()
{
    loginAttempts = 0;

    configureRoutes();
    server.begin();
    lastActivity = millis();

    // BLOCKING LOOP (as per your specification)
    while (true)
    {
        server.handleClient();
        HardwareInit::watchdogKick();

        if (millis() - lastActivity > OTA_TIMEOUT_MS) {
          rebootDevice();
        }
        delay(2);
    }
}
