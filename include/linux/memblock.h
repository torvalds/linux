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

#define MAX_MEMBLOCK_REGIONS 128

struct memblock_region {
	u64 base;
	u64 size;
};

struct memblock_type {
	unsigned long cnt;
	u64 size;
	struct memblock_region regions[MAX_MEMBLOCK_REGIONS+1];
};

struct memblock {
	unsigned long debug;
	u64 rmo_size;
	struct memblock_type memory;
	struct memblock_type reserved;
};

extern struct memblock memblock;

extern void __init memblock_init(void);
extern void __init memblock_analyze(void);
extern long memblock_add(u64 base, u64 size);
extern long memblock_remove(u64 base, u64 size);
extern long __init memblock_free(u64 base, u64 size);
extern long __init memblock_reserve(u64 base, u64 size);
extern u64 __init memblock_alloc_nid(u64 size, u64 align, int nid,
				u64 (*nid_range)(u64, u64, int *));
extern u64 __init memblock_alloc(u64 size, u64 align);
extern u64 __init memblock_alloc_base(u64 size,
		u64, u64 max_addr);
extern u64 __init __memblock_alloc_base(u64 size,
		u64 align, u64 max_addr);
extern u64 __init memblock_phys_mem_size(void);
extern u64 memblock_end_of_DRAM(void);
extern void __init memblock_enforce_memory_limit(u64 memory_limit);
extern int memblock_is_memory(u64 addr);
extern int memblock_is_region_memory(u64 base, u64 size);
extern int __init memblock_is_reserved(u64 addr);
extern int memblock_is_region_reserved(u64 base, u64 size);

extern void memblock_dump_all(void);

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
