#ifndef _LINUX_MEMBLOCK_H
#define _LINUX_MEMBLOCK_H
#ifdef __KERNEL__

/*
 * Logical memory blocks.
 *
 * Copyright (C) 2001 Peter Bergner, IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/init.h>
#include <linux/mm.h>

#include <asm/memblock.h>

#define INIT_MEMBLOCK_REGIONS 128

struct memblock_region {
	phys_addr_t base;
	phys_addr_t size;
};

struct memblock_type {
	unsigned long cnt;	/* number of regions */
	unsigned long max;	/* size of the allocated array */
	struct memblock_region *regions;
};

struct memblock {
	phys_addr_t current_limit;
	phys_addr_t memory_size;	/* Updated by memblock_analyze() */
	struct memblock_type memory;
	struct memblock_type reserved;
};

extern struct memblock memblock;

extern void __init memblock_init(void);
extern void __init memblock_analyze(void);
extern long memblock_add(phys_addr_t base, phys_addr_t size);
extern long memblock_remove(phys_addr_t base, phys_addr_t size);
extern long __init memblock_free(phys_addr_t base, phys_addr_t size);
extern long __init memblock_reserve(phys_addr_t base, phys_addr_t size);

extern phys_addr_t __init memblock_alloc_nid(phys_addr_t size, phys_addr_t align, int nid);
extern phys_addr_t __init memblock_alloc(phys_addr_t size, phys_addr_t align);

/* Flags for memblock_alloc_base() amd __memblock_alloc_base() */
#define MEMBLOCK_ALLOC_ANYWHERE	(~(phys_addr_t)0)
#define MEMBLOCK_ALLOC_ACCESSIBLE	0

extern phys_addr_t __init memblock_alloc_base(phys_addr_t size,
					 phys_addr_t align,
					 phys_addr_t max_addr);
extern phys_addr_t __init __memblock_alloc_base(phys_addr_t size,
					   phys_addr_t align,
					   phys_addr_t max_addr);
extern phys_addr_t __init memblock_phys_mem_size(void);
extern phys_addr_t memblock_end_of_DRAM(void);
extern void __init memblock_enforce_memory_limit(phys_addr_t memory_limit);
extern int memblock_is_memory(phys_addr_t addr);
extern int memblock_is_region_memory(phys_addr_t base, phys_addr_t size);
extern int __init memblock_is_reserved(phys_addr_t addr);
extern int memblock_is_region_reserved(phys_addr_t base, phys_addr_t size);

extern void memblock_dump_all(void);

/* Provided by the architecture */
extern phys_addr_t memblock_nid_range(phys_addr_t start, phys_addr_t end, int *nid);
extern int memblock_memory_can_coalesce(phys_addr_t addr1, phys_addr_t size1,
				   phys_addr_t addr2, phys_addr_t size2);

/**
 * memblock_set_current_limit - Set the current allocation limit to allow
 *                         limiting allocations to what is currently
 *                         accessible during boot
 * @limit: New limit value (physical address)
 */
extern void memblock_set_current_limit(phys_addr_t limit);


/*
 * pfn conversion functions
 *
 * While the memory MEMBLOCKs should always be page aligned, the reserved
 * MEMBLOCKs may not be. This accessor attempt to provide a very clear
 * idea of what they return for such non aligned MEMBLOCKs.
 */

/**
 * memblock_region_base_pfn - Return the lowest pfn intersecting with the region
 * @reg: memblock_region structure
 */
static inline unsigned long memblock_region_base_pfn(const struct memblock_region *reg)
{
	return reg->base >> PAGE_SHIFT;
}

/**
 * memblock_region_last_pfn - Return the highest pfn intersecting with the region
 * @reg: memblock_region structure
 */
static inline unsigned long memblock_region_last_pfn(const struct memblock_region *reg)
{
	return (reg->base + reg->size - 1) >> PAGE_SHIFT;
}

/**
 * memblock_region_end_pfn - Return the pfn of the first page following the region
 *                      but not intersecting it
 * @reg: memblock_region structure
 */
static inline unsigned long memblock_region_end_pfn(const struct memblock_region *reg)
{
	return memblock_region_last_pfn(reg) + 1;
}

/**
 * memblock_region_pages - Return the number of pages covering a region
 * @reg: memblock_region structure
 */
static inline unsigned long memblock_region_pages(const struct memblock_region *reg)
{
	return memblock_region_end_pfn(reg) - memblock_region_end_pfn(reg);
}

#define for_each_memblock(memblock_type, region)					\
	for (region = memblock.memblock_type.regions;				\
	     region < (memblock.memblock_type.regions + memblock.memblock_type.cnt);	\
	     region++)


#endif /* __KERNEL__ */

#endif /* _LINUX_MEMBLOCK_H */
