#include <cstdio>
#include "pico/stdlib.h"

#include "libraries/pico_graphics/pico_graphics.hpp"
#include "libraries/interstate75/interstate75.hpp"
#include "drivers/hub75/hub75_spwm.hpp"

/*
Test pattern for HUB75 panels with S-PWM driver chips (SM16380SH, ICND2055, FM6373).

Button A: next pattern
Button B: cycle brightness

Patterns:
 0. Orientation: red top-left, green top-right, blue bottom-left, white bottom-right
    corners, a white border and the row number every 8 rows.
    Checks column order, top/bottom halves, row addressing and colour order.
 1. Gradients: one red, green, blue and white ramp per quarter.
    Checks greyscale depth and dark-colour flicker (try lowering colour_depth).
 2. Scrolling text: checks the panel copes with frequent frame updates.
*/

using namespace pimoroni;

Button button_a(Interstate75::A);
Button button_b(Interstate75::B);

// A 128x64 P2.5 panel with SM16380SH drivers, 1/32 scan
Hub75SPWM hub75(128, 64, Hub75SPWM::Chip::SM16380SH);

PicoGraphics_PenRGB888 graphics(hub75.width, hub75.height, nullptr);

void __isr pio_irq() {
  hub75.pio_irq();
}

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

int main() {
  stdio_init_all();

  graphics.set_font("bitmap8");

  hub75.start(pio_irq);

  const uint8_t brightness_levels[] = {255, 128, 64, 16};
  uint brightness = 0;
  uint pattern = 0;
  int offset = 0;

  while(true) {
    if(button_a.read()) {
      pattern = (pattern + 1) % 3;
      printf("Pattern %u\n", pattern);
    }
    if(button_b.read()) {
      brightness = (brightness + 1) % count_of(brightness_levels);
      hub75.brightness = brightness_levels[brightness];
      printf("Brightness %u\n", hub75.brightness);
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

    hub75.update(&graphics);

    sleep_ms(pattern == 2 ? 20 : 100);
  }
}
