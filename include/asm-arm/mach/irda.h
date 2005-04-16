/*
 *  linux/include/asm-arm/mach/irda.h
 *
 *  Copyright (C) 2004 Russell King.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __ASM_ARM_MACH_IRDA_H
#define __ASM_ARM_MACH_IRDA_H

struct irda_platform_data {
	int (*startup)(struct device *);
	void (*shutdown)(struct device *);
	int (*set_power)(struct device *, unsigned int state);
	void (*set_speed)(struct device *, unsigned int speed);
};

#endif
