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


#define PWM_REG_CNTR         	0x00
#define PWM_REG_HRC          	0x04 
#define PWM_REG_LRC          	0x08
#define PWM_REG_CTRL         	0x0c

#define PWM_DIV2            (0<<9)
#define PWM_DIV4            (1<<9)
#define PWM_DIV8            (2<<9)
#define PWM_DIV16           (3<<9)
#define PWM_DIV32           (4<<9)
#define PWM_DIV64           (5<<9)
#define PWM_DIV128          (6<<9)
#define PWM_DIV256          (7<<9)
#define PWM_DIV512          (8<<9)
#define PWM_DIV1024         (9<<9)

#define PWM_CAPTURE         (1<<8)
#define PWM_RESET           (1<<7)
#define PWM_INTCLR          (1<<6)
#define PWM_INTEN           (1<<5)
#define PWM_SINGLE          (1<<6)

#define PWM_ENABLE          (1<<3)
#define PWM_TimeEN          (1)

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
	int	duty_cycle;
	int	min_uV;
	int	max_uV;
	int	*pwm_voltage_map;
	struct regulator_init_data *init_data;
};

#endif

