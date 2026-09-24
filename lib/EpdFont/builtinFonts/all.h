#pragma once

#if FREEINK_DEVICE_EMINIMAL
// Reader fonts for the ~226 DPI 7.8" panel: one family (NotoSerif, sans read
// no different on it) at 18-24 pt, where the stock 12-18 pt set is sized for
// ~150 DPI panels, English + numbers/symbols only (no Latin Extended, Cyrillic,
// Vietnamese). Other families, sizes and scripts come from the SD card.
#include <builtinFonts/notoserif_en_18_bold.h>
#include <builtinFonts/notoserif_en_18_bolditalic.h>
#include <builtinFonts/notoserif_en_18_italic.h>
#include <builtinFonts/notoserif_en_18_regular.h>
#include <builtinFonts/notoserif_en_20_bold.h>
#include <builtinFonts/notoserif_en_20_bolditalic.h>
#include <builtinFonts/notoserif_en_20_italic.h>
#include <builtinFonts/notoserif_en_20_regular.h>
#include <builtinFonts/notoserif_en_22_bold.h>
#include <builtinFonts/notoserif_en_22_bolditalic.h>
#include <builtinFonts/notoserif_en_22_italic.h>
#include <builtinFonts/notoserif_en_22_regular.h>
#include <builtinFonts/notoserif_en_24_bold.h>
#include <builtinFonts/notoserif_en_24_bolditalic.h>
#include <builtinFonts/notoserif_en_24_italic.h>
#include <builtinFonts/notoserif_en_24_regular.h>
#else
#include <builtinFonts/notoserif_12_bold.h>
#include <builtinFonts/notoserif_12_bolditalic.h>
#include <builtinFonts/notoserif_12_italic.h>
#include <builtinFonts/notoserif_12_regular.h>
#include <builtinFonts/notoserif_14_bold.h>
#include <builtinFonts/notoserif_14_bolditalic.h>
#include <builtinFonts/notoserif_14_italic.h>
#include <builtinFonts/notoserif_14_regular.h>
#include <builtinFonts/notoserif_16_bold.h>
#include <builtinFonts/notoserif_16_bolditalic.h>
#include <builtinFonts/notoserif_16_italic.h>
#include <builtinFonts/notoserif_16_regular.h>
#include <builtinFonts/notoserif_18_bold.h>
#include <builtinFonts/notoserif_18_bolditalic.h>
#include <builtinFonts/notoserif_18_italic.h>
#include <builtinFonts/notoserif_18_regular.h>
#include <builtinFonts/notosans_12_bold.h>
#include <builtinFonts/notosans_12_bolditalic.h>
#include <builtinFonts/notosans_12_italic.h>
#include <builtinFonts/notosans_12_regular.h>
#include <builtinFonts/notosans_14_bold.h>
#include <builtinFonts/notosans_14_bolditalic.h>
#include <builtinFonts/notosans_14_italic.h>
#include <builtinFonts/notosans_14_regular.h>
#include <builtinFonts/notosans_16_bold.h>
#include <builtinFonts/notosans_16_bolditalic.h>
#include <builtinFonts/notosans_16_italic.h>
#include <builtinFonts/notosans_16_regular.h>
#include <builtinFonts/notosans_18_bold.h>
#include <builtinFonts/notosans_18_bolditalic.h>
#include <builtinFonts/notosans_18_italic.h>
#include <builtinFonts/notosans_18_regular.h>
#endif
// UI chrome fonts: one tier per device class. main.cpp registers whichever
// tier is compiled here under the same font slots (small / UI_10 / UI_12).
#if FREEINK_DEVICE_EMINIMAL
// The UI tier for the 7.8" panel: Ubuntu 20/24 and NotoSans 16 small (2x the
// stock 10/12/8), English only -- no Hebrew/Arabic/Vietnamese stack, no
// Cyrillic (convert-builtin-fonts.sh, e-Minimal section). The 8/10/12 tier
// below is not compiled for this device.
#include <builtinFonts/notosans_12_small.h>  // button legends, kept at the 1.5x size
#include <builtinFonts/notosans_16_small.h>
#include <builtinFonts/ubuntu_20_bold.h>
#include <builtinFonts/ubuntu_20_regular.h>
#include <builtinFonts/ubuntu_24_bold.h>
#include <builtinFonts/ubuntu_24_regular.h>
#else
#include <builtinFonts/notosans_8_regular.h>
#include <builtinFonts/ubuntu_10_bold.h>
#include <builtinFonts/ubuntu_10_regular.h>
#include <builtinFonts/ubuntu_12_bold.h>
#include <builtinFonts/ubuntu_12_regular.h>
#endif
