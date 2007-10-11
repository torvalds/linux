#ifndef _ASM_IST_H
#define _ASM_IST_H

/*
 * Include file for the interface to IST BIOS
 * Copyright 2002 Andy Grover <andrew.grover@intel.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */


#ifdef __KERNEL__

#include <linux/types.h>

struct ist_info {
	u32 signature;
	u32 command;
	u32 event;
	u32 perf_level;
};

extern struct ist_info ist_info;

#endif	/* __KERNEL__ */
#endif	/* _ASM_IST_H */
