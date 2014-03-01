/*
 * DaVinci GPIO Platform Related Defines
 *
 * Copyright (C) 2013 Texas Instruments Incorporated - http://www.ti.com/
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __DAVINCI_GPIO_PLATFORM_H
#define __DAVINCI_GPIO_PLATFORM_H

#include <linux/io.h>
#include <linux/spinlock.h>

#include <asm-generic/gpio.h>

enum davinci_gpio_type {
	GPIO_TYPE_TNETV107X = 0,
};

struct davinci_gpio_platform_data {
	u32	ngpio;
	u32	gpio_unbanked;
};


struct davinci_gpio_controller {
	struct gpio_chip	chip;
	struct irq_domain	*irq_domain;
	/* Serialize access to GPIO registers */
	spinlock_t		lock;
	void __iomem		*regs;
	void __iomem		*set_data;
	void __iomem		*clr_data;
	void __iomem		*in_data;
	int			gpio_unbanked;
	unsigned		gpio_irq;
};

/*
 * basic gpio routines
 */
#define	GPIO(X)		(X)	/* 0 <= X <= (DAVINCI_N_GPIO - 1) */

/* Convert GPIO signal to GPIO pin number */
#define GPIO_TO_PIN(bank, gpio)	(16 * (bank) + (gpio))

static inline u32 __gpio_mask(unsigned gpio)
{
	return 1 << (gpio % 32);
}
#endif
