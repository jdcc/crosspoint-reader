#pragma once

#include <cstdint>
#include <cstring>

namespace ReaderUtils {

// Gray mask for a panel whose text waveform shows one gray level.
//
// Fonts carry two edge values, light and dark gray. Sending both to the single
// gray thickens strokes that have light edges; sending only the dark ones thins
// them. Instead each edge pixel is rounded by which side of the stroke it is on:
// with full ink to its left or above it rounds darker (light -> gray, dark ->
// black), otherwise lighter (light -> white, dark -> gray). Every stroke then
// gains on one side what it gives up on the other and keeps its weight.
//
// All buffers are full panel planes of `rows` x `widthBytes`, MSB-first.
//   base: the B/W page with dark-gray and black values as ink (0), 1 = white.
//   dark: 1 where the glyph value is dark gray. Receives the result.
//   gray: 1 where the glyph value is light or dark gray.
// widthBytes must not exceed MAX_WIDTH_BYTES.
constexpr int SINGLE_GRAY_MAX_WIDTH_BYTES = 128;

inline void singleGrayEdgeMask(const uint8_t* base, uint8_t* dark, const uint8_t* gray, const int widthBytes,
                               const int rows) {
  uint8_t inkAbove[SINGLE_GRAY_MAX_WIDTH_BYTES] = {};
  uint8_t ink[SINGLE_GRAY_MAX_WIDTH_BYTES];
  for (int y = 0; y < rows; ++y) {
    const uint8_t* b = base + static_cast<size_t>(y) * widthBytes;
    uint8_t* d = dark + static_cast<size_t>(y) * widthBytes;
    const uint8_t* g = gray + static_cast<size_t>(y) * widthBytes;
    for (int i = 0; i < widthBytes; ++i) ink[i] = static_cast<uint8_t>(~b[i] & ~d[i]);
    for (int i = 0; i < widthBytes; ++i) {
      const uint8_t inkLeft = static_cast<uint8_t>((ink[i] >> 1) | (i > 0 ? ink[i - 1] << 7 : 0));
      const uint8_t darker = inkLeft | inkAbove[i];
      const uint8_t light = static_cast<uint8_t>(g[i] & ~d[i]);
      d[i] = static_cast<uint8_t>((light & darker) | (d[i] & ~darker));
    }
    memcpy(inkAbove, ink, widthBytes);
  }
}

}  // namespace ReaderUtils
