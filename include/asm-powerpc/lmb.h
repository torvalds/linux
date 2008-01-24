#ifndef _ASM_POWERPC_LMB_H
#define _ASM_POWERPC_LMB_H
#ifdef __KERNEL__

/*
 * Definitions for talking to the Open Firmware PROM on
 * Power Macintosh computers.
 *
 * Copyright (C) 2001 Peter Bergner, IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/init.h>
#include <asm/prom.h>

#define MAX_LMB_REGIONS 128

struct lmb_property {
	unsigned long base;
	unsigned long size;
};

struct lmb_region {
	unsigned long cnt;
	unsigned long size;
	struct lmb_property region[MAX_LMB_REGIONS+1];
};

struct lmb {
	unsigned long debug;
	unsigned long rmo_size;
	struct lmb_region memory;
	struct lmb_region reserved;
};

extern struct lmb lmb;

extern void __init lmb_init(void);
extern void __init lmb_analyze(void);
extern long __init lmb_add(unsigned long base, unsigned long size);
extern long __init lmb_reserve(unsigned long base, unsigned long size);
extern unsigned long __init lmb_alloc(unsigned long size, unsigned long align);
extern unsigned long __init lmb_alloc_base(unsigned long size,
		unsigned long align, unsigned long max_addr);
extern unsigned long __init __lmb_alloc_base(unsigned long size,
		unsigned long align, unsigned long max_addr);
extern unsigned long __init lmb_phys_mem_size(void);
extern unsigned long __init lmb_end_of_DRAM(void);
extern void __init lmb_enforce_memory_limit(unsigned long memory_limit);
extern int __init lmb_is_reserved(unsigned long addr);

extern void lmb_dump_all(void);

static inline unsigned long
lmb_size_bytes(struct lmb_region *type, unsigned long region_nr)
{
	return type->region[region_nr].size;
}
static inline unsigned long
lmb_size_pages(struct lmb_region *type, unsigned long region_nr)
{
	return lmb_size_bytes(type, region_nr) >> PAGE_SHIFT;
}
static inline unsigned long
lmb_start_pfn(struct lmb_region *type, unsigned long region_nr)
{
	return type->region[region_nr].base >> PAGE_SHIFT;
}
static inline unsigned long
lmb_end_pfn(struct lmb_region *type, unsigned long region_nr)
{
	return lmb_start_pfn(type, region_nr) +
	       lmb_size_pages(type, region_nr);
}

#endif /* __KERNEL__ */
#endif /* _ASM_POWERPC_LMB_H */
