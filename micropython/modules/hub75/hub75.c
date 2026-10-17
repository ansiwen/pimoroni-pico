#include "hub75.h"


/***** Methods *****/
MP_DEFINE_CONST_FUN_OBJ_1(Hub75___del___obj, Hub75___del__);
MP_DEFINE_CONST_FUN_OBJ_KW(Hub75_set_pixel_obj, 5, Hub75_set_pixel);
MP_DEFINE_CONST_FUN_OBJ_1(Hub75_clear_obj, Hub75_clear);
MP_DEFINE_CONST_FUN_OBJ_1(Hub75_start_obj, Hub75_start);
MP_DEFINE_CONST_FUN_OBJ_1(Hub75_stop_obj, Hub75_stop);
MP_DEFINE_CONST_FUN_OBJ_2(Hub75_update_obj, Hub75_update);


/***** Binding of Methods *****/
static const mp_rom_map_elem_t Hub75_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR___del__), MP_ROM_PTR(&Hub75___del___obj) },
    { MP_ROM_QSTR(MP_QSTR_set_pixel), MP_ROM_PTR(&Hub75_set_pixel_obj) },
    { MP_ROM_QSTR(MP_QSTR_clear), MP_ROM_PTR(&Hub75_clear_obj) },
    { MP_ROM_QSTR(MP_QSTR_start), MP_ROM_PTR(&Hub75_start_obj) },
    { MP_ROM_QSTR(MP_QSTR_stop), MP_ROM_PTR(&Hub75_stop_obj) },
    { MP_ROM_QSTR(MP_QSTR_update), MP_ROM_PTR(&Hub75_update_obj) },
};

static MP_DEFINE_CONST_DICT(Hub75_locals_dict, Hub75_locals_dict_table);

/***** Class Definition *****/
#ifdef MP_DEFINE_CONST_OBJ_TYPE
MP_DEFINE_CONST_OBJ_TYPE(
    Hub75_type,
    MP_QSTR_hub75,
    MP_TYPE_FLAG_NONE,
    make_new, Hub75_make_new,
    print, Hub75_print,
    locals_dict, (mp_obj_dict_t*)&Hub75_locals_dict
);
#else
const mp_obj_type_t Hub75_type = {
    { &mp_type_type },
    .name = MP_QSTR_hub75,
    .print = Hub75_print,
    .make_new = Hub75_make_new,
    .locals_dict = (mp_obj_dict_t*)&Hub75_locals_dict,
};
#endif

/***** Hub75SPWM *****/
MP_DEFINE_CONST_FUN_OBJ_1(Hub75SPWM___del___obj, Hub75SPWM___del__);
MP_DEFINE_CONST_FUN_OBJ_1(Hub75SPWM_start_obj, Hub75SPWM_start);
MP_DEFINE_CONST_FUN_OBJ_1(Hub75SPWM_stop_obj, Hub75SPWM_stop);
MP_DEFINE_CONST_FUN_OBJ_1(Hub75SPWM_clear_obj, Hub75SPWM_clear);
MP_DEFINE_CONST_FUN_OBJ_KW(Hub75SPWM_set_pixel_obj, 6, Hub75SPWM_set_pixel);
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(Hub75SPWM_update_obj, 2, 3, Hub75SPWM_update);
MP_DEFINE_CONST_FUN_OBJ_2(Hub75SPWM_set_refresh_rate_obj, Hub75SPWM_set_refresh_rate);
MP_DEFINE_CONST_FUN_OBJ_2(Hub75SPWM_set_brightness_obj, Hub75SPWM_set_brightness);
MP_DEFINE_CONST_FUN_OBJ_2(Hub75SPWM_set_colour_depth_obj, Hub75SPWM_set_colour_depth);
MP_DEFINE_CONST_FUN_OBJ_2(Hub75SPWM_set_avoid_tearing_obj, Hub75SPWM_set_avoid_tearing);
MP_DEFINE_CONST_FUN_OBJ_2(Hub75SPWM_set_upload_clock_div_obj, Hub75SPWM_set_upload_clock_div);
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(Hub75SPWM_set_config_registers_obj, 2, 4, Hub75SPWM_set_config_registers);
MP_DEFINE_CONST_FUN_OBJ_1(Hub75SPWM_load_chip_defaults_obj, Hub75SPWM_load_chip_defaults);
MP_DEFINE_CONST_FUN_OBJ_1(Hub75SPWM_stats_obj, Hub75SPWM_stats);

static const mp_rom_map_elem_t Hub75SPWM_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR___del__), MP_ROM_PTR(&Hub75SPWM___del___obj) },
    { MP_ROM_QSTR(MP_QSTR_start), MP_ROM_PTR(&Hub75SPWM_start_obj) },
    { MP_ROM_QSTR(MP_QSTR_stop), MP_ROM_PTR(&Hub75SPWM_stop_obj) },
    { MP_ROM_QSTR(MP_QSTR_clear), MP_ROM_PTR(&Hub75SPWM_clear_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_pixel), MP_ROM_PTR(&Hub75SPWM_set_pixel_obj) },
    { MP_ROM_QSTR(MP_QSTR_update), MP_ROM_PTR(&Hub75SPWM_update_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_refresh_rate), MP_ROM_PTR(&Hub75SPWM_set_refresh_rate_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_brightness), MP_ROM_PTR(&Hub75SPWM_set_brightness_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_colour_depth), MP_ROM_PTR(&Hub75SPWM_set_colour_depth_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_avoid_tearing), MP_ROM_PTR(&Hub75SPWM_set_avoid_tearing_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_upload_clock_div), MP_ROM_PTR(&Hub75SPWM_set_upload_clock_div_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_config_registers), MP_ROM_PTR(&Hub75SPWM_set_config_registers_obj) },
    { MP_ROM_QSTR(MP_QSTR_load_chip_defaults), MP_ROM_PTR(&Hub75SPWM_load_chip_defaults_obj) },
    { MP_ROM_QSTR(MP_QSTR_stats), MP_ROM_PTR(&Hub75SPWM_stats_obj) },
};

static MP_DEFINE_CONST_DICT(Hub75SPWM_locals_dict, Hub75SPWM_locals_dict_table);

#ifdef MP_DEFINE_CONST_OBJ_TYPE
MP_DEFINE_CONST_OBJ_TYPE(
    Hub75SPWM_type,
    MP_QSTR_Hub75SPWM,
    MP_TYPE_FLAG_NONE,
    make_new, Hub75SPWM_make_new,
    print, Hub75SPWM_print,
    locals_dict, (mp_obj_dict_t*)&Hub75SPWM_locals_dict
);
#else
const mp_obj_type_t Hub75SPWM_type = {
    { &mp_type_type },
    .name = MP_QSTR_Hub75SPWM,
    .print = Hub75SPWM_print,
    .make_new = Hub75SPWM_make_new,
    .locals_dict = (mp_obj_dict_t*)&Hub75SPWM_locals_dict,
};
#endif

/***** Globals Table *****/

static const mp_map_elem_t hub75_globals_table[] = {
    { MP_OBJ_NEW_QSTR(MP_QSTR___name__), MP_OBJ_NEW_QSTR(MP_QSTR_hub75) },
    { MP_OBJ_NEW_QSTR(MP_QSTR_Hub75), (mp_obj_t)&Hub75_type },
    { MP_ROM_QSTR(MP_QSTR_PANEL_GENERIC), MP_ROM_INT(0) },
    { MP_ROM_QSTR(MP_QSTR_PANEL_FM6126A), MP_ROM_INT(1) },
    { MP_OBJ_NEW_QSTR(MP_QSTR_Hub75SPWM), (mp_obj_t)&Hub75SPWM_type },
    { MP_ROM_QSTR(MP_QSTR_PANEL_SM16380SH), MP_ROM_INT(PANEL_SM16380SH) },
    { MP_ROM_QSTR(MP_QSTR_PANEL_ICND2055), MP_ROM_INT(PANEL_ICND2055) },
    { MP_ROM_QSTR(MP_QSTR_PANEL_FM6373), MP_ROM_INT(PANEL_FM6373) },
    { MP_ROM_QSTR(MP_QSTR_BUTTON_A), MP_ROM_INT(14) },
    { MP_ROM_QSTR(MP_QSTR_BUTTON_USER), MP_ROM_INT(23) },
    { MP_ROM_QSTR(MP_QSTR_LED_R), MP_ROM_INT(16) },
    { MP_ROM_QSTR(MP_QSTR_LED_G), MP_ROM_INT(17) },
    { MP_ROM_QSTR(MP_QSTR_LED_B), MP_ROM_INT(18) },
    { MP_ROM_QSTR(MP_QSTR_PIN_A0), MP_ROM_INT(26) },
    { MP_ROM_QSTR(MP_QSTR_PIN_A1), MP_ROM_INT(27) },
    { MP_ROM_QSTR(MP_QSTR_PIN_A2), MP_ROM_INT(28) },
    { MP_ROM_QSTR(MP_QSTR_PIN_INT), MP_ROM_INT(19) },
    { MP_ROM_QSTR(MP_QSTR_PIN_SDA), MP_ROM_INT(20) },
    { MP_ROM_QSTR(MP_QSTR_PIN_SCL), MP_ROM_INT(21) },
    { MP_ROM_QSTR(MP_QSTR_CURRENT_SENSE), MP_ROM_INT(29) },

    { MP_ROM_QSTR(MP_QSTR_COLOR_ORDER_RGB), MP_ROM_INT(0x00) },
    { MP_ROM_QSTR(MP_QSTR_COLOR_ORDER_RBG), MP_ROM_INT(0x01) },
    { MP_ROM_QSTR(MP_QSTR_COLOR_ORDER_GRB), MP_ROM_INT(0x02) },
    { MP_ROM_QSTR(MP_QSTR_COLOR_ORDER_GBR), MP_ROM_INT(0x03) },
    { MP_ROM_QSTR(MP_QSTR_COLOR_ORDER_BRG), MP_ROM_INT(0x04) },
    { MP_ROM_QSTR(MP_QSTR_COLOR_ORDER_BGR), MP_ROM_INT(0x05) },
};
static MP_DEFINE_CONST_DICT(mp_module_hub75_globals, hub75_globals_table);

/***** Module Definition *****/
const mp_obj_module_t hub75_user_cmodule = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t*)&mp_module_hub75_globals,
};
#if MICROPY_VERSION <= 70144
MP_REGISTER_MODULE(MP_QSTR_hub75, hub75_user_cmodule, MODULE_HUB75_ENABLED);
#else
MP_REGISTER_MODULE(MP_QSTR_hub75, hub75_user_cmodule);
#endif