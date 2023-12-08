/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * tps51632-regulator.h -- TPS51632 regulator
 *
 * Interface for regulator driver for TPS51632 3-2-1 Phase D-Cap Step Down
 * Driverless Controller with serial VID control and DVFS.
 *
 * Copyright (C) 2012 NVIDIA Corporation

 * Author: Laxman Dewangan <ldewangan@nvidia.com>
 */

#ifndef __LINUX_REGULATOR_TPS51632_H
#define __LINUX_REGULATOR_TPS51632_H

/*
 * struct tps51632_regulator_platform_data - tps51632 regulator platform data.
 *
 * @reg_init_data: The regulator init data.
 * @enable_pwm_dvfs: Enable PWM DVFS or not.
 * @dvfs_step_20mV: Step for DVFS is 20mV or 10mV.
 * @max_voltage_uV: Maximum possible voltage in PWM-DVFS mode.
 * @base_voltage_uV: Base voltage when PWM-DVFS enabled.
 */
struct tps51632_regulator_platform_data {
	struct regulator_init_data *reg_init_data;
	bool enable_pwm_dvfs;
	bool dvfs_step_20mV;
	int max_voltage_uV;
	int base_voltage_uV;
};

#endif /* __LINUX_REGULATOR_TPS51632_H */
