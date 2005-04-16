/*
 *  linux/include/asm-arm/hardware/icst307.h
 *
 *  Copyright (C) 2003 Deep Blue Solutions, Ltd, All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Support functions for calculating clocks/divisors for the ICS307
 *  clock generators.  See http://www.icst.com/ for more information
 *  on these devices.
 *
 *  This file is similar to the icst525.h file
 */
#ifndef ASMARM_HARDWARE_ICST307_H
#define ASMARM_HARDWARE_ICST307_H

struct icst307_params {
	unsigned long	ref;
	unsigned long	vco_max;	/* inclusive */
	unsigned short	vd_min;		/* inclusive */
	unsigned short	vd_max;		/* inclusive */
	unsigned char	rd_min;		/* inclusive */
	unsigned char	rd_max;		/* inclusive */
};

struct icst307_vco {
	unsigned short	v;
	unsigned char	r;
	unsigned char	s;
};

unsigned long icst307_khz(const struct icst307_params *p, struct icst307_vco vco);
struct icst307_vco icst307_khz_to_vco(const struct icst307_params *p, unsigned long freq);
struct icst307_vco icst307_ps_to_vco(const struct icst307_params *p, unsigned long period);

#endif
