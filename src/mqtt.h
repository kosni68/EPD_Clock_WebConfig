#pragma once
#include <Arduino.h>

void setupMQTT();
bool publishMQTT_reading(float temperatureC, float humidityPct, int batteryMv);
