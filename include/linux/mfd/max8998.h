/*
 * max8998.h - Voltage regulator driver for the Maxim 8998
 *
 *  Copyright (C) 2009-2010 Samsung Electrnoics
 *  Kyungmin Park <kyungmin.park@samsung.com>
 *  Marek Szyprowski <m.szyprowski@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef __LINUX_MFD_MAX8998_H
#define __LINUX_MFD_MAX8998_H

#include <linux/regulator/machine.h>

/* MAX 8998 regulator ids */
enum {
	MAX8998_LDO2 = 2,
	MAX8998_LDO3,
	MAX8998_LDO4,
	MAX8998_LDO5,
	MAX8998_LDO6,
	MAX8998_LDO7,
	MAX8998_LDO8,
	MAX8998_LDO9,
	MAX8998_LDO10,
	MAX8998_LDO11,
	MAX8998_LDO12,
	MAX8998_LDO13,
	MAX8998_LDO14,
	MAX8998_LDO15,
	MAX8998_LDO16,
	MAX8998_LDO17,
	MAX8998_BUCK1,
	MAX8998_BUCK2,
	MAX8998_BUCK3,
	MAX8998_BUCK4,
	MAX8998_EN32KHZ_AP,
	MAX8998_EN32KHZ_CP,
	MAX8998_ENVICHG,
	MAX8998_ESAFEOUT1,
	MAX8998_ESAFEOUT2,
};

/**
 * max8998_regulator_data - regulator data
 * @id: regulator id
 * @initdata: regulator init data (contraints, supplies, ...)
 */
struct max8998_regulator_data {
	int				id;
	struct regulator_init_data	*initdata;
};

/**
 * struct max8998_board - packages regulator init data
 * @regulators: array of defined regulators
 * @num_regulators: number of regultors used
 * @irq_base: base IRQ number for max8998, required for IRQs
 * @ono: power onoff IRQ number for max8998
 * @buck_voltage_lock: Do NOT change the values of the following six
 *   registers set by buck?_voltage?. The voltage of BUCK1/2 cannot
 *   be other than the preset values.
 * @buck1_voltage1: BUCK1 DVS mode 1 voltage register
 * @buck1_voltage2: BUCK1 DVS mode 2 voltage register
 * @buck1_voltage3: BUCK1 DVS mode 3 voltage register
 * @buck1_voltage4: BUCK1 DVS mode 4 voltage register
 * @buck2_voltage1: BUCK2 DVS mode 1 voltage register
 * @buck2_voltage2: BUCK2 DVS mode 2 voltage register
 * @buck1_set1: BUCK1 gpio pin 1 to set output voltage
 * @buck1_set2: BUCK1 gpio pin 2 to set output voltage
 * @buck1_default_idx: Default for BUCK1 gpio pin 1, 2
 * @buck2_set3: BUCK2 gpio pin to set output voltage
 * @buck2_default_idx: Default for BUCK2 gpio pin.
 * @wakeup: Allow to wake up from suspend
 * @rtc_delay: LP3974 RTC chip bug that requires delay after a register
 * write before reading it.
 * @eoc: End of Charge Level in percent: 10% ~ 45% by 5% step
 *   If it equals 0, leave it unchanged.
 *   Otherwise, it is a invalid value.
 * @restart: Restart Level in mV: 100, 150, 200, and -1 for disable.
 *   If it equals 0, leave it unchanged.
 *   Otherwise, it is a invalid value.
 * @timeout: Full Timeout in hours: 5, 6, 7, and -1 for disable.
 *   If it equals 0, leave it unchanged.
 *   Otherwise, leave it unchanged.
 */
struct max8998_platform_data {
	struct max8998_regulator_data	*regulators;
	int				num_regulators;
	int				irq_base;
	int				ono;
	bool				buck_voltage_lock;
	int				buck1_voltage1;
	int				buck1_voltage2;
	int				buck1_voltage3;
	int				buck1_voltage4;
	int				buck2_voltage1;
	int				buck2_voltage2;
	int				buck1_set1;
	int				buck1_set2;
	int				buck1_default_idx;
	int				buck2_set3;
	int				buck2_default_idx;
	bool				wakeup;
	bool				rtc_delay;
	int				eoc;
	int				restart;
	int				timeout;
};

#endif /*  __LINUX_MFD_MAX8998_H */
