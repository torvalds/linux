/* SPDX-License-Identifier: (GPL-2.0 OR MIT) */
/*
 * Microsemi Ocelot Switch driver
 *
 * Copyright (c) 2021 Innovative Advantage Inc.
 */

#ifndef VSC7514_REGS_H
#define VSC7514_REGS_H

#include <soc/mscc/ocelot_vcap.h>

extern struct vcap_props vsc7514_vcap_props[];

extern const struct reg_field vsc7514_regfields[REGFIELD_MAX];

extern const u32 *vsc7514_regmap[TARGET_MAX];

#endif
