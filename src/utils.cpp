#include "utils.h"
#include "config.h"
#include "config_manager.h"
#include <WiFi.h>

bool connectWiFiShort(uint32_t timeoutMs)
{
    if (WiFi.status() == WL_CONNECTED)
        return true;

    const auto cfg = ConfigManager::instance().getConfig();
    if (strlen(cfg.wifi_ssid) == 0)
    {
        DEBUG_PRINT("[WiFi] Aucun SSID configuré, pas de connexion STA.");
        return false;
    }

    WiFi.mode(WIFI_STA);
    if (strlen(cfg.wifi_pass) == 0)
        WiFi.begin(cfg.wifi_ssid);
    else
        WiFi.begin(cfg.wifi_ssid, cfg.wifi_pass);

    DEBUG_PRINTF("[WiFi] Connexion à '%s'...\n", cfg.wifi_ssid);

    uint32_t t0 = millis();
    while (millis() - t0 < timeoutMs)
    {
        if (WiFi.status() == WL_CONNECTED)
        {
            DEBUG_PRINTF("[WiFi] Connecté: %s\n", WiFi.localIP().toString().c_str());
            return true;
        }
        delay(200);
    }

    DEBUG_PRINT("[WiFi] Timeout de connexion.");
    return (WiFi.status() == WL_CONNECTED);
}

void disconnectWiFiClean()
{
    if (WiFi.status() == WL_CONNECTED)
    {
        DEBUG_PRINT("[WiFi] Déconnexion propre...");
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
