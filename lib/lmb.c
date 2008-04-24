/*
 * Procedures for maintaining information about logical memory blocks.
 *
 * Peter Bergner, IBM Corp.	June 2001.
 * Copyright (C) 2001 Peter Bergner.
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/bitops.h>
#include <linux/lmb.h>

#define LMB_ALLOC_ANYWHERE	0

struct lmb lmb;

void lmb_dump_all(void)
{
#ifdef DEBUG
	unsigned long i;

	pr_debug("lmb_dump_all:\n");
	pr_debug("    memory.cnt		  = 0x%lx\n", lmb.memory.cnt);
	pr_debug("    memory.size		  = 0x%llx\n",
	    (unsigned long long)lmb.memory.size);
	for (i=0; i < lmb.memory.cnt ;i++) {
		pr_debug("    memory.region[0x%x].base       = 0x%llx\n",
		    i, (unsigned long long)lmb.memory.region[i].base);
		pr_debug("		      .size     = 0x%llx\n",
		    (unsigned long long)lmb.memory.region[i].size);
	}

	pr_debug("    reserved.cnt	  = 0x%lx\n", lmb.reserved.cnt);
	pr_debug("    reserved.size	  = 0x%lx\n", lmb.reserved.size);
	for (i=0; i < lmb.reserved.cnt ;i++) {
		pr_debug("    reserved.region[0x%x].base       = 0x%llx\n",
		    i, (unsigned long long)lmb.reserved.region[i].base);
		pr_debug("		      .size     = 0x%llx\n",
		    (unsigned long long)lmb.reserved.region[i].size);
	}
#endif /* DEBUG */
}

static unsigned long __init lmb_addrs_overlap(u64 base1, u64 size1,
		u64 base2, u64 size2)
{
	return ((base1 < (base2 + size2)) && (base2 < (base1 + size1)));
}

static long __init lmb_addrs_adjacent(u64 base1, u64 size1,
		u64 base2, u64 size2)
{
	if (base2 == base1 + size1)
		return 1;
	else if (base1 == base2 + size2)
		return -1;

	return 0;
}

static long __init lmb_regions_adjacent(struct lmb_region *rgn,
		unsigned long r1, unsigned long r2)
{
	u64 base1 = rgn->region[r1].base;
	u64 size1 = rgn->region[r1].size;
	u64 base2 = rgn->region[r2].base;
	u64 size2 = rgn->region[r2].size;

	return lmb_addrs_adjacent(base1, size1, base2, size2);
}

static void __init lmb_remove_region(struct lmb_region *rgn, unsigned long r)
{
	unsigned long i;

	for (i = r; i < rgn->cnt - 1; i++) {
		rgn->region[i].base = rgn->region[i + 1].base;
		rgn->region[i].size = rgn->region[i + 1].size;
	}
	rgn->cnt--;
}

/* Assumption: base addr of region 1 < base addr of region 2 */
static void __init lmb_coalesce_regions(struct lmb_region *rgn,
		unsigned long r1, unsigned long r2)
{
	rgn->region[r1].size += rgn->region[r2].size;
	lmb_remove_region(rgn, r2);
}

void __init lmb_init(void)
{
	/* Create a dummy zero size LMB which will get coalesced away later.
	 * This simplifies the lmb_add() code below...
	 */
	lmb.memory.region[0].base = 0;
	lmb.memory.region[0].size = 0;
	lmb.memory.cnt = 1;

	/* Ditto. */
	lmb.reserved.region[0].base = 0;
	lmb.reserved.region[0].size = 0;
	lmb.reserved.cnt = 1;
}

void __init lmb_analyze(void)
{
	int i;

	lmb.memory.size = 0;

	for (i = 0; i < lmb.memory.cnt; i++)
		lmb.memory.size += lmb.memory.region[i].size;
}

static long __init lmb_add_region(struct lmb_region *rgn, u64 base, u64 size)
{
	unsigned long coalesced = 0;
	long adjacent, i;

	if ((rgn->cnt == 1) && (rgn->region[0].size == 0)) {
		rgn->region[0].base = base;
		rgn->region[0].size = size;
		return 0;
	}

	/* First try and coalesce this LMB with another. */
	for (i = 0; i < rgn->cnt; i++) {
		u64 rgnbase = rgn->region[i].base;
		u64 rgnsize = rgn->region[i].size;

		if ((rgnbase == base) && (rgnsize == size))
			/* Already have this region, so we're done */
			return 0;

		adjacent = lmb_addrs_adjacent(base, size, rgnbase, rgnsize);
		if (adjacent > 0) {
			rgn->region[i].base -= size;
			rgn->region[i].size += size;
			coalesced++;
			break;
		} else if (adjacent < 0) {
			rgn->region[i].size += size;
			coalesced++;
			break;
		}
	}

	if ((i < rgn->cnt - 1) && lmb_regions_adjacent(rgn, i, i+1)) {
		lmb_coalesce_regions(rgn, i, i+1);
		coalesced++;
	}

	if (coalesced)
		return coalesced;
	if (rgn->cnt >= MAX_LMB_REGIONS)
		return -1;

	/* Couldn't coalesce the LMB, so add it to the sorted table. */
	for (i = rgn->cnt - 1; i >= 0; i--) {
		if (base < rgn->region[i].base) {
			rgn->region[i+1].base = rgn->region[i].base;
			rgn->region[i+1].size = rgn->region[i].size;
		} else {
			rgn->region[i+1].base = base;
			rgn->region[i+1].size = size;
			break;
		}
	}

	if (base < rgn->region[0].base) {
		rgn->region[0].base = base;
		rgn->region[0].size = size;
	}
	rgn->cnt++;

	return 0;
}

long __init lmb_add(u64 base, u64 size)
{
	struct lmb_region *_rgn = &lmb.memory;

	/* On pSeries LPAR systems, the first LMB is our RMO region. */
	if (base == 0)
		lmb.rmo_size = size;

	return lmb_add_region(_rgn, base, size);

}

long __init lmb_reserve(u64 base, u64 size)
{
	struct lmb_region *_rgn = &lmb.reserved;

	BUG_ON(0 == size);

	return lmb_add_region(_rgn, base, size);
}

long __init lmb_overlaps_region(struct lmb_region *rgn, u64 base, u64 size)
{
	unsigned long i;

	for (i = 0; i < rgn->cnt; i++) {
		u64 rgnbase = rgn->region[i].base;
		u64 rgnsize = rgn->region[i].size;
		if (lmb_addrs_overlap(base, size, rgnbase, rgnsize))
			break;
	}

	return (i < rgn->cnt) ? i : -1;
}

static u64 lmb_align_down(u64 addr, u64 size)
{
	return addr & ~(size - 1);
}

static u64 lmb_align_up(u64 addr, u64 size)
{
	return (addr + (size - 1)) & ~(size - 1);
}

static u64 __init lmb_alloc_nid_unreserved(u64 start, u64 end,
					   u64 size, u64 align)
{
	u64 base, res_base;
	long j;

	base = lmb_align_down((end - size), align);
	while (start <= base) {
		j = lmb_overlaps_region(&lmb.reserved, base, size);
		if (j < 0) {
			/* this area isn't reserved, take it */
			if (lmb_add_region(&lmb.reserved, base,
					   lmb_align_up(size, align)) < 0)
				base = ~(u64)0;
			return base;
		}
		res_base = lmb.reserved.region[j].base;
		if (res_base < size)
			break;
		base = lmb_align_down(res_base - size, align);
	}

	return ~(u64)0;
}

static u64 __init lmb_alloc_nid_region(struct lmb_property *mp,
				       u64 (*nid_range)(u64, u64, int *),
				       u64 size, u64 align, int nid)
{
	u64 start, end;

	start = mp->base;
	end = start + mp->size;

	start = lmb_align_up(start, align);
	while (start < end) {
		u64 this_end;
		int this_nid;

		this_end = nid_range(start, end, &this_nid);
		if (this_nid == nid) {
			u64 ret = lmb_alloc_nid_unreserved(start, this_end,
							   size, align);
			if (ret != ~(u64)0)
				return ret;
		}
		start = this_end;
	}

	return ~(u64)0;
}

u64 __init lmb_alloc_nid(u64 size, u64 align, int nid,
			 u64 (*nid_range)(u64 start, u64 end, int *nid))
{
	struct lmb_region *mem = &lmb.memory;
	int i;

	for (i = 0; i < mem->cnt; i++) {
		u64 ret = lmb_alloc_nid_region(&mem->region[i],
					       nid_range,
					       size, align, nid);
		if (ret != ~(u64)0)
			return ret;
	}

	return lmb_alloc(size, align);
}

u64 __init lmb_alloc(u64 size, u64 align)
{
	return lmb_alloc_base(size, align, LMB_ALLOC_ANYWHERE);
}

u64 __init lmb_alloc_base(u64 size, u64 align, u64 max_addr)
{
	u64 alloc;

	alloc = __lmb_alloc_base(size, align, max_addr);

	if (alloc == 0)
		panic("ERROR: Failed to allocate 0x%llx bytes below 0x%llx.\n",
		      (unsigned long long) size, (unsigned long long) max_addr);

	return alloc;
}

u64 __init __lmb_alloc_base(u64 size, u64 align, u64 max_addr)
{
	long i, j;
	u64 base = 0;
	u64 res_base;

	BUG_ON(0 == size);

	/* On some platforms, make sure we allocate lowmem */
	/* Note that LMB_REAL_LIMIT may be LMB_ALLOC_ANYWHERE */
	if (max_addr == LMB_ALLOC_ANYWHERE)
		max_addr = LMB_REAL_LIMIT;

	for (i = lmb.memory.cnt - 1; i >= 0; i--) {
		u64 lmbbase = lmb.memory.region[i].base;
		u64 lmbsize = lmb.memory.region[i].size;

		if (lmbsize < size)
			continue;
		if (max_addr == LMB_ALLOC_ANYWHERE)
			base = lmb_align_down(lmbbase + lmbsize - size, align);
		else if (lmbbase < max_addr) {
			base = min(lmbbase + lmbsize, max_addr);
			base = lmb_align_down(base - size, align);
		} else
			continue;

		while (base && lmbbase <= base) {
			j = lmb_overlaps_region(&lmb.reserved, base, size);
			if (j < 0) {
				/* this area isn't reserved, take it */
				if (lmb_add_region(&lmb.reserved, base,
						   size) < 0)
					return 0;
				return base;
			}
			res_base = lmb.reserved.region[j].base;
			if (res_base < size)
				break;
			base = lmb_align_down(res_base - size, align);
		}
	}
	return 0;
}

/* You must call lmb_analyze() before this. */
u64 __init lmb_phys_mem_size(void)
{
	return lmb.memory.size;
}

u64 __init lmb_end_of_DRAM(void)
{
	int idx = lmb.memory.cnt - 1;

	return (lmb.memory.region[idx].base + lmb.memory.region[idx].size);
}

/* You must call lmb_analyze() after this. */
void __init lmb_enforce_memory_limit(u64 memory_limit)
{
	unsigned long i;
	u64 limit;
	struct lmb_property *p;

	if (!memory_limit)
		return;

	/* Truncate the lmb regions to satisfy the memory limit. */
	limit = memory_limit;
	for (i = 0; i < lmb.memory.cnt; i++) {
		if (limit > lmb.memory.region[i].size) {
			limit -= lmb.memory.region[i].size;
			continue;
		}

		lmb.memory.region[i].size = limit;
		lmb.memory.cnt = i + 1;
		break;
	}

	if (lmb.memory.region[0].size < lmb.rmo_size)
		lmb.rmo_size = lmb.memory.region[0].size;

	/* And truncate any reserves above the limit also. */
	for (i = 0; i < lmb.reserved.cnt; i++) {
		p = &lmb.reserved.region[i];

		if (p->base > memory_limit)
			p->size = 0;
		else if ((p->base + p->size) > memory_limit)
			p->size = memory_limit - p->base;

		if (p->size == 0) {
			lmb_remove_region(&lmb.reserved, i);
			i--;
		}
	}
}

int __init lmb_is_reserved(u64 addr)
{
	int i;

	for (i = 0; i < lmb.reserved.cnt; i++) {
		u64 upper = lmb.reserved.region[i].base +
			lmb.reserved.region[i].size - 1;
		if ((addr >= lmb.reserved.region[i].base) && (addr <= upper))
			return 1;
	}
	return 0;
}
