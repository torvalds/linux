/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * tps62360.h -- TI tps62360
 *
 * Interface for regulator driver for TI TPS62360 Processor core supply
 *
 * Copyright (C) 2012 NVIDIA Corporation

 * Author: Laxman Dewangan <ldewangan@nvidia.com>
 */

#ifndef __LINUX_REGULATOR_TPS62360_H
#define __LINUX_REGULATOR_TPS62360_H

/*
 * struct tps62360_regulator_platform_data - tps62360 regulator platform data.
 *
 * @reg_init_data: The regulator init data.
 * @en_discharge: Enable discharge the output capacitor via internal
 *                register.
 * @en_internal_pulldn: internal pull down enable or not.
 * @vsel0_gpio: Gpio number for vsel0. It should be -1 if this is tied with
 *              fixed logic.
 * @vsel1_gpio: Gpio number for vsel1. It should be -1 if this is tied with
 *              fixed logic.
 * @vsel0_def_state: Default state of vsel0. 1 if it is high else 0.
 * @vsel1_def_state: Default state of vsel1. 1 if it is high else 0.
 */
struct tps62360_regulator_platform_data {
	struct regulator_init_data *reg_init_data;
	bool en_discharge;
	bool en_internal_pulldn;
	int vsel0_gpio;
	int vsel1_gpio;
	int vsel0_def_state;
	int vsel1_def_state;
};

#endif /* __LINUX_REGULATOR_TPS62360_H */
