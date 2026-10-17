// All SM16380SH 1/32 scan register profiles from DMD_STM32, see
// drivers/hub75/hub75_spwm_profiles.h
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdint.h>
#include "drivers/hub75/hub75_spwm_profiles.h"

struct SPWMProfile {
  unsigned number;      // DMD_STM32's profile number, "REG <number>"
  const char *name;
  uint16_t regs[3][32]; // red, green, blue
};

#define SPWM_PROFILE(n, name) \
  {n, name, {{HUB75_SPWM_SM16380SH_REG##n##_RED}, {HUB75_SPWM_SM16380SH_REG##n##_GREEN}, {HUB75_SPWM_SM16380SH_REG##n##_BLUE}}},

static const SPWMProfile SM16380SH_PROFILES[] = {
  HUB75_SPWM_SM16380SH_PROFILES(SPWM_PROFILE)
};

#undef SPWM_PROFILE
