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
	bool reset_enable;		/* Enable RTC in RESET Mode */
	enum gpio_select *gpio_rsel;	/* Select regulator set thru GPIO 1/2 */
	enum gpio_select *gpio_ren;	/* Enable regulator thru GPIO 1/2 */
};
#endif /* __DA9055_PDATA_H */
