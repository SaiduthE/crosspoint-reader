#pragma once

// The name and version the device shows its user. A build that ships under its
// own name (e.g. [env:eminimal]) sets FIRMWARE_PRODUCT_NAME and
// FIRMWARE_PRODUCT_VERSION; every other build shows CrossPoint's. The machine
// version (web API, User-Agent, OTA comparison) stays CROSSPOINT_VERSION.
#ifdef FIRMWARE_PRODUCT_NAME
#define FIRMWARE_NAME_TEXT FIRMWARE_PRODUCT_NAME
#else
#define FIRMWARE_NAME_TEXT tr(STR_CROSSPOINT)
#endif

#if defined(FIRMWARE_PRODUCT_NAME) && defined(FIRMWARE_PRODUCT_VERSION)
#define FIRMWARE_DISPLAY_VERSION FIRMWARE_PRODUCT_NAME " v" FIRMWARE_PRODUCT_VERSION
#else
#define FIRMWARE_DISPLAY_VERSION CROSSPOINT_VERSION
#endif

// The .local name a device advertises over mDNS in Calibre mode.
#ifdef FIRMWARE_PRODUCT_HOSTNAME
#define FIRMWARE_HOSTNAME FIRMWARE_PRODUCT_HOSTNAME
#else
#define FIRMWARE_HOSTNAME "crosspoint"
#endif

// About's firmware row: a renamed build also names the CrossPoint release it is based on.
#ifdef CROSSPOINT_BASE_VERSION
#define FIRMWARE_ABOUT_VERSION FIRMWARE_DISPLAY_VERSION " (CrossPoint " CROSSPOINT_BASE_VERSION ")"
#else
#define FIRMWARE_ABOUT_VERSION FIRMWARE_DISPLAY_VERSION
#endif
