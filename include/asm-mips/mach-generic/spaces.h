/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994 - 1999, 2000, 03, 04 Ralf Baechle
 * Copyright (C) 2000, 2002  Maciej W. Rozycki
 * Copyright (C) 1990, 1999, 2000 Silicon Graphics, Inc.
 */
#ifndef _ASM_MACH_GENERIC_SPACES_H
#define _ASM_MACH_GENERIC_SPACES_H


#ifdef CONFIG_32BIT

#define CAC_BASE		0x80000000
#define IO_BASE			0xa0000000
#define UNCAC_BASE		0xa0000000

#ifndef MAP_BASE
#define MAP_BASE		0xc0000000
#endif

/*
 * This handles the memory map.
 * We handle pages at KSEG0 for kernels with 32 bit address space.
 */
#ifndef PAGE_OFFSET
#define PAGE_OFFSET		0x80000000UL
#endif

/*
 * Memory above this physical address will be considered highmem.
 */
#ifndef HIGHMEM_START
#define HIGHMEM_START		0x20000000UL
#endif

#endif /* CONFIG_32BIT */

#ifdef CONFIG_64BIT

/*
 * This handles the memory map.
 */
#ifndef PAGE_OFFSET
#ifdef CONFIG_DMA_NONCOHERENT
#define PAGE_OFFSET	0x9800000000000000UL
#else
#define PAGE_OFFSET	0xa800000000000000UL
#endif
#endif

/*
 * Memory above this physical address will be considered highmem.
 * Fixme: 59 bits is a fictive number and makes assumptions about processors
 * in the distant future.  Nobody will care for a few years :-)
 */
#ifndef HIGHMEM_START
#define HIGHMEM_START		(1UL << 59UL)
#endif

#ifndef CAC_BASE
#ifdef CONFIG_DMA_NONCOHERENT
#define CAC_BASE		0x9800000000000000UL
#else
#define CAC_BASE		0xa800000000000000UL
#endif
#endif

#ifndef IO_BASE
#define IO_BASE			0x9000000000000000UL
#endif

#ifndef UNCAC_BASE
#define UNCAC_BASE		0x9000000000000000UL
#endif

#ifndef MAP_BASE
#define MAP_BASE		0xc000000000000000UL
#endif

#define TO_PHYS(x)		(             ((x) & TO_PHYS_MASK))
#define TO_CAC(x)		(CAC_BASE   | ((x) & TO_PHYS_MASK))
#define TO_UNCAC(x)		(UNCAC_BASE | ((x) & TO_PHYS_MASK))

#endif /* CONFIG_64BIT */

#endif /* __ASM_MACH_GENERIC_SPACES_H */
