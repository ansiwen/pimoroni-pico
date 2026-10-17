# HUB75 S-PWM driver <!-- omit in toc -->

`Hub75SPWM` drives HUB75 panels built with S-PWM ("PWM type") LED driver chips. These are common on newer, finer pitch panels. Classic panels are just shift registers that the microcontroller refreshes continuously. S-PWM chips instead keep the frame in internal SRAM and generate PWM themselves. The driver uploads a frame only when you call `update()`, and between uploads it keeps feeding the panel two clocks:
- a reference clock (DCLK) on the CLK line
- a row clock (GCLK) on the OE line

The protocol, PIO programs and register values are ported from [DMD_STM32](https://github.com/board707/DMD_STM32) by Dmitry Dmitriev (board707). Because of that, `hub75_spwm.*` are licensed under the GPL-3.0-or-later, unlike the rest of this repository.

- [Supported chips](#supported-chips)
- [Usage](#usage)
- [Bring-up and tuning](#bring-up-and-tuning)

## Supported chips

You can identify the chips from the markings on the larger ICs on the back of the panel.

| Chip      | `Hub75SPWM::Chip` | Status                                      |
|-----------|-------------------|---------------------------------------------|
| SM16380SH | `SM16380SH`       | Ported from DMD_STM32, not yet hardware-tested here |
| ICND2055  | `ICND2055`        | Ported from DMD_STM32, untested             |
| FM6373    | `FM6373`          | Ported from DMD_STM32, untested             |

DMD_STM32 also supports FM6353/ICN2153, FM6363, DP3264 and ICND1065. Those chips use different command sequences and are not ported yet.

The row select lines A-E must drive a binary row decoder. Most modern row driver chips work this way, for example 74HC138, SM5166 and RUC7258. The 7262 (C7262FJ) is probably one too: DMD_STM32 lists it on A-E panels. 595-style shift register row drivers are not supported.

## Usage

```c++
#include "drivers/hub75/hub75_spwm.hpp"

// width is the total length of the chain and must be a multiple of 16
Hub75SPWM hub75(128, 64, Hub75SPWM::Chip::SM16380SH);
PicoGraphics_PenRGB888 graphics(hub75.width, hub75.height, nullptr);

void __isr pio_irq() {
    hub75.pio_irq();
}

int main() {
    hub75.start(pio_irq);
    while(true) {
        // draw with graphics...
        hub75.update(&graphics);  // blocks while the frame is uploaded
    }
}
```

The driver uses 3 state machines and 28 instructions of `pio0`, and one DMA channel. The default pins match Interstate 75 and Interstate 75 W:
- R0-B1: GPIO 0-5
- A-E: GPIO 6-10
- CLK: GPIO 11
- LAT: GPIO 12
- OE: GPIO 13

See `examples/interstate75/interstate75_spwm_test.cpp` for a test pattern.

## Bring-up and tuning

1. Run `interstate75_spwm_test` and check the orientation pattern. You should see a red corner top-left, green top-right, blue bottom-left and white bottom-right, inside a white border.
2. If the image is noisy, shifted or flickers, the chip registers probably don't suit your panel. DMD_STM32 collects working register sets for many panels in [`register_test/profiles`](https://github.com/board707/DMD_STM32/tree/dev-V2/register_test/profiles), sorted by chip and scan rate. Pick a profile matching your chip, pitch and row driver, and pass its first (red) array of values:

    ```c++
    const uint16_t regs[] = { 0x021f, 0x0300, /* ... */ };
    hub75.set_config_registers(regs, count_of(regs));
    ```

    Most profiles have different values for the red, green and blue drivers. To use all three, call `set_config_registers(red, green, blue, count)`. `examples/interstate75/sm16380sh_profiles.hpp` contains every SM16380SH 1/32 scan profile. The test example can switch between them with button B.

    You can call this before or after `start()`. If the clocks are running, all registers are sent immediately, which takes about half a second. Register `0x02` (scan lines) is always overwritten to match the panel height.
3. If dark colours flicker or show noise, try a lower `hub75.colour_depth`. The default is 10 bits; DMD_STM32 uses 4.
4. `hub75.brightness` (0-255) scales the greyscale values sent to the panel. It takes effect on the next `update()`.

Each upload briefly disturbs the panel's PWM, so `update()` skips the upload when the image, `brightness` and `colour_depth` are unchanged. `refresh()` always uploads.
