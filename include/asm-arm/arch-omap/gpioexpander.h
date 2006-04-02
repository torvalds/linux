/*
 * linux/include/asm-arm/arch-omap/gpioexpander.h
 *
 *
 * Copyright (C) 2004 Texas Instruments, Inc.
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#ifndef __ASM_ARCH_OMAP_GPIOEXPANDER_H
#define __ASM_ARCH_OMAP_GPIOEXPANDER_H

/* Function Prototypes for GPIO Expander functions */

int read_gpio_expa(u8 *, int);
int write_gpio_expa(u8 , int);

#endif /* __ASM_ARCH_OMAP_GPIOEXPANDER_H */
