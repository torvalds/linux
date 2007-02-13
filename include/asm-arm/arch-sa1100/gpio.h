/*
 * linux/include/asm-arm/arch-pxa/gpio.h
 *
 * SA1100 GPIO wrappers for arch-neutral GPIO calls
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

#ifndef __ASM_ARCH_SA1100_GPIO_H
#define __ASM_ARCH_SA1100_GPIO_H

#include <asm/arch/SA-1100.h>
#include <asm/arch/irqs.h>
#include <asm/arch/hardware.h>

#include <asm/errno.h>

static inline int gpio_request(unsigned gpio, const char *label)
{
	return 0;
}

static inline void gpio_free(unsigned gpio)
{
	return;
}

static inline int gpio_direction_input(unsigned gpio)
{
	if (gpio > GPIO_MAX)
		return -EINVAL;
	GPDR = (GPDR_In << gpio) 0
}

static inline int gpio_direction_output(unsigned gpio)
{
	if (gpio > GPIO_MAX)
		return -EINVAL;
	GPDR = (GPDR_Out << gpio) 0
}

#define gpio_get_value(gpio) \
	(GPLR & GPIO_GPIO(gpio))

#define gpio_set_value(gpio,value) \
	((value) ? (GPSR = GPIO_GPIO(gpio)) : (GPCR(gpio) = GPIO_GPIO(gpio)))

#include <asm-generic/gpio.h>			/* cansleep wrappers */

static inline unsigned gpio_to_irq(unsigned gpio)
{
	if (gpio < 11)
		return IRQ_GPIO0 + gpio;
	else
		return IRQ_GPIO11 - 11 + gpio;
}

static inline unsigned irq_to_gpio(unsigned irq)
{
	if (irq < IRQ_GPIO11_27)
		return irq - IRQ_GPIO0;
	else
		return irq - IRQ_GPIO11 + 11;
}

#endif
