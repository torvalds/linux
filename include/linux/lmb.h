#ifndef _LINUX_LMB_H
#define _LINUX_LMB_H
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

#define MAX_LMB_REGIONS 128

struct lmb_property {
	u64 base;
	u64 size;
};

struct lmb_region {
	unsigned long cnt;
	u64 size;
	struct lmb_property region[MAX_LMB_REGIONS+1];
};

struct lmb {
	unsigned long debug;
	u64 rmo_size;
	struct lmb_region memory;
	struct lmb_region reserved;
};

extern struct lmb lmb;

extern void __init lmb_init(void);
extern void __init lmb_analyze(void);
extern long lmb_add(u64 base, u64 size);
extern long lmb_remove(u64 base, u64 size);
extern long __init lmb_reserve(u64 base, u64 size);
extern u64 __init lmb_alloc_nid(u64 size, u64 align, int nid,
				u64 (*nid_range)(u64, u64, int *));
extern u64 __init lmb_alloc(u64 size, u64 align);
extern u64 __init lmb_alloc_base(u64 size,
		u64, u64 max_addr);
extern u64 __init __lmb_alloc_base(u64 size,
		u64 align, u64 max_addr);
extern u64 __init lmb_phys_mem_size(void);
extern u64 lmb_end_of_DRAM(void);
extern void __init lmb_enforce_memory_limit(u64 memory_limit);
extern int __init lmb_is_reserved(u64 addr);
extern int lmb_is_region_reserved(u64 base, u64 size);
extern int lmb_find(struct lmb_property *res);

extern void lmb_dump_all(void);

static inline u64
lmb_size_bytes(struct lmb_region *type, unsigned long region_nr)
{
	return type->region[region_nr].size;
}
static inline u64
lmb_size_pages(struct lmb_region *type, unsigned long region_nr)
{
	return lmb_size_bytes(type, region_nr) >> PAGE_SHIFT;
}
static inline u64
lmb_start_pfn(struct lmb_region *type, unsigned long region_nr)
{
	return type->region[region_nr].base >> PAGE_SHIFT;
}
static inline u64
lmb_end_pfn(struct lmb_region *type, unsigned long region_nr)
{
	return lmb_start_pfn(type, region_nr) +
	       lmb_size_pages(type, region_nr);
}

#include <asm/lmb.h>

#endif /* __KERNEL__ */

#endif /* _LINUX_LMB_H */
