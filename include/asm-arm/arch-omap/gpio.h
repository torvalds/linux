/*
 * linux/include/asm-arm/arch-omap/gpio.h
 *
 * OMAP GPIO handling defines and functions
 *
 * Copyright (C) 2003-2005 Nokia Corporation
 *
 * Written by Juha Yrjölä <juha.yrjola@nokia.com>
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

#ifndef __ASM_ARCH_OMAP_GPIO_H
#define __ASM_ARCH_OMAP_GPIO_H

#include <asm/hardware.h>
#include <asm/arch/irqs.h>
#include <asm/io.h>

#define OMAP_MPUIO_BASE			(void __iomem *)0xfffb5000

#ifdef CONFIG_ARCH_OMAP730
#define OMAP_MPUIO_INPUT_LATCH		0x00
#define OMAP_MPUIO_OUTPUT		0x02
#define OMAP_MPUIO_IO_CNTL		0x04
#define OMAP_MPUIO_KBR_LATCH		0x08
#define OMAP_MPUIO_KBC			0x0a
#define OMAP_MPUIO_GPIO_EVENT_MODE	0x0c
#define OMAP_MPUIO_GPIO_INT_EDGE	0x0e
#define OMAP_MPUIO_KBD_INT		0x10
#define OMAP_MPUIO_GPIO_INT		0x12
#define OMAP_MPUIO_KBD_MASKIT		0x14
#define OMAP_MPUIO_GPIO_MASKIT		0x16
#define OMAP_MPUIO_GPIO_DEBOUNCING	0x18
#define OMAP_MPUIO_LATCH		0x1a
#else
#define OMAP_MPUIO_INPUT_LATCH		0x00
#define OMAP_MPUIO_OUTPUT		0x04
#define OMAP_MPUIO_IO_CNTL		0x08
#define OMAP_MPUIO_KBR_LATCH		0x10
#define OMAP_MPUIO_KBC			0x14
#define OMAP_MPUIO_GPIO_EVENT_MODE	0x18
#define OMAP_MPUIO_GPIO_INT_EDGE	0x1c
#define OMAP_MPUIO_KBD_INT		0x20
#define OMAP_MPUIO_GPIO_INT		0x24
#define OMAP_MPUIO_KBD_MASKIT		0x28
#define OMAP_MPUIO_GPIO_MASKIT		0x2c
#define OMAP_MPUIO_GPIO_DEBOUNCING	0x30
#define OMAP_MPUIO_LATCH		0x34
#endif

#define OMAP_MPUIO(nr)		(OMAP_MAX_GPIO_LINES + (nr))
#define OMAP_GPIO_IS_MPUIO(nr)	((nr) >= OMAP_MAX_GPIO_LINES)

#define OMAP_GPIO_IRQ(nr)	(OMAP_GPIO_IS_MPUIO(nr) ? \
				 IH_MPUIO_BASE + ((nr) & 0x0f) : \
				 IH_GPIO_BASE + (nr))

extern int omap_gpio_init(void);	/* Call from board init only */
extern int omap_request_gpio(int gpio);
extern void omap_free_gpio(int gpio);
extern void omap_set_gpio_direction(int gpio, int is_input);
extern void omap_set_gpio_dataout(int gpio, int enable);
extern int omap_get_gpio_datain(int gpio);

/*-------------------------------------------------------------------------*/

/* wrappers for "new style" GPIO calls. the old OMAP-specfic ones should
 * eventually be removed (along with this errno.h inclusion), and maybe
 * gpios should put MPUIOs last too.
 */

#include <asm/errno.h>

static inline int gpio_request(unsigned gpio, const char *label)
{
	return omap_request_gpio(gpio);
}

static inline void gpio_free(unsigned gpio)
{
	omap_free_gpio(gpio);
}

static inline int __gpio_set_direction(unsigned gpio, int is_input)
{
	if (cpu_class_is_omap2()) {
		if (gpio > OMAP_MAX_GPIO_LINES)
			return -EINVAL;
	} else {
		if (gpio > (OMAP_MAX_GPIO_LINES + 16 /* MPUIO */))
			return -EINVAL;
	}
	omap_set_gpio_direction(gpio, is_input);
	return 0;
}

static inline int gpio_direction_input(unsigned gpio)
{
	return __gpio_set_direction(gpio, 1);
}

static inline int gpio_direction_output(unsigned gpio, int value)
{
	omap_set_gpio_dataout(gpio, value);
	return __gpio_set_direction(gpio, 0);
}

static inline int gpio_get_value(unsigned gpio)
{
	return omap_get_gpio_datain(gpio);
}

static inline void gpio_set_value(unsigned gpio, int value)
{
	omap_set_gpio_dataout(gpio, value);
}

#include <asm-generic/gpio.h>		/* cansleep wrappers */

static inline int gpio_to_irq(unsigned gpio)
{
	return OMAP_GPIO_IRQ(gpio);
}

static inline int irq_to_gpio(unsigned irq)
{
	if (cpu_class_is_omap1() && (irq < (IH_MPUIO_BASE + 16)))
		return (irq - IH_MPUIO_BASE) + OMAP_MAX_GPIO_LINES;
	return irq - IH_GPIO_BASE;
}

#endif
