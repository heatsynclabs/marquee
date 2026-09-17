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
// Both can also be supplied as build flags, to keep them out of the source.
#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif
#ifndef WIFI_PASS
#define WIFI_PASS ""
#endif

// --- Defaults ---------------------------------------------------------------
#define DEFAULT_HOSTNAME   "marquee"
#define DEFAULT_BRIGHTNESS 60      // 0-255
#define DEFAULT_POWER_MA   800     // supply budget, see README
#define DEFAULT_TZ         "PST8PDT,M3.2.0,M11.1.0"
#define DEFAULT_ROTATE_MS  15000   // how long each idle entry stays up

// --- Status poll ------------------------------------------------------------
// Polls a JSON endpoint for a boolean and shows something extra while it is
// true. The defaults watch whether HeatSync Labs is open.
#define DEFAULT_STATUS_URL   "https://members.heatsynclabs.org/space_api.json"
#define DEFAULT_STATUS_KEY   "open"
#define DEFAULT_STATUS_POLL  300   // seconds between polls
#define DEFAULT_STATUS_EVERY 2     // show it every Nth idle entry while true
#define STATUS_TLS_BUFFER    4096  // BearSSL receive buffer for the poll
#define DEFAULT_STATUS_TEXT  "LAB OPEN"
#define DEFAULT_STATUS_COLOR "#33ff66"

// --- CLASP relay ------------------------------------------------------------
// A wss:// relay behind a CDN sends TLS records up to 16 KB and will not
// negotiate anything smaller, so the receive buffer has to be this big. With
// the web server and the panel running there is not enough heap left for it,
// and the firmware says so and stays on rather than trying. A ws:// relay on
// your own network costs nothing. See docs/clasp.md.
#define CLASP_TLS_BUFFER  16384
#define CLASP_TLS_HEAP    (CLASP_TLS_BUFFER + 14000)

// --- Network ----------------------------------------------------------------
#define HTTP_PORT 80
#define WS_PORT   81
#define UDP_PORT  21324  // WLED-compatible realtime port

// --- Timing -----------------------------------------------------------------
#define FRAME_MS 20      // 50 fps
