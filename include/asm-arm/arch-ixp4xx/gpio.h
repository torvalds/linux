/*
 * linux/include/asm-arm/arch-ixp4xx/gpio.h
 *
 * IXP4XX GPIO wrappers for arch-neutral GPIO calls
 *
 * Written by Milan Svoboda <msvoboda@ra.rockwell.com>
 * Based on PXA implementation by Philipp Zabel <philipp.zabel@gmail.com>
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

#ifndef __ASM_ARCH_IXP4XX_GPIO_H
#define __ASM_ARCH_IXP4XX_GPIO_H

#include <asm/hardware.h>

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
	gpio_line_config(gpio, IXP4XX_GPIO_IN);
	return 0;
}

static inline int gpio_direction_output(unsigned gpio, int level)
{
	gpio_line_set(gpio, level);
	gpio_line_config(gpio, IXP4XX_GPIO_OUT);
	return 0;
}

static inline int gpio_get_value(unsigned gpio)
{
	int value;

	gpio_line_get(gpio, &value);

	return value;
}

static inline void gpio_set_value(unsigned gpio, int value)
{
	gpio_line_set(gpio, value);
}

#include <asm-generic/gpio.h>			/* cansleep wrappers */

extern int gpio_to_irq(int gpio);
extern int irq_to_gpio(int gpio);

#endif

