/*
 * Renesas R-Car GPIO Support
 *
 *  Copyright (C) 2013 Magnus Damm
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __GPIO_RCAR_H__
#define __GPIO_RCAR_H__

struct gpio_rcar_config {
	int gpio_base;
	unsigned int irq_base;
	unsigned int number_of_pins;
	const char *pctl_name;
	unsigned has_both_edge_trigger:1;
};

#define RCAR_GP_PIN(bank, pin)		(((bank) * 32) + (pin))

#endif /* __GPIO_RCAR_H__ */
