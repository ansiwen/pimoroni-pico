#include <cstdio>
#include "pico/stdlib.h"

#include "libraries/pico_graphics/pico_graphics.hpp"
#include "libraries/interstate75/interstate75.hpp"
#include "drivers/hub75/hub75_spwm.hpp"

#include "sm16380sh_profiles.hpp"

/*
Test pattern and tuning tool for HUB75 panels with S-PWM driver chips.

Button A short press: next pattern
Button A long press:  toggle colour depth (10 / 4 bits)
Button B short press: next register profile (SM16380SH 1/32 scan)
Button B long press:  next brightness

The current settings are shown in the top-left corner for two seconds after
each change, and printed over USB serial.

Patterns:
 0. Orientation: red top-left, green top-right, blue bottom-left, white bottom-right
    corners, a white border and the row number every 8 rows.
    Checks column order, top/bottom halves, row addressing and colour order.
 1. Gradients: one red, green, blue and white ramp per quarter.
    Checks greyscale depth and dark-colour flicker.
 2. Scrolling text: checks the panel copes with frequent frame updates.
*/

using namespace pimoroni;

// A 128x64 P2.5 panel with SM16380SH drivers, 1/32 scan
Hub75SPWM hub75(128, 64, Hub75SPWM::Chip::SM16380SH);

PicoGraphics_PenRGB888 graphics(hub75.width, hub75.height, nullptr);

void __isr pio_irq() {
  hub75.pio_irq();
}

// Distinguishes short and long presses, reported on release / after the hold time
class PressDetector {
  public:
  enum Event { NONE, SHORT, LONG };

  PressDetector(uint pin) : button(pin) {}

  Event poll() {
    bool down = button.raw();
    uint32_t now = to_ms_since_boot(get_absolute_time());
    Event event = NONE;
    if(down && !was_down) {
      down_time = now;
      long_sent = false;
    } else if(down && !long_sent && now - down_time >= 700) {
      long_sent = true;
      event = LONG;
    } else if(!down && was_down && !long_sent && now - down_time >= 30) {
      event = SHORT;
    }
    was_down = down;
    return event;
  }

  private:
  Button button;
  bool was_down = false;
  bool long_sent = false;
  uint32_t down_time = 0;
};

PressDetector button_a(Interstate75::A);
PressDetector button_b(Interstate75::B);

const uint8_t BRIGHTNESS_LEVELS[] = {255, 128, 64, 16};
const uint PROFILE_COUNT = count_of(SM16380SH_PROFILES) + 1; // + the driver's default

void draw_orientation() {
  int w = graphics.bounds.w;
  int h = graphics.bounds.h;

  graphics.set_pen(255, 255, 255);
  graphics.rectangle(Rect(0, 0, w, 1));
  graphics.rectangle(Rect(0, h - 1, w, 1));
  graphics.rectangle(Rect(0, 0, 1, h));
  graphics.rectangle(Rect(w - 1, 0, 1, h));

  graphics.set_pen(255, 0, 0);
  graphics.rectangle(Rect(0, 0, 8, 8));
  graphics.set_pen(0, 255, 0);
  graphics.rectangle(Rect(w - 8, 0, 8, 8));
  graphics.set_pen(0, 0, 255);
  graphics.rectangle(Rect(0, h - 8, 8, 8));
  graphics.set_pen(255, 255, 255);
  graphics.rectangle(Rect(w - 8, h - 8, 8, 8));

  graphics.set_pen(255, 255, 0);
  for(int y = 8; y < h - 8; y += 8) {
    graphics.text(std::to_string(y), Point(12, y), -1, 1);
  }
}

void draw_gradients() {
  int w = graphics.bounds.w;
  int band = graphics.bounds.h / 4;
  for(int x = 0; x < w; x++) {
    uint8_t v = x * 255 / (w - 1);
    graphics.set_pen(v, 0, 0);
    graphics.rectangle(Rect(x, 0, 1, band));
    graphics.set_pen(0, v, 0);
    graphics.rectangle(Rect(x, band, 1, band));
    graphics.set_pen(0, 0, v);
    graphics.rectangle(Rect(x, band * 2, 1, band));
    graphics.set_pen(v, v, v);
    graphics.rectangle(Rect(x, band * 3, 1, band));
  }
}

void draw_scroller(int offset) {
  const std::string message = "Hello S-PWM!";
  int width = graphics.measure_text(message, 2);
  int x = graphics.bounds.w - (offset % (graphics.bounds.w + width));
  graphics.set_pen(0, 255, 255);
  graphics.text(message, Point(x, graphics.bounds.h / 2 - 8), -1, 2);
}

// Profile 0 is the driver's built-in default, 1.. are SM16380SH_PROFILES
void apply_profile(uint profile) {
  if(profile == 0) {
    hub75.load_chip_defaults();
  } else {
    const SPWMProfile &p = SM16380SH_PROFILES[profile - 1];
    hub75.set_config_registers(p.regs[0], p.regs[1], p.regs[2], count_of(p.regs[0]));
  }
}

const char *profile_name(uint profile) {
  return profile == 0 ? "REG 0: driver default" : SM16380SH_PROFILES[profile - 1].name;
}

int main() {
  stdio_init_all();

  graphics.set_font("bitmap8");

  hub75.start(pio_irq);

  uint pattern = 0;
  uint profile = 0;
  uint brightness = 0;
  int offset = 0;
  uint32_t overlay_until = 0;

  while(true) {
    uint32_t now = to_ms_since_boot(get_absolute_time());
    bool changed = false;

    switch(button_a.poll()) {
      case PressDetector::SHORT:
        pattern = (pattern + 1) % 3;
        changed = true;
        break;
      case PressDetector::LONG:
        hub75.colour_depth = hub75.colour_depth == 10 ? 4 : 10;
        changed = true;
        break;
      default:
        break;
    }

    switch(button_b.poll()) {
      case PressDetector::SHORT:
        profile = (profile + 1) % PROFILE_COUNT;
        apply_profile(profile);
        changed = true;
        break;
      case PressDetector::LONG:
        brightness = (brightness + 1) % count_of(BRIGHTNESS_LEVELS);
        hub75.brightness = BRIGHTNESS_LEVELS[brightness];
        changed = true;
        break;
      default:
        break;
    }

    if(changed) {
      overlay_until = now + 2000;
      printf("pattern %u, %s, colour depth %u, brightness %u\n",
        pattern, profile_name(profile), hub75.colour_depth, hub75.brightness);
    }

    graphics.set_pen(0, 0, 0);
    graphics.clear();

    switch(pattern) {
      case 0:
        draw_orientation();
        break;
      case 1:
        draw_gradients();
        break;
      case 2:
        draw_scroller(offset++);
        break;
    }

    if(now < overlay_until) {
      char label[32];
      snprintf(label, sizeof(label), "R%u D%u B%u", profile, hub75.colour_depth, hub75.brightness);
      graphics.set_pen(0, 0, 0);
      graphics.rectangle(Rect(0, 0, graphics.measure_text(label, 1) + 2, 9));
      graphics.set_pen(255, 255, 255);
      graphics.text(label, Point(1, 1), -1, 1);
    }

    // Only uploads to the panel if the image or settings changed
    hub75.update(&graphics);

    sleep_ms(pattern == 2 ? 20 : 10);
  }
}
