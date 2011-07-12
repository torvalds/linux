#ifndef _LINUX_MEMBLOCK_H
#define _LINUX_MEMBLOCK_H
#ifdef __KERNEL__

#ifdef CONFIG_HAVE_MEMBLOCK
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

#define INIT_MEMBLOCK_REGIONS	128

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
extern int memblock_debug;
extern int memblock_can_resize;

#define memblock_dbg(fmt, ...) \
	if (memblock_debug) printk(KERN_INFO pr_fmt(fmt), ##__VA_ARGS__)

u64 memblock_find_in_range(u64 start, u64 end, u64 size, u64 align);
int memblock_free_reserved_regions(void);
int memblock_reserve_reserved_regions(void);

extern void memblock_init(void);
extern void memblock_analyze(void);
extern long memblock_add(phys_addr_t base, phys_addr_t size);
extern long memblock_remove(phys_addr_t base, phys_addr_t size);
extern long memblock_free(phys_addr_t base, phys_addr_t size);
extern long memblock_reserve(phys_addr_t base, phys_addr_t size);

/* The numa aware allocator is only available if
 * CONFIG_ARCH_POPULATES_NODE_MAP is set
 */
extern phys_addr_t memblock_alloc_nid(phys_addr_t size, phys_addr_t align,
					int nid);
extern phys_addr_t memblock_alloc_try_nid(phys_addr_t size, phys_addr_t align,
					    int nid);

extern phys_addr_t memblock_alloc(phys_addr_t size, phys_addr_t align);

/* Flags for memblock_alloc_base() amd __memblock_alloc_base() */
#define MEMBLOCK_ALLOC_ANYWHERE	(~(phys_addr_t)0)
#define MEMBLOCK_ALLOC_ACCESSIBLE	0

extern phys_addr_t memblock_alloc_base(phys_addr_t size,
					 phys_addr_t align,
					 phys_addr_t max_addr);
extern phys_addr_t __memblock_alloc_base(phys_addr_t size,
					   phys_addr_t align,
					   phys_addr_t max_addr);
extern phys_addr_t memblock_phys_mem_size(void);
extern phys_addr_t memblock_end_of_DRAM(void);
extern void memblock_enforce_memory_limit(phys_addr_t memory_limit);
extern int memblock_is_memory(phys_addr_t addr);
extern int memblock_is_region_memory(phys_addr_t base, phys_addr_t size);
extern int memblock_is_reserved(phys_addr_t addr);
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
 * memblock_region_memory_base_pfn - Return the lowest pfn intersecting with the memory region
 * @reg: memblock_region structure
 */
static inline unsigned long memblock_region_memory_base_pfn(const struct memblock_region *reg)
{
	return PFN_UP(reg->base);
}

/**
 * memblock_region_memory_end_pfn - Return the end_pfn this region
 * @reg: memblock_region structure
 */
static inline unsigned long memblock_region_memory_end_pfn(const struct memblock_region *reg)
{
	return PFN_DOWN(reg->base + reg->size);
}

/**
 * memblock_region_reserved_base_pfn - Return the lowest pfn intersecting with the reserved region
 * @reg: memblock_region structure
 */
static inline unsigned long memblock_region_reserved_base_pfn(const struct memblock_region *reg)
{
	return PFN_DOWN(reg->base);
}

/**
 * memblock_region_reserved_end_pfn - Return the end_pfn this region
 * @reg: memblock_region structure
 */
static inline unsigned long memblock_region_reserved_end_pfn(const struct memblock_region *reg)
{
	return PFN_UP(reg->base + reg->size);
}

#define for_each_memblock(memblock_type, region)					\
	for (region = memblock.memblock_type.regions;				\
	     region < (memblock.memblock_type.regions + memblock.memblock_type.cnt);	\
	     region++)


#ifdef ARCH_DISCARD_MEMBLOCK
#define __init_memblock __init
#define __initdata_memblock __initdata
#else
#define __init_memblock
#define __initdata_memblock
#endif

#else
static inline phys_addr_t memblock_alloc(phys_addr_t size, phys_addr_t align)
{
	return 0;
}

#endif /* CONFIG_HAVE_MEMBLOCK */

#endif /* __KERNEL__ */

#endif /* _LINUX_MEMBLOCK_H */
