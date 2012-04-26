/*
 * PM2301 charger driver.
 *
 * Copyright (C) 2012 ST Ericsson Corporation
 *
 * Contact: Olivier LAUNAY (olivier.launay@stericsson.com
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#ifndef __LINUX_PM2301_H
#define __LINUX_PM2301_H

/**
 * struct pm2xxx_bm_charger_parameters - Charger specific parameters
 * @ac_volt_max:	maximum allowed AC charger voltage in mV
 * @ac_curr_max:	maximum allowed AC charger current in mA
 */
struct pm2xxx_bm_charger_parameters {
	int ac_volt_max;
	int ac_curr_max;
};

/**
 * struct pm2xxx_bm_data - pm2xxx battery management data
 * @enable_overshoot    flag to enable VBAT overshoot control
 * @chg_params	  charger parameters
 */
struct pm2xxx_bm_data {
	bool enable_overshoot;
	const struct pm2xxx_bm_charger_parameters *chg_params;
};

struct pm2xxx_charger_platform_data {
	char **supplied_to;
	size_t num_supplicants;
	int i2c_bus;
	const char *label;
	int irq_number;
	int irq_type;
};

struct pm2xxx_platform_data {
	struct pm2xxx_charger_platform_data *wall_charger;
	struct pm2xxx_bm_data *battery;
};

#endif /* __LINUX_PM2301_H */
