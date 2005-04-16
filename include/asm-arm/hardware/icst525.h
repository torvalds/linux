/*
 *  linux/include/asm-arm/hardware/icst525.h
 *
 *  Copyright (C) 2003 Deep Blue Solutions, Ltd, All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Support functions for calculating clocks/divisors for the ICST525
 *  clock generators.  See http://www.icst.com/ for more information
 *  on these devices.
 */
#ifndef ASMARM_HARDWARE_ICST525_H
#define ASMARM_HARDWARE_ICST525_H

struct icst525_params {
	unsigned long	ref;
	unsigned long	vco_max;	/* inclusive */
	unsigned short	vd_min;		/* inclusive */
	unsigned short	vd_max;		/* inclusive */
	unsigned char	rd_min;		/* inclusive */
	unsigned char	rd_max;		/* inclusive */
};

struct icst525_vco {
	unsigned short	v;
	unsigned char	r;
	unsigned char	s;
};

unsigned long icst525_khz(const struct icst525_params *p, struct icst525_vco vco);
struct icst525_vco icst525_khz_to_vco(const struct icst525_params *p, unsigned long freq);
struct icst525_vco icst525_ps_to_vco(const struct icst525_params *p, unsigned long period);

#endif
