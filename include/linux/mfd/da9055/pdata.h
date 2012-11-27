/* Copyright (C) 2012 Dialog Semiconductor Ltd.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 */
#ifndef __DA9055_PDATA_H
#define __DA9055_PDATA_H

#define DA9055_MAX_REGULATORS	8

struct da9055;

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
	 * that controls the regulator state, 0 if not available.
	 */
	enum gpio_select *reg_ren;
	/*
	 * Regulator mode control bits value (GPI offset) that
	 * controls the regulator set A/B, 0 if  not available.
	 */
	enum gpio_select *reg_rsel;
	/* GPIOs to enable regulator, 0 if not available */
	int *ena_gpio;
};
#endif /* __DA9055_PDATA_H */
