#include <cstdio>
#include "pico/stdlib.h"
#include "hardware/clocks.h"

#include "libraries/pico_graphics/pico_graphics.hpp"
#include "libraries/interstate75/interstate75.hpp"
#include "drivers/hub75/hub75_spwm.hpp"

#include "sm16380sh_profiles.hpp"

/*
Test pattern and tuning tool for HUB75 panels with S-PWM driver chips.

Button A short press: next pattern
Button A long press:  next frame upload clock speed (experiment, D = PIO clock
                      divider, see UPLOAD_CLOCK_DIVS)
Button B short press: next register profile (SM16380SH 1/32 scan)
Button B long press:  toggle interrupt stress test (S1): a timer interrupt
                      blocks the CPU for 50us every 5ms

The current settings are shown in the top-left corner for two seconds after
each change, and printed over USB serial. Every second, the time spent in
update() and in the driver's background frame start are printed too.

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

// Interrupt load for the stress test, much longer than a scan line (~10us)
bool stress_callback(repeating_timer_t *) {
  busy_wait_us(50);
  return true;
}

// The driver's first SM16380SH default, from DMD_STM32's DMD_SPWM_Driver_RP.h
const uint16_t RP2040_DEFAULT[] = {
  0x021f, 0x0300, 0x0400, 0x0500, 0x0600, 0x0750, 0x0800, 0x0900, 0x0a02, 0x0b0c,
  0x0c08, 0x0d00, 0x0e05, 0x0f00, 0x1000, 0x1100, 0x1200, 0x1300, 0x1414, 0x1500,
  0x1630, 0x1700, 0x1801, 0x1904, 0x1a03, 0x1b14, 0x1c12, 0x1d00, 0x1e00, 0x1f0c
};

const uint PROFILE_COUNT = count_of(SM16380SH_PROFILES) + 1;

// Frame upload clock dividers to try. A data clock takes 6 PIO cycles, so at
// 150MHz: 3 = 8.3MHz, 2.5 = 10MHz, 2 = 12.5MHz, 1.5 = 16.7MHz,
// 1.25 = 20MHz, 1 = 25MHz (default). Fractional dividers jitter by one system clock.
const float UPLOAD_CLOCK_DIVS[] = {3.0f, 2.5f, 2.0f, 1.5f, 1.25f, 1.0f};

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

// Profile 0 is the DMD_STM32 RP2040 default, 1.. are SM16380SH_PROFILES.
// Profile 2 is the same as the driver's built-in default.
void apply_profile(uint profile) {
  if(profile == 0) {
    hub75.set_config_registers(RP2040_DEFAULT, count_of(RP2040_DEFAULT));
  } else {
    const SPWMProfile &p = SM16380SH_PROFILES[profile - 1];
    hub75.set_config_registers(p.regs[0], p.regs[1], p.regs[2], count_of(p.regs[0]));
  }
}

const char *profile_name(uint profile) {
  return profile == 0 ? "DMD_STM32 RP2040 default" : SM16380SH_PROFILES[profile - 1].name;
}

int main() {
  stdio_init_all();

  graphics.set_font("bitmap8");

  hub75.start();

  // Start stepping the upload clock from the driver's default
  uint clock_step = 0;
  for(auto i = 0u; i < count_of(UPLOAD_CLOCK_DIVS); i++) {
    if(UPLOAD_CLOCK_DIVS[i] == hub75.get_upload_clock_div()) clock_step = i;
  }

  uint pattern = 0;
  uint profile = 2;
  uint frame = 0;
  absolute_time_t next_frame = get_absolute_time();
  repeating_timer_t stress_timer;
  bool stress = false;
  int offset = 0;
  uint32_t overlay_until = to_ms_since_boot(get_absolute_time()) + 2000;

  while(true) {
    uint32_t now = to_ms_since_boot(get_absolute_time());
    bool changed = false;

    switch(button_a.poll()) {
      case PressDetector::SHORT:
        pattern = (pattern + 1) % 3;
        changed = true;
        break;
      case PressDetector::LONG:
        clock_step = (clock_step + 1) % count_of(UPLOAD_CLOCK_DIVS);
        hub75.set_upload_clock_div(UPLOAD_CLOCK_DIVS[clock_step]);
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
        stress = !stress;
        if(stress) {
          add_repeating_timer_us(5000, stress_callback, nullptr, &stress_timer);
        } else {
          cancel_repeating_timer(&stress_timer);
        }
        changed = true;
        break;
      default:
        break;
    }

    if(changed) {
      overlay_until = now + 2000;
      float div = hub75.get_upload_clock_div();
      printf("pattern %u, R%u (%s), upload clock div %.2f (%.1fMHz), stress %s\n",
        pattern, profile, profile_name(profile), div, clock_get_hz(clk_sys) / 6e6f / div, stress ? "on" : "off");
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
      snprintf(label, sizeof(label), "R%u D%.2f S%u", profile, hub75.get_upload_clock_div(), stress ? 1 : 0);
      graphics.set_pen(0, 0, 0);
      graphics.rectangle(Rect(0, 0, graphics.measure_text(label, 1) + 2, 9));
      graphics.set_pen(255, 255, 255);
      graphics.text(label, Point(1, 1), -1, 1);
    }

    // The driver refreshes the panel in the background, update() only converts the image
    absolute_time_t start = get_absolute_time();
    hub75.update(&graphics);
    int64_t update_us = absolute_time_diff_us(start, get_absolute_time());
    if(++frame % 60 == 0) {
      printf("upload %luus, update %lldus (waiting %luus, converting %luus), frame start %luus, torn %lu\n", // once per second
        hub75.upload_us, update_us, hub75.update_wait_us, hub75.update_convert_us, hub75.frame_start_us, hub75.torn_frames);
    }

    // Draw at 60Hz, the same rate as the refresh
    next_frame = delayed_by_us(next_frame, 1000000 / 60);
    if(absolute_time_diff_us(get_absolute_time(), next_frame) < 0) {
      next_frame = get_absolute_time(); // Too slow for 60Hz, don't try to catch up
    }
    sleep_until(next_frame);
  }
}
