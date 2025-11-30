#include "web_server.h"
#include "config.h"
#include "config_manager.h"
#include "utils.h"
#include "mqtt.h"

#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <WiFi.h>

static AsyncWebServer server(80);

// Non-blocking Wi-Fi scan state to avoid starving AsyncTCP/task watchdog
static bool wifiScanRunning = false;
static uint32_t wifiScanStartedMs = 0;
static String wifiScanApsJson = "[]";
static const uint32_t WIFI_SCAN_TIMEOUT_MS = 12000;
static uint32_t wifiScanLastCompleteMs = 0;
static const uint32_t WIFI_SCAN_MIN_INTERVAL_MS = 5000;

static void beginAsyncWifiScan()
{
    // Ensure STA is enabled while keeping AP alive, so scan can run in AP mode too
    wifi_mode_t mode = WiFi.getMode();
    if (mode == WIFI_MODE_AP)
    {
        WiFi.mode(WIFI_MODE_APSTA);
    }

    WiFi.scanDelete();
    wifiScanStartedMs = millis();
    wifiScanRunning = true;
    DEBUG_PRINT("[WEB][WiFi] Starting async scan...");
    WiFi.scanNetworks(true /*async*/, true /*show hidden*/);
}

static void finalizeWifiScanIfComplete()
{
    if (!wifiScanRunning)
        return;

    int16_t res = WiFi.scanComplete();
    if (res == WIFI_SCAN_RUNNING)
    {
        if ((uint32_t)(millis() - wifiScanStartedMs) > WIFI_SCAN_TIMEOUT_MS)
        {
            DEBUG_PRINT("[WEB][WiFi] Scan timeout, aborting.");
            WiFi.scanDelete();
            wifiScanRunning = false;
            wifiScanLastCompleteMs = millis();
        }
        return;
    }

    if (res < 0)
    {
        DEBUG_PRINTF("[WEB][WiFi] Scan failed (%d)\n", res);
        WiFi.scanDelete();
        wifiScanRunning = false;
        wifiScanLastCompleteMs = millis();
        return;
    }

    String json = "[";
    for (int i = 0; i < res; i++)
    {
        String ssid = WiFi.SSID(i);
        ssid.replace("\\", "\\\\");
        ssid.replace("\"", "\\\"");
        if (i > 0)
            json += ",";
        json += "{\"ssid\":\"" + ssid + "\",\"rssi\":" + String(WiFi.RSSI(i)) + "}";
    }
    json += "]";

    WiFi.scanDelete();
    wifiScanApsJson = json;
    wifiScanRunning = false;
    wifiScanLastCompleteMs = millis();
}

static String buildWifiScanResponse(bool inProgress)
{
    String json = "{\"ok\":true,\"in_progress\":";
    json += inProgress ? "true" : "false";
    json += ",\"aps\":";
    json += wifiScanApsJson;
    json += "}";
    return json;
}

static void handleGetConfig(AsyncWebServerRequest *request);
static void handlePostConfig(AsyncWebServerRequest *request, const String &body);
extern void readTimeAndSensorAndPrepareStrings(float &tempC, float &humidityPct, int &batteryMv);
extern void epdDraw(bool fullRefresh);

void startWebServer()
{
    DEBUG_PRINT("[WEB] Initializing HTTP server...");

    if (!LittleFS.begin(true))
    {
        DEBUG_PRINT("[WEB][ERROR] LittleFS mount failed!");
        while (true)
            delay(1000);
    }

    if (!connectWiFiShort(8000))
    {
        DEBUG_PRINT("[WEB][WARN] Wi-Fi connection failed. Enabling access point mode...");
        WiFi.mode(WIFI_AP);
        WiFi.softAP("EPD_Clock");
        DEBUG_PRINTF("[WEB] Access point active: %s\n", WiFi.softAPIP().toString().c_str());
    }
    else
    {
        DEBUG_PRINTF("[WEB] Connected to Wi-Fi: %s\n", WiFi.localIP().toString().c_str());
    }

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
              {
        DEBUG_PRINT("[WEB] GET /index.html");
        if (LittleFS.exists("/index.html")) {
            AsyncWebServerResponse *response = request->beginResponse(LittleFS, "/index.html", "text/html");
            response->addHeader("Content-Type", "text/html; charset=utf-8");
            request->send(response);
        } else {
            request->send(200, "text/html; charset=utf-8",
                          "<!doctype html><html><body><h2>EPD Clock</h2>"
                          "<p><a href=\"/config.html\">Settings</a></p></body></html>");
        } });

    server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request)
              {
        DEBUG_PRINT("[WEB] GET /style.css");
        AsyncWebServerResponse *response = request->beginResponse(LittleFS, "/style.css", "text/css");
        response->addHeader("Content-Type", "text/css; charset=utf-8");
        request->send(response); });

    server.on("/config.html", HTTP_GET, [](AsyncWebServerRequest *request)
              {
        const char *adminUser = ConfigManager::instance().getAdminUser();
        const char *adminPass = ConfigManager::instance().getAdminPass();
        if (!request->authenticate(adminUser, adminPass)) {
            DEBUG_PRINT("[WEB][AUTH] Authentication required on /config.html");
            return request->requestAuthentication();
        }
        DEBUG_PRINT("[WEB] GET /config.html (auth OK)");
        AsyncWebServerResponse *response = request->beginResponse(LittleFS, "/config.html", "text/html");
        response->addHeader("Content-Type", "text/html; charset=utf-8");
        request->send(response); });

    server.on("/script_config.js", HTTP_GET, [](AsyncWebServerRequest *request)
              {
        const char *adminUser = ConfigManager::instance().getAdminUser();
        const char *adminPass = ConfigManager::instance().getAdminPass();
        if (!request->authenticate(adminUser, adminPass)) {
            DEBUG_PRINT("[WEB][AUTH] Authentication required on /script_config.js");
            return request->requestAuthentication();
        }
        DEBUG_PRINT("[WEB] GET /script_config.js (auth OK)");
        AsyncWebServerResponse *response = request->beginResponse(LittleFS, "/script_config.js", "application/javascript");
        response->addHeader("Content-Type", "application/javascript; charset=utf-8");
        request->send(response); });

    server.on("/ping", HTTP_POST, [](AsyncWebServerRequest *request)
              {
        String page = request->arg("page");
        interactiveLastTouchMs.store(millis());
        DEBUG_PRINTF("[WEB] POST /ping (%s)\n", page.c_str());
        request->send(200, "application/json; charset=utf-8", "{\"ok\":true}"); });

    server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest *request)
              {
        DEBUG_PRINT("[WEB] GET /api/config");
        handleGetConfig(request); });

    server.on("/api/mqtt/test", HTTP_POST, [](AsyncWebServerRequest *request)
              {
        const char *adminUser = ConfigManager::instance().getAdminUser();
        const char *adminPass = ConfigManager::instance().getAdminPass();
        if (!request->authenticate(adminUser, adminPass)) {
            DEBUG_PRINT("[WEB][AUTH] /api/mqtt/test auth required");
            return request->requestAuthentication();
        }

        DEBUG_PRINT("[WEB] POST /api/mqtt/test (attempting test publish)");
        float t = 0.0f, h = 0.0f;
        int batt = 0;
        readTimeAndSensorAndPrepareStrings(t, h, batt);
        bool ok = publishMQTT_reading(t, h, batt);
        if (ok) request->send(200, "application/json; charset=utf-8", "{\"ok\":true}");
        else request->send(500, "application/json; charset=utf-8", "{\"ok\":false}");
    });

    server.on("/api/logs", HTTP_GET, [](AsyncWebServerRequest *request)
              {
        const char *adminUser = ConfigManager::instance().getAdminUser();
        const char *adminPass = ConfigManager::instance().getAdminPass();
        if (!request->authenticate(adminUser, adminPass)) {
            DEBUG_PRINT("[WEB][AUTH] /api/logs auth required");
            return request->requestAuthentication();
        }
        DEBUG_PRINT("[WEB] GET /api/logs");
        String logs = getLogs();
        request->send(200, "text/plain; charset=utf-8", logs);
    });

    // Combined dashboard endpoint: returns metrics + logs. Also serves as a ping (updates interactive touch)
    server.on("/api/dashboard", HTTP_POST, [](AsyncWebServerRequest *request)
              {
        // Update interactive ping (acts like /ping)
        interactiveLastTouchMs.store(millis());

        DEBUG_PRINT("[WEB] POST /api/dashboard");
        // metrics
        String metricsJson = getLatestMetricsJson();
        // logs (escape for JSON)
        String logs = getLogs();
        String logsEscaped;
        logsEscaped.reserve(logs.length() * 2 + 10);
        for (size_t i = 0; i < logs.length(); i++) {
            char c = logs.charAt(i);
            if (c == '"') logsEscaped += "\\\"";
            else if (c == '\\') logsEscaped += "\\\\";
            else if (c == '\n') logsEscaped += "\\n";
            else if (c == '\r') logsEscaped += "\\r";
            else logsEscaped += c;
        }

        String json = "{";
        json += "\"ok\":true,";
        json += "\"metrics\":{" + metricsJson + "},";
        json += "\"logs\":\"" + logsEscaped + "\"";
        json += "}";

        request->send(200, "application/json; charset=utf-8", json);
    });

    // Allow GET as well for simple polling (no auth)
    server.on("/api/dashboard", HTTP_GET, [](AsyncWebServerRequest *request)
              {
        interactiveLastTouchMs.store(millis());
        DEBUG_PRINT("[WEB] GET /api/dashboard");
        String metricsJson = getLatestMetricsJson();
        String logs = getLogs();
        String logsEscaped;
        logsEscaped.reserve(logs.length() * 2 + 10);
        for (size_t i = 0; i < logs.length(); i++) {
            char c = logs.charAt(i);
            if (c == '"') logsEscaped += "\\\"";
            else if (c == '\\') logsEscaped += "\\\\";
            else if (c == '\n') logsEscaped += "\\n";
            else if (c == '\r') logsEscaped += "\\r";
            else logsEscaped += c;
        }
        String json = "{";
        json += "\"ok\":true,";
        json += "\"metrics\":{" + metricsJson + "},";
        json += "\"logs\":\"" + logsEscaped + "\"";
        json += "}";
        request->send(200, "application/json; charset=utf-8", json);
    });

    // Scan Wi-Fi networks (STA/APSTA/AP). Returns a small JSON with SSID + RSSI
    server.on("/api/wifi/scan", HTTP_GET, [](AsyncWebServerRequest *request)
              {
        const char *adminUser = ConfigManager::instance().getAdminUser();
        const char *adminPass = ConfigManager::instance().getAdminPass();
        if (!request->authenticate(adminUser, adminPass)) {
            DEBUG_PRINT("[WEB][AUTH] /api/wifi/scan auth required");
            return request->requestAuthentication();
        }

        interactiveLastTouchMs.store(millis());
        DEBUG_PRINT("[WEB] GET /api/wifi/scan (scanning)");

        // Avoid blocking the async web server: kick off async scan and return cached/partial data
        finalizeWifiScanIfComplete();
        const uint32_t nowMs = millis();
        bool allowNewScan = (!wifiScanRunning) &&
                            (wifiScanLastCompleteMs == 0 ||
                             (uint32_t)(nowMs - wifiScanLastCompleteMs) > WIFI_SCAN_MIN_INTERVAL_MS);
        if (allowNewScan)
        {
            beginAsyncWifiScan();
        }
        finalizeWifiScanIfComplete(); // pick up instant completions

        String json = buildWifiScanResponse(wifiScanRunning);
        request->send(200, "application/json; charset=utf-8", json);
    });

    server.on("/api/config", HTTP_POST,
              [](AsyncWebServerRequest *request) {},
              nullptr,
              [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
              {
                  const char *adminUser = ConfigManager::instance().getAdminUser();
                  const char *adminPass = ConfigManager::instance().getAdminPass();
                  if (!request->authenticate(adminUser, adminPass))
                  {
                      DEBUG_PRINT("[WEB][AUTH] /api/config POST not authorized");
                      request->requestAuthentication();
                      return;
                  }

                  if (index == 0)
                  {
                      if (total > 4096)
                      {
                          DEBUG_PRINTF("[WEB] Payload too large (%u)\n", (unsigned)total);
                          request->send(413, "application/json; charset=utf-8",
                                        "{\"ok\":false,\"err\":\"payload too large\"}");
                          return;
                      }
                      request->_tempObject = new String();
                      ((String *)request->_tempObject)->reserve(total);
                      DEBUG_PRINTF("[WEB] Begin receiving JSON body (%u bytes)\n", (unsigned)total);
                  }

                  String *body = reinterpret_cast<String *>(request->_tempObject);
                  if (body)
                  {
                      body->concat(String((const char *)data).substring(0, len));
                  }

                  if (index + len == total)
                  {
                      DEBUG_PRINTF("[WEB] Full JSON body received (%u bytes)\n", (unsigned)total);
                      if (body)
                      {
                          handlePostConfig(request, *body);
                          delete body;
                          request->_tempObject = nullptr;
                      }
                  }
              });

    server.on("/api/reboot", HTTP_POST, [](AsyncWebServerRequest *request)
              {
        const char *adminUser = ConfigManager::instance().getAdminUser();
        const char *adminPass = ConfigManager::instance().getAdminPass();
        if (!request->authenticate(adminUser, adminPass))
        {
            DEBUG_PRINT("[WEB][AUTH] /api/reboot POST not authorized");
            return request->requestAuthentication();
        }

        DEBUG_PRINT("[WEB] Reboot requested...");
        request->send(200, "application/json; charset=utf-8", "{\"ok\":true}");
        delay(500);
        ESP.restart();
    });

    server.begin();
    DEBUG_PRINT("[WEB] Web server started.");
}

static void handleGetConfig(AsyncWebServerRequest *request)
{
    const char *adminUser = ConfigManager::instance().getAdminUser();
    const char *adminPass = ConfigManager::instance().getAdminPass();
    if (!request->authenticate(adminUser, adminPass))
    {
        DEBUG_PRINT("[WEB][AUTH] /api/config GET not authorized");
        return request->requestAuthentication();
    }

    DEBUG_PRINT("[WEB] GET /api/config (auth OK)");
    String json = ConfigManager::instance().toJsonString();
    request->send(200, "application/json; charset=utf-8", json);
}

static void handlePostConfig(AsyncWebServerRequest *request, const String &body)
{
    DEBUG_PRINT("[WEB] POST /api/config received");

    const char *adminUser = ConfigManager::instance().getAdminUser();
    const char *adminPass = ConfigManager::instance().getAdminPass();
    if (!request->authenticate(adminUser, adminPass))
    {
        DEBUG_PRINT("[WEB][AUTH] /api/config POST not authorized (2nd check)");
        return request->requestAuthentication();
    }

    if (body.isEmpty())
    {
        DEBUG_PRINT("[WEB][ERR] Empty JSON body!");
        request->send(400, "application/json; charset=utf-8", "{\"ok\":false,\"err\":\"empty body\"}");
        return;
    }

    bool okUpdate = ConfigManager::instance().updateFromJson(body);
    if (okUpdate)
    {
        DEBUG_PRINT("[WEB] Configuration updated (deferred save).");
        // Apply changes immediately: re-read sensors and refresh display so offsets take effect
        {
            float tmpT = 0.0f, tmpH = 0.0f;
            int batt = 0;
            // readTimeAndSensorAndPrepareStrings updates latest_* snapshot
            readTimeAndSensorAndPrepareStrings(tmpT, tmpH, batt);
            // If the device is in interactive mode, refresh the EPD now
            extern bool interactiveMode;
            if (interactiveMode) {
                epdDraw(false);
            }
        }
        request->send(200, "application/json; charset=utf-8", "{\"ok\":true}");
    }
    else
    {
        DEBUG_PRINT("[WEB][ERR] JSON update failed!");
        request->send(400, "application/json; charset=utf-8", "{\"ok\":false}");
    }
}
