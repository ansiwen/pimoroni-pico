# HUB75 <!-- omit in toc -->

This library can be used with the Interstate 75 and 75W bypasses the use of picographics and can be used for situations where RAM is constrained or for custom display configurations like 2x2 HUB75 panels.

For most cases we recommend using the picographics based module for Interstate 75 and 75W as it contains a lot of helper functions to draw text and shapes, further information on its usage can be found here: [Interstate75](../../modules_py/interstate75.md) 

The Interstate 75 library is intended for the Interstate 75 and Interstate 75 W "HUB75" matrix panel driver board.

It can, in theory, be used with your own custom wiring, though custom pin assignments are not supported yet.

- [Notes On PIO \& DMA Limitations](#notes-on-pio--dma-limitations)
- [Getting Started](#getting-started)
  - [FM6216A Panels](#fm6216a-panels)
  - [Setting Colour Order](#setting-colour-order)
- [Quick Reference](#quick-reference)
  - [Set A Pixel](#set-a-pixel)
  - [Clear the display](#clear-the-display)
- [S-PWM Panels (Hub75SPWM)](#s-pwm-panels-hub75spwm)
  - [Timing your frames](#timing-your-frames)
  - [Tuning](#tuning)
  - [Statistics](#statistics)

## Notes On PIO & DMA Limitations

The Hub 75 driver uses the PIO hardware on the RP2040. There are only two PIOs with four state machines each, and hub75 uses one of these (`PIO0`) and two state machines- one for clocking out pixels, and another for latching/lighting a row.

It also uses two DMA channels, one to copy pixel data from the back buffer back to the front buffer and one to supply the row driving PIO with row data.

## Getting Started

Construct a new `Hub75` instance, specifying the width/height of the display and any additional options.

```python
import hub75

WIDTH = 64
HEIGHT = 64

matrix = hub75.Hub75(WIDTH, HEIGHT, stb_invert=True)
```

Use `stb_invert` if you see a missing middle row corruption on the top row.

Start the matrix strip by calling start. This sets up DMA and PIO to drive your panel, pulling rows from the back buffer and refreshing as fast as it can.

```python
matrix.start()
```

### FM6216A Panels

Some panels - based on the FM6126A chips - require a couple of register settings for them to display anything at all. Interstate 75 will set these for you if you specify `panel_type=hub75.PANEL_FM6126A`. Eg:

```python
import hub75

WIDTH = 64
HEIGHT = 64

matrix = hub75.Hub75(WIDTH, HEIGHT, panel_type=hub75.PANEL_FM6126A)
```

### Setting Colour Order

Some hub 75 panels have varying colour orders. A keyword argument is supplied to configure this:

```python
matrix = hub75.Hub75(WIDTH, HEIGHT, color_order=hub75.COLOR_ORDER_RBG)
```

The available orders are defined as constants in `hub75`:

* `COLOR_ORDER_RGB`
* `COLOR_ORDER_RBG`
* `COLOR_ORDER_GRB`
* `COLOR_ORDER_GBR`
* `COLOR_ORDER_BRG`
* `COLOR_ORDER_BGR`

## Quick Reference

### Set A Pixel

You can set the colour of a pixel using RGB values. This will instantly update the pixel on the matrix display.

Set the top left-most LED - `0, 0` - to Purple `255, 0, 255`:

```python
matrix.set_pixel(0, 0, 255, 0, 255)
```

### Clear the display

Calling `.clear()` will clear the whole contents of the display

```python
matrix.clear()
```

## S-PWM Panels (Hub75SPWM)

Some newer, finer pitch panels use S-PWM driver chips that keep the frame in their own memory and generate the PWM themselves. `Hub75SPWM` drives them. Look at the larger chips on the back of the panel and pick the matching `panel_type`:

* `PANEL_SM16380SH`
* `PANEL_ICND2055` (untested)
* `PANEL_FM6373` (untested)

```python
import hub75
from picographics import PicoGraphics, DISPLAY_INTERSTATE75_128X64, PEN_RGB888

graphics = PicoGraphics(display=DISPLAY_INTERSTATE75_128X64, pen_type=PEN_RGB888)
matrix = hub75.Hub75SPWM(128, 64, panel_type=hub75.PANEL_SM16380SH)
matrix.start()

# draw with graphics...
matrix.update(graphics)
```

The width must be a multiple of 16. `color_order` works as for `Hub75`, and `buffer` can be a `bytearray` of `width * height * 4` bytes, the same as `Hub75`. Only one `Hub75SPWM` can run at a time. It uses 5 PIO state machines in two PIOs, and one DMA channel.

`start()` refreshes the panel at a steady 60Hz in the background, and `update(graphics)` converts the image, which takes ~2ms for 128x64. If a frame is being sent, `update()` first waits for the part it converts to be sent, ~2.6ms at most, so a frame never shows half of an update. `update()` can be called from a thread on the other core, so drawing doesn't hold up the rest of your program.

### Timing your frames

The panel shows each image for a whole number of refreshes. If you draw at 25fps while the panel refreshes at 60Hz, images are shown for 2 or 3 refreshes, and scrolling judders. Instead, set a refresh rate that is a multiple of your frame rate, and let `update()` time your frames with `sync`: it waits until `sync` refreshes have been shown since the last `update()` with `sync`, then converts the image between two refreshes:

```python
matrix.set_refresh_rate(50)
matrix.start()
while True:
    # draw with graphics...
    matrix.update(graphics, 2)  # each image is shown for exactly 2 refreshes, 25fps
```

### Tuning

* `set_config_registers(regs)` or `set_config_registers(red, green, blue)`: replace the chip's configuration registers with a profile that suits your panel, eg. from [DMD_STM32's register_test/profiles](https://github.com/board707/DMD_STM32/tree/dev-V2/register_test/profiles). They are sent one per refresh, so all of them are sent within a second. `load_chip_defaults()` restores the built-in ones.
* `set_brightness(0-255)` and `set_colour_depth(1-10)`: take effect with the next `update()`.
* `set_avoid_tearing(False)`: never wait for the frame being sent, like `Hub75`. The frame being sent can then show the old image above a line and the new one below it.
* `set_upload_clock_div(div)`: slow down the frame upload if the image is noisy, 1 (25MHz) by default.
* `set_refresh_rate(hz)`: 60Hz by default.

### Statistics

`stats()` returns a dictionary with the timings of the last frame and `update()` in microseconds, and `torn_frames`: the number of frames where an upload overtook `update()`, which should stay at 0. If it doesn't, use `sync` or a higher `set_upload_clock_div()`.

```python
>>> matrix.stats()
{'upload_us': 2615, 'frame_start_us': 280, 'update_wait_us': 0, 'update_convert_us': 1934, 'torn_frames': 0}
```

