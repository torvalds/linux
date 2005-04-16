/* $Id$ */
/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 by Hiroyuki Kondo
 *
 * Defitions for the address spaces of the M32R CPUs.
 */
#ifndef __ASM_M32R_ADDRSPACE_H
#define __ASM_M32R_ADDRSPACE_H

/*
 * Memory segments (32bit kernel mode addresses)
 */
#define KUSEG                   0x00000000
#define KSEG0                   0x80000000
#define KSEG1                   0xa0000000
#define KSEG2                   0xc0000000
#define KSEG3                   0xe0000000

#define K0BASE  KSEG0

/*
 * Returns the kernel segment base of a given address
 */
#ifndef __ASSEMBLY__
#define KSEGX(a)                (((unsigned long)(a)) & 0xe0000000)
#else
#define KSEGX(a)                ((a) & 0xe0000000)
#endif

/*
 * Returns the physical address of a KSEG0/KSEG1 address
 */
#ifndef __ASSEMBLY__
#define PHYSADDR(a)		(((unsigned long)(a)) & 0x1fffffff)
#else
#define PHYSADDR(a)		((a) & 0x1fffffff)
#endif

/*
 * Map an address to a certain kernel segment
 */
#ifndef __ASSEMBLY__
#define KSEG0ADDR(a)		((__typeof__(a))(((unsigned long)(a) & 0x1fffffff) | KSEG0))
#define KSEG1ADDR(a)		((__typeof__(a))(((unsigned long)(a) & 0x1fffffff) | KSEG1))
#define KSEG2ADDR(a)		((__typeof__(a))(((unsigned long)(a) & 0x1fffffff) | KSEG2))
#define KSEG3ADDR(a)		((__typeof__(a))(((unsigned long)(a) & 0x1fffffff) | KSEG3))
#else
#define KSEG0ADDR(a)		(((a) & 0x1fffffff) | KSEG0)
#define KSEG1ADDR(a)		(((a) & 0x1fffffff) | KSEG1)
#define KSEG2ADDR(a)		(((a) & 0x1fffffff) | KSEG2)
#define KSEG3ADDR(a)		(((a) & 0x1fffffff) | KSEG3)
#endif

#endif /* __ASM_M32R_ADDRSPACE_H */
