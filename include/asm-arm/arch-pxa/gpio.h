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
	if (gpio > PXA_LAST_GPIO)
		return -EINVAL;
	pxa_gpio_mode(gpio | GPIO_IN);
}

static inline int gpio_direction_output(unsigned gpio)
{
	if (gpio > PXA_LAST_GPIO)
		return -EINVAL;
	pxa_gpio_mode(gpio | GPIO_OUT);
}

/* REVISIT these macros are correct, but suffer code explosion
 * for non-constant parameters.  Provide out-line versions too.
 */
#define gpio_get_value(gpio) \
	(GPLR(gpio) & GPIO_bit(gpio))

#define gpio_set_value(gpio,value) \
	((value) ? (GPSR(gpio) = GPIO_bit(gpio)):(GPCR(gpio) = GPIO_bit(gpio)))

#include <asm-generic/gpio.h>			/* cansleep wrappers */

#define gpio_to_irq(gpio)	IRQ_GPIO(gpio)
#define irq_to_gpio(irq)	IRQ_TO_GPIO(irq)


#endif
