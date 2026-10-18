// Include MicroPython API.
#include "py/runtime.h"
#include "py/objstr.h"

/***** Extern of Class Definition *****/
extern const mp_obj_type_t Hub75_type;

/***** Extern of Class Methods *****/
extern void Hub75_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind);
extern mp_obj_t Hub75_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *all_args);
extern mp_obj_t Hub75___del__(mp_obj_t self_in);
extern mp_obj_t Hub75_start(mp_obj_t self_in);
extern mp_obj_t Hub75_stop(mp_obj_t self_in);
extern mp_obj_t Hub75_set_pixel(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args);
extern mp_obj_t Hub75_clear(mp_obj_t self_in);
extern mp_obj_t Hub75_update(mp_obj_t self_in, mp_obj_t graphics_in);

/***** Hub75SPWM: panels with S-PWM driver chips *****/

// panel_type values, continuing PANEL_GENERIC (0) and PANEL_FM6126A (1)
enum {
    PANEL_SM16380SH = 2,
    PANEL_ICND2055 = 3,
    PANEL_FM6373 = 4,
};

// lanes and row_driver values, as Hub75SPWM::Lanes and RowDriver
enum {
    LANES_HALVES = 0,
    LANES_SINGLE = 1,
    LANES_LEFT_RIGHT = 2,
};
enum {
    ROWS_DECODER = 0,
    ROWS_SHIFT_REGISTER = 1,
};

extern const mp_obj_type_t Hub75SPWM_type;

extern void Hub75SPWM_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind);
extern mp_obj_t Hub75SPWM_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *all_args);
extern mp_obj_t Hub75SPWM___del__(mp_obj_t self_in);
extern mp_obj_t Hub75SPWM_start(mp_obj_t self_in);
extern mp_obj_t Hub75SPWM_stop(mp_obj_t self_in);
extern mp_obj_t Hub75SPWM_clear(mp_obj_t self_in);
extern mp_obj_t Hub75SPWM_set_pixel(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args);
extern mp_obj_t Hub75SPWM_update(size_t n_args, const mp_obj_t *args);
extern mp_obj_t Hub75SPWM_set_refresh_rate(mp_obj_t self_in, mp_obj_t hz_in);
extern mp_obj_t Hub75SPWM_set_brightness(mp_obj_t self_in, mp_obj_t brightness_in);
extern mp_obj_t Hub75SPWM_set_colour_depth(mp_obj_t self_in, mp_obj_t bits_in);
extern mp_obj_t Hub75SPWM_set_avoid_tearing(mp_obj_t self_in, mp_obj_t avoid_in);
extern mp_obj_t Hub75SPWM_set_clock_div(mp_obj_t self_in, mp_obj_t div_in);
extern mp_obj_t Hub75SPWM_set_config_registers(size_t n_args, const mp_obj_t *args);
extern mp_obj_t Hub75SPWM_load_chip_defaults(mp_obj_t self_in);
extern mp_obj_t Hub75SPWM_set_gclk_bits(mp_obj_t self_in, mp_obj_t bits_in);
extern mp_obj_t Hub75SPWM_stats(mp_obj_t self_in);
