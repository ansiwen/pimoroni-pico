#pragma once

#include <stdint.h>
#include "pico/stdlib.h"

#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "libraries/pico_graphics/pico_graphics.hpp"

#include "hub75.hpp"

#ifndef NO_QSTR
#include "hub75_spwm.pio.h"
#endif

/*
Driver for HUB75 panels built with S-PWM ("PWM type") LED driver chips.

Unlike classic HUB75 panels, which are made of plain shift registers that we
must refresh continuously with binary coded modulation, these chips hold the
greyscale data for the whole panel in internal SRAM and generate the PWM
themselves. We only need to upload a new frame when the image changes, while
continuously feeding them a reference clock (DCLK on the CLK line) and a
row-switch clock (GCLK on the OE line).

Based on the DMD_STM32 library by Dmitry Dmitriev (board707):
https://github.com/board707/DMD_STM32 (DMD_SPWM_Driver_RP.h)

Supported chips (all use the "decoupled" DCLK/GCLK scheme):
 - SM16380SH
 - ICND2055
 - FM6373

SPDX-License-Identifier: GPL-3.0-or-later
*/

namespace pimoroni {

class Hub75SPWM {
    public:
    enum class Chip {
        SM16380SH,
        ICND2055,
        FM6373,
    };

    uint width;
    uint height;
    uint scan_rows;     // number of multiplexed rows, height / 2
    Chip chip;

    // 0 (off) - 255 (full brightness)
    uint8_t brightness = 255;

    // Number of significant bits (1 - 10) of the gamma corrected colour
    // sent to the panel. DMD_STM32 only sends 4 bits, reduce this if you
    // see flicker or noise in dark colours.
    uint colour_depth = 10;

    // Experimental: send VSYNC after the frame data instead of before it.
    // In DMD_STM32's order (false) each frame is only shown at the next upload.
    bool vsync_after_data = false;

    // Interstate 75 (W) pinout
    uint pin_r0 = 0;      // R0, G0, B0, R1, G1, B1 must be consecutive
    uint pin_row_a = 6;   // A, B, C, D, E must be consecutive
    uint pin_clk = 11;
    uint pin_lat = 12;    // LAT and OE must be consecutive
    uint pin_oe = 13;


    // width is the total length of the chain and must be a multiple of 16.
    // `buffer` must hold buffer_size() words, or nullptr to allocate it.
    Hub75SPWM(uint width, uint height, Chip chip = Chip::SM16380SH, uint32_t *buffer = nullptr);
    ~Hub75SPWM();

    // Replace the chip configuration registers, eg. with a profile from
    // DMD_STM32's register_test/profiles that matches your panel.
    // Register 0x02 (scan lines) is always set to match the panel height.
    // If the clocks are running, all registers are sent immediately.
    void set_config_registers(const uint16_t *regs, uint count);
    // As above, with separate values for the red, green and blue drivers
    void set_config_registers(const uint16_t *red, const uint16_t *green, const uint16_t *blue, uint count);
    // Restore the built-in configuration registers for the chip
    void load_chip_defaults();

    // Size of the frame buffer in 32-bit words: width * height, the same RAM as Hub75
    uint buffer_size() const { return width * height; }

    // Brightness and colour depth are applied when a pixel is set
    void set_pixel(uint x, uint y, uint8_t r, uint8_t g, uint8_t b);
    void clear();

    // Start/stop the clocks
    void start();
    void stop();

    // Upload the frame buffer to the panel. Blocks for the length of the transfer.
    void refresh();

    // Convert a PicoGraphics frame buffer into the frame buffer and upload it to the panel.
    // Skips the upload if the converted image didn't change (including brightness and
    // colour_depth), since every upload briefly disturbs the panel's PWM, unless `force` is set.
    void update(PicoGraphics *graphics, bool force = false);

    private:
    static const uint MAX_CONFIG_REGISTERS = 40;
    static const uint CHANNELS_PER_CHIP = 16;

    // Greyscale data as it is clocked out, see hub75_spwm_pack.hpp
    uint32_t *frame_buffer = nullptr;
    bool managed_buffer = false;

    // Bit position + 1 of the greyscale MSB in the chip's 16-bit word
    uint gclk_bits = 14;

    uint16_t config_registers[3][MAX_CONFIG_REGISTERS]; // red, green, blue
    uint config_register_count = 0;
    uint next_config_register = 0;

    // State machines
    PIO pio = nullptr;      // data, clk_lat
    PIO pio_gclk = nullptr; // clk_cnt, rows
    uint sm_frame = 0;
    uint sm_regs = 0;
    uint sm_clk_lat = 0;
    uint sm_clk_cnt = 0;
    uint sm_rows = 0;
    uint frame_prog_offs = 0;
    uint regs_prog_offs = 0;
    uint clk_lat_prog_offs = 0;
    uint clk_cnt_prog_offs = 0;
    uint rows_prog_offs = 0;
    pio_sm_config frame_config;
    pio_sm_config regs_config;
    pio_sm_config clk_lat_config;
    pio_sm_config clk_cnt_config;
    pio_sm_config rows_config;

    int dma_channel = -1;

    bool running = false;

    // Set when the frame buffer differs from the panel
    bool dirty = true;
    bool gclk_running = false;

    void send_all_config_registers();
    uint32_t greyscale(uint8_t v);

    uint current_row();

    void send_clocks(uint count, bool lat = false);
    void send_latches(uint count) { send_clocks(count, true); }
    void send_oe(uint count);
    void send_vsync();
    void send_register(uint16_t value, uint latches) { send_register(value, value, value, latches); }
    void send_register(uint16_t red, uint16_t green, uint16_t blue, uint latches);
    void start_dma(uint sm, const void *src, uint count, uint ring_bits);
    void wait_dma(uint sm);

    void init_mux();
    void start_gclk();
    void stop_gclk();
    void start_dclk();
    void load_config_register();
    void frame_transfer();
};

}
