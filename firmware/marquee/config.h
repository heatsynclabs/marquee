// Compile-time defaults. Everything here can be changed at runtime through
// /api/config, except the pin and matrix geometry.
#pragma once

// --- Panel ------------------------------------------------------------------
#define LED_PIN       D2   // GPIO4 on a NodeMCU
#define MATRIX_WIDTH  30
#define MATRIX_HEIGHT 8

// Wiring: pixel 0 sits in the top-right corner and the rows zigzag.
#define MATRIX_SERPENTINE true   // rows alternate direction
#define MATRIX_ROW0_RIGHT true   // first pixel of row 0 is on the right
#define MATRIX_ROW0_TOP   true   // row 0 is the top row

// --- Wi-Fi ------------------------------------------------------------------
// Leave empty to get the setup portal (AP "marquee-setup") on first boot.
#define WIFI_SSID ""
#define WIFI_PASS ""

// --- Defaults ---------------------------------------------------------------
#define DEFAULT_HOSTNAME   "marquee"
#define DEFAULT_BRIGHTNESS 60      // 0-255
#define DEFAULT_POWER_MA   800     // supply budget, see README
#define DEFAULT_IDLE       "clock" // effect shown when nothing is queued
#define DEFAULT_TZ         "PST8PDT,M3.2.0,M11.1.0"

// --- Network ----------------------------------------------------------------
#define HTTP_PORT 80
#define WS_PORT   81
#define UDP_PORT  21324  // WLED-compatible realtime port

// --- Timing -----------------------------------------------------------------
#define FRAME_MS 20      // 50 fps
