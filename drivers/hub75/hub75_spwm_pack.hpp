#pragma once

#include <stdint.h>

/*
Compact frame buffer format for hub75_spwm_frame (see hub75_spwm.pio).

Each chip receives one 16-bit greyscale word per channel per scan line, MSB
first. Only the 10 bits below the leading zeros carry data, so the buffer holds
just those 10 clocks for each column: two 32-bit words of 5 clocks, 6 bits per
clock (R0, G0, B0, R1, G1, B1), the first clock in the LSBs.

The words are in upload order: for each scan line, for each of the 16 channels,
for each chip in the chain. Chain position `pos` is channel pos % 16 of chip
pos / 16. Each clock carries both lanes, R0, G0, B0 (lane 0) and R1, G1, B1
(lane 1); which pixels they drive depends on the panel, see Hub75SPWM::Lanes.

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

    // Index of the first of the two words for chain position `pos` of scan line
    // `line`, with `chain` pixels per lane and scan line
    constexpr uint32_t word_index(uint32_t pos, uint32_t line, uint32_t chain) {
        uint32_t channel = pos % 16;
        uint32_t chip = pos / 16;
        return ((line * 16 + channel) * (chain / 16) + chip) * 2;
    }

    // Write the 10-bit greyscale values of one pixel of a lane (0 or 1) to its
    // two words. Returns true if they changed.
    inline bool set(uint32_t *buffer, uint32_t pos, uint32_t line, uint32_t lane, uint32_t chain,
                    uint32_t r, uint32_t g, uint32_t b) {
        uint32_t *w = &buffer[word_index(pos, line, chain)];
        uint32_t shift = lane ? 3 : 0;    // R0, G0, B0 or R1, G1, B1
        uint32_t mask = (CLOCK_BITS * 0b111) << shift;

        uint32_t w0 = (SPREAD5.t[r >> 5] | SPREAD5.t[g >> 5] << 1 | SPREAD5.t[b >> 5] << 2) << shift;
        uint32_t w1 = (SPREAD5.t[r & 31] | SPREAD5.t[g & 31] << 1 | SPREAD5.t[b & 31] << 2) << shift;

        w0 |= w[0] & ~mask;
        w1 |= w[1] & ~mask;
        bool changed = w0 != w[0] || w1 != w[1];
        w[0] = w0;
        w[1] = w1;
        return changed;
    }

    // Write the 10-bit greyscale values of both lanes at chain position `pos`
    // of scan line `line`: lane 0 (r0, g0, b0) and lane 1 (r1, g1, b1)
    inline void set_pair(uint32_t *buffer, uint32_t pos, uint32_t line, uint32_t chain,
                         uint32_t r0, uint32_t g0, uint32_t b0, uint32_t r1, uint32_t g1, uint32_t b1) {
        uint32_t *w = &buffer[word_index(pos, line, chain)];
        const uint32_t *t = SPREAD5.t;
        w[0] = t[r0 >> 5] | t[g0 >> 5] << 1 | t[b0 >> 5] << 2 | t[r1 >> 5] << 3 | t[g1 >> 5] << 4 | t[b1 >> 5] << 5;
        w[1] = t[r0 & 31] | t[g0 & 31] << 1 | t[b0 & 31] << 2 | t[r1 & 31] << 3 | t[g1 & 31] << 4 | t[b1 & 31] << 5;
    }

}
}
