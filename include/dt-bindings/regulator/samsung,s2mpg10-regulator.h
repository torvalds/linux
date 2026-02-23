/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * Copyright 2021 Google LLC
 * Copyright 2025 Linaro Ltd.
 *
 * Device Tree binding constants for the Samsung S2MPG1x PMIC regulators
 */

#ifndef _DT_BINDINGS_REGULATOR_SAMSUNG_S2MPG10_H
#define _DT_BINDINGS_REGULATOR_SAMSUNG_S2MPG10_H

/*
 * Several regulators may be controlled via external signals instead of via
 * software. These constants describe the possible signals for such regulators
 * and generally correspond to the respecitve on-chip pins.
 *
 * S2MPG10 regulators supporting these are:
 * - buck1m .. buck7m buck10m
 * - ldo3m .. ldo19m
 *
 * ldo20m supports external control, but using a different set of control
 * signals.
 *
 * S2MPG11 regulators supporting these are:
 * - buck1s .. buck3s buck5s buck8s buck9s bucka buckd
 * - ldo1s ldo2s ldo8s ldo13s
 */
#define S2MPG10_EXTCTRL_PWREN       0 /* PWREN pin */
#define S2MPG10_EXTCTRL_PWREN_MIF   1 /* PWREN_MIF pin */
#define S2MPG10_EXTCTRL_AP_ACTIVE_N 2 /* ~AP_ACTIVE_N pin */
#define S2MPG10_EXTCTRL_CPUCL1_EN   3 /* CPUCL1_EN pin */
#define S2MPG10_EXTCTRL_CPUCL1_EN2  4 /* CPUCL1_EN & PWREN pins */
#define S2MPG10_EXTCTRL_CPUCL2_EN   5 /* CPUCL2_EN pin */
#define S2MPG10_EXTCTRL_CPUCL2_EN2  6 /* CPUCL2_E2 & PWREN pins */
#define S2MPG10_EXTCTRL_TPU_EN      7 /* TPU_EN pin */
#define S2MPG10_EXTCTRL_TPU_EN2     8 /* TPU_EN & ~AP_ACTIVE_N pins */
#define S2MPG10_EXTCTRL_TCXO_ON     9 /* TCXO_ON pin */
#define S2MPG10_EXTCTRL_TCXO_ON2    10 /* TCXO_ON & ~AP_ACTIVE_N pins */

#define S2MPG10_EXTCTRL_LDO20M_EN2  11 /* VLDO20M_EN & LDO20M_SFR */
#define S2MPG10_EXTCTRL_LDO20M_EN   12 /* VLDO20M_EN pin */

#define S2MPG11_EXTCTRL_PWREN       0 /* PWREN pin */
#define S2MPG11_EXTCTRL_PWREN_MIF   1 /* PWREN_MIF pin */
#define S2MPG11_EXTCTRL_AP_ACTIVE_N 2 /* ~AP_ACTIVE_N pin */
#define S2MPG11_EXTCTRL_G3D_EN      3 /* G3D_EN pin */
#define S2MPG11_EXTCTRL_G3D_EN2     4 /* G3D_EN & ~AP_ACTIVE_N pins */
#define S2MPG11_EXTCTRL_AOC_VDD     5 /* AOC_VDD pin */
#define S2MPG11_EXTCTRL_AOC_RET     6 /* AOC_RET pin */
#define S2MPG11_EXTCTRL_UFS_EN      7 /* UFS_EN pin */
#define S2MPG11_EXTCTRL_LDO13S_EN   8 /* VLDO13S_EN pin */

#endif /* _DT_BINDINGS_REGULATOR_SAMSUNG_S2MPG10_H */
