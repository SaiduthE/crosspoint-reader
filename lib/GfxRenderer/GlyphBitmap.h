#pragma once

#include <algorithm>
#include <cstdint>

// Glyph rasterizer that resolves orientation, clipping and framebuffer
// addressing once per glyph instead of once per pixel. It has no dependency
// on GfxRenderer so it can be unit-tested on the host.
namespace glyphBitmap {

// Framebuffer plane a glyph is painted into.
enum class Plane : uint8_t { BW, GrayLSB, GrayMSB };

// Position of glyph pixel (0, 0) plus the step for one move along the glyph's
// x and y axes, all in one coordinate space. The axes must describe an
// orthogonal unit rotation: each is +/-1 on exactly one component.
struct Frame {
  int x;
  int y;
  int dxX;
  int dxY;
  int dyX;
  int dyY;
};

// Where a glyph lands in physical framebuffer space.
struct Target {
  uint8_t* buffer;  // First byte of row originY (framebuffer or strip scratch)
  int width;        // Physical panel width in pixels
  int stride;       // Bytes per physical row
  int originY;      // First physical row held by buffer
  int rows;         // Number of physical rows held by buffer
  Frame frame;      // Physical placement of the glyph
};

// Half-open rectangle in glyph-local pixel coordinates.
struct Clip {
  int left;
  int top;
  int right;
  int bottom;
};

// Narrow [start, end) on one glyph axis so that base + i * step stays within
// [lower, upper). step is +1 or -1 because the transform is orthogonal.
inline void clipAxis(int base, int step, int lower, int upper, int& start, int& end) {
  if (step > 0) {
    start = std::max(start, lower - base);
    end = std::min(end, upper - base);
  } else {
    start = std::max(start, base - upper + 1);
    end = std::min(end, base - lower + 1);
  }
}

// Narrow a glyph-local clip so every kept pixel maps inside the half-open
// rectangle [left, right) x [top, bottom) of the space frame is expressed in.
// Which glyph axis runs along that space's x depends on the rotation:
// dxX != 0 means glyph x does. Runs twice per glyph, so it stays inline
// under -Os.
__attribute__((always_inline)) inline void clipToRect(const Frame& frame, int left, int top, int right, int bottom,
                                                      Clip& clip) {
  if (frame.dxX != 0) {
    clipAxis(frame.x, frame.dxX, left, right, clip.left, clip.right);
    clipAxis(frame.y, frame.dyY, top, bottom, clip.top, clip.bottom);
  } else {
    clipAxis(frame.x, frame.dyX, left, right, clip.top, clip.bottom);
    clipAxis(frame.y, frame.dxY, top, bottom, clip.left, clip.right);
  }
}

// Keep pixel writes inline when decoding a group of four pixels.
__attribute__((always_inline)) inline void paint(uint8_t* buffer, int destination, uint8_t ink, uint8_t levels,
                                                 bool clearBits) {
  if ((levels & (1u << ink)) == 0) return;
  const uint8_t mask = 0x80u >> (destination & 7);
  if (clearBits)
    buffer[destination >> 3] &= static_cast<uint8_t>(~mask);
  else
    buffer[destination >> 3] |= mask;
}

// Paint a packed glyph into target.
//
// bitmap: rows are contiguous, MSB first, 1 or 2 bits per pixel; widths need
//   not be byte-aligned. 2bpp values are 0=white, 1=light gray, 2=dark gray,
//   3=black. 1bpp value 1 is ink.
// plane: which 2bpp values are painted. BW paints every non-white value and
//   honours state (true clears the bit, i.e. black). The gray planes only ever
//   set bits, because there 0 means leave alone and 1 means update: MSB paints
//   both grays, LSB paints dark gray only. 1bpp glyphs paint ink with state on
//   every plane.
// Clipping happens before pixel decoding, so fully hidden glyphs cost nothing.
inline void draw(const uint8_t* bitmap, int width, int height, bool twoBit, Plane plane, bool state,
                 const Target& target, Clip clip) {
  // Bit n of levels set means source value n is painted.
  uint8_t levels = 0x02;
  bool clearBits = state;
  if (twoBit) {
    levels = plane == Plane::BW ? 0x0e : plane == Plane::GrayMSB ? 0x06 : 0x04;
    if (plane != Plane::BW) clearBits = false;
  }

  // Intersect with the glyph bounds, then with the panel width and the
  // buffered row band.
  clip.left = std::max(clip.left, 0);
  clip.top = std::max(clip.top, 0);
  clip.right = std::min(clip.right, width);
  clip.bottom = std::min(clip.bottom, height);
  clipToRect(target.frame, 0, target.originY, target.width, target.originY + target.rows, clip);
  if (clip.left >= clip.right || clip.top >= clip.bottom) return;

  // Walk the framebuffer as a flat bit index. One glyph column advances
  // stepX bits and one glyph row advances stepY bits; each is +/-1 for the
  // axis that maps to physical x, or +/-strideBits for physical y.
  const Frame& frame = target.frame;
  const int strideBits = target.stride * 8;
  const int stepX = frame.dxY * strideBits + frame.dxX;
  const int stepY = frame.dyY * strideBits + frame.dyX;
  int rowBit = (frame.y - target.originY) * strideBits + frame.x + clip.left * stepX + clip.top * stepY;
  for (int y = clip.top; y < clip.bottom; ++y, rowBit += stepY) {
    int source = y * width + clip.left;
    int destination = rowBit;
    int remaining = clip.right - clip.left;
    if (twoBit) {
      // Rows are bit-contiguous, not byte-padded. Align after any clipped prefix.
      while (remaining && (source & 3)) {
        paint(target.buffer, destination, (bitmap[source >> 2] >> (6 - (source & 3) * 2)) & 3, levels, clearBits);
        ++source;
        destination += stepX;
        --remaining;
      }
      while (remaining >= 4) {
        const uint8_t packed = bitmap[source >> 2];
        paint(target.buffer, destination, packed >> 6, levels, clearBits);
        paint(target.buffer, destination + stepX, (packed >> 4) & 3, levels, clearBits);
        paint(target.buffer, destination + 2 * stepX, (packed >> 2) & 3, levels, clearBits);
        paint(target.buffer, destination + 3 * stepX, packed & 3, levels, clearBits);
        source += 4;
        destination += 4 * stepX;
        remaining -= 4;
      }
    }
    // One-bit glyphs and the clipped tail use scalar decoding.
    while (remaining--) {
      const uint8_t ink = twoBit ? ((bitmap[source >> 2] >> (6 - (source & 3) * 2)) & 3)
                                 : ((bitmap[source >> 3] >> (7 - (source & 7))) & 1);
      paint(target.buffer, destination, ink, levels, clearBits);
      ++source;
      destination += stepX;
    }
  }
}

// --- 16-level (4bpp) target ---------------------------------------------------
// A whole physical frame, two pixels per byte, the left (even x) pixel in the
// high nibble, 0x0 black .. 0xF white (the IT8951 host format).
struct Gray4Target {
  uint8_t* buffer;  // Row 0 of the frame
  int width;        // Physical panel width in pixels
  int height;       // Physical panel height in pixels
  int stride;       // Bytes per physical row (>= width / 2)
  Frame frame;      // Physical placement of the glyph
};

// Value for "leave the pixel alone" in a nibble table.
constexpr uint8_t kGray4Skip = 0xFF;

// Nibble each source value paints, as the plane path would have composed it
// (driver gray4(): base white -> 0xF, MSB only -> 0xA, MSB+LSB -> 0x5, none
// -> 0x0). 2bpp source values: 0 white (untouched), 1 light, 2 dark, 3 black.
//  - antiAliased && state (black text): 1 -> 0xA, 2 -> 0x5, 3 -> 0x0 -- what
//    the B/W base + LSB + MSB passes produce for an AA glyph.
//  - !antiAliased && state: every ink value -> 0x0, the B/W pass alone.
//  - !state (white ink): every ink value -> 0xF; the planes only ever add grey
//    where the base is black, so white ink stays white.
// 1bpp source: value 1 is ink, painted 0x0 or 0xF by state.
inline void gray4Levels(bool twoBit, bool antiAliased, bool state, uint8_t levels[4]) {
  const uint8_t ink = state ? 0x0 : 0xF;
  levels[0] = kGray4Skip;
  if (!twoBit) {
    levels[1] = ink;
    levels[2] = levels[3] = kGray4Skip;  // unreachable for 1bpp
    return;
  }
  levels[1] = (state && antiAliased) ? 0xA : ink;
  levels[2] = (state && antiAliased) ? 0x5 : ink;
  levels[3] = ink;
}

__attribute__((always_inline)) inline void paintNibble(uint8_t* buffer, int destination, uint8_t value) {
  if (value == kGray4Skip) return;
  uint8_t& byte = buffer[destination >> 1];
  byte = (destination & 1) ? static_cast<uint8_t>((byte & 0xF0) | value)
                           : static_cast<uint8_t>((byte & 0x0F) | (value << 4));
}

// Paint a packed glyph (same bitmap format as draw()) into a 4bpp frame.
// Clipping and addressing are resolved once per glyph, as in draw(); the walk
// is a flat nibble index (stride * 2 nibbles per physical row).
inline void drawGray4(const uint8_t* bitmap, int width, int height, bool twoBit, bool antiAliased, bool state,
                      const Gray4Target& target, Clip clip) {
  uint8_t levels[4];
  gray4Levels(twoBit, antiAliased, state, levels);

  clip.left = std::max(clip.left, 0);
  clip.top = std::max(clip.top, 0);
  clip.right = std::min(clip.right, width);
  clip.bottom = std::min(clip.bottom, height);
  clipToRect(target.frame, 0, 0, target.width, target.height, clip);
  if (clip.left >= clip.right || clip.top >= clip.bottom) return;

  const Frame& frame = target.frame;
  const int strideNibbles = target.stride * 2;
  const int stepX = frame.dxY * strideNibbles + frame.dxX;
  const int stepY = frame.dyY * strideNibbles + frame.dyX;
  int rowNibble = frame.y * strideNibbles + frame.x + clip.left * stepX + clip.top * stepY;
  for (int y = clip.top; y < clip.bottom; ++y, rowNibble += stepY) {
    int source = y * width + clip.left;
    int destination = rowNibble;
    for (int remaining = clip.right - clip.left; remaining > 0; --remaining) {
      const uint8_t value = twoBit ? ((bitmap[source >> 2] >> (6 - (source & 3) * 2)) & 3)
                                   : ((bitmap[source >> 3] >> (7 - (source & 7))) & 1);
      paintNibble(target.buffer, destination, levels[value]);
      ++source;
      destination += stepX;
    }
  }
}

}  // namespace glyphBitmap
