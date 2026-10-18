#include <cstdio>
#include <cstring>
#include <cstdlib>
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/vreg.h"

#include "libraries/pico_graphics/pico_graphics.hpp"
#include "libraries/interstate75/interstate75.hpp"
#include "drivers/hub75/hub75_spwm.hpp"

#include "fm6373_profiles.hpp"

/*
Finds out how an S-PWM panel is wired, for panels that aren't the usual 1/32
scan with two halves, eg. a 128x64 1/64 scan panel with FM6373 drivers and a
shift register row driver (6258).

Two things can differ from panel to panel:
- The lanes: which pixels the R0, G0, B0 and R1, G1, B1 data lines drive (see
  Hub75SPWM::Lanes): only R0 (SINGLE), left/right (LEFT_RIGHT) or top/bottom
  (HALVES).
- The row driver: a binary decoder on A-E (DECODER) or a shift register
  (SHIFT_REGISTER), clocked once per scan line, with CLK, LAT and data on
  A, B and C by default.

The raw patterns show what the chips receive, whatever the layout: lane 0
(R0, G0, B0) in red, lane 1 (R1, G1, B1) in green.

Patterns:
 0. Orientation: red top-left, green top-right, blue bottom-left, white
    bottom-right corners, a white border and the row number every 8 rows.
    Correct with the right lanes, row driver and colour order.
 1. Raw lanes: every pixel of lane 0 red, of lane 1 green.
 2. Raw scan line: only scan line N lit. Shows which row(s) the row driver
    selects for it.
 3. Raw chain position: only position N of every scan line lit. Shows which
    column(s) it drives.
 4. Raw scan line colours: scan line L in colour L % 8 (red, green, blue,
    yellow, magenta, cyan, white, orange), lane 1 dimmer, and chain position 0
    of every line white.
 5. Gradients: one red, green, blue and white ramp per quarter.
 6. Scrolling text.
 7. Refresh test: the top half alternates between white and black with every
    refresh (update() with sync 1), so it looks evenly grey only if the panel
    shows every frame. The bottom half has red, green, blue and white ramps.

Button A short press: next pattern
Button A long press:  next preset (lanes and row driver), see PRESETS
Button B short press: patterns 2 and 3: next N, otherwise next profile
Button B long press:  previous register profile (FM6373, DMD_STM32's numbers)

Everything can also be set over USB serial, send "h" for help. The settings
are printed after every change.
*/

using namespace pimoroni;

const uint WIDTH = 128;
const uint HEIGHT = 64;

Hub75SPWM hub75(WIDTH, HEIGHT, Hub75SPWM::Chip::FM6373);

PicoGraphics_PenRGB888 graphics(WIDTH, HEIGHT, nullptr);

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

struct Preset {
  Hub75SPWM::Lanes lanes;
  Hub75SPWM::RowDriver rows;
};
const Preset PRESETS[] = {
  {Hub75SPWM::Lanes::SINGLE, Hub75SPWM::RowDriver::SHIFT_REGISTER},
  {Hub75SPWM::Lanes::LEFT_RIGHT, Hub75SPWM::RowDriver::SHIFT_REGISTER},
  {Hub75SPWM::Lanes::HALVES, Hub75SPWM::RowDriver::SHIFT_REGISTER},
  {Hub75SPWM::Lanes::SINGLE, Hub75SPWM::RowDriver::DECODER},
  {Hub75SPWM::Lanes::LEFT_RIGHT, Hub75SPWM::RowDriver::DECODER},
  {Hub75SPWM::Lanes::HALVES, Hub75SPWM::RowDriver::DECODER},
};

const uint PATTERN_COUNT = 8;
const uint PROFILE_COUNT = count_of(FM6373_PROFILES);

uint pattern = 0;
uint probe = 0;     // N of patterns 2 and 3
uint profile = 1;   // REG 6, without ghosting on a P2.5 128x64 1/64 panel
uint preset = 0;
uint8_t raw_level = 128;  // every LED at 255 draws a lot of current
bool raw_dirty = true;
uint32_t overlay_until = 0;

const char *lanes_name(Hub75SPWM::Lanes l) {
  switch(l) {
    case Hub75SPWM::Lanes::HALVES: return "HALVES";
    case Hub75SPWM::Lanes::SINGLE: return "SINGLE";
    case Hub75SPWM::Lanes::LEFT_RIGHT: return "LEFT_RIGHT";
  }
  return "?";
}

char pin_name(uint i) { return i < 5 ? 'A' + i : '-'; }

void print_status() {
  printf("\npattern %u", pattern);
  if(pattern == 2) printf(" (scan line %u)", probe);
  if(pattern == 3) printf(" (chain position %u)", probe);
  printf(", lanes %s: %u scan lines, chain %u\n", lanes_name(hub75.lanes), hub75.scan_lines, hub75.chain);
  if(hub75.row_driver == Hub75SPWM::RowDriver::SHIFT_REGISTER) {
    printf("rows SHIFT_REGISTER: CLK %c, LAT %c, data %c%s\n", pin_name(hub75.row_shift_clk),
      pin_name(hub75.row_shift_lat), pin_name(hub75.row_shift_data), hub75.row_shift_data_inverted ? " inverted" : "");
  } else {
    printf("rows DECODER (A-E)\n");
  }
  float sys_mhz = clock_get_hz(clk_sys) / 1e6f;
  float div = hub75.get_clock_div();
  printf("profile %s, gclk_bits %u, ", FM6373_PROFILES[profile].name, hub75.get_gclk_bits());
  if(hub75.gclk_d1) printf("GCLK %u/%u/%u", hub75.gclk_d1, hub75.gclk_d2, hub75.gclk_d3);
  else printf("GCLK automatic");
  printf(", first row repeated: %s", hub75.repeat_first_row ? "yes" : "no");
  printf(", brightness %u\n", hub75.brightness);
  printf("system clock %.0fMHz, clock div %.3f: upload %.2fMHz, DCLK %.2fMHz\n",
    sys_mhz, div, sys_mhz / (6 * (div < 1 ? 1 : div)), sys_mhz / (6 * div));
}

void print_help() {
  printf(
    "\nCommands (end with Enter):\n"
    "  h              this help\n"
    "  s              status\n"
    "  p N            pattern 0-%u\n"
    "  n N            N of patterns 2 (scan line) and 3 (chain position)\n"
    "  v N            level 0-255 of the raw patterns, default 128\n"
    "  P N            profile by index 0-%u, R N by DMD_STM32 number (REG N)\n"
    "  c              show the registers sent, * where they differ from the profile\n"
    "  w RR VV [r|g|b] set register RR to VV (hex), for one colour or all, eg. w 0f 11\n"
    "  x N RR ...     copy registers RR (hex) from profile REG N, eg. x 2 0f 1d,\n"
    "                 tail = the 11 words before the last one\n"
    "  l single|lr|halves     lanes\n"
    "  r shift|decoder        row driver\n"
    "  m CLK LAT DATA shift register pins, eg. m a b c, m a - c (no latch)\n"
    "  i              invert the shift register data\n"
    "  g N            gclk_bits 12-15 (position + 1 of the greyscale MSB)\n"
    "  L              repeat the 12 CLK first row pulse every loop (groups = reg 03 + 1)\n"
    "  t D1 D2 D3     CLKs per scan line with OE low, high, low, 0 0 0: automatic\n"
    "                 (28 4 96, shift register 38 4 86)\n"
    "  d N            clock divider: system clock / (6 * N), eg. d 2. Below 1, eg. d 2/3,\n"
    "                 only DCLK gets faster (down to 1/3), the upload stays at d 1\n"
    "  F N            system clock in MHz, 150-200 (overclocking above 150)\n"
    "  b N            brightness 0-255\n"
    "  f N            refresh rate in Hz\n",
    PATTERN_COUNT - 1, PROFILE_COUNT - 1);
}

// The registers sent to the panel: a copy of the selected profile, which the
// w and x commands change
uint16_t work[3][47];
uint work_count = 0;

void apply_work() {
  hub75.set_config_registers(work[0], work[1], work[2], work_count);
}

void apply_profile() {
  const FM6373Profile &p = FM6373_PROFILES[profile];
  memcpy(work, p.regs, sizeof(work));
  work_count = p.count;
  apply_work();
}

// Index of register `reg` in the working copy, or -1. Only for registers 0x00-0x22,
// which every 47 word profile has at the same index.
int work_index(uint reg) {
  if(reg > 0x22) return -1;
  for(auto i = 0u; i < work_count; i++) {
    if((work[0][i] >> 8) == reg) return i;
  }
  return -1;
}

void print_work() {
  const FM6373Profile &p = FM6373_PROFILES[profile];
  printf("registers, %s, * = changed (red green blue):\n", p.name);
  for(auto i = 0u; i < work_count; i++) {
    bool changed = false;
    for(auto c = 0u; c < 3; c++) changed |= work[c][i] != p.regs[c][i];
    printf("%2u: %04x %04x %04x%s\n", i, work[0][i], work[1][i], work[2][i], changed ? " *" : "");
  }
}

// The driver's GCLK timing for the row driver (DMD_STM32's)
void set_default_gclk(Hub75SPWM::RowDriver) {
  hub75.gclk_d1 = hub75.gclk_d2 = hub75.gclk_d3 = 0;
}

// Lanes and row driver are applied by start()
void restart(Hub75SPWM::Lanes lanes, Hub75SPWM::RowDriver rows) {
  hub75.stop();
  hub75.set_lanes(lanes);
  if(rows != hub75.row_driver) set_default_gclk(rows);
  hub75.row_driver = rows;
  apply_work();
  hub75.start();
  raw_dirty = true;
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

// Pattern 7: the top half white on odd frames, black on even ones, ramps below
void draw_refresh_test(bool white) {
  int w = graphics.bounds.w;
  int half = graphics.bounds.h / 2;
  int band = half / 4;
  if(white) {
    graphics.set_pen(255, 255, 255);
    graphics.rectangle(Rect(0, 0, w, half));
  }
  for(int x = 0; x < w; x++) {
    uint8_t v = x * 255 / (w - 1);
    graphics.set_pen(v, 0, 0);
    graphics.rectangle(Rect(x, half, 1, band));
    graphics.set_pen(0, v, 0);
    graphics.rectangle(Rect(x, half + band, 1, band));
    graphics.set_pen(0, 0, v);
    graphics.rectangle(Rect(x, half + band * 2, 1, band));
    graphics.set_pen(v, v, v);
    graphics.rectangle(Rect(x, half + band * 3, 1, band));
  }
}

void draw_scroller(int offset) {
  const std::string message = "Hello FM6373!";
  int width = graphics.measure_text(message, 2);
  int x = graphics.bounds.w - (offset % (graphics.bounds.w + width));
  graphics.set_pen(0, 255, 255);
  graphics.text(message, Point(x, graphics.bounds.h / 2 - 8), -1, 2);
}

// The raw patterns, written straight to the frame buffer
void draw_raw() {
  const uint8_t v = raw_level;
  const uint8_t d = raw_level / 4;
  static const uint8_t palette[8][3] = {
    {255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 255, 0},
    {255, 0, 255}, {0, 255, 255}, {255, 255, 255}, {255, 96, 0},
  };
  hub75.clear();
  for(auto line = 0u; line < hub75.scan_lines; line++) {
    for(auto pos = 0u; pos < hub75.chain; pos++) {
      switch(pattern) {
        case 1:
          hub75.set_chain_pixel(line, pos, 0, v, 0, 0);
          hub75.set_chain_pixel(line, pos, 1, 0, v, 0);
          break;
        case 2:
          if(line == probe % hub75.scan_lines) {
            hub75.set_chain_pixel(line, pos, 0, v, 0, 0);
            hub75.set_chain_pixel(line, pos, 1, 0, v, 0);
          }
          break;
        case 3:
          if(pos == probe % hub75.chain) {
            hub75.set_chain_pixel(line, pos, 0, v, 0, 0);
            hub75.set_chain_pixel(line, pos, 1, 0, v, 0);
          }
          break;
        case 4: {
          const uint8_t *c = palette[line % 8];
          if(pos == 0) {
            hub75.set_chain_pixel(line, pos, 0, v, v, v);
            hub75.set_chain_pixel(line, pos, 1, v, v, v);
          } else {
            hub75.set_chain_pixel(line, pos, 0, c[0] * v / 255, c[1] * v / 255, c[2] * v / 255);
            hub75.set_chain_pixel(line, pos, 1, c[0] * d / 255, c[1] * d / 255, c[2] * d / 255);
          }
          break;
        }
      }
    }
  }
}

bool is_raw(uint p) { return p >= 1 && p <= 4; }

uint pin_index(const char *s) {
  char c = s[0] | 0x20;
  return (c >= 'a' && c <= 'e') ? c - 'a' : 255;
}

// One line from USB serial
void command(char *line) {
  printf("> %s\n", line);  // terminals don't echo
  char *args = line + 1;
  while(*args == ' ') args++;
  long n = strtol(args, nullptr, 0);
  switch(line[0]) {
    case 'h': print_help(); return;
    case 'c': print_work(); return;
    case 'w': {
      unsigned reg, value;
      char colour[4] = {0};
      int got = sscanf(args, "%x %x %3s", &reg, &value, colour);
      int i = got >= 2 ? work_index(reg) : -1;
      if(i < 0 || value > 0xff) { printf("w RR VV [r|g|b], RR 00-22 in hex\n"); return; }
      for(auto c = 0u; c < 3; c++) {
        if(got < 3 || colour[0] == "rgb"[c]) work[c][i] = reg << 8 | value;
      }
      apply_work();
      print_work();
      break;
    }
    case 'x': {
      char *rest;
      long number = strtol(args, &rest, 10);
      const FM6373Profile *src = nullptr;
      for(auto &q : FM6373_PROFILES) if(q.number == (unsigned)number) src = &q;
      if(!src || src->count != work_count) { printf("x N RR ...: no profile REG %ld with %u words\n", number, work_count); return; }
      char *tok = strtok(rest, " ");
      if(!tok) { printf("x N RR ...: which registers?\n"); return; }
      for(; tok; tok = strtok(nullptr, " ")) {
        if(strcmp(tok, "tail") == 0) {
          for(auto i = work_count - 12; i < work_count - 1; i++)
            for(auto c = 0u; c < 3; c++) work[c][i] = src->regs[c][i];
          continue;
        }
        int i = work_index(strtoul(tok, nullptr, 16));
        if(i < 0) { printf("skipped %s: registers 00-22 or tail\n", tok); continue; }
        for(auto c = 0u; c < 3; c++) work[c][i] = src->regs[c][i];
      }
      apply_work();
      print_work();
      break;
    }
    case 's': break;
    case 'p': pattern = n % PATTERN_COUNT; probe = 0; break;
    case 'n': probe = n; break;
    case 'v': raw_level = n; break;
    case 'P': profile = n % PROFILE_COUNT; apply_profile(); break;
    case 'R': {
      bool found = false;
      for(auto i = 0u; i < PROFILE_COUNT; i++) {
        if(FM6373_PROFILES[i].number == (unsigned)n) { profile = i; found = true; }
      }
      if(!found) { printf("no profile REG %ld\n", n); return; }
      apply_profile();
      break;
    }
    case 'l': {
      Hub75SPWM::Lanes l;
      if(strncmp(args, "single", 6) == 0) l = Hub75SPWM::Lanes::SINGLE;
      else if(strncmp(args, "lr", 2) == 0) l = Hub75SPWM::Lanes::LEFT_RIGHT;
      else if(strncmp(args, "halves", 6) == 0) l = Hub75SPWM::Lanes::HALVES;
      else { printf("lanes: single, lr or halves\n"); return; }
      restart(l, hub75.row_driver);
      break;
    }
    case 'r':
      if(strncmp(args, "shift", 5) == 0) restart(hub75.lanes, Hub75SPWM::RowDriver::SHIFT_REGISTER);
      else if(strncmp(args, "decoder", 7) == 0) restart(hub75.lanes, Hub75SPWM::RowDriver::DECODER);
      else { printf("row driver: shift or decoder\n"); return; }
      break;
    case 'm': {
      char a[4] = {0}, b[4] = {0}, c[4] = {0};
      if(sscanf(args, "%3s %3s %3s", a, b, c) != 3) { printf("m CLK LAT DATA, eg. m a b c\n"); return; }
      uint clk = pin_index(a), lat = pin_index(b), data = pin_index(c);
      if(clk > 4 || data > 4 || clk == data || clk == lat || data == lat) { printf("pins must be different, A-E (LAT also -)\n"); return; }
      hub75.row_shift_clk = clk;
      hub75.row_shift_lat = lat;
      hub75.row_shift_data = data;
      restart(hub75.lanes, hub75.row_driver);
      break;
    }
    case 'i':
      hub75.row_shift_data_inverted = !hub75.row_shift_data_inverted;
      restart(hub75.lanes, hub75.row_driver);
      break;
    case 'g': hub75.set_gclk_bits(n); break;
    case 'L': hub75.repeat_first_row = !hub75.repeat_first_row; break;
    case 't': {
      unsigned d1, d2, d3;
      bool automatic = sscanf(args, "%u %u %u", &d1, &d2, &d3) == 3 && !d1 && !d2 && !d3;
      if(!automatic && (sscanf(args, "%u %u %u", &d1, &d2, &d3) != 3 || !d1 || !d2 || !d3 || d1 > 256 || d2 > 256 || d3 > 256)) {
        printf("t D1 D2 D3, 1-256 each, eg. t 28 4 96, or t 0 0 0 for automatic\n"); return;
      }
      hub75.gclk_d1 = d1; hub75.gclk_d2 = d2; hub75.gclk_d3 = d3;
      break;
    }
    case 'd': {
      float num = 0, den = 1;
      if(sscanf(args, "%f/%f", &num, &den) < 1 || den <= 0 || num / den < 1.0f / 3 - 0.001f || num / den > 16) {
        printf("d N, 1/3 to 16, eg. d 2 or d 2/3\n"); return;
      }
      hub75.set_clock_div(num / den);
      break;
    }
    case 'F': {
      if(n < 150 || n > 200) { printf("F N, 150-200 MHz\n"); return; }
      hub75.stop();
      // A little more core voltage for overclocking, well within the RP2350's range
      vreg_set_voltage(n > 150 ? VREG_VOLTAGE_1_15 : VREG_VOLTAGE_DEFAULT);
      sleep_ms(1);
      if(!set_sys_clock_khz(n * 1000, false)) printf("can't set %ld MHz\n", n);
      hub75.start();
      raw_dirty = true;
      break;
    }
    case 'b': hub75.brightness = n; break;
    case 'f': if(n >= 10 && n <= 200) hub75.set_refresh_rate(n); break;
    default: printf("unknown command, h for help\n"); return;
  }
  raw_dirty = true;
  overlay_until = to_ms_since_boot(get_absolute_time()) + 2000;
  print_status();
}

void poll_serial() {
  static char line[64];
  static uint len = 0;
  int c;
  while((c = getchar_timeout_us(0)) != PICO_ERROR_TIMEOUT) {
    if(c == '\r' || c == '\n') {
      line[len] = 0;
      if(len) command(line);
      len = 0;
    } else if(len < sizeof(line) - 1) {
      line[len++] = c;
    }
  }
}

int main() {
  stdio_init_all();

  graphics.set_font("bitmap8");

  hub75.row_driver = PRESETS[preset].rows;
  set_default_gclk(hub75.row_driver);
  hub75.set_lanes(PRESETS[preset].lanes);
  apply_profile();
  hub75.start();

  absolute_time_t next_frame = get_absolute_time();
  absolute_time_t next_print = get_absolute_time();
  int offset = 0;
  overlay_until = to_ms_since_boot(get_absolute_time()) + 2000;
  bool first = true;

  while(true) {
    uint32_t now = to_ms_since_boot(get_absolute_time());
    bool changed = false;

    poll_serial();

    switch(button_a.poll()) {
      case PressDetector::SHORT:
        pattern = (pattern + 1) % PATTERN_COUNT;
        probe = 0;
        changed = true;
        break;
      case PressDetector::LONG:
        preset = (preset + 1) % count_of(PRESETS);
        restart(PRESETS[preset].lanes, PRESETS[preset].rows);
        changed = true;
        break;
      default:
        break;
    }

    switch(button_b.poll()) {
      case PressDetector::SHORT:
        if(pattern == 2 || pattern == 3) {
          probe = (probe + 1) % (pattern == 2 ? hub75.scan_lines : hub75.chain);
        } else {
          profile = (profile + 1) % PROFILE_COUNT;
          apply_profile();
        }
        changed = true;
        break;
      case PressDetector::LONG:
        profile = (profile + PROFILE_COUNT - 1) % PROFILE_COUNT;
        apply_profile();
        changed = true;
        break;
      default:
        break;
    }

    if(changed) {
      raw_dirty = true;
      overlay_until = now + 2000;
      print_status();
    }

    // USB serial needs a moment after boot
    if(first && now > 3000) {
      first = false;
      printf("\nS-PWM probe for a %ux%u panel, send h for help\n", WIDTH, HEIGHT);
      print_status();
    }

    if(is_raw(pattern)) {
      // Written once, the driver keeps refreshing the panel
      if(raw_dirty) {
        draw_raw();
        raw_dirty = false;
      }
      sleep_ms(10);
    } else {
      graphics.set_pen(0, 0, 0);
      graphics.clear();
      switch(pattern) {
        case 0: draw_orientation(); break;
        case 5: draw_gradients(); break;
        case 6: draw_scroller(offset++); break;
        case 7: draw_refresh_test(offset++ & 1); break;
      }

      if(now < overlay_until) {
        char label[32];
        snprintf(label, sizeof(label), "P%u REG%u G%u", preset, FM6373_PROFILES[profile].number, hub75.get_gclk_bits());
        graphics.set_pen(0, 0, 0);
        graphics.rectangle(Rect(0, 0, graphics.measure_text(label, 1) + 2, 9));
        graphics.set_pen(255, 255, 255);
        graphics.text(label, Point(1, 1), -1, 1);
      }

      if(pattern == 7) {
        // A new image for every refresh
        hub75.update(&graphics, 1);
      } else {
        next_frame = delayed_by_us(next_frame, 1000000 / 30);
        if(absolute_time_diff_us(get_absolute_time(), next_frame) < 0) {
          next_frame = get_absolute_time();
        }
        sleep_until(next_frame);
        hub75.update(&graphics);
      }
    }

    if(absolute_time_diff_us(next_print, get_absolute_time()) >= 0) {
      next_print = delayed_by_ms(next_print, 5000);
      printf("upload %luus, update convert %luus, frame start %luus, torn %lu\n",
        hub75.upload_us, hub75.update_convert_us, hub75.frame_start_us, hub75.torn_frames);
    }
  }
}
