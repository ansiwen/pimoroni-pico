#include "drivers/hub75/hub75_spwm.hpp"
#include "libraries/pico_graphics/pico_graphics.hpp"

#include "micropython/modules/util.hpp"

/*
MicroPython binding for Hub75SPWM, HUB75 panels with S-PWM driver chips.

SPDX-License-Identifier: GPL-3.0-or-later
*/

using namespace pimoroni;

extern "C" {
#include "hub75.h"
#include "py/runtime.h"
#include "micropython/modules/picographics/picographics.h"

// The start of PicoGraphics' object (micropython/modules/picographics/picographics.cpp)
typedef struct _ModPicoGraphics_obj_prefix_t {
    mp_obj_base_t base;
    PicoGraphics *graphics;
} ModPicoGraphics_obj_prefix_t;

typedef struct _Hub75SPWM_obj_t {
    mp_obj_base_t base;
    Hub75SPWM *hub75;
    void *buf;
} _Hub75SPWM_obj_t;

static_assert((int)Hub75SPWM::Lanes::HALVES == LANES_HALVES && (int)Hub75SPWM::Lanes::SINGLE == LANES_SINGLE
    && (int)Hub75SPWM::Lanes::LEFT_RIGHT == LANES_LEFT_RIGHT, "LANES_* must match Hub75SPWM::Lanes");

// The driver can only refresh one panel at a time
static _Hub75SPWM_obj_t *running_obj = nullptr;

static Hub75SPWM::Chip chip_from_panel_type(mp_int_t panel_type) {
    switch(panel_type) {
        case PANEL_SM16380SH: return Hub75SPWM::Chip::SM16380SH;
        case PANEL_ICND2055: return Hub75SPWM::Chip::ICND2055;
        case PANEL_FM6373: return Hub75SPWM::Chip::FM6373;
        default:
            mp_raise_ValueError(MP_ERROR_TEXT("panel_type must be PANEL_SM16380SH, PANEL_ICND2055 or PANEL_FM6373"));
    }
}

void Hub75SPWM_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    (void)kind;
    _Hub75SPWM_obj_t *self = MP_OBJ_TO_PTR2(self_in, _Hub75SPWM_obj_t);
    mp_printf(print, "Hub75SPWM(%u x %u)", self->hub75->width, self->hub75->height);
}

mp_obj_t Hub75SPWM___del__(mp_obj_t self_in) {
    _Hub75SPWM_obj_t *self = MP_OBJ_TO_PTR2(self_in, _Hub75SPWM_obj_t);
    self->hub75->stop();
    if(running_obj == self) running_obj = nullptr;
    m_del_class(Hub75SPWM, self->hub75);
    return mp_const_none;
}

mp_obj_t Hub75SPWM_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *all_args) {
    enum { ARG_width, ARG_height, ARG_buffer, ARG_panel_type, ARG_color_order, ARG_lanes, ARG_row_driver };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_width, MP_ARG_REQUIRED | MP_ARG_INT },
        { MP_QSTR_height, MP_ARG_REQUIRED | MP_ARG_INT },
        { MP_QSTR_buffer, MP_ARG_OBJ, {.u_obj = mp_const_none} },
        { MP_QSTR_panel_type, MP_ARG_INT, {.u_int = PANEL_SM16380SH} },
        { MP_QSTR_color_order, MP_ARG_INT, {.u_int = (uint8_t)Hub75::COLOR_ORDER::RGB} },
        { MP_QSTR_lanes, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = LANES_HALVES} },
        { MP_QSTR_row_driver, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = ROWS_DECODER} },
    };

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, all_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    mp_int_t width = args[ARG_width].u_int;
    mp_int_t height = args[ARG_height].u_int;
    Hub75SPWM::Chip chip = chip_from_panel_type(args[ARG_panel_type].u_int);
    mp_int_t color_order = args[ARG_color_order].u_int;

    if(width <= 0 || width % 16 != 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("width must be a multiple of 16"));
    }
    if(height <= 0 || height % 2 != 0 || height > 64) {
        mp_raise_ValueError(MP_ERROR_TEXT("height must be even and at most 64"));
    }
    if(color_order < 0 || color_order > (mp_int_t)Hub75::COLOR_ORDER::BGR) {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid color_order"));
    }
    mp_int_t lanes = args[ARG_lanes].u_int;
    mp_int_t row_driver = args[ARG_row_driver].u_int;
    if(lanes < LANES_HALVES || lanes > LANES_LEFT_RIGHT) {
        mp_raise_ValueError(MP_ERROR_TEXT("lanes must be LANES_HALVES, LANES_SINGLE or LANES_LEFT_RIGHT"));
    }
    if(lanes == LANES_LEFT_RIGHT && width % 32 != 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("width must be a multiple of 32 for LANES_LEFT_RIGHT"));
    }
    if(row_driver != ROWS_DECODER && row_driver != ROWS_SHIFT_REGISTER) {
        mp_raise_ValueError(MP_ERROR_TEXT("row_driver must be ROWS_DECODER or ROWS_SHIFT_REGISTER"));
    }

    // The frame buffer: width * height words for every layout, the same RAM as Hub75
    size_t buffer_words = width * height;
    uint32_t *buffer = nullptr;
    if(args[ARG_buffer].u_obj != mp_const_none) {
        mp_buffer_info_t bufinfo;
        mp_get_buffer_raise(args[ARG_buffer].u_obj, &bufinfo, MP_BUFFER_RW);
        if(bufinfo.len < buffer_words * sizeof(uint32_t)) {
            mp_raise_ValueError(MP_ERROR_TEXT("Supplied buffer is too small!"));
        }
        if((uintptr_t)bufinfo.buf % sizeof(uint32_t) != 0) {
            mp_raise_ValueError(MP_ERROR_TEXT("Supplied buffer must be word aligned"));
        }
        buffer = (uint32_t *)bufinfo.buf;
    } else {
        buffer = m_new(uint32_t, buffer_words);
    }

    _Hub75SPWM_obj_t *self = mp_obj_malloc_with_finaliser(_Hub75SPWM_obj_t, &Hub75SPWM_type);
    self->buf = buffer;
    self->hub75 = m_new_class(Hub75SPWM, width, height, chip, buffer, (Hub75::COLOR_ORDER)color_order);
    self->hub75->row_driver = row_driver == ROWS_SHIFT_REGISTER ? Hub75SPWM::RowDriver::SHIFT_REGISTER : Hub75SPWM::RowDriver::DECODER;
    // Can't fail: every layout fits in width * height words
    self->hub75->set_lanes((Hub75SPWM::Lanes)lanes);

    return MP_OBJ_FROM_PTR(self);
}

mp_obj_t Hub75SPWM_start(mp_obj_t self_in) {
    _Hub75SPWM_obj_t *self = MP_OBJ_TO_PTR2(self_in, _Hub75SPWM_obj_t);
    if(running_obj != nullptr && running_obj != self) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("another Hub75SPWM is running, stop it first"));
    }
    self->hub75->start();
    running_obj = self;
    return mp_const_none;
}

mp_obj_t Hub75SPWM_stop(mp_obj_t self_in) {
    _Hub75SPWM_obj_t *self = MP_OBJ_TO_PTR2(self_in, _Hub75SPWM_obj_t);
    self->hub75->stop();
    if(running_obj == self) running_obj = nullptr;
    return mp_const_none;
}

mp_obj_t Hub75SPWM_clear(mp_obj_t self_in) {
    _Hub75SPWM_obj_t *self = MP_OBJ_TO_PTR2(self_in, _Hub75SPWM_obj_t);
    self->hub75->clear();
    return mp_const_none;
}

mp_obj_t Hub75SPWM_set_pixel(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_self, ARG_x, ARG_y, ARG_r, ARG_g, ARG_b };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_, MP_ARG_REQUIRED | MP_ARG_OBJ },
        { MP_QSTR_x, MP_ARG_REQUIRED | MP_ARG_INT },
        { MP_QSTR_y, MP_ARG_REQUIRED | MP_ARG_INT },
        { MP_QSTR_r, MP_ARG_REQUIRED | MP_ARG_INT },
        { MP_QSTR_g, MP_ARG_REQUIRED | MP_ARG_INT },
        { MP_QSTR_b, MP_ARG_REQUIRED | MP_ARG_INT }
    };

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    _Hub75SPWM_obj_t *self = MP_OBJ_TO_PTR2(args[ARG_self].u_obj, _Hub75SPWM_obj_t);
    self->hub75->set_pixel(args[ARG_x].u_int, args[ARG_y].u_int,
        args[ARG_r].u_int, args[ARG_g].u_int, args[ARG_b].u_int);
    return mp_const_none;
}

// update(graphics, sync=0)
mp_obj_t Hub75SPWM_update(size_t n_args, const mp_obj_t *args) {
    _Hub75SPWM_obj_t *self = MP_OBJ_TO_PTR2(args[0], _Hub75SPWM_obj_t);
    if(!mp_obj_is_type(args[1], &ModPicoGraphics_type)) {
        mp_raise_TypeError(MP_ERROR_TEXT("expected a PicoGraphics instance"));
    }
    ModPicoGraphics_obj_prefix_t *picographics = (ModPicoGraphics_obj_prefix_t *)MP_OBJ_TO_PTR(args[1]);
    PicoGraphics *graphics = picographics->graphics;
    if(graphics->bounds.w != (int32_t)self->hub75->width || graphics->bounds.h != (int32_t)self->hub75->height) {
        mp_raise_ValueError(MP_ERROR_TEXT("PicoGraphics size doesn't match the panel"));
    }

    mp_int_t sync = n_args > 2 ? mp_obj_get_int(args[2]) : 0;
    if(sync < 0) mp_raise_ValueError(MP_ERROR_TEXT("sync must be 0 or more"));

    // Doesn't release anything: on rp2 there is no GIL, other threads keep running
    self->hub75->update(graphics, sync);
    return mp_const_none;
}

mp_obj_t Hub75SPWM_set_refresh_rate(mp_obj_t self_in, mp_obj_t hz_in) {
    _Hub75SPWM_obj_t *self = MP_OBJ_TO_PTR2(self_in, _Hub75SPWM_obj_t);
    mp_int_t hz = mp_obj_get_int(hz_in);
    if(hz < 10 || hz > 200) mp_raise_ValueError(MP_ERROR_TEXT("refresh rate must be 10-200Hz"));
    self->hub75->set_refresh_rate(hz);
    return mp_const_none;
}

mp_obj_t Hub75SPWM_set_brightness(mp_obj_t self_in, mp_obj_t brightness_in) {
    _Hub75SPWM_obj_t *self = MP_OBJ_TO_PTR2(self_in, _Hub75SPWM_obj_t);
    mp_int_t brightness = mp_obj_get_int(brightness_in);
    if(brightness < 0 || brightness > 255) mp_raise_ValueError(MP_ERROR_TEXT("brightness must be 0-255"));
    self->hub75->brightness = brightness;
    return mp_const_none;
}

mp_obj_t Hub75SPWM_set_colour_depth(mp_obj_t self_in, mp_obj_t bits_in) {
    _Hub75SPWM_obj_t *self = MP_OBJ_TO_PTR2(self_in, _Hub75SPWM_obj_t);
    mp_int_t bits = mp_obj_get_int(bits_in);
    if(bits < 1 || bits > 10) mp_raise_ValueError(MP_ERROR_TEXT("colour depth must be 1-10"));
    self->hub75->colour_depth = bits;
    return mp_const_none;
}

mp_obj_t Hub75SPWM_set_avoid_tearing(mp_obj_t self_in, mp_obj_t avoid_in) {
    _Hub75SPWM_obj_t *self = MP_OBJ_TO_PTR2(self_in, _Hub75SPWM_obj_t);
    self->hub75->avoid_tearing = mp_obj_is_true(avoid_in);
    return mp_const_none;
}

// set_clock_div(div): CLK = system clock / (6 * div), eg. 2 = 12.5MHz (default), 1 = 25MHz at 150MHz
mp_obj_t Hub75SPWM_set_clock_div(mp_obj_t self_in, mp_obj_t div_in) {
    _Hub75SPWM_obj_t *self = MP_OBJ_TO_PTR2(self_in, _Hub75SPWM_obj_t);
    mp_float_t div = mp_obj_get_float(div_in);
    if(div < 1.0f || div > 85.0f) mp_raise_ValueError(MP_ERROR_TEXT("divider must be 1-85"));
    self->hub75->set_clock_div(div);
    return mp_const_none;
}

// Copy a sequence of register values into `regs`, returns the count
static size_t get_registers(mp_obj_t seq_in, uint16_t *regs, size_t max) {
    size_t len;
    mp_obj_t *items;
    mp_obj_get_array(seq_in, &len, &items);
    if(len == 0 || len > max) mp_raise_ValueError(MP_ERROR_TEXT("expected 1-48 register values"));
    for(size_t i = 0; i < len; i++) {
        mp_int_t v = mp_obj_get_int(items[i]);
        if(v < 0 || v > 0xffff) mp_raise_ValueError(MP_ERROR_TEXT("register values must be 16-bit"));
        regs[i] = v;
    }
    return len;
}

// set_config_registers(regs), set_config_registers(red, green, blue) or
// set_config_registers(profile), a profile like hub75.SM16380SH_REG2 being (red, green, blue)
mp_obj_t Hub75SPWM_set_config_registers(size_t n_args, const mp_obj_t *args) {
    _Hub75SPWM_obj_t *self = MP_OBJ_TO_PTR2(args[0], _Hub75SPWM_obj_t);
    const size_t max = 48;
    uint16_t regs[3][max];
    mp_obj_t profile_args[4];
    if(n_args == 2) {
        size_t len;
        mp_obj_t *items;
        mp_obj_get_array(args[1], &len, &items);
        if(len == 3 && !mp_obj_is_int(items[0])) {
            // A profile: (red, green, blue)
            profile_args[0] = args[0];
            profile_args[1] = items[0];
            profile_args[2] = items[1];
            profile_args[3] = items[2];
            args = profile_args;
            n_args = 4;
        }
    }
    if(n_args == 2) {
        size_t count = get_registers(args[1], regs[0], max);
        self->hub75->set_config_registers(regs[0], count);
    } else if(n_args == 4) {
        size_t count = get_registers(args[1], regs[0], max);
        if(get_registers(args[2], regs[1], max) != count || get_registers(args[3], regs[2], max) != count) {
            mp_raise_ValueError(MP_ERROR_TEXT("red, green and blue must have the same length"));
        }
        self->hub75->set_config_registers(regs[0], regs[1], regs[2], count);
    } else {
        mp_raise_TypeError(MP_ERROR_TEXT("expected regs or red, green, blue"));
    }
    return mp_const_none;
}

mp_obj_t Hub75SPWM_load_chip_defaults(mp_obj_t self_in) {
    _Hub75SPWM_obj_t *self = MP_OBJ_TO_PTR2(self_in, _Hub75SPWM_obj_t);
    self->hub75->load_chip_defaults();
    return mp_const_none;
}

// set_gclk_bits(bits): position + 1 of the greyscale MSB in the chips' words, 12-15
mp_obj_t Hub75SPWM_set_gclk_bits(mp_obj_t self_in, mp_obj_t bits_in) {
    _Hub75SPWM_obj_t *self = MP_OBJ_TO_PTR2(self_in, _Hub75SPWM_obj_t);
    mp_int_t bits = mp_obj_get_int(bits_in);
    if(bits < 12 || bits > 15) mp_raise_ValueError(MP_ERROR_TEXT("gclk_bits must be 12-15"));
    self->hub75->set_gclk_bits(bits);
    return mp_const_none;
}

// Timing statistics in microseconds, and the number of torn frames
mp_obj_t Hub75SPWM_stats(mp_obj_t self_in) {
    _Hub75SPWM_obj_t *self = MP_OBJ_TO_PTR2(self_in, _Hub75SPWM_obj_t);
    Hub75SPWM *h = self->hub75;
    mp_obj_t dict = mp_obj_new_dict(5);
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_upload_us), mp_obj_new_int_from_uint(h->upload_us));
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_frame_start_us), mp_obj_new_int_from_uint(h->frame_start_us));
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_update_wait_us), mp_obj_new_int_from_uint(h->update_wait_us));
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_update_convert_us), mp_obj_new_int_from_uint(h->update_convert_us));
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_torn_frames), mp_obj_new_int_from_uint(h->torn_frames));
    return dict;
}

}
