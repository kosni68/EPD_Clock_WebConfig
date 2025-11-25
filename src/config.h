#pragma once

#include <Arduino.h>
#include <atomic>

// ---------- DEBUG ----------
#include <stdio.h>

// Forward declaration for log buffer append (implemented in utils.cpp)
void appendLog(const char *msg);

#define DEBUG true
#define DEBUG_PRINT(x)             if (DEBUG) { Serial.println(x); appendLog(x); }
#define DEBUG_PRINTF(...) do { if (DEBUG) { char _dbgbuf[256]; snprintf(_dbgbuf, sizeof(_dbgbuf), __VA_ARGS__); Serial.printf(__VA_ARGS__); appendLog(_dbgbuf); } } while(0)

// Dernier "contact" interactif (ping HTTP)
extern std::atomic<uint32_t> interactiveLastTouchMs;
