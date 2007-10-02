/*
 * linux/include/asm-arm/arch-pxa/gpio.h
 *
 * PXA GPIO wrappers for arch-neutral GPIO calls
 *
 * Written by Philipp Zabel <philipp.zabel@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */

#ifndef __ASM_ARCH_PXA_GPIO_H
#define __ASM_ARCH_PXA_GPIO_H

#include <asm/arch/pxa-regs.h>
#include <asm/irq.h>
#include <asm/hardware.h>

static inline int gpio_request(unsigned gpio, const char *label)
{
	return 0;
}

static inline void gpio_free(unsigned gpio)
{
	return;
}

extern int gpio_direction_input(unsigned gpio);
extern int gpio_direction_output(unsigned gpio, int value);

static inline int __gpio_get_value(unsigned gpio)
{
	return GPLR(gpio) & GPIO_bit(gpio);
}

#define gpio_get_value(gpio)			\
	(__builtin_constant_p(gpio) ?		\
	 __gpio_get_value(gpio) :		\
	 pxa_gpio_get_value(gpio))

static inline void __gpio_set_value(unsigned gpio, int value)
{
	if (value)
		GPSR(gpio) = GPIO_bit(gpio);
	else
		GPCR(gpio) = GPIO_bit(gpio);
}

#define gpio_set_value(gpio,value)		\
	(__builtin_constant_p(gpio) ?		\
	 __gpio_set_value(gpio, value) :	\
	 pxa_gpio_set_value(gpio, value))

#include <asm-generic/gpio.h>			/* cansleep wrappers */

#define gpio_to_irq(gpio)	IRQ_GPIO(gpio)
#define irq_to_gpio(irq)	IRQ_TO_GPIO(irq)


#endif
