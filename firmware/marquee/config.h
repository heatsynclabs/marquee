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
#define DEFAULT_TZ         "MST7"    // Arizona: UTC-7, no daylight saving
#define DEFAULT_ROTATE_MS  15000   // how long each idle entry stays up

// --- Status poll ------------------------------------------------------------
// Polls a JSON endpoint for a boolean and shows something extra while it is
// true. The defaults watch whether HeatSync Labs is open.
#define DEFAULT_STATUS_URL   "https://members.heatsynclabs.org/space_api.json"
#define DEFAULT_STATUS_KEY   "open"
#define DEFAULT_STATUS_POLL  300   // seconds between polls
#define DEFAULT_STATUS_EVERY 2     // show it every Nth idle entry while true
#define STATUS_TLS_BUFFER    4096  // BearSSL receive buffer for the poll
// OPEN held still (it fits), then the whole sentence in one smooth scroll.
// {until} becomes "UNTIL 9 PM" when the feed has "open_until", else is dropped.
#define DEFAULT_STATUS_CARDS "OPEN|OPEN HOURS - WE'RE OPEN - COME IN - {until}"
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
// The NeoPixel driver turns interrupts off for about 7 ms per push of 240
// pixels, and Wi-Fi needs those interrupts. 30 fps keeps that under a quarter
// of the time, and frames that did not change are not pushed at all.
#define FRAME_MS        33     // ~30 fps
#define REFRESH_MS      1000   // re-push an unchanged frame this often anyway

// --- Health -----------------------------------------------------------------
// Below this largest free block the TLS poll and the web server start failing.
// If it stays there, the panel restarts itself rather than limping on.
#define HEAP_LOW_BLOCK  4096
#define HEAP_LOW_MS     60000
// Wi-Fi gone this long: restart, which clears any stuck driver state.
#define WIFI_LOST_REBOOT_MS (10UL * 60 * 1000)
// Never connected this long after boot: open the setup portal next to the
// normal connection attempts, so a router that boots slowly does not strand
// the panel in setup mode.
#define PORTAL_AFTER_MS     (3UL * 60 * 1000)

// Default scroll speed. At 30 fps this is one pixel per frame: as fast as
// text can go while every frame still moves by exactly one column.
#define DEFAULT_SPEED   30

#define FW_VERSION "1.2.0"
