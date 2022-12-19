/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * OF helpers for the GPIO API
 *
 * Copyright (c) 2007-2008  MontaVista Software, Inc.
 *
 * Author: Anton Vorontsov <avorontsov@ru.mvista.com>
 */

#ifndef __LINUX_OF_GPIO_H
#define __LINUX_OF_GPIO_H

#include <linux/compiler.h>
#include <linux/gpio/driver.h>
#include <linux/gpio.h>		/* FIXME: Shouldn't be here */
#include <linux/of.h>

struct device_node;

/*
 * This is Linux-specific flags. By default controllers' and Linux' mapping
 * match, but GPIO controllers are free to translate their own flags to
 * Linux-specific in their .xlate callback. Though, 1:1 mapping is recommended.
 */
enum of_gpio_flags {
	OF_GPIO_ACTIVE_LOW = 0x1,
	OF_GPIO_SINGLE_ENDED = 0x2,
	OF_GPIO_OPEN_DRAIN = 0x4,
	OF_GPIO_TRANSITORY = 0x8,
	OF_GPIO_PULL_UP = 0x10,
	OF_GPIO_PULL_DOWN = 0x20,
	OF_GPIO_PULL_DISABLE = 0x40,
};

#ifdef CONFIG_OF_GPIO

#include <linux/container_of.h>

/*
 * OF GPIO chip for memory mapped banks
 */
struct of_mm_gpio_chip {
	struct gpio_chip gc;
	void (*save_regs)(struct of_mm_gpio_chip *mm_gc);
	void __iomem *regs;
};

static inline struct of_mm_gpio_chip *to_of_mm_gpio_chip(struct gpio_chip *gc)
{
	return container_of(gc, struct of_mm_gpio_chip, gc);
}

extern int of_get_named_gpio(const struct device_node *np,
			     const char *list_name, int index);

extern int of_mm_gpiochip_add_data(struct device_node *np,
				   struct of_mm_gpio_chip *mm_gc,
				   void *data);
static inline int of_mm_gpiochip_add(struct device_node *np,
				     struct of_mm_gpio_chip *mm_gc)
{
	return of_mm_gpiochip_add_data(np, mm_gc, NULL);
}
extern void of_mm_gpiochip_remove(struct of_mm_gpio_chip *mm_gc);

#else /* CONFIG_OF_GPIO */

#include <linux/errno.h>

/* Drivers may not strictly depend on the GPIO support, so let them link. */
static inline int of_get_named_gpio(const struct device_node *np,
                                   const char *propname, int index)
{
	return -ENOSYS;
}

#endif /* CONFIG_OF_GPIO */

#endif /* __LINUX_OF_GPIO_H */
