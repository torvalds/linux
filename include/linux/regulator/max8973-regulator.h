/*
 * max8973-regulator.h -- MAXIM 8973 regulator
 *
 * Interface for regulator driver for MAXIM 8973 DC-DC step-down
 * switching regulator.
 *
 * Copyright (C) 2012 NVIDIA Corporation

 * Author: Laxman Dewangan <ldewangan@nvidia.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA	02110-1301, USA.
 *
 */

#ifndef __LINUX_REGULATOR_MAX8973_H
#define __LINUX_REGULATOR_MAX8973_H

/*
 * Control flags for configuration of the device.
 * Client need to pass this information with ORed
 */
#define MAX8973_CONTROL_REMOTE_SENSE_ENABLE			0x00000001
#define MAX8973_CONTROL_FALLING_SLEW_RATE_ENABLE		0x00000002
#define MAX8973_CONTROL_OUTPUT_ACTIVE_DISCH_ENABLE		0x00000004
#define MAX8973_CONTROL_BIAS_ENABLE				0x00000008
#define MAX8973_CONTROL_PULL_DOWN_ENABLE			0x00000010
#define MAX8973_CONTROL_FREQ_SHIFT_9PER_ENABLE			0x00000020

#define MAX8973_CONTROL_CLKADV_TRIP_DISABLED			0x00000000
#define MAX8973_CONTROL_CLKADV_TRIP_75mV_PER_US			0x00010000
#define MAX8973_CONTROL_CLKADV_TRIP_150mV_PER_US		0x00020000
#define MAX8973_CONTROL_CLKADV_TRIP_75mV_PER_US_HIST_DIS	0x00030000

#define MAX8973_CONTROL_INDUCTOR_VALUE_NOMINAL			0x00000000
#define MAX8973_CONTROL_INDUCTOR_VALUE_MINUS_30_PER		0x00100000
#define MAX8973_CONTROL_INDUCTOR_VALUE_PLUS_30_PER		0x00200000
#define MAX8973_CONTROL_INDUCTOR_VALUE_PLUS_60_PER		0x00300000

/*
 * struct max8973_regulator_platform_data - max8973 regulator platform data.
 *
 * @reg_init_data: The regulator init data.
 * @control_flags: Control flags which are ORed value of above flags to
 *		configure device.
 * @junction_temp_warning: Junction temp in millicelcius on which warning need
 *			   to be set. Thermal functionality is only supported on
 *			   MAX77621. The threshold warning supported by MAX77621
 *			   are 120C and 140C.
 * @enable_ext_control: Enable the voltage enable/disable through external
 *		control signal from EN input pin. If it is false then
 *		voltage output will be enabled/disabled through EN bit of
 *		device register.
 * @enable_gpio: Enable GPIO. If EN pin is controlled through GPIO from host
 *		then GPIO number can be provided. If no GPIO controlled then
 *		it should be -1.
 * @dvs_gpio: GPIO for dvs. It should be -1 if this is tied with fixed logic.
 * @dvs_def_state: Default state of dvs. 1 if it is high else 0.
 */
struct max8973_regulator_platform_data {
	struct regulator_init_data *reg_init_data;
	unsigned long control_flags;
	unsigned long junction_temp_warning;
	bool enable_ext_control;
	int enable_gpio;
	int dvs_gpio;
	unsigned dvs_def_state:1;
};

#endif /* __LINUX_REGULATOR_MAX8973_H */
