#pragma once

/**
 * Hestia OTA Web Interface
 *
 * Provides a blocking OTA Web UI for manual firmware updates.
 *
 * Behaviour:
 *   - If iot_user / iot_pass are empty → skip login → go directly to upload page.
 *   - If credentials exist → show login page with max 5 attempts.
 *   - Upload is processed via multipart/form-data and streamed into Update().
 *   - On success → HTML "Rebooting…" page → reboot.
 *   - On cancel or too many login failures → reboot.
 *
 * This function is fully blocking and will not return.
 * Wi-Fi must already be connected before calling.
 *
 * Usage:
 *     HestiaOTA_Web_Start();   // enters OTA UI and never returns
 */
void HestiaOTA_Web_Start();
