#include <cstring>
#include <algorithm>

#include "hardware/clocks.h"
#include "hardware/sync.h"

#include "hub75_spwm.hpp"
#include "hub75_spwm_pack.hpp"

/*
S-PWM driver protocol, as implemented in DMD_STM32 by Dmitry Dmitriev (board707):
https://github.com/board707/DMD_STM32 (DMD_SPWM_Driver_RP.h)

SPDX-License-Identifier: GPL-3.0-or-later
*/

namespace pimoroni {

// Maximum CLK frequency in MHz (most HUB75 panels handle about 20-25MHz)
static const uint MAX_CLK_MHZ = 15;

// Maximum CLK frequency of the frame upload in kHz. A 128x64 frame takes ~2.6ms
// at 25MHz, still slower than update() converts it on an RP2350 (~1.9ms).
static const uint MAX_UPLOAD_CLK_KHZ = 25000;

// Frame rate of the background refresh
static const int64_t FRAME_US = 1000000 / 60;

// The driver that the DMA IRQ handler belongs to (only one instance is supported)
static Hub75SPWM *instance = nullptr;

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

Hub75SPWM::Hub75SPWM(uint width, uint height, Chip chip, uint32_t *buffer)
 : width(width), height(height), scan_rows(height / 2), chip(chip) {
    // Row address lines are driven by the rows SM once started, hold row 0 until then
    for(auto i = 0u; i < 5; i++) {
        gpio_init(pin_row_a + i);
        gpio_set_dir(pin_row_a + i, true);
        gpio_put(pin_row_a + i, 0);
    }

    if(buffer == nullptr) {
        frame_buffer = new uint32_t[buffer_size()];
        managed_buffer = true;
    } else {
        frame_buffer = buffer;
    }
    clear();

    load_chip_defaults();
}

Hub75SPWM::~Hub75SPWM() {
    if(managed_buffer) delete[] frame_buffer;
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
    // The background refresh reads these, don't let it see a partial update
    uint32_t status = save_and_disable_interrupts();
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

    restore_interrupts(status);
    // One register is sent with every frame, all of them within ~0.5s
}

// Gamma corrected 10-bit greyscale value, with brightness and colour depth applied
uint32_t Hub75SPWM::greyscale(uint8_t v) {
    const uint32_t mask = 0x3ff & ~((1u << (10 - std::clamp(colour_depth, 1u, 10u))) - 1);
    return ((GAMMA_10BIT[v] & mask) * (brightness + 1)) >> 8;
}

void Hub75SPWM::set_pixel(uint x, uint y, uint8_t r, uint8_t g, uint8_t b) {
    if(x >= width || y >= height) return;
    hub75_spwm_pack::set(frame_buffer, x, y, width, scan_rows, greyscale(r), greyscale(g), greyscale(b));
}

void Hub75SPWM::clear() {
    memset(frame_buffer, 0, buffer_size() * sizeof(uint32_t));
}

// The row currently selected on the A-E lines
uint Hub75SPWM::current_row() {
    return (gpio_get_all() >> pin_row_a) & 0b11111;
}

// Find a PIO other than `exclude` with `sm_count` free state machines and room
// for all of `programs`, then claim the state machines and load the programs.
static bool claim_pio(PIO exclude, PIO &pio, uint *sms, uint sm_count,
                      const pio_program_t *const *programs, uint *offsets, uint program_count) {
    for(auto i = 0u; i < NUM_PIOS; i++) {
        PIO p = pio_get_instance(i);
        if(p == exclude) continue;

        uint claimed = 0;
        while(claimed < sm_count) {
            int sm = pio_claim_unused_sm(p, false);
            if(sm < 0) break;
            sms[claimed++] = sm;
        }

        uint added = 0;
        if(claimed == sm_count) {
            while(added < program_count && pio_can_add_program(p, programs[added])) {
                offsets[added] = pio_add_program(p, programs[added]);
                added++;
            }
        }

        if(claimed == sm_count && added == program_count) {
            pio = p;
            return true;
        }

        // Not enough room, undo and try the next PIO
        while(added--) pio_remove_program(p, programs[added], offsets[added]);
        while(claimed--) pio_sm_unclaim(p, sms[claimed]);
    }
    return false;
}

void Hub75SPWM::start() {
    if(running) return;

    uint cycles_per_us = clock_get_hz(clk_sys) / 1000000;

    // The frame, register and command programs fill a PIO, the clock counting
    // and row switching programs go in another one. CLK can be read from any PIO.
    uint sms[3], offsets[3];
    const pio_program_t *data_programs[] = {&hub75_spwm_frame_program, &hub75_spwm_regs_program, &hub75_spwm_clk_lat_program};
    if(!claim_pio(nullptr, pio, sms, 3, data_programs, offsets, 3)) {
        panic("Hub75SPWM: no PIO with 3 free state machines and %u free instructions",
            hub75_spwm_frame_program.length + hub75_spwm_regs_program.length + hub75_spwm_clk_lat_program.length);
    }
    sm_frame = sms[0]; sm_regs = sms[1]; sm_clk_lat = sms[2];
    frame_prog_offs = offsets[0]; regs_prog_offs = offsets[1]; clk_lat_prog_offs = offsets[2];

    const pio_program_t *gclk_programs[] = {&hub75_spwm_clk_cnt_program, &hub75_spwm_rows_program};
    if(!claim_pio(pio, pio_gclk, sms, 2, gclk_programs, offsets, 2)) {
        panic("Hub75SPWM: no second PIO with 2 free state machines and %u free instructions",
            hub75_spwm_clk_cnt_program.length + hub75_spwm_rows_program.length);
    }
    sm_clk_cnt = sms[0]; sm_rows = sms[1];
    clk_cnt_prog_offs = offsets[0]; rows_prog_offs = offsets[1];

    // The greyscale bits below the leading zeros are sent from the frame buffer
    hub75_spwm_frame_set_zeros(pio, frame_prog_offs, 16 - gclk_bits, gclk_bits - 10);

    // Integer dividers keep the CLK below MAX_CLK_MHZ (frame/regs SMs: 6 cycles per clock, clk_lat: 2)
    uint data_clkdiv = 1 + cycles_per_us / (MAX_CLK_MHZ * 5);
    if(upload_clock_div == 0) {
        // Smallest integer divider, 6 cycles per clock: 1 (25MHz) at 150MHz
        uint upload_clk_khz = clock_get_hz(clk_sys) / 6000;
        upload_clock_div = (upload_clk_khz + MAX_UPLOAD_CLK_KHZ - 1) / MAX_UPLOAD_CLK_KHZ;
    }
    frame_config = hub75_spwm_frame_program_init(pio, sm_frame, frame_prog_offs, data_clkdiv, pin_r0, pin_clk);
    regs_config = hub75_spwm_regs_program_init(pio, sm_regs, regs_prog_offs, data_clkdiv, pin_r0, pin_lat, pin_clk);
    clk_lat_config = hub75_spwm_clk_lat_program_init(pio, sm_clk_lat, clk_lat_prog_offs,
        1 + cycles_per_us / (MAX_CLK_MHZ * 2), pin_lat, pin_clk);
    clk_cnt_config = hub75_spwm_clk_cnt_program_init(pio_gclk, sm_clk_cnt, clk_cnt_prog_offs,
        1, pin_clk, pin_oe);
    rows_config = hub75_spwm_rows_program_init(pio_gclk, sm_rows, rows_prog_offs, pin_row_a);
    pio_sm_set_enabled(pio, sm_clk_lat, true);

    dma_channel = dma_claim_unused_channel(true);

    // The end of a frame upload restarts DCLK, so it must not wait for other handlers
    instance = this;
    irq_add_shared_handler(DMA_IRQ_1, dma_irq_handler, PICO_SHARED_IRQ_HANDLER_HIGHEST_ORDER_PRIORITY);
    irq_set_priority(DMA_IRQ_1, PICO_HIGHEST_IRQ_PRIORITY);
    irq_set_enabled(DMA_IRQ_1, true);

    running = true;

    // Refresh the panel at FRAME_US intervals in the background
    frame_alarm = add_alarm_in_us(0, frame_alarm_callback, this, true);
}

void Hub75SPWM::stop() {
    if(!running) return;

    // Let the current frame finish, then stop the background refresh
    uint32_t status = save_and_disable_interrupts();
    cancel_alarm(frame_alarm);
    restore_interrupts(status);
    while(state != State::IDLE) tight_loop_contents();

    irq_remove_handler(DMA_IRQ_1, dma_irq_handler);
    instance = nullptr;

    stop_gclk();

    if(dma_channel != -1) {
        dma_channel_abort(dma_channel);
        dma_channel_unclaim(dma_channel);
        dma_channel = -1;
    }

    pio_sm_set_enabled(pio, sm_frame, false);
    pio_sm_set_enabled(pio, sm_regs, false);
    pio_sm_set_enabled(pio, sm_clk_lat, false);
    pio_sm_set_enabled(pio_gclk, sm_clk_cnt, false);
    pio_sm_set_enabled(pio_gclk, sm_rows, false);
    pio_interrupt_clear(pio_gclk, 0);

    pio_remove_program(pio, &hub75_spwm_frame_program, frame_prog_offs);
    pio_remove_program(pio, &hub75_spwm_regs_program, regs_prog_offs);
    pio_remove_program(pio, &hub75_spwm_clk_lat_program, clk_lat_prog_offs);
    pio_remove_program(pio_gclk, &hub75_spwm_clk_cnt_program, clk_cnt_prog_offs);
    pio_remove_program(pio_gclk, &hub75_spwm_rows_program, rows_prog_offs);

    pio_sm_unclaim(pio, sm_frame);
    pio_sm_unclaim(pio, sm_regs);
    pio_sm_unclaim(pio, sm_clk_lat);
    pio_sm_unclaim(pio_gclk, sm_clk_cnt);
    pio_sm_unclaim(pio_gclk, sm_rows);

    // Return the pins to a known state
    for(auto pin : {pin_r0, pin_r0 + 1, pin_r0 + 2, pin_r0 + 3, pin_r0 + 4, pin_r0 + 5, pin_clk, pin_lat, pin_oe,
                    pin_row_a, pin_row_a + 1, pin_row_a + 2, pin_row_a + 3, pin_row_a + 4}) {
        gpio_init(pin);
        gpio_set_dir(pin, true);
        gpio_put(pin, 0);
    }

    running = false;
}

// Generate `count` CLK pulses with LAT held at `lat`
void Hub75SPWM::send_clocks(uint count, bool lat) {
    if(count == 0) return;
    for(auto i = 0u; i < count; i++) {
        pio_sm_put_blocking(pio, sm_clk_lat, lat);
    }
    hub75_spwm_wait_tx_stall(pio, sm_clk_lat);
    if(lat) {
        // Return LAT low
        pio_sm_exec(pio, sm_clk_lat, pio_encode_mov(pio_pins, pio_null));
    }
}

// VSYNC = 3 clocks LAT pulse
void Hub75SPWM::send_vsync() {
    send_clocks(1);
    send_latches(3);
}

// Start a DMA transfer of `count` words to the TX FIFO of `sm`, optionally
// wrapping the read address every 2^ring_bits bytes
void Hub75SPWM::start_dma(uint sm, const void *src, uint count, uint ring_bits) {
    dma_channel_config c = dma_channel_get_default_config(dma_channel);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_ring(&c, false, ring_bits);
    channel_config_set_dreq(&c, pio_get_dreq(pio, sm, true));
    dma_channel_configure(dma_channel, &c, &pio->txf[sm], src, count, true);
}

// Wait until the DMA transfer is done and `sm` has clocked out everything
void Hub75SPWM::wait_dma(uint sm) {
    dma_channel_wait_for_finish_blocking(dma_channel);
    hub75_spwm_wait_tx_stall(pio, sm);
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

    pio_sm_set_enabled(pio, sm_regs, false);
    pio_sm_init(pio, sm_regs, regs_prog_offs, &regs_config);
    pio_sm_put(pio, sm_regs, ((width - latches - 1) << 16) | (width - 1));
    pio_sm_set_enabled(pio, sm_regs, true);

    // 4 clocks per word, wrapping every 16 bytes to repeat the value for each chip
    start_dma(sm_regs, reg, width / 4, 4);
    wait_dma(sm_regs);

    pio_sm_exec(pio, sm_regs, pio_encode_mov(pio_pins, pio_null)); // RGB back to 0
    pio_sm_set_enabled(pio, sm_regs, false);
}

// Select row 0 and restart the rows SM: it selects row 1 at the first IRQ 0 from
// clk_cnt (when start_gclk() runs), then 2, 3, ... scan_rows - 1, 0, 1, ...
// Hold OE (GCLK) high for `count` CLK pulses. OE belongs to the clk_cnt SM's PIO,
// which is stopped while the commands are sent.
void Hub75SPWM::send_oe(uint count) {
    pio_sm_exec(pio_gclk, sm_clk_cnt, pio_encode_mov_not(pio_pins, pio_null)); // OE high
    send_clocks(count);
    pio_sm_exec(pio_gclk, sm_clk_cnt, pio_encode_mov(pio_pins, pio_null));     // OE low
}

void Hub75SPWM::init_mux() {
    pio_sm_set_enabled(pio_gclk, sm_rows, false);
    pio_sm_init(pio_gclk, sm_rows, rows_prog_offs, &rows_config);
    pio_interrupt_clear(pio_gclk, 0);
    pio_sm_put(pio_gclk, sm_rows, 31 - scan_rows);
    pio_sm_exec(pio_gclk, sm_rows, pio_encode_pull(false, true));
    pio_sm_exec(pio_gclk, sm_rows, pio_encode_mov(pio_y, pio_osr));    // Y = 31 - scan rows
    pio_sm_exec(pio_gclk, sm_rows, pio_encode_set(pio_x, 31));         // X = 31 - row
    pio_sm_exec(pio_gclk, sm_rows, pio_encode_mov_not(pio_pins, pio_x)); // Row 0
    pio_sm_set_enabled(pio_gclk, sm_rows, true);

    send_oe(12);
    send_clocks(88);
}

void Hub75SPWM::start_gclk() {
    pio_sm_init(pio_gclk, sm_clk_cnt, clk_cnt_prog_offs, &clk_cnt_config);

    // Three segments of 9 bits: (clocks - 1) in the low 8 bits, OE level in the MSB
    uint32_t control = (GCLK_D3 - 1);
    control = (control << 9) | (1 << 8) | (GCLK_D2 - 1);
    control = (control << 9) | (GCLK_D1 - 1);
    pio_sm_put(pio_gclk, sm_clk_cnt, control);
    pio_sm_exec(pio_gclk, sm_clk_cnt, pio_encode_pull(false, true));
    pio_sm_exec(pio_gclk, sm_clk_cnt, pio_encode_mov(pio_isr, pio_osr)); // Keep a copy

    pio_sm_set_enabled(pio_gclk, sm_clk_cnt, true);

    gclk_running = true;
}

void Hub75SPWM::stop_gclk() {
    if(gclk_running) {
        // Stop DCLK and GCLK as soon as the rows SM selects row 0, the start of
        // the frame. Interrupts are disabled so nothing delays this, it takes
        // at most one frame of scan lines (~330us for 32 rows).
        // A frame is at most 64 rows * 256 clocks, this should never time out.
        uint32_t status = save_and_disable_interrupts();
        absolute_time_t timeout = make_timeout_time_ms(100);
        while(current_row() == 0 && !time_reached(timeout)) {}
        while(current_row() != 0 && !time_reached(timeout)) {}
        pio_sm_set_enabled(pio_gclk, sm_clk_cnt, false);
        pio_sm_set_enabled(pio, sm_clk_lat, false);
        restore_interrupts(status);
        gclk_running = false;
    }

    pio_sm_set_enabled(pio_gclk, sm_clk_cnt, false);

    // Restart clk_lat at the top of the program, which leaves CLK low
    pio_sm_set_enabled(pio, sm_clk_lat, false);
    pio_sm_init(pio, sm_clk_lat, clk_lat_prog_offs, &clk_lat_config);
    pio_sm_set_enabled(pio, sm_clk_lat, true);
    busy_wait_us(10); // Runs in the frame alarm IRQ, no sleeping
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

// Start of a frame (timer IRQ): VSYNC, commands and a register at row 0, then
// start the frame upload in the background. Takes up to ~0.5ms.
int64_t Hub75SPWM::frame_alarm_callback(alarm_id_t, void *user_data) {
    return ((Hub75SPWM *)user_data)->start_frame();
}

int64_t Hub75SPWM::start_frame() {
    // The previous upload hasn't finished (shouldn't happen at 60Hz)
    if(state != State::IDLE) return -FRAME_US;

    state = State::SYNCING;
    absolute_time_t start = get_absolute_time();

    // DMD_STM32 order: each frame is shown at the next frame's VSYNC, 1 frame later
    stop_gclk();
    load_config_register();

    // Upload the frame buffer, the DMA IRQ handler finishes the frame
    pio_sm_set_enabled(pio, sm_frame, false);
    sm_config_set_clkdiv(&frame_config, upload_clock_div);
    pio_sm_init(pio, sm_frame, frame_prog_offs, &frame_config);
    pio_sm_put(pio, sm_frame, width / CHANNELS_PER_CHIP - 1);
    pio_sm_exec(pio, sm_frame, pio_encode_pull(false, true));
    pio_sm_exec(pio, sm_frame, pio_encode_mov(pio_isr, pio_osr));  // ISR = chips - 1
    pio_sm_exec(pio, sm_frame, pio_encode_out(pio_null, 32));      // Empty OSR so the data is autopulled
    pio_sm_set_enabled(pio, sm_frame, true);

    dma_channel_acknowledge_irq1(dma_channel); // Clear completions of the register writes
    dma_channel_set_irq1_enabled(dma_channel, true);
    upload_start_us = time_us_32();
    start_dma(sm_frame, frame_buffer, buffer_size(), 0);

    // In this order, so update() on the other core can rely on upload_line()
    // once it sees UPLOADING or a new frame_number
    __dmb();
    state = State::UPLOADING;
    __dmb();
    frame_number = frame_number + 1;
    frame_start_us = absolute_time_diff_us(start, get_absolute_time());

    // Negative: relative to when this alarm was due, so the frame rate doesn't drift
    return -FRAME_US;
}

void __not_in_flash_func(Hub75SPWM::dma_irq_handler)() {
    if(instance && dma_irqn_get_channel_status(1, instance->dma_channel)) {
        dma_irqn_acknowledge_channel(1, instance->dma_channel);
        instance->finish_frame();
    }
}

// End of a frame upload (DMA IRQ): wait for the last words to be clocked out,
// then restart DCLK so the panel keeps displaying.
void __not_in_flash_func(Hub75SPWM::finish_frame)() {
    dma_channel_set_irq1_enabled(dma_channel, false);
    hub75_spwm_wait_tx_stall(pio, sm_frame);
    pio_sm_set_enabled(pio, sm_frame, false);
    start_dclk();
    upload_us = time_us_32() - upload_start_us;
    state = State::IDLE;
}

// Free-running CLK is the reference clock (DCLK) for the chips' PWM
void __not_in_flash_func(Hub75SPWM::start_dclk)() {
    pio_sm_init(pio, sm_clk_lat, clk_lat_prog_offs + hub75_spwm_clk_lat_offset_free_run, &clk_lat_config);
    pio_sm_set_enabled(pio, sm_clk_lat, true);
}

// The first scan line that the frame upload hasn't read yet, or scan_rows if
// no upload is running
uint Hub75SPWM::upload_line() {
    if(state != State::UPLOADING) return scan_rows;
    uint32_t read = (uint32_t)dma_channel_hw_addr(dma_channel)->read_addr - (uint32_t)frame_buffer;
    return read / (sizeof(uint32_t) * 2 * width); // 2 words per column
}

void Hub75SPWM::update(PicoGraphics *graphics) {
    absolute_time_t update_start = get_absolute_time();
    int64_t wait = 0;

    // Gamma, colour depth and brightness for every 8-bit value
    uint16_t lut[256];
    for(auto i = 0u; i < 256; i++) lut[i] = greyscale(i);

    if(graphics->pen_type != PicoGraphics::PEN_RGB888) {
        // Converted row by row, wait for the current upload to finish first.
        // A frame that starts while converting can show part of the update.
        while(state != State::IDLE) tight_loop_contents();
        uint offset = 0;
        graphics->frame_convert(PicoGraphics::PEN_RGB888, [this, &offset, &lut](void *data, size_t length) {
            uint32_t *p = (uint32_t *)data;
            for(auto i = 0u; i < length / sizeof(RGB888); i++) {
                uint32_t c = *p++;
                uint x = offset % width, y = offset / width;
                if(y < height) {
                    hub75_spwm_pack::set(frame_buffer, x, y, width, scan_rows,
                        lut[(c >> 16) & 0xff], lut[(c >> 8) & 0xff], lut[c & 0xff]);
                }
                offset++;
            }
        });
        return;
    }

    // Convert scan line by scan line, in the order they are uploaded, so a frame
    // never shows half of an update, without delaying the frames:
    // - If an upload is running, only convert lines it has already read, so that
    //   frame is entirely old. Converting is faster than uploading, so this
    //   finishes just after the upload.
    // - Otherwise convert straight away. If an upload starts meanwhile, it
    //   shouldn't catch up with the conversion, so that frame is entirely new.
    //   If it does, the frame is counted in torn_frames.
    // The interrupts can run on the other core, only state, frame_number and
    // the DMA position are shared.
    const uint32_t *src = (const uint32_t *)graphics->frame_buffer;
    bool behind = avoid_tearing && state == State::UPLOADING;
    uint32_t frame = frame_number;
    bool torn = false;

    for(auto line = 0u; line < scan_rows; line++) {
        if(behind && frame_number == frame && upload_line() <= line) {
            absolute_time_t wait_start = get_absolute_time();
            while(frame_number == frame && upload_line() <= line) tight_loop_contents();
            wait += absolute_time_diff_us(wait_start, get_absolute_time());
        }
        const uint32_t *top = &src[line * width];
        const uint32_t *bottom = &src[(line + scan_rows) * width];
        for(auto x = 0u; x < width; x++) {
            uint32_t c0 = top[x], c1 = bottom[x];
            hub75_spwm_pack::set_pair(frame_buffer, x, line, width, scan_rows,
                lut[(c0 >> 16) & 0xff], lut[(c0 >> 8) & 0xff], lut[c0 & 0xff],
                lut[(c1 >> 16) & 0xff], lut[(c1 >> 8) & 0xff], lut[c1 & 0xff]);
        }
        // An upload that started meanwhile has read (part of) this line before
        // it was written. Conservative: it may have read the line just after.
        if(!torn && frame_number != frame && upload_line() > line) {
            torn = true;
            torn_frames = torn_frames + 1;
        }
    }
    update_wait_us = wait;
    update_convert_us = absolute_time_diff_us(update_start, get_absolute_time()) - wait;
}

}
