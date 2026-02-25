#pragma once

// -------------------------------------------------------
// WiFi credentials — change before flashing
// -------------------------------------------------------
#define WIFI_SSID     "SSID"
#define WIFI_PASSWORD "PASSWORD"

// -------------------------------------------------------
// Price thresholds (c/kWh) for colour coding
//   green  < LOW
//   yellow  LOW … HIGH
//   red    > HIGH
// -------------------------------------------------------
#define PRICE_LOW   10.0f
#define PRICE_HIGH 20.0f

// Re-fetch interval (ms)  — every hour
#define FETCH_INTERVAL_MS  3600000UL
