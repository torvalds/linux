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

#define MAX_MEMBLOCK_REGIONS 128

struct memblock_property {
	u64 base;
	u64 size;
};

struct memblock_region {
	unsigned long cnt;
	u64 size;
	struct memblock_property region[MAX_MEMBLOCK_REGIONS+1];
};

struct memblock {
	unsigned long debug;
	u64 rmo_size;
	struct memblock_region memory;
	struct memblock_region reserved;
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
extern int __init memblock_is_reserved(u64 addr);
extern int memblock_is_region_reserved(u64 base, u64 size);
extern int memblock_find(struct memblock_property *res);

extern void memblock_dump_all(void);

static inline u64
memblock_size_bytes(struct memblock_region *type, unsigned long region_nr)
{
	return type->region[region_nr].size;
}
static inline u64
memblock_size_pages(struct memblock_region *type, unsigned long region_nr)
{
	return memblock_size_bytes(type, region_nr) >> PAGE_SHIFT;
}
static inline u64
memblock_start_pfn(struct memblock_region *type, unsigned long region_nr)
{
	return type->region[region_nr].base >> PAGE_SHIFT;
}
static inline u64
memblock_end_pfn(struct memblock_region *type, unsigned long region_nr)
{
	return memblock_start_pfn(type, region_nr) +
	       memblock_size_pages(type, region_nr);
}

#include <asm/memblock.h>

#endif /* __KERNEL__ */

#endif /* _LINUX_MEMBLOCK_H */
