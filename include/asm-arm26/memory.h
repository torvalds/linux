/*
 *  linux/include/asm-arm26/memory.h
 *
 *  Copyright (C) 2000-2002 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Note: this file should not be included by non-asm/.h files
 */
#ifndef __ASM_ARM_MEMORY_H
#define __ASM_ARM_MEMORY_H

/*
 * User space: 26MB
 */
#define TASK_SIZE       (0x01a00000UL)

/*
 * This decides where the kernel will search for a free chunk of vm
 * space during mmap's.
 */
#define TASK_UNMAPPED_BASE (TASK_SIZE / 3)

/*
 * Page offset: 32MB
 */
#define PAGE_OFFSET     (0x02000000UL)
#define PHYS_OFFSET     (0x02000000UL)

#define PHYS_TO_NID(addr)       (0)

/*
 * PFNs are used to describe any physical page; this means
 * PFN 0 == physical address 0.
 *
 * This is the PFN of the first RAM page in the kernel
 * direct-mapped view.  We assume this is the first page
 * of RAM in the mem_map as well.
 */
#define PHYS_PFN_OFFSET	(PHYS_OFFSET >> PAGE_SHIFT)

/*
 * These are *only* valid on the kernel direct mapped RAM memory.
 */
static inline unsigned long virt_to_phys(void *x)
{
	return (unsigned long)x;
}

static inline void *phys_to_virt(unsigned long x)
{
	return (void *)((unsigned long)x);
}

#define __pa(x)			(unsigned long)(x)
#define __va(x)			((void *)(unsigned long)(x))

/*
 * Virtual <-> DMA view memory address translations
 * Again, these are *only* valid on the kernel direct mapped RAM
 * memory.  Use of these is *depreciated*.
 */
#define virt_to_bus(x)		((unsigned long)(x))
#define bus_to_virt(x)		((void *)((unsigned long)(x)))

/*
 * Conversion between a struct page and a physical address.
 *
 * Note: when converting an unknown physical address to a
 * struct page, the resulting pointer must be validated
 * using VALID_PAGE().  It must return an invalid struct page
 * for any physical address not corresponding to a system
 * RAM address.
 *
 *  page_to_pfn(page)	convert a struct page * to a PFN number
 *  pfn_to_page(pfn)	convert a _valid_ PFN number to struct page *
 *  pfn_valid(pfn)	indicates whether a PFN number is valid
 *
 *  virt_to_page(k)	convert a _valid_ virtual address to struct page *
 *  virt_addr_valid(k)	indicates whether a virtual address is valid
 */
#define page_to_pfn(page)	(((page) - mem_map) + PHYS_PFN_OFFSET)
#define pfn_to_page(pfn)	((mem_map + (pfn)) - PHYS_PFN_OFFSET)
#define pfn_valid(pfn)		((pfn) >= PHYS_PFN_OFFSET && (pfn) < (PHYS_PFN_OFFSET + max_mapnr))

#define virt_to_page(kaddr)	(pfn_to_page(__pa(kaddr) >> PAGE_SHIFT))
#define virt_addr_valid(kaddr)	((int)(kaddr) >= PAGE_OFFSET && (int)(kaddr) < (unsigned long)high_memory)

/*
 * For BIO.  "will die".  Kill me when bio_to_phys() and bvec_to_phys() die.
 */
#define page_to_phys(page)	(page_to_pfn(page) << PAGE_SHIFT)

/*
 * We should really eliminate virt_to_bus() here - it's depreciated.
 */
#define page_to_bus(page)	(page_address(page))

#endif
