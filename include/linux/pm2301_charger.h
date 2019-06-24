/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * PM2301 charger driver.
 *
 * Copyright (C) 2012 ST Ericsson Corporation
 *
 * Contact: Olivier LAUNAY (olivier.launay@stericsson.com
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
	int gpio_irq_number;
	unsigned int lpn_gpio;
	int irq_type;
};

struct pm2xxx_platform_data {
	struct pm2xxx_charger_platform_data *wall_charger;
	struct pm2xxx_bm_data *battery;
};

#endif /* __LINUX_PM2301_H */
