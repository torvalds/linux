/*
 * OF helpers for the GPIO API
 *
 * Copyright (c) 2007-2008  MontaVista Software, Inc.
 *
 * Author: Anton Vorontsov <avorontsov@ru.mvista.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __LINUX_OF_GPIO_H
#define __LINUX_OF_GPIO_H

#include <linux/errno.h>
#include <linux/gpio.h>

#ifdef CONFIG_OF_GPIO

/*
 * Generic OF GPIO chip
 */
struct of_gpio_chip {
	struct gpio_chip gc;
	int gpio_cells;
	int (*xlate)(struct of_gpio_chip *of_gc, struct device_node *np,
		     const void *gpio_spec);
};

static inline struct of_gpio_chip *to_of_gpio_chip(struct gpio_chip *gc)
{
	return container_of(gc, struct of_gpio_chip, gc);
}

/*
 * OF GPIO chip for memory mapped banks
 */
struct of_mm_gpio_chip {
	struct of_gpio_chip of_gc;
	void (*save_regs)(struct of_mm_gpio_chip *mm_gc);
	void __iomem *regs;
};

static inline struct of_mm_gpio_chip *to_of_mm_gpio_chip(struct gpio_chip *gc)
{
	struct of_gpio_chip *of_gc = to_of_gpio_chip(gc);

	return container_of(of_gc, struct of_mm_gpio_chip, of_gc);
}

extern int of_get_gpio(struct device_node *np, int index);
extern int of_mm_gpiochip_add(struct device_node *np,
			      struct of_mm_gpio_chip *mm_gc);
extern int of_gpio_simple_xlate(struct of_gpio_chip *of_gc,
				struct device_node *np,
				const void *gpio_spec);
#else

/* Drivers may not strictly depend on the GPIO support, so let them link. */
static inline int of_get_gpio(struct device_node *np, int index)
{
	return -ENOSYS;
}

#endif /* CONFIG_OF_GPIO */

#endif /* __LINUX_OF_GPIO_H */
