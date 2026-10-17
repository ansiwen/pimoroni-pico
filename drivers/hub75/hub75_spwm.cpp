#include <cstring>
#include <algorithm>

#include "hardware/clocks.h"

#include "hub75_spwm.hpp"

/*
S-PWM driver protocol, as implemented in DMD_STM32 by Dmitry Dmitriev (board707):
https://github.com/board707/DMD_STM32 (DMD_SPWM_Driver_RP.h)

SPDX-License-Identifier: GPL-3.0-or-later
*/

namespace pimoroni {

// Maximum CLK frequency in MHz (most HUB75 panels handle about 20-25MHz)
static const uint MAX_CLK_MHZ = 15;

// Clocks per scan line: OE (GCLK) off, OE on, OE off
static const uint GCLK_D1 = 28;
static const uint GCLK_D2 = 4;
static const uint GCLK_D3 = 96;

// DMD_STM32 register_test profile "regtype2: preserved former built-in SM16380SH CONFIG1".
// The same as regtype1 (the current DMD_STM32 default) except register 0x07, which is brighter.
static const uint16_t SM16380SH_CONFIG[3][32] = {
    {0x021f, 0x0300, 0x0400, 0x0500, 0x0600, 0x072c, 0x0800, 0x0900, 0x0a02, 0x0b0c, 0x0c08, 0x0d00, 0x0e05, 0x0f00, 0x1000, 0x1100,
     0x1200, 0x1308, 0x1414, 0x1500, 0x1630, 0x1700, 0x1801, 0x1904, 0x1a03, 0x1b14, 0x1c12, 0x1d00, 0x1e00, 0x1f0c, 0x2000, 0x2200},
    {0x021f, 0x0300, 0x0400, 0x0500, 0x0600, 0x072c, 0x0800, 0x0900, 0x0a02, 0x0b0c, 0x0c18, 0x0d00, 0x0e05, 0x0f00, 0x1000, 0x1100,
     0x1200, 0x1308, 0x1422, 0x1500, 0x1630, 0x1700, 0x1801, 0x1903, 0x1a01, 0x1b14, 0x1c8f, 0x1d00, 0x1e00, 0x1f0c, 0x2000, 0x2200},
    {0x021f, 0x0300, 0x0400, 0x0500, 0x0600, 0x072c, 0x0800, 0x0900, 0x0a02, 0x0b0c, 0x0c30, 0x0d00, 0x0e05, 0x0f00, 0x1000, 0x1100,
     0x1200, 0x1308, 0x1432, 0x1500, 0x1630, 0x1700, 0x1801, 0x1903, 0x1a01, 0x1b14, 0x1c8f, 0x1d00, 0x1e00, 0x1f0c, 0x2000, 0x2200}
};

static const uint16_t ICND2055_CONFIG[] = {
    0x021f, 0x033f, 0x0400, 0x0507, 0x0603, 0x0720, 0x0820, 0x0908, 0x0a08, 0x0b00,
    0x0c08, 0x0d01, 0x0e04, 0x0f01, 0x1082, 0x1121, 0x1201, 0x17f0, 0x181f, 0x1950,
    0x1a1f, 0x1b10, 0x1ccf, 0x1d0a, 0x1e4c, 0x1f20, 0x2008, 0x2101, 0x221c
};

static const uint16_t FM6373_CONFIG[] = {
    0x021f, 0x033f, 0x0402, 0x0507, 0x0603, 0x0720, 0x0820, 0x0900, 0x0a00, 0x0b00,
    0x0c01, 0x0d01, 0x0e04, 0x0f01, 0x10c2, 0x1121, 0x1201, 0x17f0, 0x181f, 0x1900,
    0x1a1f, 0x1b10, 0x1cc1, 0x1d0a, 0x1e42, 0x1f04, 0x2008, 0x2101, 0x221c
};

Hub75SPWM::Hub75SPWM(uint width, uint height, Chip chip)
 : width(width), height(height), scan_rows(height / 2), chip(chip) {
    // Row address lines are driven directly from the PIO IRQ handler
    for(auto i = 0u; i < 5; i++) {
        gpio_init(pin_row_a + i);
        gpio_set_dir(pin_row_a + i, true);
        gpio_put(pin_row_a + i, 0);
    }

    back_buffer = new Pixel[width * height];
    clear();

    // Each transfer shifts one 16-bit word into every chip in the chain
    line_buffer[0] = new uint8_t[width];
    line_buffer[1] = new uint8_t[width];

    load_chip_defaults();
}

Hub75SPWM::~Hub75SPWM() {
    delete[] back_buffer;
    delete[] line_buffer[0];
    delete[] line_buffer[1];
}

void Hub75SPWM::load_chip_defaults() {
    switch(chip) {
        case Chip::SM16380SH:
            gclk_bits = 14;
            set_config_registers(SM16380SH_CONFIG[0], SM16380SH_CONFIG[1], SM16380SH_CONFIG[2], count_of(SM16380SH_CONFIG[0]));
            break;
        case Chip::ICND2055:
            gclk_bits = 13;
            set_config_registers(ICND2055_CONFIG, count_of(ICND2055_CONFIG));
            break;
        case Chip::FM6373:
            gclk_bits = 14;
            set_config_registers(FM6373_CONFIG, count_of(FM6373_CONFIG));
            break;
    }
}

void Hub75SPWM::set_config_registers(const uint16_t *regs, uint count) {
    set_config_registers(regs, regs, regs, count);
}

void Hub75SPWM::set_config_registers(const uint16_t *red, const uint16_t *green, const uint16_t *blue, uint count) {
    count = std::min(count, MAX_CONFIG_REGISTERS);
    memcpy(config_registers[0], red, count * sizeof(uint16_t));
    memcpy(config_registers[1], green, count * sizeof(uint16_t));
    memcpy(config_registers[2], blue, count * sizeof(uint16_t));
    config_register_count = count;
    next_config_register = 0;

    // Register 0x02 holds the number of scan lines - 1
    for(auto c = 0u; c < 3; c++) {
        for(auto i = 0u; i < count; i++) {
            if((config_registers[c][i] >> 8) == 0x02) {
                config_registers[c][i] = 0x0200 | ((scan_rows - 1) & 0xff);
            }
        }
    }

    if(running) send_all_config_registers();
}

// One register is sent per frame upload, so upload one frame per register
void Hub75SPWM::send_all_config_registers() {
    for(auto i = 0u; i < config_register_count; i++) {
        refresh();
        sleep_ms(3);
    }
}

void Hub75SPWM::set_pixel(uint x, uint y, uint8_t r, uint8_t g, uint8_t b) {
    if(x >= width || y >= height) return;
    uint half = 0;
    if(y >= scan_rows) {
        y -= scan_rows;
        half = 1;
    }
    Pixel p(r, g, b);
    Pixel &dest = back_buffer[(y * width + x) * 2 + half];
    if(dest.color != p.color) {
        dest = p;
        dirty = true;
    }
}

void Hub75SPWM::clear() {
    for(auto i = 0u; i < width * height; i++) {
        back_buffer[i] = Pixel();
    }
    dirty = true;
}

void Hub75SPWM::set_row(uint r) {
    gpio_put_masked(0b11111 << pin_row_a, (r & 0b11111) << pin_row_a);
}

void Hub75SPWM::start(irq_handler_t handler) {
    if(running || !handler) return;

    uint cycles_per_us = clock_get_hz(clk_sys) / 1000000;

    sm_data = pio_claim_unused_sm(pio, true);
    sm_clk_lat = pio_claim_unused_sm(pio, true);
    sm_clk_cnt = pio_claim_unused_sm(pio, true);

    data_prog_offs = pio_add_program(pio, &hub75_spwm_data_program);
    clk_lat_prog_offs = pio_add_program(pio, &hub75_spwm_clk_lat_program);
    clk_cnt_prog_offs = pio_add_program(pio, &hub75_spwm_clk_cnt_program);

    // Integer dividers keep the CLK below MAX_CLK_MHZ (data SM: 6 cycles per clock, clk_lat: 2)
    data_config = hub75_spwm_data_program_init(pio, sm_data, data_prog_offs,
        1 + cycles_per_us / (MAX_CLK_MHZ * 5), pin_r0, pin_lat, pin_clk);
    clk_lat_config = hub75_spwm_clk_lat_program_init(pio, sm_clk_lat, clk_lat_prog_offs,
        1 + cycles_per_us / (MAX_CLK_MHZ * 2), pin_lat, pin_clk);
    clk_cnt_config = hub75_spwm_clk_cnt_program_init(pio, sm_clk_cnt, clk_cnt_prog_offs,
        1, pin_clk, pin_oe);
    pio_sm_set_enabled(pio, sm_clk_lat, true);

    dma_channel = dma_claim_unused_channel(true);
    dma_config = dma_channel_get_default_config(dma_channel);
    channel_config_set_transfer_data_size(&dma_config, DMA_SIZE_8);
    channel_config_set_read_increment(&dma_config, true);
    channel_config_set_write_increment(&dma_config, false);
    channel_config_set_dreq(&dma_config, pio_get_dreq(pio, sm_data, true));
    dma_channel_configure(dma_channel, &dma_config, &pio->txf[sm_data], nullptr, width, false);

    // PIO IRQ 0 (raised by the clk_cnt SM) switches rows
    uint irq_num = pio_get_irq_num(pio, 0);
    pio_set_irq0_source_enabled(pio, pis_interrupt0, true);
    irq_add_shared_handler(irq_num, handler, PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY);
    irq_set_enabled(irq_num, true);

    running = true;

    // Send every config register once, then start displaying
    send_all_config_registers();
}

void Hub75SPWM::stop(irq_handler_t handler) {
    if(!running) return;

    stop_gclk();

    uint irq_num = pio_get_irq_num(pio, 0);
    pio_set_irq0_source_enabled(pio, pis_interrupt0, false);
    irq_remove_handler(irq_num, handler);
    pio_interrupt_clear(pio, 0);

    if(dma_channel != -1) {
        dma_channel_abort(dma_channel);
        dma_channel_unclaim(dma_channel);
        dma_channel = -1;
    }

    pio_sm_set_enabled(pio, sm_data, false);
    pio_sm_set_enabled(pio, sm_clk_lat, false);
    pio_sm_set_enabled(pio, sm_clk_cnt, false);

    pio_remove_program(pio, &hub75_spwm_data_program, data_prog_offs);
    pio_remove_program(pio, &hub75_spwm_clk_lat_program, clk_lat_prog_offs);
    pio_remove_program(pio, &hub75_spwm_clk_cnt_program, clk_cnt_prog_offs);

    pio_sm_unclaim(pio, sm_data);
    pio_sm_unclaim(pio, sm_clk_lat);
    pio_sm_unclaim(pio, sm_clk_cnt);

    // Return the pins to a known state
    for(auto pin : {pin_r0, pin_r0 + 1, pin_r0 + 2, pin_r0 + 3, pin_r0 + 4, pin_r0 + 5, pin_clk, pin_lat, pin_oe}) {
        gpio_init(pin);
        gpio_set_dir(pin, true);
        gpio_put(pin, 0);
    }
    set_row(0);

    running = false;
}

void Hub75SPWM::pio_irq() {
    if(!pio_interrupt_get(pio, 0)) return;
    pio_interrupt_clear(pio, 0);

    set_row(row);

    if(row == 0 && gclk_stop_requested) {
        // Stop DCLK and GCLK at the start of the frame
        pio_sm_set_enabled(pio, sm_clk_cnt, false);
        pio_sm_set_enabled(pio, sm_clk_lat, false);
        gclk_stop_requested = false;
        gclk_running = false;
        return;
    }

    uint next = row + 1;
    row = next >= scan_rows ? 0 : next;
}

// Generate `count` CLK pulses with LAT (bit 0) and OE (bit 1) held at `lat_oe`
void Hub75SPWM::send_clocks(uint count, uint32_t lat_oe) {
    if(count == 0) return;
    for(auto i = 0u; i < count; i++) {
        pio_sm_put_blocking(pio, sm_clk_lat, lat_oe);
    }
    hub75_spwm_wait_tx_stall(pio, sm_clk_lat);
    if(lat_oe) {
        // Return LAT and OE low
        pio_sm_exec(pio, sm_clk_lat, pio_encode_mov(pio_pins, pio_null));
    }
}

// VSYNC = 3 clocks LAT pulse
void Hub75SPWM::send_vsync() {
    send_clocks(1);
    send_latches(3);
}

// Restart the data SM for a transfer of `width` clocks with LAT high for the last `latches`
void Hub75SPWM::restart_data_sm(uint latches) {
    pio_sm_set_enabled(pio, sm_data, false);
    pio_sm_init(pio, sm_data, data_prog_offs, &data_config);
    pio_sm_set_enabled(pio, sm_data, true);
    pio_sm_put_blocking(pio, sm_data, ((width - latches - 1) << 16) | (width - 1));
}

// Wait for the DMA and the data SM to finish a transfer
void Hub75SPWM::wait_data_sm() {
    dma_channel_wait_for_finish_blocking(dma_channel);
    while(!pio_interrupt_get(pio, 7)) {
        tight_loop_contents();
    }
    pio_interrupt_clear(pio, 7);
}

// Send a 16-bit register value to every chip, with separate values for the
// red, green and blue drivers of both halves
void Hub75SPWM::send_register(uint16_t red, uint16_t green, uint16_t blue, uint latches) {
    // DMA ring mode requires the buffer to be aligned to its size
    static uint8_t reg[16] __attribute__((aligned(16)));

    for(auto bit = 0u; bit < 16; bit++) {
        uint16_t mask = 0x8000 >> bit;
        reg[bit] = ((red & mask) ? 0b001001 : 0)
                 | ((green & mask) ? 0b010010 : 0)
                 | ((blue & mask) ? 0b100100 : 0);
    }

    restart_data_sm(latches);

    // Wrap the read address every 16 bytes to repeat the value for each chip
    channel_config_set_ring(&dma_config, false, 4);
    dma_channel_set_config(dma_channel, &dma_config, false);
    dma_channel_set_trans_count(dma_channel, width, false);
    dma_channel_set_read_addr(dma_channel, reg, true);

    wait_data_sm();

    channel_config_set_ring(&dma_config, false, 0);
    dma_channel_set_config(dma_channel, &dma_config, false);
}

void Hub75SPWM::init_mux() {
    set_row(0);
    row = 1;
    send_oe(12);
    send_clocks(88);
}

void Hub75SPWM::start_gclk() {
    pio_sm_init(pio, sm_clk_cnt, clk_cnt_prog_offs, &clk_cnt_config);
    pio_sm_set_enabled(pio, sm_clk_cnt, true);

    // Three segments of 9 bits: (clocks - 1) in the low 8 bits, OE level in the MSB
    uint32_t control = (GCLK_D3 - 1);
    control = (control << 9) | (1 << 8) | (GCLK_D2 - 1);
    control = (control << 9) | (GCLK_D1 - 1);
    pio_sm_put_blocking(pio, sm_clk_cnt, control);

    gclk_running = true;
}

void Hub75SPWM::stop_gclk() {
    if(gclk_running) {
        // Ask the IRQ handler to stop the clocks once it reaches row 0
        gclk_stop_requested = true;

        // A frame is at most 64 rows * 256 clocks, this should never time out
        absolute_time_t timeout = make_timeout_time_ms(100);
        while(gclk_running && !time_reached(timeout)) {
            tight_loop_contents();
        }
        gclk_stop_requested = false;
        gclk_running = false;
    }

    pio_sm_set_enabled(pio, sm_clk_cnt, false);

    // Restart clk_lat at the top of the program, which leaves CLK low
    pio_sm_set_enabled(pio, sm_clk_lat, false);
    pio_sm_init(pio, sm_clk_lat, clk_lat_prog_offs, &clk_lat_config);
    pio_sm_set_enabled(pio, sm_clk_lat, true);
    sleep_us(10);
}

// Send VSYNC and the next config register (one per frame, round-robin)
void Hub75SPWM::load_config_register() {
    uint i = next_config_register;
    next_config_register++;
    if(next_config_register >= config_register_count) next_config_register = 0;

    send_vsync();
    send_clocks(8);
    send_latches(11); // Pre-active command
    send_clocks(8);
    send_latches(14); // Pre-active command
    send_clocks(8);
    init_mux();
    pio_sm_set_enabled(pio, sm_clk_lat, false);

    start_gclk();

    // Register writes are framed by 0x00AA 0x01AA before and 0x0055 0x0155 after
    send_register(0x00aa, 5);
    send_register(0x01aa, 5);
    send_register(config_registers[0][i], config_registers[1][i], config_registers[2][i], 5);
    send_register(0x0055, 5);
    send_register(0x0155, 5);
}

// Fill `buf` with the bits for `channel` of scan line `y` for every chip in the chain.
// Each chip drives 16 columns and receives a 16-bit word, MSB first.
void Hub75SPWM::fill_line(uint8_t *buf, uint y, uint channel) {
    const uint shift = gclk_bits - 10;
    const uint32_t scale = brightness + 1;
    const uint32_t mask = 0x3ff & ~((1u << (10 - std::clamp(colour_depth, 1u, 10u))) - 1);

    auto greyscale = [&](uint32_t c) -> uint32_t {
        return (((c & mask) << shift) * scale) >> 8;
    };

    const Pixel *line = &back_buffer[y * width * 2];

    for(auto n = 0u; n < width / CHANNELS_PER_CHIP; n++) {
        uint x = n * CHANNELS_PER_CHIP + channel;
        uint32_t top = line[x * 2].color;
        uint32_t bottom = line[x * 2 + 1].color;

        uint32_t r0 = greyscale(top), g0 = greyscale(top >> 10), b0 = greyscale(top >> 20);
        uint32_t r1 = greyscale(bottom), g1 = greyscale(bottom >> 10), b1 = greyscale(bottom >> 20);

        for(int bit = 15; bit >= 0; bit--) {
            *buf++ = ((r0 >> bit) & 1)
                   | ((g0 >> bit) & 1) << 1
                   | ((b0 >> bit) & 1) << 2
                   | ((r1 >> bit) & 1) << 3
                   | ((g1 >> bit) & 1) << 4
                   | ((b1 >> bit) & 1) << 5;
        }
    }
}

// Upload the greyscale data: for every scan line, 16 transfers (one per channel)
// each followed by a single clock data latch.
void Hub75SPWM::data_transfer() {
    uint current = 0;
    bool first = true;

    dma_channel_set_trans_count(dma_channel, width, false);

    for(auto y = 0u; y < scan_rows; y++) {
        for(auto channel = 0u; channel < CHANNELS_PER_CHIP; channel++) {
            // Fill one buffer while the other is being sent
            fill_line(line_buffer[current], y, channel);

            if(first) {
                first = false;
            } else {
                wait_data_sm();
            }

            restart_data_sm(1);
            dma_channel_set_read_addr(dma_channel, line_buffer[current], true);

            current ^= 1;
        }
    }
}

void Hub75SPWM::refresh() {
    if(!running) return;

    dirty = false;
    uploaded_brightness = brightness;
    uploaded_colour_depth = colour_depth;

    if(vsync_after_data) {
        // Stop DCLK, GCLK keeps running from the data clocks during the upload
        pio_sm_set_enabled(pio, sm_clk_lat, false);
        data_transfer();
        wait_data_sm();
        pio_sm_set_enabled(pio, sm_data, false);

        // stop_gclk() needs DCLK running to reach row 0
        start_dclk();
        stop_gclk();
        load_config_register(); // waits for each register write
    } else {
        // DMD_STM32 order: each frame is shown at the next frame's VSYNC
        stop_gclk();
        load_config_register();
        data_transfer();
        wait_data_sm();
    }
    pio_sm_set_enabled(pio, sm_data, false);

    start_dclk();
}

// Free-running CLK is the reference clock (DCLK) for the chips' PWM
void Hub75SPWM::start_dclk() {
    pio_sm_init(pio, sm_clk_lat, clk_lat_prog_offs + hub75_spwm_clk_lat_offset_free_run, &clk_lat_config);
    pio_sm_set_enabled(pio, sm_clk_lat, true);
}

void Hub75SPWM::update(PicoGraphics *graphics, bool force) {
    if(graphics->pen_type == PicoGraphics::PEN_RGB888) {
        uint32_t *p = (uint32_t *)graphics->frame_buffer;
        for(auto y = 0u; y < height; y++) {
            for(auto x = 0u; x < width; x++) {
                uint32_t c = *p++;
                set_pixel(x, y, (c >> 16) & 0xff, (c >> 8) & 0xff, c & 0xff);
            }
        }
    } else {
        uint offset = 0;
        graphics->frame_convert(PicoGraphics::PEN_RGB888, [this, &offset](void *data, size_t length) {
            uint32_t *p = (uint32_t *)data;
            for(auto i = 0u; i < length / sizeof(RGB888); i++) {
                uint32_t c = *p++;
                set_pixel(offset % width, offset / width, (c >> 16) & 0xff, (c >> 8) & 0xff, c & 0xff);
                offset++;
            }
        });
    }

    if(force || dirty || brightness != uploaded_brightness || colour_depth != uploaded_colour_depth) {
        refresh();
    }
}

}
