/*
 * include/asm-arm/arch-ixp2000/ixp2000-gpio.h
 *
 * Copyright (C) 2002 Intel Corporation.
 *
 * This program is free software, you can redistribute it and/or modify 
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/*
 * IXP2000 GPIO in/out, edge/level detection for IRQs:
 * IRQs are generated on Falling-edge, Rising-Edge, Level-low, Level-High
 * or both Falling-edge and Rising-edge.  
 * This must be called *before* the corresponding IRQ is registerd.
 * Use this instead of directly setting the GPIO registers.
 * GPIOs may also be used as GPIOs (e.g. for emulating i2c/smb)
 */
#ifndef _ASM_ARCH_IXP2000_GPIO_H_
#define _ASM_ARCH_IXP2000_GPIO_H_

#ifndef __ASSEMBLY__
#define GPIO_OUT			0x0
#define GPIO_IN				0x80

#define IXP2000_GPIO_LOW		0
#define IXP2000_GPIO_HIGH		1

#define GPIO_NO_EDGES           	0
#define GPIO_FALLING_EDGE       	1
#define GPIO_RISING_EDGE        	2
#define GPIO_BOTH_EDGES         	3
#define GPIO_LEVEL_LOW          	4
#define GPIO_LEVEL_HIGH         	8

extern void set_GPIO_IRQ_edge(int gpio_nr, int edge);
extern void set_GPIO_IRQ_level(int gpio_nr, int level);
extern void gpio_line_config(int line, int style);

static inline int gpio_line_get(int line)
{
	return (((*IXP2000_GPIO_PLR) >> line) & 1);
}

static inline void gpio_line_set(int line, int value)
{
	if (value == IXP2000_GPIO_HIGH) {
		ixp_reg_write(IXP2000_GPIO_POSR, BIT(line));
	} else if (value == IXP2000_GPIO_LOW)
		ixp_reg_write(IXP2000_GPIO_POCR, BIT(line));
}

#endif /* !__ASSEMBLY__ */
#endif /* ASM_ARCH_IXP2000_GPIO_H_ */

