/* SPDX-License-Identifier: (GPL-2.0 OR MIT) */
/*
 * Microsemi Ocelot Switch driver
 *
 * Copyright (c) 2021 Innovative Advantage Inc.
 */

#ifndef VSC7514_REGS_H
#define VSC7514_REGS_H

#include <soc/mscc/ocelot_vcap.h>

extern const u32 vsc7514_ana_regmap[];
extern const u32 vsc7514_qs_regmap[];
extern const u32 vsc7514_qsys_regmap[];
extern const u32 vsc7514_rew_regmap[];
extern const u32 vsc7514_sys_regmap[];
extern const u32 vsc7514_vcap_regmap[];
extern const u32 vsc7514_ptp_regmap[];
extern const u32 vsc7514_dev_gmii_regmap[];

extern const struct vcap_field vsc7514_vcap_es0_keys[];
extern const struct vcap_field vsc7514_vcap_es0_actions[];
extern const struct vcap_field vsc7514_vcap_is1_keys[];
extern const struct vcap_field vsc7514_vcap_is1_actions[];
extern const struct vcap_field vsc7514_vcap_is2_keys[];
extern const struct vcap_field vsc7514_vcap_is2_actions[];

#endif
