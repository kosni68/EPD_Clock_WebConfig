#include "config_manager.h"
#include <Preferences.h>

ConfigManager &ConfigManager::instance()
{
    static ConfigManager mgr;
    return mgr;
}

bool ConfigManager::begin()
{
    Serial.println("[ConfigManager] Initialization...");

    Preferences prefs;
    if (!prefs.begin("config", true))
    {
        Serial.println("[ConfigManager] Erreur: impossible d’ouvrir les preferences en lecture.");
        return false;
    }

    bool exists = prefs.isKey("mqtt_host") || prefs.isKey("device_name") || prefs.isKey("wifi_ssid");
    prefs.end();

    if (!exists)
    {
        Serial.println("[ConfigManager] No configuration found. Applying default values...");
        applyDefaultsIfNeeded();
        save();
        return true;
    }

    bool ok = loadFromPreferences();
    applyDefaultsIfNeeded();
    Serial.printf("[ConfigManager] Load %s\n", ok ? "succeeded" : "failed");
    return ok;
}

void ConfigManager::applyDefaultsIfNeeded()
{
    std::lock_guard<std::mutex> lk(mutex_);
    Serial.println("[ConfigManager] Checking default values...");

    if (config_.interactive_timeout_min == 0)
    {
        config_.interactive_timeout_min = 5;
        Serial.println("  -> interactive_timeout_min set to 5");
    }
    if (config_.deepsleep_interval_min == 0)
    {
        config_.deepsleep_interval_min = 5;
        Serial.println("  -> deepsleep_interval_min set to 5");
    }
    if (config_.measure_interval_ms < 50)
    {
        config_.measure_interval_ms = 1000;
        Serial.println("  -> measure_interval_ms set to 1000");
    }
    if (config_.mqtt_port == 0)
    {
        config_.mqtt_port = 1883;
        Serial.println("  -> mqtt_port set to 1883");
    }
    if (strlen(config_.mqtt_host) == 0)
    {
        strcpy(config_.mqtt_host, "broker.local");
        Serial.println("  -> mqtt_host set to broker.local");
    }
    if (strlen(config_.device_name) == 0)
    {
        strcpy(config_.device_name, "EPD-Clock");
        Serial.println("  -> device_name set to EPD-Clock");
    }
    if (strlen(config_.app_version) == 0)
    {
        strcpy(config_.app_version, "1.0.0");
        Serial.println("  -> app_version set to 1.0.0");
    }
    if (strlen(config_.admin_user) == 0)
    {
        strcpy(config_.admin_user, "admin");
        Serial.println("  -> admin_user set to 'admin' (please change!)");
    }
    if (strlen(config_.admin_pass) == 0)
    {
        strcpy(config_.admin_pass, "admin");
        Serial.println("  -> admin_pass set to 'admin' (please change!)");
    }

    if (config_.avg_alpha <= 0.0f || config_.avg_alpha > 1.0f)
    {
        config_.avg_alpha = 0.25f;
        Serial.println("  -> avg_alpha set to 0.25");
    }
    if (config_.median_n == 0 || config_.median_n > 15)
    {
        config_.median_n = 5;
        Serial.println("  -> median_n set to 5");
    }
    if (config_.median_delay_ms > 1000)
    {
        config_.median_delay_ms = 50;
        Serial.println("  -> median_delay_ms set to 50");
    }
    if (config_.filter_min_cm <= 0.0f)
    {
        config_.filter_min_cm = 2.0f;
        Serial.println("  -> filter_min_cm set to 2.0");
    }
    if (config_.filter_max_cm < config_.filter_min_cm)
    {
        config_.filter_max_cm = 400.0f;
        Serial.println("  -> filter_max_cm set to 400.0");
    }
    if (strlen(config_.tz_string) == 0)
    {
        strcpy(config_.tz_string, "CET-1CEST,M3.5.0/2,M10.5.0/3");
        Serial.println("  -> tz_string set to Europe/Paris (DST auto)");
    }
}

bool ConfigManager::loadFromPreferences()
{
    std::lock_guard<std::mutex> lk(mutex_);
    Serial.println("[ConfigManager] Loading from Preferences...");

    Preferences prefs;
    if (!prefs.begin("config", true))
    {
        Serial.println("  -> Error: cannot open Preferences for reading.");
        return false;
    }

    prefs.getString("wifi_ssid", config_.wifi_ssid, sizeof(config_.wifi_ssid));
    prefs.getString("wifi_pass", config_.wifi_pass, sizeof(config_.wifi_pass));

    config_.mqtt_enabled = prefs.getBool("mqtt_en", false);
    prefs.getString("mqtt_host", config_.mqtt_host, sizeof(config_.mqtt_host));
    config_.mqtt_port = prefs.getUShort("mqtt_port", 1883);
    prefs.getString("mqtt_user", config_.mqtt_user, sizeof(config_.mqtt_user));
    prefs.getString("mqtt_pass", config_.mqtt_pass, sizeof(config_.mqtt_pass));
    prefs.getString("mqtt_topic", config_.mqtt_topic, sizeof(config_.mqtt_topic));

    config_.measure_interval_ms = prefs.getUInt("meas_int_ms", 1000);
    config_.measure_offset_cm = prefs.getFloat("meas_off_cm", 0.0f);
    // sensor offsets (new)
    config_.temp_offset_c = prefs.getFloat("temp_off_c", 0.0f);
    config_.hum_offset_pct = prefs.getFloat("hum_off_pct", 0.0f);

    config_.avg_alpha = prefs.getFloat("avg_alpha", 0.25f);
    config_.median_n = prefs.getUShort("median_n", 5);
    config_.median_delay_ms = prefs.getUShort("median_delay_ms", 50);
    config_.filter_min_cm = prefs.getFloat("f_min_cm", 2.0f);
    config_.filter_max_cm = prefs.getFloat("f_max_cm", 400.0f);

    prefs.getString("dev_name", config_.device_name, sizeof(config_.device_name));
    uint32_t storedTimeoutMin = prefs.getUInt("int_to_min", 0);
    uint32_t legacyTimeoutMs = prefs.getUInt("int_to_ms", 0);
    if (storedTimeoutMin > 0)
    {
        config_.interactive_timeout_min = storedTimeoutMin;
    }
    else if (legacyTimeoutMs > 0)
    {
        config_.interactive_timeout_min = (legacyTimeoutMs + 59999UL) / 60000UL;
        Serial.printf("  -> Converted legacy interactive timeout: %lu ms -> %lu min\n",
                      (unsigned long)legacyTimeoutMs,
                      (unsigned long)config_.interactive_timeout_min);
    }
    else
    {
        config_.interactive_timeout_min = 0;
    }
    // Prefer new minute-based key; fall back to legacy seconds key (converted)
    config_.deepsleep_interval_min = prefs.getUInt("deep_int_min", 0);
    if (config_.deepsleep_interval_min == 0)
    {
        uint32_t legacySeconds = prefs.getUInt("deep_int_s", 0);
        if (legacySeconds > 0)
        {
            config_.deepsleep_interval_min = (legacySeconds + 59U) / 60U;
            Serial.printf("  -> Converted legacy deep sleep: %lu s -> %lu min\n",
                          (unsigned long)legacySeconds,
                          (unsigned long)config_.deepsleep_interval_min);
        }
    }

    prefs.getString("adm_user", config_.admin_user, sizeof(config_.admin_user));
    prefs.getString("adm_pass", config_.admin_pass, sizeof(config_.admin_pass));
    prefs.getString("app_ver", config_.app_version, sizeof(config_.app_version));

    prefs.getString("tz_str", config_.tz_string, sizeof(config_.tz_string));

    prefs.end();

    Serial.printf("  -> WiFi SSID: %s (%s)\n",
                  (strlen(config_.wifi_ssid) ? config_.wifi_ssid : "<not configured>"),
                  (strlen(config_.wifi_pass) ? "password set" : "no password"));
    Serial.printf("  -> MQTT %s @ %s:%d (user=%s)\n",
                  config_.mqtt_enabled ? "enabled" : "disabled",
                  config_.mqtt_host, config_.mqtt_port, config_.mqtt_user);
    Serial.printf("  -> Device: %s, Measure interval: %lu ms\n",
                  config_.device_name, config_.measure_interval_ms);
    Serial.printf("  -> DeepSleep: %lu min, Interactive timeout: %lu min\n",
                  (unsigned long)config_.deepsleep_interval_min,
                  (unsigned long)config_.interactive_timeout_min);
    return true;
}

bool ConfigManager::save()
{
    std::lock_guard<std::mutex> lk(mutex_);
    Preferences prefs;
    if (!prefs.begin("config", false))
        return false;

    Serial.println("[ConfigManager] Saving to Preferences...");

    prefs.putString("wifi_ssid", config_.wifi_ssid);
    prefs.putString("wifi_pass", config_.wifi_pass);

    prefs.putBool("mqtt_en", config_.mqtt_enabled);
    prefs.putString("mqtt_host", config_.mqtt_host);
    prefs.putUShort("mqtt_port", config_.mqtt_port);
    prefs.putString("mqtt_user", config_.mqtt_user);
    prefs.putString("mqtt_pass", config_.mqtt_pass);
    prefs.putString("mqtt_topic", config_.mqtt_topic);

    prefs.putUInt("meas_int_ms", config_.measure_interval_ms);
    prefs.putFloat("meas_off_cm", config_.measure_offset_cm);
    prefs.putFloat("temp_off_c", config_.temp_offset_c);
    prefs.putFloat("hum_off_pct", config_.hum_offset_pct);
    prefs.putFloat("avg_alpha", config_.avg_alpha);
    prefs.putUShort("median_n", config_.median_n);
    prefs.putUShort("median_delay_ms", config_.median_delay_ms);
    prefs.putFloat("f_min_cm", config_.filter_min_cm);
    prefs.putFloat("f_max_cm", config_.filter_max_cm);

    prefs.putString("dev_name", config_.device_name);
    prefs.putUInt("int_to_min", config_.interactive_timeout_min);
    prefs.putUInt("deep_int_min", config_.deepsleep_interval_min);

    prefs.putString("adm_user", config_.admin_user);
    prefs.putString("adm_pass", config_.admin_pass);
    prefs.putString("app_ver", config_.app_version);

    prefs.putString("tz_str", config_.tz_string);

    prefs.end();
    Serial.println(" Configuration saved successfully!");
    return true;
}

String ConfigManager::toJsonString()
{
    std::lock_guard<std::mutex> lk(mutex_);
    JsonDocument doc;

    doc["wifi_ssid"] = config_.wifi_ssid;
    doc["wifi_pass"] = "*****";

    doc["mqtt_enabled"] = config_.mqtt_enabled;
    doc["mqtt_host"] = config_.mqtt_host;
    doc["mqtt_port"] = config_.mqtt_port;
    doc["mqtt_user"] = config_.mqtt_user;
    doc["mqtt_pass"] = "*****";
    doc["mqtt_topic"] = config_.mqtt_topic;

    doc["measure_interval_ms"] = config_.measure_interval_ms;
    doc["measure_offset_cm"] = config_.measure_offset_cm;
    doc["temp_offset_c"] = config_.temp_offset_c;
    doc["hum_offset_pct"] = config_.hum_offset_pct;
    doc["avg_alpha"] = config_.avg_alpha;
    doc["median_n"] = config_.median_n;
    doc["median_delay_ms"] = config_.median_delay_ms;
    doc["filter_min_cm"] = config_.filter_min_cm;
    doc["filter_max_cm"] = config_.filter_max_cm;

    doc["device_name"] = config_.device_name;
    doc["interactive_timeout_min"] = config_.interactive_timeout_min;
    doc["deepsleep_interval_min"] = config_.deepsleep_interval_min;

    doc["admin_user"] = config_.admin_user;
    doc["admin_pass"] = "*****";
    doc["app_version"] = config_.app_version;

    doc["tz_string"] = config_.tz_string;

    String s;
    serializeJson(doc, s);
    Serial.println("[ConfigManager] Converted to JSON.");
    return s;
}

bool ConfigManager::updateFromJson(const String &json)
{
    Serial.println("[ConfigManager] Updating from JSON...");

    JsonDocument doc;
    auto err = deserializeJson(doc, json);
    if (err)
    {
        Serial.println("[ConfigManager][ERR] Invalid JSON!");
        return false;
    }

    {
        std::lock_guard<std::mutex> lk(mutex_);

        if (doc["wifi_ssid"].is<const char *>())
            strlcpy(config_.wifi_ssid, doc["wifi_ssid"], sizeof(config_.wifi_ssid));
        if (doc["wifi_pass"].is<const char *>())
        {
            const char *wp = doc["wifi_pass"];
            if (wp && strcmp(wp, "*****") != 0 && strlen(wp) > 0)
                strlcpy(config_.wifi_pass, wp, sizeof(config_.wifi_pass));
        }

        if (doc["mqtt_enabled"].is<bool>())
            config_.mqtt_enabled = doc["mqtt_enabled"].as<bool>();
        if (doc["mqtt_host"].is<const char *>())
            strlcpy(config_.mqtt_host, doc["mqtt_host"], sizeof(config_.mqtt_host));
        if (doc["mqtt_port"].is<uint16_t>())
            config_.mqtt_port = doc["mqtt_port"];
        if (doc["mqtt_user"].is<const char *>())
            strlcpy(config_.mqtt_user, doc["mqtt_user"], sizeof(config_.mqtt_user));
        if (doc["mqtt_pass"].is<const char *>())
        {
            const char *mp = doc["mqtt_pass"];
            if (mp && strcmp(mp, "*****") != 0 && strlen(mp) > 0)
                strlcpy(config_.mqtt_pass, mp, sizeof(config_.mqtt_pass));
        }
        if (doc["mqtt_topic"].is<const char *>())
            strlcpy(config_.mqtt_topic, doc["mqtt_topic"], sizeof(config_.mqtt_topic));

        if (doc["measure_interval_ms"].is<uint32_t>())
            config_.measure_interval_ms = doc["measure_interval_ms"];
        if (doc["measure_offset_cm"].is<float>())
            config_.measure_offset_cm = doc["measure_offset_cm"];
        if (doc["temp_offset_c"].is<float>())
            config_.temp_offset_c = doc["temp_offset_c"];
        if (doc["hum_offset_pct"].is<float>())
            config_.hum_offset_pct = doc["hum_offset_pct"];

        if (doc["avg_alpha"].is<float>())
            config_.avg_alpha = doc["avg_alpha"];
        if (doc["median_n"].is<uint16_t>())
            config_.median_n = doc["median_n"];
        if (doc["median_delay_ms"].is<uint16_t>())
            config_.median_delay_ms = doc["median_delay_ms"];
        if (doc["filter_min_cm"].is<float>())
            config_.filter_min_cm = doc["filter_min_cm"];
        if (doc["filter_max_cm"].is<float>())
            config_.filter_max_cm = doc["filter_max_cm"];

        if (doc["device_name"].is<const char *>())
            strlcpy(config_.device_name, doc["device_name"], sizeof(config_.device_name));
        if (doc["interactive_timeout_min"].is<uint32_t>())
        {
            config_.interactive_timeout_min = doc["interactive_timeout_min"].as<uint32_t>();
        }
        else if (doc["interactive_timeout_ms"].is<uint32_t>())
        {
            uint32_t legacyMs = doc["interactive_timeout_ms"].as<uint32_t>();
            config_.interactive_timeout_min = (legacyMs + 59999UL) / 60000UL;
        }
        if (doc["deepsleep_interval_min"].is<uint32_t>())
            config_.deepsleep_interval_min = doc["deepsleep_interval_min"];
        else if (doc["deepsleep_interval_s"].is<uint32_t>())
            config_.deepsleep_interval_min = (doc["deepsleep_interval_s"].as<uint32_t>() + 59U) / 60U;

        if (doc["admin_user"].is<const char *>())
            strlcpy(config_.admin_user, doc["admin_user"], sizeof(config_.admin_user));
        if (doc["admin_pass"].is<const char *>())
        {
            const char *ap = doc["admin_pass"];
            if (ap && strcmp(ap, "*****") != 0 && strlen(ap) > 0)
                strlcpy(config_.admin_pass, ap, sizeof(config_.admin_pass));
        }
        if (doc["tz_string"].is<const char *>())
            strlcpy(config_.tz_string, doc["tz_string"], sizeof(config_.tz_string));
    }

    Serial.println("  -> In-memory configuration update OK.");

    applyDefaultsIfNeeded();

    xTaskCreate(
        [](void *)
        {
            Serial.println("[ConfigManager][Task] Async save in progress...");
            ConfigManager::instance().save();
            Serial.println("[ConfigManager][Task] Save completed!");
            vTaskDelete(nullptr);
        },
        "saveConfigAsync", 4096, nullptr, 1, nullptr);

    return true;
}

AppConfig ConfigManager::getConfig()
{
    std::lock_guard<std::mutex> lk(mutex_);
    return config_;
}

uint32_t ConfigManager::getMeasureIntervalMs()
{
    std::lock_guard<std::mutex> lk(mutex_);
    return config_.measure_interval_ms;
}

float ConfigManager::getMeasureOffsetCm()
{
    std::lock_guard<std::mutex> lk(mutex_);
    return config_.measure_offset_cm;
}

float ConfigManager::getRunningAverageAlpha()
{
    std::lock_guard<std::mutex> lk(mutex_);
    return config_.avg_alpha;
}

uint16_t ConfigManager::getMedianSamples()
{
    std::lock_guard<std::mutex> lk(mutex_);
    return config_.median_n;
}

uint16_t ConfigManager::getMedianSampleDelayMs()
{
    std::lock_guard<std::mutex> lk(mutex_);
    return config_.median_delay_ms;
}

float ConfigManager::getFilterMinCm()
{
    std::lock_guard<std::mutex> lk(mutex_);
    return config_.filter_min_cm;
}

float ConfigManager::getFilterMaxCm()
{
    std::lock_guard<std::mutex> lk(mutex_);
    return config_.filter_max_cm;
}

float ConfigManager::getTempOffsetC()
{
    std::lock_guard<std::mutex> lk(mutex_);
    return config_.temp_offset_c;
}

float ConfigManager::getHumOffsetPct()
{
    std::lock_guard<std::mutex> lk(mutex_);
    return config_.hum_offset_pct;
}

bool ConfigManager::isMQTTEnabled()
{
    std::lock_guard<std::mutex> lk(mutex_);
    return config_.mqtt_enabled;
}

const char *ConfigManager::getAdminUser()
{
    std::lock_guard<std::mutex> lk(mutex_);
    return config_.admin_user;
}

const char *ConfigManager::getAdminPass()
{
    std::lock_guard<std::mutex> lk(mutex_);
    return config_.admin_pass;
}
