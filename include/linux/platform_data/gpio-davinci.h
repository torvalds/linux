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

#define MAX_REGS_BANKS		5
#define MAX_INT_PER_BANK 32

struct davinci_gpio_platform_data {
	u32	ngpio;
	u32	gpio_unbanked;
};

struct davinci_gpio_irq_data {
	void __iomem			*regs;
	struct davinci_gpio_controller	*chip;
	int				bank_num;
};

struct davinci_gpio_controller {
	struct gpio_chip	chip;
	struct irq_domain	*irq_domain;
	/* Serialize access to GPIO registers */
	spinlock_t		lock;
	void __iomem		*regs[MAX_REGS_BANKS];
	int			gpio_unbanked;
	int			irqs[MAX_INT_PER_BANK];
	unsigned int		base;
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
