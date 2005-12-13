/*
 *  linux/include/asm-arm/memory.h
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
 * Allow for constants defined here to be used from assembly code
 * by prepending the UL suffix only with actual C code compilation.
 */
#ifndef __ASSEMBLY__
#define UL(x) (x##UL)
#else
#define UL(x) (x)
#endif

#include <linux/config.h>
#include <linux/compiler.h>
#include <asm/arch/memory.h>

#ifndef TASK_SIZE
/*
 * TASK_SIZE - the maximum size of a user space task.
 * TASK_UNMAPPED_BASE - the lower boundary of the mmap VM area
 */
#define TASK_SIZE		UL(0xbf000000)
#define TASK_UNMAPPED_BASE	UL(0x40000000)
#endif

/*
 * The maximum size of a 26-bit user space task.
 */
#define TASK_SIZE_26		UL(0x04000000)

/*
 * Page offset: 3GB
 */
#ifndef PAGE_OFFSET
#define PAGE_OFFSET		UL(0xc0000000)
#endif

/*
 * Physical vs virtual RAM address space conversion.  These are
 * private definitions which should NOT be used outside memory.h
 * files.  Use virt_to_phys/phys_to_virt/__pa/__va instead.
 */
#ifndef __virt_to_phys
#define __virt_to_phys(x)	((x) - PAGE_OFFSET + PHYS_OFFSET)
#define __phys_to_virt(x)	((x) - PHYS_OFFSET + PAGE_OFFSET)
#endif

/*
 * The module space lives between the addresses given by TASK_SIZE
 * and PAGE_OFFSET - it must be within 32MB of the kernel text.
 */
#define MODULE_END	(PAGE_OFFSET)
#define MODULE_START	(MODULE_END - 16*1048576)

#if TASK_SIZE > MODULE_START
#error Top of user space clashes with start of module space
#endif

/*
 * The XIP kernel gets mapped at the bottom of the module vm area.
 * Since we use sections to map it, this macro replaces the physical address
 * with its virtual address while keeping offset from the base section.
 */
#define XIP_VIRT_ADDR(physaddr)  (MODULE_START + ((physaddr) & 0x000fffff))

#ifndef __ASSEMBLY__

/*
 * The DMA mask corresponding to the maximum bus address allocatable
 * using GFP_DMA.  The default here places no restriction on DMA
 * allocations.  This must be the smallest DMA mask in the system,
 * so a successful GFP_DMA allocation will always satisfy this.
 */
#ifndef ISA_DMA_THRESHOLD
#define ISA_DMA_THRESHOLD	(0xffffffffULL)
#endif

#ifndef arch_adjust_zones
#define arch_adjust_zones(node,size,holes) do { } while (0)
#endif

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
 * Note: Drivers should NOT use these.  They are the wrong
 * translation for translating DMA addresses.  Use the driver
 * DMA support - see dma-mapping.h.
 */
static inline unsigned long virt_to_phys(void *x)
{
	return __virt_to_phys((unsigned long)(x));
}

static inline void *phys_to_virt(unsigned long x)
{
	return (void *)(__phys_to_virt((unsigned long)(x)));
}

/*
 * Drivers should NOT use these either.
 */
#define __pa(x)			__virt_to_phys((unsigned long)(x))
#define __va(x)			((void *)__phys_to_virt((unsigned long)(x)))
#define pfn_to_kaddr(pfn)	__va((pfn) << PAGE_SHIFT)

/*
 * Virtual <-> DMA view memory address translations
 * Again, these are *only* valid on the kernel direct mapped RAM
 * memory.  Use of these is *deprecated* (and that doesn't mean
 * use the __ prefixed forms instead.)  See dma-mapping.h.
 */
static inline __deprecated unsigned long virt_to_bus(void *x)
{
	return __virt_to_bus((unsigned long)x);
}

static inline __deprecated void *bus_to_virt(unsigned long x)
{
	return (void *)__bus_to_virt(x);
}

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
#ifndef CONFIG_DISCONTIGMEM

#define page_to_pfn(page)	(((page) - mem_map) + PHYS_PFN_OFFSET)
#define pfn_to_page(pfn)	((mem_map + (pfn)) - PHYS_PFN_OFFSET)
#define pfn_valid(pfn)		((pfn) >= PHYS_PFN_OFFSET && (pfn) < (PHYS_PFN_OFFSET + max_mapnr))

#define virt_to_page(kaddr)	(pfn_to_page(__pa(kaddr) >> PAGE_SHIFT))
#define virt_addr_valid(kaddr)	((unsigned long)(kaddr) >= PAGE_OFFSET && (unsigned long)(kaddr) < (unsigned long)high_memory)

#define PHYS_TO_NID(addr)	(0)

#else /* CONFIG_DISCONTIGMEM */

/*
 * This is more complex.  We have a set of mem_map arrays spread
 * around in memory.
 */
#include <linux/numa.h>

#define page_to_pfn(page)					\
	(( (page) - page_zone(page)->zone_mem_map)		\
	  + page_zone(page)->zone_start_pfn)

#define pfn_to_page(pfn)					\
	(PFN_TO_MAPBASE(pfn) + LOCAL_MAP_NR((pfn) << PAGE_SHIFT))

#define pfn_valid(pfn)						\
	({							\
		unsigned int nid = PFN_TO_NID(pfn);		\
		int valid = nid < MAX_NUMNODES;			\
		if (valid) {					\
			pg_data_t *node = NODE_DATA(nid);	\
			valid = (pfn - node->node_start_pfn) <	\
				node->node_spanned_pages;	\
		}						\
		valid;						\
	})

#define virt_to_page(kaddr)					\
	(ADDR_TO_MAPBASE(kaddr) + LOCAL_MAP_NR(kaddr))

#define virt_addr_valid(kaddr)	(KVADDR_TO_NID(kaddr) < MAX_NUMNODES)

/*
 * Common discontigmem stuff.
 *  PHYS_TO_NID is used by the ARM kernel/setup.c
 */
#define PHYS_TO_NID(addr)	PFN_TO_NID((addr) >> PAGE_SHIFT)

#endif /* !CONFIG_DISCONTIGMEM */

/*
 * For BIO.  "will die".  Kill me when bio_to_phys() and bvec_to_phys() die.
 */
#define page_to_phys(page)	(page_to_pfn(page) << PAGE_SHIFT)

/*
 * Optional device DMA address remapping. Do _not_ use directly!
 * We should really eliminate virt_to_bus() here - it's deprecated.
 */
#ifndef __arch_page_to_dma
#define page_to_dma(dev, page)		((dma_addr_t)__virt_to_bus((unsigned long)page_address(page)))
#define dma_to_virt(dev, addr)		((void *)__bus_to_virt(addr))
#define virt_to_dma(dev, addr)		((dma_addr_t)__virt_to_bus((unsigned long)(addr)))
#else
#define page_to_dma(dev, page)		(__arch_page_to_dma(dev, page))
#define dma_to_virt(dev, addr)		(__arch_dma_to_virt(dev, addr))
#define virt_to_dma(dev, addr)		(__arch_virt_to_dma(dev, addr))
#endif

#endif

#endif
