#pragma once

#include <Arduino.h>
#include <atomic>

// ---------- DEBUG ----------
#define DEBUG true
#define DEBUG_PRINT(x)             if (DEBUG)                     {                                  Serial.println(x);         }
#define DEBUG_PRINTF(...)                   if (DEBUG)                              {                                           Serial.printf(__VA_ARGS__);         }

// Dernier "contact" interactif (ping HTTP)
extern std::atomic<uint32_t> interactiveLastTouchMs;
