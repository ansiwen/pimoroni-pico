#pragma once

#include <stdint.h>

/*
Compact frame buffer format for hub75_spwm_frame (see hub75_spwm.pio).

Each chip receives one 16-bit greyscale word per channel per scan line, MSB
first. Only the 10 bits below the leading zeros carry data, so the buffer holds
just those 10 clocks for each column: two 32-bit words of 5 clocks, 6 bits per
clock (R0, G0, B0, R1, G1, B1), the first clock in the LSBs.

The words are in upload order: for each scan line, for each of the 16 channels,
for each chip in the chain. Column x is channel x % 16 of chip x / 16.

SPDX-License-Identifier: GPL-3.0-or-later
*/

namespace pimoroni {
namespace hub75_spwm_pack {

    // Spread a 5-bit value over 5 clocks, MSB first, one bit every 6 bits
    constexpr uint32_t spread5(uint32_t v) {
        uint32_t s = 0;
        for(int i = 0; i < 5; i++) {
            if(v & (0b10000 >> i)) s |= 1u << (6 * i);
        }
        return s;
    }

    struct Spread5Table {
        uint32_t t[32];
        constexpr Spread5Table() : t() {
            for(auto v = 0u; v < 32; v++) t[v] = spread5(v);
        }
    };

    constexpr Spread5Table SPREAD5;

    // Bit 0 of every clock
    constexpr uint32_t CLOCK_BITS = spread5(0b11111);

    // Index of the first of the two words for pixel (x, y)
    constexpr uint32_t word_index(uint32_t x, uint32_t y, uint32_t width, uint32_t scan_rows) {
        uint32_t line = y % scan_rows;
        uint32_t channel = x % 16;
        uint32_t chip = x / 16;
        return ((line * 16 + channel) * (width / 16) + chip) * 2;
    }

    // Write the 10-bit greyscale values of a pixel to its two words.
    // Returns true if they changed.
    inline bool set(uint32_t *buffer, uint32_t x, uint32_t y, uint32_t width, uint32_t scan_rows,
                    uint32_t r, uint32_t g, uint32_t b) {
        uint32_t *w = &buffer[word_index(x, y, width, scan_rows)];
        uint32_t lane = y < scan_rows ? 0 : 3;    // R0, G0, B0 or R1, G1, B1
        uint32_t mask = (CLOCK_BITS * 0b111) << lane;

        uint32_t w0 = (SPREAD5.t[r >> 5] | SPREAD5.t[g >> 5] << 1 | SPREAD5.t[b >> 5] << 2) << lane;
        uint32_t w1 = (SPREAD5.t[r & 31] | SPREAD5.t[g & 31] << 1 | SPREAD5.t[b & 31] << 2) << lane;

        w0 |= w[0] & ~mask;
        w1 |= w[1] & ~mask;
        bool changed = w0 != w[0] || w1 != w[1];
        w[0] = w0;
        w[1] = w1;
        return changed;
    }

}
}
