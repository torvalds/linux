/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2012 Dialog Semiconductor Ltd.
 */
#ifndef __DA9055_PDATA_H
#define __DA9055_PDATA_H

#define DA9055_MAX_REGULATORS	8

struct da9055;
struct gpio_desc;

enum gpio_select {
	NO_GPIO = 0,
	GPIO_1,
	GPIO_2
};

struct da9055_pdata {
	int (*init) (struct da9055 *da9055);
	int irq_base;
	int gpio_base;

	struct regulator_init_data *regulators[DA9055_MAX_REGULATORS];
	/* Enable RTC in RESET Mode */
	bool reset_enable;
	/*
	 * GPI muxed pin to control
	 * regulator state A/B, 0 if not available.
	 */
	int *gpio_ren;
	/*
	 * GPI muxed pin to control
	 * regulator set, 0 if not available.
	 */
	int *gpio_rsel;
	/*
	 * Regulator mode control bits value (GPI offset) that
	 * controls the regulator state, 0 if not available.
	 */
	enum gpio_select *reg_ren;
	/*
	 * Regulator mode control bits value (GPI offset) that
	 * controls the regulator set A/B, 0 if  not available.
	 */
	enum gpio_select *reg_rsel;
	/* GPIO descriptors to enable regulator, NULL if not available */
	struct gpio_desc **ena_gpiods;
};
#endif /* __DA9055_PDATA_H */
