// FM6373 register profiles from DMD_STM32, see drivers/hub75/hub75_spwm_profiles.h
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdint.h>
#include "drivers/hub75/hub75_spwm_profiles.h"

struct FM6373Profile {
  unsigned number;      // DMD_STM32's profile number, "REG <number>"
  unsigned count;       // register values per colour
  const char *name;
  uint16_t regs[3][47]; // red, green, blue
};

#define FM6373_PROFILE(n, count, name) \
  {n, count, name, {{HUB75_SPWM_FM6373_REG##n##_RED}, {HUB75_SPWM_FM6373_REG##n##_GREEN}, {HUB75_SPWM_FM6373_REG##n##_BLUE}}},

static const FM6373Profile FM6373_PROFILES[] = {
  HUB75_SPWM_FM6373_PROFILES(FM6373_PROFILE)
};

#undef FM6373_PROFILE
