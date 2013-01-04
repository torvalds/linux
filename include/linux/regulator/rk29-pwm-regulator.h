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
#include <plat/pwm.h>

#define PWM_DIV              PWM_DIV2


struct regulator_init_data;

struct pwm_platform_data {
	int	pwm_id;
	int 	pwm_gpio;
	//char	pwm_iomux_name[50];
	char*	pwm_iomux_name;
	unsigned int 	pwm_iomux_pwm;
	int 	pwm_iomux_gpio;
	int 	pwm_voltage;
	int	suspend_voltage;
	int	coefficient;
	int	min_uV;
	int	max_uV;
	const int	*pwm_voltage_map;
	struct regulator_init_data *init_data;
};

#endif

