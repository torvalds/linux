#ifndef _ASM_SPARC64_OF_PLATFORM_H
#define _ASM_SPARC64_OF_PLATFORM_H
/*
 *    Copyright (C) 2006 Benjamin Herrenschmidt, IBM Corp.
 *			 <benh@kernel.crashing.org>
 *    Modified for Sparc by merging parts of asm-sparc/of_device.h
 *		by Stephen Rothwell
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 */

/* This is just here during the transition */
#include <linux/of_platform.h>

extern struct bus_type isa_bus_type;
extern struct bus_type ebus_bus_type;
extern struct bus_type sbus_bus_type;

#define of_bus_type	of_platform_bus_type	/* for compatibility */

#endif	/* _ASM_SPARC64_OF_PLATFORM_H */
