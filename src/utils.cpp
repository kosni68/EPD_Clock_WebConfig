#include "utils.h"
#include "config.h"
#include "config_manager.h"
#include <WiFi.h>

// Simple circular log buffer
static const int LOG_LINES = 200;
static String logLines[LOG_LINES];
static int logIndex = 0;
static int logCount = 0;

void appendLog(const char *msg)
{
    // store timestamp + message
    char buf[256];
    unsigned long ms = millis();
    int n = snprintf(buf, sizeof(buf), "[%lu] %s", ms, msg);
    logLines[logIndex] = String(buf);
    logIndex = (logIndex + 1) % LOG_LINES;
    if (logCount < LOG_LINES) logCount++;
}

String getLogs()
{
    String out = "";
    int start = (logIndex - logCount + LOG_LINES) % LOG_LINES;
    for (int i = 0; i < logCount; i++) {
        int idx = (start + i) % LOG_LINES;
        out += logLines[idx];
        out += '\n';
    }
    return out;
}

bool connectWiFiShort(uint32_t timeoutMs)
{
    if (WiFi.status() == WL_CONNECTED)
        return true;

    const auto cfg = ConfigManager::instance().getConfig();
    if (strlen(cfg.wifi_ssid) == 0)
    {
        DEBUG_PRINT("[WiFi] No SSID configured, skipping STA connection.");
        return false;
    }

    WiFi.mode(WIFI_STA);
    if (strlen(cfg.wifi_pass) == 0)
        WiFi.begin(cfg.wifi_ssid);
    else
        WiFi.begin(cfg.wifi_ssid, cfg.wifi_pass);

    DEBUG_PRINTF("[WiFi] Connecting to '%s'...\n", cfg.wifi_ssid);

    uint32_t t0 = millis();
    while (millis() - t0 < timeoutMs)
    {
        if (WiFi.status() == WL_CONNECTED)
        {
            DEBUG_PRINTF("[WiFi] Connected: %s\n", WiFi.localIP().toString().c_str());
            return true;
        }
        delay(200);
    }

    DEBUG_PRINT("[WiFi] Connection timeout.");
    return (WiFi.status() == WL_CONNECTED);
}

void disconnectWiFiClean()
{
    if (WiFi.status() == WL_CONNECTED)
    {
        DEBUG_PRINT("[WiFi] Clean disconnect...");
        WiFi.disconnect(true, true);
        WiFi.mode(WIFI_OFF);
        delay(50);
    }
}

static inline bool modeIsAp(wifi_mode_t mode)
{
    return (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA
#ifdef WIFI_AP
            || mode == WIFI_AP || mode == WIFI_AP_STA
#endif
    );
}

bool isApModeActive()
{
    wifi_mode_t mode = WiFi.getMode();
    return modeIsAp(mode);
}
