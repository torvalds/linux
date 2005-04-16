#ifndef _NACA_H
#define _NACA_H

/* 
 * c 2001 PPC 64 Team, IBM Corp
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <asm/types.h>

#ifndef __ASSEMBLY__

struct naca_struct {
	/* Kernel only data - undefined for user space */
	void *xItVpdAreas;              /* VPD Data                  0x00 */
	void *xRamDisk;                 /* iSeries ramdisk           0x08 */
	u64   xRamDiskSize;		/* In pages                  0x10 */
};

extern struct naca_struct naca;

#endif /* __ASSEMBLY__ */

#define NACA_PAGE      0x4
#define NACA_PHYS_ADDR (NACA_PAGE<<PAGE_SHIFT)

#endif /* _NACA_H */
