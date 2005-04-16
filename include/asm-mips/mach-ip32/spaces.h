/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994 - 1999, 2000, 03, 04, 05 Ralf Baechle (ralf@linux-mips.org)
 * Copyright (C) 2000, 2002  Maciej W. Rozycki
 * Copyright (C) 1990, 1999, 2000 Silicon Graphics, Inc.
 */
#ifndef _ASM_MACH_IP32_SPACES_H
#define _ASM_MACH_IP32_SPACES_H

/*
 * Memory above this physical address will be considered highmem.
 * Fixme: 59 bits is a fictive number and makes assumptions about processors
 * in the distant future.  Nobody will care for a few years :-)
 */
#ifndef HIGHMEM_START
#define HIGHMEM_START		(1UL << 59UL)
#endif

#define CAC_BASE		0x9800000000000000
#define IO_BASE			0x9000000000000000
#define UNCAC_BASE		0x9000000000000000
#define MAP_BASE		0xc000000000000000

#define TO_PHYS(x)		(             ((x) & TO_PHYS_MASK))
#define TO_CAC(x)		(CAC_BASE   | ((x) & TO_PHYS_MASK))
#define TO_UNCAC(x)		(UNCAC_BASE | ((x) & TO_PHYS_MASK))

/*
 * This handles the memory map.
 */
#define PAGE_OFFSET		CAC_BASE

#endif /* __ASM_MACH_IP32_SPACES_H */
