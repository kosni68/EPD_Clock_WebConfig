#include "mqtt.h"
#include "config.h"
#include "config_manager.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <atomic>

static WiFiClient wifiClient;
static PubSubClient mqttClient(wifiClient);
static std::atomic<bool> mqttBusy{false};

void setupMQTT()
{
    const auto cfg = ConfigManager::instance().getConfig();
    mqttClient.setServer(cfg.mqtt_host, cfg.mqtt_port);
}

bool publishMQTT_reading(float temperatureC, float humidityPct, int batteryMv)
{
    bool expected = false;
    if (!mqttBusy.compare_exchange_strong(expected, true))
    {
        DEBUG_PRINT("[MQTT] Busy - skipping publish");
        return false;
    }

    bool ok = false;
    const auto cfg = ConfigManager::instance().getConfig();

    if (!cfg.mqtt_enabled)
    {
        DEBUG_PRINT("[MQTT] MQTT désactivé, publication ignorée.");
        mqttBusy.store(false);
        return true;
    }

    if (WiFi.status() != WL_CONNECTED)
    {
        DEBUG_PRINT("[MQTT] WiFi non connecté !");
        mqttBusy.store(false);
        return false;
    }

    mqttClient.setServer(cfg.mqtt_host, cfg.mqtt_port);

    String clientId = String(cfg.device_name);
    if (clientId.isEmpty())
        clientId = String("EPDClock-") + String((uint32_t)ESP.getEfuseMac(), HEX);

    DEBUG_PRINTF("[MQTT] Connexion à %s:%d en tant que %s\n",
                 cfg.mqtt_host, cfg.mqtt_port, clientId.c_str());

    bool connected = false;
    if (strlen(cfg.mqtt_user) == 0)
        connected = mqttClient.connect(clientId.c_str());
    else
        connected = mqttClient.connect(clientId.c_str(), cfg.mqtt_user, cfg.mqtt_pass);

    if (!connected)
    {
        DEBUG_PRINTF("[MQTT] Connexion échouée, state=%d\n", mqttClient.state());
        mqttBusy.store(false);
        return false;
    }

    char payload[256];
    snprintf(payload, sizeof(payload),
             "{\"temperature_c\":%.2f,\"humidity_pct\":%.2f,\"battery_mv\":%d}",
             temperatureC, humidityPct, batteryMv);

    DEBUG_PRINTF("[MQTT] Publish sur %s: %s\n", cfg.mqtt_topic, payload);

    ok = mqttClient.publish(cfg.mqtt_topic, payload);
    mqttClient.loop();
    delay(50);
    mqttClient.disconnect();

    mqttBusy.store(false);

    DEBUG_PRINT(ok ? "[MQTT] Publish success!" : "[MQTT] Publish failed!");
    return ok;
}
