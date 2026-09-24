#!/bin/bash

set -e

cd "$(dirname "$0")"

READER_FONT_STYLES=("Regular" "Italic" "Bold" "BoldItalic")
NOTOSERIF_FONT_SIZES=(12 14 16 18)
NOTOSANS_FONT_SIZES=(12 14 16 18)

for size in ${NOTOSERIF_FONT_SIZES[@]}; do
  for style in ${READER_FONT_STYLES[@]}; do
    font_name="notoserif_${size}_$(echo $style | tr '[:upper:]' '[:lower:]')"
    font_path="../builtinFonts/source/NotoSerif/NotoSerif-${style}.ttf"
    output_path="../builtinFonts/${font_name}.h"
    python fontconvert.py $font_name $size $font_path --2bit --compress --pnum --zopfli > $output_path
    echo "Generated $output_path"
  done
done

for size in ${NOTOSANS_FONT_SIZES[@]}; do
  for style in ${READER_FONT_STYLES[@]}; do
    font_name="notosans_${size}_$(echo $style | tr '[:upper:]' '[:lower:]')"
    font_path="../builtinFonts/source/NotoSans/NotoSans-${style}.ttf"
    output_path="../builtinFonts/${font_name}.h"
    python fontconvert.py $font_name $size $font_path --2bit --compress --pnum --zopfli > $output_path
    echo "Generated $output_path"
  done
done

UI_FONT_SIZES=(10 12)
UI_FONT_STYLES=("Regular" "Bold")

# Arabic glyphs for UI text (menus, file browser titles). The built-in fonts
# must cover the *output* of MiniBidi's do_shape() — contextual presentation
# forms — not base letters, or shaped UI text silently drops glyphs.
# Curated for firmware-size budget: core Arabic (Presentation Forms-B,
# incl. the Lam-Alef ligature forms) plus the Farsi/Urdu extra letters'
# Presentation Forms-A blocks, the few characters shaping leaves at their
# base codepoint, Arabic punctuation, and both digit sets. No harakat and
# no Sindhi/Pashto/Kurdish forms — book text gets those from SD-card fonts.
ARABIC_INTERVALS=(
  --additional-intervals 0x060C,0x060C  # Arabic comma
  --additional-intervals 0x061B,0x061B  # Arabic semicolon
  --additional-intervals 0x061F,0x061F  # Arabic question mark
  --additional-intervals 0x0621,0x0621  # hamza (non-joining, never shaped)
  --additional-intervals 0x0640,0x0640  # tatweel
  --additional-intervals 0x0654,0x0654  # Persian/Urdu ezafe hamza, Arabic hamza carriers
  --additional-intervals 0x0660,0x0669  # Arabic-Indic digits
  --additional-intervals 0x06BA,0x06BA  # noon ghunna base (initial/medial keep base cp)
  --additional-intervals 0x06D4,0x06D4  # Urdu full stop
  --additional-intervals 0x06D5,0x06D5  # ae (isolated; Kurdish/Uyghur/Ottoman) — has no presentation form
  --additional-intervals 0x06F0,0x06F9  # extended Arabic-Indic digits (Farsi/Urdu)
  --additional-intervals 0xFB56,0xFB59  # peh (Farsi)
  --additional-intervals 0xFB66,0xFB69  # tteh (Urdu)
  --additional-intervals 0xFB7A,0xFB7D  # tcheh (Farsi)
  --additional-intervals 0xFB88,0xFB95  # ddal, jeh, rreh (Urdu), keheh, gaf (Farsi/Urdu)
  --additional-intervals 0xFB9E,0xFB9F  # noon ghunna isolated/final (Urdu)
  --additional-intervals 0xFBA6,0xFBB1  # heh goal, heh doachashmee, yeh barree(+hamza) (Urdu)
  --additional-intervals 0xFBFC,0xFBFF  # farsi yeh (Farsi/Urdu)
  --additional-intervals 0xFE80,0xFEFC  # Presentation Forms-B: core Arabic + Lam-Alef
)

for size in ${UI_FONT_SIZES[@]}; do
  for style in ${UI_FONT_STYLES[@]}; do
    font_name="ubuntu_${size}_$(echo $style | tr '[:upper:]' '[:lower:]')"
    font_path="../builtinFonts/source/Ubuntu/Ubuntu-${style}.ttf"
    hebrew_path="../builtinFonts/source/NotoSansHebrew/NotoSansHebrew-${style}.ttf"
    arabic_path="../builtinFonts/source/NotoSansArabic/NotoSansArabic-${style}.ttf"
    # Ubuntu lacks the Latin Extended Additional block (U+1EA0-U+1EF9) used for
    # Vietnamese tone marks. Append a Vietnamese-only Ubuntu cut so those glyphs
    # are filled from it while every glyph Ubuntu already has stays unchanged
    # (fontstack is ordered by descending priority).
    viet_path="../builtinFonts/source/Ubuntu/Ubuntu-Vietnamese-${style}.ttf"
    output_path="../builtinFonts/${font_name}.h"
    python fontconvert.py $font_name $size $font_path $hebrew_path $arabic_path $viet_path \
      --additional-intervals 0x05D0,0x05EA "${ARABIC_INTERVALS[@]}" > $output_path
    echo "Generated $output_path"
  done
done

python fontconvert.py notosans_8_regular 8 \
  ../builtinFonts/source/NotoSans/NotoSans-Regular.ttf \
  ../builtinFonts/source/NotoSansHebrew/NotoSansHebrew-Regular.ttf \
  ../builtinFonts/source/NotoSansArabic/NotoSansArabic-Regular.ttf \
  --additional-intervals 0x05D0,0x05EA "${ARABIC_INTERVALS[@]}" > ../builtinFonts/notosans_8_regular.h

# e-Minimal 7.8" (~226 DPI): English + numbers, symbols and special characters
# only, for both the reader and the UI -- Basic Latin, Latin-1 and the few
# Windows-1252 extras, punctuation, super/subscripts, currency, letterlike,
# number forms, arrows, math, combining marks and the fi/fl ligatures. No
# Latin Extended, Vietnamese, Cyrillic, Hebrew or Arabic (the SD card carries
# other scripts). Only builtinFonts/all.h under FREEINK_DEVICE_EMINIMAL
# includes these; the stock sets above are untouched.
EMINIMAL_INTERVALS=(--no-default-intervals)
for range in 0x0020,0x007E 0x00A0,0x00FF 0x0152,0x0153 0x0160,0x0161 0x0178,0x0178              0x017D,0x017E 0x0192,0x0192 0x02C6,0x02C6 0x02DC,0x02DC 0x0300,0x036F              0x2000,0x206F 0x2070,0x209F 0x20A0,0x20CF 0x2100,0x214F 0x2150,0x218F              0x2190,0x21FF 0x2200,0x22FF 0xFB00,0xFB06 0xFFFD,0xFFFD; do
  EMINIMAL_INTERVALS+=(--additional-intervals $range)
done

# Reader: NotoSerif 18/20/22/24 pt, 4 styles, as notoserif_en_* (18 pt is also
# a stock size, so the e-Minimal set keeps its own names).
for size in 18 20 22 24; do
  for style in ${READER_FONT_STYLES[@]}; do
    font_name="notoserif_en_${size}_$(echo $style | tr '[:upper:]' '[:lower:]')"
    python fontconvert.py $font_name $size ../builtinFonts/source/NotoSerif/NotoSerif-${style}.ttf       --2bit --compress --pnum --zopfli "${EMINIMAL_INTERVALS[@]}" > ../builtinFonts/${font_name}.h
    echo "Generated ../builtinFonts/${font_name}.h"
  done
done

# UI: Ubuntu 20/24 (2x the stock 10/12), NotoSans 16 small, NotoSans 12 for the
# button legends.
for size in 20 24; do
  for style in ${UI_FONT_STYLES[@]}; do
    font_name="ubuntu_${size}_$(echo $style | tr '[:upper:]' '[:lower:]')"
    python fontconvert.py $font_name $size ../builtinFonts/source/Ubuntu/Ubuntu-${style}.ttf       "${EMINIMAL_INTERVALS[@]}" > ../builtinFonts/${font_name}.h
    echo "Generated ../builtinFonts/${font_name}.h"
  done
done
for size in 16 12; do
  python fontconvert.py notosans_${size}_small $size ../builtinFonts/source/NotoSans/NotoSans-Regular.ttf     "${EMINIMAL_INTERVALS[@]}" > ../builtinFonts/notosans_${size}_small.h
  echo "Generated ../builtinFonts/notosans_${size}_small.h"
done

echo ""
echo "Running compression verification..."
python verify_compression.py ../builtinFonts/
