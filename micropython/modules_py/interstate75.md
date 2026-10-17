# Interstate75 (MicroPython) <!-- omit in toc -->

This library offers convenient functions for interacting with [Interstate75](https://shop.pimoroni.com/products/interstate-75) and [Interstate75W](https://shop.pimoroni.com/products/interstate-75-w) - Interstate75 and Interstate75W offer a convenient way and 2 input buttons for all your display and control needs.

## Table of Content
- [Table of Content](#table-of-content)
- [Interstate75 Class](#interstate75-class)
  - [Switches](#switches)
  - [RGB LED](#rgb-led)
- [Display](#display)
- [S-PWM Panels](#s-pwm-panels)



## Interstate75 Class

The `Interstate75` class deals with RGB LED and buttons on the Interstate75 and 75W. To create one, import the `interstate75` module, then define a new `board` variable.


```python
import interstate75

display = interstate75.DISPLAY_INTERSTATE75_32X32

board = interstate75.Interstate75(display=display)
```

The version of Intersate75 you're using should be automatically detected. Check `board.interstate75w` to verify this. It should be `True` on a W and `False` on a non-W.

You can choose the HUB75 matrix display size that you wish to use by defining `display=` as one of the following:

```python
DISPLAY_INTERSTATE75_32X32
DISPLAY_INTERSTATE75_64X32
DISPLAY_INTERSTATE75_96X32
DISPLAY_INTERSTATE75_96X48
DISPLAY_INTERSTATE75_128X32
DISPLAY_INTERSTATE75_64X64
DISPLAY_INTERSTATE75_128X64
DISPLAY_INTERSTATE75_192X64
DISPLAY_INTERSTATE75_256X64
```

### Switches

Interstate75 and 75W have two switches in the front of the board. To read one of the switches, call `.switch_pressed(switch)`, where `switch` is a value from `0` to `.NUM_SWITCHES - 1`. This returns `True` when the specified switch is pressed, and `False` otherwise.

To read a specific input, the `interstate75` module contains these handy constants:

* `SWITCH_A` = `0`
* `SWITCH_B` = `1`

The Interstate75 (non W) uses the boot button instead of `SWITCH_B`

* `SWITCH_A` = `0`
* `SWITCH_BOOT` = `1`

```python
if board.switch_pressed(SWITCH_A):
  # Do something interesting here!

# Either for Interstate 75W
if board.switch_pressed(SWITCH_B):
  # Do something else even more interesting here!

# Or for Interstate 75
if board.switch_pressed(SWITCH_BOOT):
  # Do something else even more interesting here!
```


### RGB LED

The Interstate has an RGB LED. This is accessed via the following method.


`.set_led(r, g, b)`

Where r, g, b are values between 0 and 255


example:

```python
board.set_led(255, 0, 0)  # Makes the LED Red
board.set_led(0, 255, 0)  # Makes the LED Blue
board.set_led(0, 0, 255)  # Makes the LED Green
```

## Display

The display is all handled by our custom picographics drivers they can be accessed via `.display`.

example:

```python
display = board.display
display.text("Hello World!", 0, 0)
display.line(0, 0, 128, 64) 
board.update()  # Update display with the above items
```
All the picographics functions can be found [Here](../modules/picographics/README.md)

## S-PWM Panels

Some newer, finer pitch panels use S-PWM driver chips, such as the SM16380SH. They need a different driver, which `Interstate75` uses when you pass the chip as the `panel_type`:

```python
board = interstate75.Interstate75(display=interstate75.DISPLAY_INTERSTATE75_128X64,
                                  panel_type=interstate75.Interstate75.PANEL_SM16380SH)
```

The supported chips are `PANEL_SM16380SH`, `PANEL_ICND2055` and `PANEL_FM6373`. The panel is refreshed in the background at a steady rate, 60Hz by default, and `board.update()` only converts the image. If you draw at a lower frame rate, set a refresh rate that is a multiple of it and pass `sync` to let the driver time your frames, so scrolling doesn't judder. For 25fps:

```python
board.hub75.set_refresh_rate(50)
while True:
    # draw...
    board.update(sync=2)  # each image is shown for exactly 2 refreshes
```

See [Hub75SPWM](../modules/hub75/README.md#s-pwm-panels-hub75spwm) for all the options.

