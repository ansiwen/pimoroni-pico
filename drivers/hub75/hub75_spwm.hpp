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

    PIO pio = pio0;

    // width is the total length of the chain and must be a multiple of 16
    Hub75SPWM(uint width, uint height, Chip chip = Chip::SM16380SH);
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

    void set_pixel(uint x, uint y, uint8_t r, uint8_t g, uint8_t b);
    void clear();

    // Start/stop the clocks. `handler` must be an __isr function that calls pio_irq()
    void start(irq_handler_t handler);
    void stop(irq_handler_t handler);
    void pio_irq();

    // Upload the back buffer to the panel. Blocks for the length of the transfer.
    void refresh();

    // Copy a PicoGraphics frame buffer to the back buffer and upload it to the panel.
    // Skips the upload if neither the image, brightness nor colour_depth changed,
    // since every upload briefly disturbs the panel's PWM, unless `force` is set.
    void update(PicoGraphics *graphics, bool force = false);

    private:
    static const uint MAX_CONFIG_REGISTERS = 40;
    static const uint CHANNELS_PER_CHIP = 16;

    // Pairs of pixels (top half, bottom half) as 10-bit gamma corrected colour
    Pixel *back_buffer = nullptr;

    // Bit position + 1 of the greyscale MSB in the chip's 16-bit word
    uint gclk_bits = 14;

    uint16_t config_registers[3][MAX_CONFIG_REGISTERS]; // red, green, blue
    uint config_register_count = 0;
    uint next_config_register = 0;

    // State machines
    uint sm_data = 0;
    uint sm_clk_lat = 0;
    uint sm_clk_cnt = 0;
    uint data_prog_offs = 0;
    uint clk_lat_prog_offs = 0;
    uint clk_cnt_prog_offs = 0;
    pio_sm_config data_config;
    pio_sm_config clk_lat_config;
    pio_sm_config clk_cnt_config;

    int dma_channel = -1;
    dma_channel_config dma_config;

    // Two line buffers, one being sent by DMA while the other is filled
    uint8_t *line_buffer[2] = {nullptr, nullptr};

    bool running = false;

    // Set when the back buffer or display settings differ from the panel
    bool dirty = true;
    uint8_t uploaded_brightness = 0;
    uint uploaded_colour_depth = 0;
    volatile bool gclk_running = false;
    volatile bool gclk_stop_requested = false;
    volatile uint row = 0;

    void send_all_config_registers();

    void set_row(uint r);

    void send_clocks(uint count, uint32_t lat_oe = 0);
    void send_latches(uint count) { send_clocks(count, 0b01); }
    void send_oe(uint count) { send_clocks(count, 0b10); }
    void send_vsync();
    void send_register(uint16_t value, uint latches) { send_register(value, value, value, latches); }
    void send_register(uint16_t red, uint16_t green, uint16_t blue, uint latches);
    void restart_data_sm(uint latches);
    void wait_data_sm();

    void init_mux();
    void start_gclk();
    void stop_gclk();
    void start_dclk();
    void load_config_register();
    void data_transfer();
    void fill_line(uint8_t *buf, uint y, uint channel);
};

}
