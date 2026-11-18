#pragma once

#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <esp_heap_caps.h>

#include <cstdint>
#include <cstring>
#include <memory>

namespace ReaderUtils {

// Bench diagnostic for the X4 Pro one-pass text waveform: shows what each of
// its four drive patterns does to a pixel that starts white, gray, or black.
//
// Step 1 paints three columns (portrait, left to right): white, gray, black.
// Step 2, a few seconds later, crosses them with four rows (top to bottom):
// black, gray, hold, and erase-to-white patterns. Thin lines between the cells
// get the hold pattern, so they keep showing the step 1 tone for reference.
inline void showDrivePatternPatches(GfxRenderer& renderer) {
#if defined(BOARD_HAS_PSRAM)
  constexpr int W = 800, H = 480, WB = W / 8;
  constexpr size_t PLANE = static_cast<size_t>(WB) * H;
  std::unique_ptr<uint8_t, void (*)(void*)> lo(
      static_cast<uint8_t*>(heap_caps_malloc(PLANE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)), free);
  std::unique_ptr<uint8_t, void (*)(void*)> hi(
      static_cast<uint8_t*>(heap_caps_malloc(PLANE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)), free);
  if (!lo || !hi || renderer.getDisplayWidthBytes() != WB || renderer.getDisplayHeight() != H) return;

  // Drive codes as (hi, lo): black 00, gray 01, hold 10, erase to white 11.
  enum Code : uint8_t { BLACK = 0, GRAY = 1, HOLD = 2, ERASE = 3 };
  const auto fill = [&](const auto& codeAt) {
    for (int row = 0; row < H; ++row) {
      for (int byte = 0; byte < WB; ++byte) {
        const uint8_t code = codeAt(row, byte * 8);
        lo.get()[row * WB + byte] = (code & 1) ? 0xFF : 0x00;
        hi.get()[row * WB + byte] = (code & 2) ? 0xFF : 0x00;
      }
    }
  };
  const auto show = [&] {
    renderer.clearScreen(0xFF);
    renderer.displayGrayscaleBase(HalDisplay::FAST_REFRESH);
    renderer.writeGrayscalePlaneStrip(true, lo.get(), 0, H);
    renderer.writeGrayscalePlaneStrip(false, hi.get(), 0, H);
    renderer.displayGrayBuffer();
    renderer.cleanupGrayscaleWithFrameBuffer();
  };
  // Panel rows run right to left in portrait: 0..159 is the right-hand column.
  const auto startCode = [](const int row) { return row < 160 ? BLACK : row < 320 ? GRAY : HOLD; };

  display.setReaderRefresh(true);
  renderer.clearScreen(0xFF);
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);  // a clean white start
  display.setTextAaMode(2);
  fill([&](const int row, int) { return static_cast<uint8_t>(startCode(row)); });
  show();
  delay(4000);
  fill([](const int row, const int x) {
    const bool gap = row % 160 >= 152 || x % 200 >= 192;
    return static_cast<uint8_t>(gap ? HOLD : x / 200);  // top to bottom: black, gray, hold, erase
  });
  show();
  display.setTextAaMode(0);
#else
  (void)renderer;
#endif
}

}  // namespace ReaderUtils
