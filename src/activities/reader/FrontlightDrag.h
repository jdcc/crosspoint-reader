#pragma once

#include <GfxRenderer.h>
#include <HalFrontlight.h>

#include <cstdlib>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "util/SlideSwitch.h"

// Frontlight control by dragging a finger up or down a page margin: the left
// ninth sets brightness, the right ninth sets warmth (boards with colour
// temperature only). Up is brighter or warmer, and 60% of the screen height
// covers the whole range. Dragging brightness below 1% turns the light off;
// dragging back up turns it on again. Warmth only moves while the light is on.
// A touch that stays put, or first moves sideways, is left alone, so taps,
// long presses and page-turn swipes in the margins keep their meaning. The top
// and bottom tenths stay with the edge gestures.
class FrontlightDrag {
 public:
  // Call once per reader loop pass. True while the contact is a frontlight drag.
  bool update(const GfxRenderer& renderer, const MappedInputManager& input) {
    if (!SETTINGS.touchReaderControls || !input.hasTouch() || !Frontlight.present()) return false;
    const int height = renderer.getScreenHeight();
    int x = 0;
    int y = 0;
    if (input.wasScreenTouchPressed(x, y)) startContact(renderer, x, y);
    if (side == Side::None) return false;
    if (!input.isScreenTouchHeld(x, y)) {
      const bool wasDragging = dragging;
      side = Side::None;
      dragging = false;
      if (!wasDragging) return false;
      // The on preference may differ from the live light (wake restore), so
      // only a drag that switched the light records it.
      const bool switched = Frontlight.isOn() != startOn;
      if (SETTINGS.frontlightBrightness != Frontlight.brightness() ||
          SETTINGS.frontlightWarmth != Frontlight.warmth() || switched) {
        SETTINGS.frontlightBrightness = Frontlight.brightness();
        SETTINGS.frontlightWarmth = Frontlight.warmth();
        if (switched) SETTINGS.frontlightOn = Frontlight.isOn() ? 1 : 0;
        SETTINGS.saveToFile();
      }
      return true;
    }
    const int travel = startY - y;
    if (!dragging) {
      const int sideways = std::abs(x - startX);
      const int vertical = std::abs(travel);
      if (sideways < height / 40 && vertical < height / 40) return false;
      // Mostly sideways: a page-turn swipe, so give up the whole contact.
      if (sideways * 2 > vertical * 3) {
        side = Side::None;
        return false;
      }
      dragging = true;
    }

    const int next = startValue + travel * 100 / (height * 6 / 10);
    if (side == Side::Warmth) {
      const uint8_t warmth = static_cast<uint8_t>(next < 0 ? 0 : next > 100 ? 100 : next);
      if (warmth != Frontlight.warmth()) Frontlight.setWarmth(warmth);
    } else {
      dragBrightness(next);
    }
    return true;
  }

 private:
  enum class Side : uint8_t { None, Brightness, Warmth };
  static constexpr int MIN_BRIGHTNESS = 1;

  void startContact(const GfxRenderer& renderer, const int x, const int y) {
    const int width = renderer.getScreenWidth();
    const int height = renderer.getScreenHeight();
    side = Side::None;
    dragging = false;
    startX = x;
    startY = y;
    startOn = Frontlight.isOn();
    if (y < height / 10 || y >= height - height / 10) return;
    if (x < width / 9) {
      side = Side::Brightness;
      // A switch-owned light keeps its level while off, as in the drawer.
      startValue = Frontlight.isOn() || slideSwitchOwnsLight() ? Frontlight.brightness() : 0;
    } else if (x >= width - width / 9 && Frontlight.hasColorTemperature() && Frontlight.isOn()) {
      side = Side::Warmth;
      startValue = Frontlight.warmth();
    }
  }

  static void dragBrightness(int next) {
    // The slide switch owns on/off when bound to the light: only the level moves.
    const bool switchOwned = slideSwitchOwnsLight();
    const bool on = switchOwned ? Frontlight.isOn() : next >= MIN_BRIGHTNESS;
    // Off keeps the last lit level for the next switch-on.
    if (on || switchOwned) {
      if (next < MIN_BRIGHTNESS) next = MIN_BRIGHTNESS;
      if (next > 100) next = 100;
      if (next != Frontlight.brightness()) Frontlight.setBrightness(static_cast<uint8_t>(next));
    }
    if (on != Frontlight.isOn()) Frontlight.setOn(on);
  }

  Side side = Side::None;
  bool dragging = false;
  bool startOn = false;
  int startX = 0;
  int startY = 0;
  int startValue = 0;
};
