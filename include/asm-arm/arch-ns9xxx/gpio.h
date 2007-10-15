/*
 * include/asm-arm/arch-ns9xxx/gpio.h
 *
 * Copyright (C) 2007 by Digi International Inc.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
*/
#ifndef __ASM_ARCH_GPIO_H
#define __ASM_ARCH_GPIO_H

#include <asm/errno.h>

int gpio_request(unsigned gpio, const char *label);

void gpio_free(unsigned gpio);

int ns9xxx_gpio_configure(unsigned gpio, int inv, int func);

int gpio_direction_input(unsigned gpio);

int gpio_direction_output(unsigned gpio, int value);

int gpio_get_value(unsigned gpio);

void gpio_set_value(unsigned gpio, int value);

/*
 * ns9xxx can use gpio pins to trigger an irq, but it's not generic
 * enough to be supported by the gpio_to_irq/irq_to_gpio interface
 */
static inline int gpio_to_irq(unsigned gpio)
{
	return -EINVAL;
}

static inline int irq_to_gpio(unsigned irq)
{
	return -EINVAL;
}

/* get the cansleep() stubs */
#include <asm-generic/gpio.h>

#endif /* ifndef __ASM_ARCH_GPIO_H */
