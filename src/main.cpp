#include <Wire.h>
#include <SPI.h>
#include <GxEPD2_BW.h>
#include <Fonts/FreeMonoBold12pt7b.h>
#include <Adafruit_SHTC3.h>
#include "background.h"
#include "fonts.h"
#include "esp_sleep.h"
#include "PCF85063A-SOLDERED.h"
#include <WiFi.h>
#include <time.h>

#include "config.h"
#include "config_manager.h"
#include "utils.h"
#include "mqtt.h"
#include "web_server.h"

#define EPD_DC 10
#define EPD_CS 11
#define EPD_SCK 12
#define EPD_MOSI 13
#define EPD_RST 9
#define EPD_BUSY 8
#define EPD_PWR 6
#define VBAT_PWR 17

#define I2C_SDA 47
#define I2C_SCL 48

#define SPI_CLOCK_HZ 4000000

GxEPD2_BW<GxEPD2_154_D67, GxEPD2_154_D67::HEIGHT> display(
    GxEPD2_154_D67(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

Adafruit_SHTC3 shtc3;
// RTC hardware removed: use system time (NTP) only
int sys_wday = 0;
// When true, epdDraw() will render a small sleep indicator overlay (e.g. "Zz")
bool showSleepIndicator = false;

int h = 0, m = 0;
int voltageSegments = 0;

String hourStr, minStr, tmp, hum2, tt, dateString;
String days[7] = {"SU", "MO", "TU", "WE", "TH", "FR", "SA"};

bool interactiveMode = false;

static int readBatteryVoltage();
void epdDraw();
static void goDeepSleep();
void readTimeAndSensorAndPrepareStrings(float &tempC, float &humidityPct, int &batteryMv);
static void syncRtcFromNtpIfPossible();
// Latest metrics snapshot (kept for dashboard polling)
static float latest_tempC = 0.0f;
static float latest_humidity = 0.0f;
static int latest_batteryMv = 0;
static String latest_time_str = "";
static String latest_date_str = "";

String getLatestMetricsJson()
{
    // Build a compact JSON object string for metrics
    String s = "";
    s += "\"temp\":" + String(latest_tempC, 2) + ",";
    s += "\"humidity\":" + String(latest_humidity, 2) + ",";
    s += "\"battery_mv\":" + String(latest_batteryMv) + ",";
    s += "\"time\":\"" + latest_time_str + "\",";
    s += "\"date\":\"" + latest_date_str + "\"";
    return s;
}

static void goDeepSleep()
{
    const auto cfg = ConfigManager::instance().getConfig();
    uint32_t sleepSeconds = cfg.deepsleep_interval_s;
    if (sleepSeconds == 0)
        sleepSeconds = 60;
    // Request epdDraw to render the current page with a sleep indicator overlay
    showSleepIndicator = true;
    epdDraw();
    showSleepIndicator = false;
    // hibernate display after rendering
    display.hibernate();
    digitalWrite(EPD_PWR, HIGH);

    gpio_hold_en((gpio_num_t)VBAT_PWR);
    gpio_hold_en((gpio_num_t)EPD_PWR);
    gpio_deep_sleep_hold_en();

    delay(5);
    esp_sleep_enable_timer_wakeup((uint64_t)sleepSeconds * 1000000ULL);

    Serial.printf("[POWER] Deep sleep pour %lu s\n", (unsigned long)sleepSeconds);
    esp_deep_sleep_start();
}

static String getWifiStatusString()
{
    wifi_mode_t mode = WiFi.getMode();

    // Si on a une connexion STA valide
    if ((mode == WIFI_MODE_STA || mode == WIFI_MODE_APSTA) &&
        WiFi.status() == WL_CONNECTED)
    {
        return String("STA ") + WiFi.localIP().toString();
    }

    // Sinon, si un AP est actif
    if (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA)
    {
        return String("AP ") + WiFi.softAPIP().toString();
    }

    // Sinon, rien d’actif
    return String("WiFi OFF");
}

void readTimeAndSensorAndPrepareStrings(float &tempC, float &humidityPct, int &batteryMv)
{
    struct tm timeinfo;
    // Try to get local time (NTP). If unavailable, fallback to epoch-based time(NULL)
    if (getLocalTime(&timeinfo, 1000)) {
        h = timeinfo.tm_hour;
        m = timeinfo.tm_min;
        sys_wday = timeinfo.tm_wday; // 0 = Sunday
        int day = timeinfo.tm_mday;
        int month = timeinfo.tm_mon + 1;
        int year = (timeinfo.tm_year + 1900) % 100; // two-digit year for display

        if (h < 10)
            hourStr = "0" + String(h);
        else
            hourStr = String(h);

        if (m < 10)
            minStr = "0" + String(m);
        else
            minStr = String(m);

        tt = hourStr + ":" + minStr;

        String dayStr = (day < 10) ? "0" + String(day) : String(day);
        String monthStr = (month < 10) ? "0" + String(month) : String(month);
        String yearStr = (year < 10) ? "0" + String(year) : String(year);

        dateString = dayStr + "/" + monthStr + "/" + yearStr;
    } else {
        // Fallback: use previous h/m values and show placeholder date
        if (h < 10)
            hourStr = "0" + String(h);
        else
            hourStr = String(h);
        if (m < 10)
            minStr = "0" + String(m);
        else
            minStr = String(m);
        tt = hourStr + ":" + minStr;
        dateString = "--/--/--";
    }

    sensors_event_t hum, temp;
    bool ok = shtc3.getEvent(&hum, &temp);
    if (!ok)
    {
        delay(5);
        shtc3.getEvent(&hum, &temp);
    }

    tempC = temp.temperature;
    humidityPct = hum.relative_humidity;
    // Apply configurable offsets (temp in °C, humidity in % points)
    float t_off = ConfigManager::instance().getTempOffsetC();
    float h_off = ConfigManager::instance().getHumOffsetPct();
    tempC += t_off;
    humidityPct += h_off;
    tmp = String(tempC, 1);
    hum2 = String(humidityPct, 1);

    // update latest metrics snapshot for dashboard
    latest_tempC = tempC;
    latest_humidity = humidityPct;
    latest_batteryMv = batteryMv;
    latest_time_str = tt;
    latest_date_str = dateString;

    batteryMv = readBatteryVoltage();
    voltageSegments = map(batteryMv, 3100, 4200, 0, 5);
    if (voltageSegments < 0)
        voltageSegments = 0;
    if (voltageSegments > 5)
        voltageSegments = 5;
}

static void syncRtcFromNtpIfPossible()
{
    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println("[NTP] Wi-Fi not connected, NTP unavailable.");
        return;
    }

    const auto cfg = ConfigManager::instance().getConfig();
    const char *tz = cfg.tz_string;
    if (!tz || strlen(tz) == 0)
    {
        // défaut : Europe/Paris
        tz = "CET-1CEST,M3.5.0/2,M10.5.0/3";
    }

    Serial.printf("[NTP] Sync NTP (TZ=\"%s\")...\n", tz);

    // Initialise SNTP + fuseau (avec heure d'été/hiver automatique)
    configTzTime(tz, "pool.ntp.org", "time.nist.gov", "time.google.com");

    struct tm timeinfo;
    if (!getLocalTime(&timeinfo, 10000))
    {
        Serial.println("[NTP][ERR] getLocalTime failed!");
        return;
    }

    // System time (NTP) is configured; no hardware RTC used anymore
    Serial.printf("[NTP] System time obtained %02d:%02d:%02d %02d/%02d/%04d (wday=%d)\n",
                  timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec,
                  timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900,
                  timeinfo.tm_wday);
}

void epdDraw()
{
    SPI.begin(EPD_SCK, -1, EPD_MOSI, EPD_CS);
    display.epd2.selectSPI(SPI, SPISettings(SPI_CLOCK_HZ, MSBFIRST, SPI_MODE0));

    display.init(115200);
    display.setRotation(0);
    display.setFullWindow();

    display.firstPage();
    do
    {
        display.drawBitmap(0, 0, backImage, 200, 200, GxEPD_BLACK);

        display.fillRect(60, 137, 124, 5, GxEPD_BLACK);
        display.fillRect(120, 82, 60, 2, GxEPD_BLACK);
        display.fillRect(10, 42, 3, 129, GxEPD_BLACK);

        display.drawRect(150, 8, 40, 16, GxEPD_BLACK);
        display.drawRect(151, 9, 38, 14, GxEPD_BLACK);
        display.fillRect(190, 12, 3, 7, GxEPD_BLACK);
        for (int i = 0; i < voltageSegments; i++)
            display.fillRect(154 + (i * 7), 12, 4, 8, GxEPD_BLACK);

        display.fillRoundRect(20, 40, 95, 45, 5, GxEPD_BLACK);

        display.fillRoundRect(35, 143, 15, 40, 8, GxEPD_BLACK);
        display.fillCircle(42, 173, 10, GxEPD_BLACK);
        display.fillRoundRect(37, 145, 11, 36, 8, GxEPD_WHITE);
        display.fillCircle(42, 173, 8, GxEPD_WHITE);
        display.fillRoundRect(40, 153, 5, 25, 2, GxEPD_BLACK);
        display.fillCircle(42, 173, 5, GxEPD_BLACK);

        for (int i = 0; i < 6; i++)
            display.fillCircle(122, 170 - (i * 3), 6 - i, GxEPD_BLACK);

        display.fillRoundRect(152, 94, 30, 22, 4, GxEPD_BLACK);

        display.setTextColor(GxEPD_BLACK);
        display.setFont(&DSEG7_Classic_Bold_36);
        display.setCursor(18, 130);
        display.print(tt);

        display.setFont(&DejaVu_Sans_Condensed_Bold_15);
        display.setTextColor(GxEPD_WHITE);
        display.setCursor(27, 57);
        display.print("DATE");

        display.setTextColor(GxEPD_BLACK);
        display.setCursor(60, 161);
        display.print("TEMP");
        display.setCursor(60, 177);
        display.print(tmp);
        display.setCursor(135, 161);
        display.print("HUM");
        display.setCursor(135, 177);
        display.print(hum2);
        display.setCursor(120, 78);
        // Display "MQTT" if enabled, otherwise "SHTC3"
        if (ConfigManager::instance().getConfig().mqtt_enabled) {
            display.print("MQTT");
        }

        display.setTextColor(GxEPD_WHITE);
        display.setCursor(156, 110);
        display.print(days[sys_wday]);

        display.setFont(&DejaVu_Sans_Condensed_Bold_18);
        display.setCursor(27, 76);
        display.print(dateString);

        display.setTextColor(GxEPD_BLACK);
        display.setFont(&DejaVu_Sans_Condensed_Bold_23);
        // Show application version instead of static label
        display.setCursor(120, 62);
        display.print(ConfigManager::instance().getConfig().app_version);

        // Wi-Fi status: AP/STA + IP
        String wifiStr = getWifiStatusString();

        display.setFont(&DejaVu_Sans_Condensed_Bold_15); // small readable font
        int16_t tbx, tby;
        uint16_t tbw, tbh;
        const int textX = 40;
        const int textY = 200;
        display.getTextBounds(wifiStr, textX, textY, &tbx, &tby, &tbw, &tbh);
        int pad = 4; // small padding around text
        int rectX = tbx - pad;
        int rectY = tby - pad;
        int rectW = tbw + (pad * 2);
        int rectH = tbh + (pad * 2);
        if (rectX < 0)
            rectX = 0;
        if (rectY < 0)
            rectY = 0;
        if (rectX + rectW > 200)
            rectW = 200 - rectX;
        if (rectY + rectH > 200)
            rectH = 200 - rectY;
        display.fillRect(rectX, rectY, rectW, rectH, GxEPD_WHITE);
        display.setTextColor(GxEPD_BLACK);
        // Bottom of the screen (y ≈ 195 on a 200px tall display)
        display.setCursor(textX, textY);
        display.print(wifiStr);

        // Display device name at the top-right area (replaces VOLOS from bitmap)
        const char *devName = ConfigManager::instance().getConfig().device_name;
        if (devName && strlen(devName) > 0) {
            display.setFont(&DejaVu_Sans_Condensed_Bold_18);
            int16_t nbx, nby;
            uint16_t nbw, nbh;
            const int nameX = 30;
            const int nameY = 25;
            display.getTextBounds(devName, nameX, nameY, &nbx, &nby, &nbw, &nbh);
            int npad = 4;
            int nrectX = nbx - npad;
            int nrectY = nby - npad;
            int nrectW = nbw + (npad * 2);
            int nrectH = nbh + (npad * 2);
            if (nrectX < 0)
                nrectX = 0;
            if (nrectY < 0)
                nrectY = 0;
            if (nrectX + nrectW > 200)
                nrectW = 200 - nrectX;
            if (nrectY + nrectH > 200)
                nrectH = 200 - nrectY;
            display.fillRect(nrectX, nrectY, nrectW, nrectH, GxEPD_WHITE);
            display.setTextColor(GxEPD_BLACK);
            display.setCursor(nameX, nameY);
            display.print(devName);
        }

        // If requested, draw a small sleep indicator overlay in the top-left corner
        if (showSleepIndicator) {
            display.setFont(&DejaVu_Sans_Condensed_Bold_15);
            display.setTextColor(GxEPD_WHITE);
            display.fillRect(0, 0, 28, 18, GxEPD_BLACK);
            display.setCursor(4, 14);
            display.print("Zz");
        }

    } while (display.nextPage());
}

void setup()
{
    Serial.begin(115200);
    DEBUG_PRINT("Boot EPD Clock...");

    gpio_deep_sleep_hold_dis();
    gpio_hold_dis((gpio_num_t)VBAT_PWR);
    gpio_hold_dis((gpio_num_t)EPD_PWR);

    pinMode(VBAT_PWR, OUTPUT);
    digitalWrite(VBAT_PWR, HIGH);

    pinMode(EPD_PWR, OUTPUT);
    digitalWrite(EPD_PWR, LOW);

    pinMode(3, OUTPUT);
    digitalWrite(3, HIGH);

    delay(10);

    Wire.begin(I2C_SDA, I2C_SCL);
    shtc3.begin();

    if (!ConfigManager::instance().begin())
    {
        Serial.println("[Config] Loading error, using default values.");
        ConfigManager::instance().save();
    }

    setupMQTT();

    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();

    float tempC = 0.0f;
    float humidity = 0.0f;
    int batteryMv = 0;

    if (cause == ESP_SLEEP_WAKEUP_TIMER)
    {
        Serial.println("[MODE] TIMER wakeup -> measurement + deep sleep mode");

        bool wifiOK = connectWiFiShort(6000);
        if (wifiOK)
        {
            // Sync system time via NTP if Wi‑Fi available (timezone aware)
            syncRtcFromNtpIfPossible();
        }

        // Re-read the current time (may have been updated) and sensors
        readTimeAndSensorAndPrepareStrings(tempC, humidity, batteryMv);

        if (wifiOK)
        {
            epdDraw();
            publishMQTT_reading(tempC, humidity, batteryMv);
            disconnectWiFiClean();
        }
        else
        {
            epdDraw();
        }

        goDeepSleep();
    }
    else
    {
        // Démarrage / reset : mode interactif + serveur web
        startWebServer();

        // If connected to Wi‑Fi (STA mode), we can perform NTP sync
        if (WiFi.status() == WL_CONNECTED)
        {
            syncRtcFromNtpIfPossible();
        }

        readTimeAndSensorAndPrepareStrings(tempC, humidity, batteryMv);

        epdDraw();

        interactiveMode = true;
        interactiveLastTouchMs.store(millis());
    }
}

void loop()
{
    if (interactiveMode)
    {
        const uint32_t timeout = ConfigManager::instance().getConfig().interactive_timeout_ms;
        const uint32_t now = millis();
        const uint32_t last = interactiveLastTouchMs.load();

        if ((uint32_t)(now - last) > timeout)
        {
            Serial.println("[MODE] Timeout interactif atteint.");

            if (isApModeActive())
            {
                Serial.println("[POWER] AP actif, on reste en mode interactif.");
                interactiveLastTouchMs.store(millis());
            }
            else
            {
                disconnectWiFiClean();
                delay(50);
                goDeepSleep();
            }
        }
    }

    delay(10);
}

static int readBatteryVoltage()
{
    int pin = 4;
    analogReadResolution(12);
    analogSetPinAttenuation(pin, ADC_11db);
    int mv = analogReadMilliVolts(pin);
    return (int)(mv * 2.0f);
}
