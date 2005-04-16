/*
 * linux/include/asm-arm/arch-omap/gpio.h
 *
 * OMAP GPIO handling defines and functions
 *
 * Copyright (C) 2003 Nokia Corporation
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

#include <asm/arch/hardware.h>
#include <asm/arch/irqs.h>
#include <asm/io.h>

#define OMAP_MPUIO_BASE			0xfffb5000
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

#define OMAP_MPUIO(nr)		(OMAP_MAX_GPIO_LINES + (nr))
#define OMAP_GPIO_IS_MPUIO(nr)	((nr) >= OMAP_MAX_GPIO_LINES)

#define OMAP_GPIO_IRQ(nr)	(OMAP_GPIO_IS_MPUIO(nr) ? \
				 IH_MPUIO_BASE + ((nr) & 0x0f) : \
				 IH_GPIO_BASE + ((nr) & 0x3f))

/* For EDGECTRL */
#define OMAP_GPIO_NO_EDGE	  0x00
#define OMAP_GPIO_FALLING_EDGE	  0x01
#define OMAP_GPIO_RISING_EDGE	  0x02
#define OMAP_GPIO_BOTH_EDGES	  0x03

extern int omap_gpio_init(void);	/* Call from board init only */
extern int omap_request_gpio(int gpio);
extern void omap_free_gpio(int gpio);
extern void omap_set_gpio_direction(int gpio, int is_input);
extern void omap_set_gpio_dataout(int gpio, int enable);
extern int omap_get_gpio_datain(int gpio);
extern void omap_set_gpio_edge_ctrl(int gpio, int edge);

#endif
