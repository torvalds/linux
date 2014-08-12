/* include/linux/regulator/charge-regulator.h
 *
 * Copyright (C) 2010 ROCKCHIP, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#ifndef __LINUX_REGULATOR_PWM_H

#define __LINUX_REGULATOR_PWM_H

#include <linux/regulator/machine.h>
/*#include <plat/pwm.h>*/

#define PWM_DIV              PWM_DIV2


struct regulator_init_data;

struct pwm_platform_data {
	int	pwm_id;
	int 	pwm_gpio;
	struct pwm_device	*pwm;
	unsigned int		period;
	unsigned int pwm_period_ns;
	unsigned int		scale;
	unsigned int 	pwm_voltage;
	unsigned int	suspend_voltage;
	unsigned int	coefficient;
	unsigned int	min_uV;
	unsigned int	max_uV;
	unsigned int 	*pwm_voltage_map;
	struct regulator_init_data *init_data;
	int num_regulators;
	struct regulator_dev **rdev;
	int pwm_vol_map_count;
	struct mutex mutex_pwm;
};

struct pwm_regulator_board {
	int pwm_gpio;
	struct pwm_device	*pwm;
	struct regulator_init_data *pwm_init_data[4];
	struct device_node *of_node[4];
	int	pwm_id;
	unsigned int *pwm_voltage_map;
	unsigned int pwm_init_vol;
	unsigned int pwm_max_vol;
	unsigned int pwm_min_vol;
	unsigned int pwm_suspend_vol;
	unsigned int pwm_coefficient;
	int num_regulators;
	struct regulator_dev **rdev;
	int pwm_vol_map_count;
};

#endif

