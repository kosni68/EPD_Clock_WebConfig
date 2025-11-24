#pragma once
#include <Arduino.h>

bool connectWiFiShort(uint32_t timeoutMs = 8000);
void disconnectWiFiClean();

bool isApModeActive();
