# EPD Clock with Web Configuration

ESP32-S3 e-paper clock firmware that shows time, date, temperature, humidity, battery level, and Wi-Fi status on a 1.54" GxEPD2 display. The device reads a SHTC3 sensor, syncs time via NTP, can push measurements to MQTT, serves a web dashboard/config UI from LittleFS, and uses deep sleep to save power.

## Features
- E-paper UI with custom bitmap background (`src/background.h`) and bitmap fonts (`src/fonts.h`)
- Time kept via NTP (no hardware RTC), timezone configurable (default: CET with DST)
- SHTC3 temperature/humidity readings with configurable offsets
- Battery voltage indicator with 5 segments
- Wi-Fi STA + fallback AP for configuration; AP SSID defaults to `EPD_Clock`
- MQTT publishing of readings (topic/host/credentials configurable)
- Web server on port 80 with password-protected config page, live metrics + logs endpoint
- Deep sleep cycle with configurable interval; interactive mode timeout before sleep
- Circular in-memory debug log exposed via HTTP

## Hardware
- Module: Waveshare ESP32-S3 E-Paper 1.54 (V2) - https://www.waveshare.com/esp32-s3-epaper-1.54.htm
- Board profile: `esp32-s3-devkitc-1` (PlatformIO target `esp32-s3-devkitc-1`)
- Display: 1.54" GxEPD2 E-Paper (pins in `src/main.cpp`: DC=10, CS=11, RST=9, BUSY=8, PWR=6, SCK=12, MOSI=13)
- I2C: SHTC3 on SDA=47, SCL=48
- Battery sense: analog pin 4 (scaled reading)
- Power: EPD power GPIO 6, VBAT power GPIO 17

## Build and Flash (PlatformIO)
1. Install PlatformIO (VS Code extension or CLI).
2. Connect the ESP32-S3 (USB CDC enabled by flags in `platformio.ini`).
3. Build and upload:
   - VS Code: "PlatformIO: Upload"
   - CLI: `pio run --target upload`
4. Monitor serial (115200 baud):
   - VS Code: "PlatformIO: Monitor"
   - CLI: `pio device monitor -b 115200`

## File Layout (key parts)
- `src/main.cpp` - boot flow, sensor read, display drawing, sleep logic
- `src/config_manager.{h,cpp}` - persistent settings (Preferences), JSON import/export, defaults
- `src/web_server.cpp` - LittleFS-backed HTTP server, config/auth, dashboard and logs
- `src/mqtt.{h,cpp}` - MQTT publish helper
- `src/utils.{h,cpp}` - Wi-Fi connect/disconnect helpers, circular log buffer
- `data/` - LittleFS assets (HTML/CSS/JS) served by the web UI

## Configuration & Usage
- On boot, tries Wi-Fi STA using saved credentials; if it fails, starts AP `EPD_Clock`.
- Web UI: browse to `http://<device-ip>/config.html` (defaults: user `admin`, pass `admin`).
- Update Wi-Fi, MQTT, offsets, time zone, display name, app version, and timeouts via the form; settings persist in Preferences.
- `POST /api/dashboard` (or GET) returns current metrics and log buffer for dashboards.
- `POST /api/mqtt/test` triggers a test publish with dummy values.

## Power Behavior
- If woken by timer: connect Wi-Fi briefly, sync NTP, read sensors, update display, publish MQTT (if enabled), then deep sleep for `deepsleep_interval_s`.
- In interactive mode (after fresh boot): serves web UI until `interactive_timeout_min` elapses; if not in AP mode, disconnects Wi-Fi and sleeps.

## Defaults (set in `ConfigManager::applyDefaultsIfNeeded`)
- `device_name=EPD-Clock`, `app_version=1.0.0`
- Wi-Fi empty (must be set)
- MQTT disabled, host `broker.local`, port 1883, topic empty
- Admin credentials: `admin` / `admin` (change them!)
- Deep sleep interval: 20 s; interactive timeout: 5 min
- Sensor offsets: 0; NTP TZ: `CET-1CEST,M3.5.0/2,M10.5.0/3`

## LittleFS Content
Place web assets in `data/` and upload to the board:
```
pio run --target uploadfs
```

## Troubleshooting
- If Wi-Fi STA fails, connect to the `EPD_Clock` AP and reconfigure.
- Logs: `GET /api/logs` (auth required) or check serial output.
- If MQTT publish fails, verify broker host/port/credentials and Wi-Fi connectivity.
