/*
 * linux/include/asm-arm/arch-pxa/gpio.h
 *
 * S3C2400 GPIO wrappers for arch-neutral GPIO calls
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
	s3c2410_gpio_cfgpin(gpio, S3C2410_GPIO_INPUT);
	return 0;
}

static inline int gpio_direction_output(unsigned gpio)
{
	s3c2410_gpio_cfgpin(gpio, S3C2410_GPIO_OUTPUT);
	return 0;
}

#define gpio_get_value(gpio)		s3c2410_gpio_getpin(gpio)
#define gpio_set_value(gpio,value)	s3c2410_gpio_setpin(gpio, value)

#include <asm-generic/gpio.h>			/* cansleep wrappers */

/* FIXME or maybe s3c2400_gpio_getirq() ... */
#define gpio_to_irq(gpio)		s3c2410_gpio_getirq(gpio)

/* FIXME implement irq_to_gpio() */

#endif
