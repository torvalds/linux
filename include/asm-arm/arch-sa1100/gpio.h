/*
 * linux/include/asm-arm/arch-sa1100/gpio.h
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

#include <asm/hardware.h>
#include <asm/irq.h>
#include <asm-generic/gpio.h>

static inline int gpio_get_value(unsigned gpio)
{
	if (__builtin_constant_p(gpio) && (gpio <= GPIO_MAX))
		return GPLR & GPIO_GPIO(gpio);
	else
		return __gpio_get_value(gpio);
}

static inline void gpio_set_value(unsigned gpio, int value)
{
	if (__builtin_constant_p(gpio) && (gpio <= GPIO_MAX))
		if (value)
			GPSR = GPIO_GPIO(gpio);
		else
			GPCR = GPIO_GPIO(gpio);
	else
		__gpio_set_value(gpio, value);
}

#define gpio_cansleep	__gpio_cansleep

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
