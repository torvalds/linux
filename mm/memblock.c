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
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/bitops.h>
#include <linux/poison.h>
#include <linux/pfn.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/memblock.h>

struct memblock memblock __initdata_memblock;

int memblock_debug __initdata_memblock;
int memblock_can_resize __initdata_memblock;
static struct memblock_region memblock_memory_init_regions[INIT_MEMBLOCK_REGIONS + 1] __initdata_memblock;
static struct memblock_region memblock_reserved_init_regions[INIT_MEMBLOCK_REGIONS + 1] __initdata_memblock;

/* inline so we don't get a warning when pr_debug is compiled out */
static inline const char *memblock_type_name(struct memblock_type *type)
{
	if (type == &memblock.memory)
		return "memory";
	else if (type == &memblock.reserved)
		return "reserved";
	else
		return "unknown";
}

/*
 * Address comparison utilities
 */

static phys_addr_t __init_memblock memblock_align_down(phys_addr_t addr, phys_addr_t size)
{
	return addr & ~(size - 1);
}

static phys_addr_t __init_memblock memblock_align_up(phys_addr_t addr, phys_addr_t size)
{
	return (addr + (size - 1)) & ~(size - 1);
}

static unsigned long __init_memblock memblock_addrs_overlap(phys_addr_t base1, phys_addr_t size1,
				       phys_addr_t base2, phys_addr_t size2)
{
	return ((base1 < (base2 + size2)) && (base2 < (base1 + size1)));
}

static long __init_memblock memblock_addrs_adjacent(phys_addr_t base1, phys_addr_t size1,
			       phys_addr_t base2, phys_addr_t size2)
{
	if (base2 == base1 + size1)
		return 1;
	else if (base1 == base2 + size2)
		return -1;

	return 0;
}

static long __init_memblock memblock_regions_adjacent(struct memblock_type *type,
				 unsigned long r1, unsigned long r2)
{
	phys_addr_t base1 = type->regions[r1].base;
	phys_addr_t size1 = type->regions[r1].size;
	phys_addr_t base2 = type->regions[r2].base;
	phys_addr_t size2 = type->regions[r2].size;

	return memblock_addrs_adjacent(base1, size1, base2, size2);
}

long __init_memblock memblock_overlaps_region(struct memblock_type *type, phys_addr_t base, phys_addr_t size)
{
	unsigned long i;

	for (i = 0; i < type->cnt; i++) {
		phys_addr_t rgnbase = type->regions[i].base;
		phys_addr_t rgnsize = type->regions[i].size;
		if (memblock_addrs_overlap(base, size, rgnbase, rgnsize))
			break;
	}

	return (i < type->cnt) ? i : -1;
}

/*
 * Find, allocate, deallocate or reserve unreserved regions. All allocations
 * are top-down.
 */

static phys_addr_t __init memblock_find_region(phys_addr_t start, phys_addr_t end,
					  phys_addr_t size, phys_addr_t align)
{
	phys_addr_t base, res_base;
	long j;

	/* In case, huge size is requested */
	if (end < size)
		return MEMBLOCK_ERROR;

	base = memblock_align_down((end - size), align);

	/* Prevent allocations returning 0 as it's also used to
	 * indicate an allocation failure
	 */
	if (start == 0)
		start = PAGE_SIZE;

	while (start <= base) {
		j = memblock_overlaps_region(&memblock.reserved, base, size);
		if (j < 0)
			return base;
		res_base = memblock.reserved.regions[j].base;
		if (res_base < size)
			break;
		base = memblock_align_down(res_base - size, align);
	}

	return MEMBLOCK_ERROR;
}

static phys_addr_t __init_memblock memblock_find_base(phys_addr_t size,
			phys_addr_t align, phys_addr_t start, phys_addr_t end)
{
	long i;

	BUG_ON(0 == size);

	size = memblock_align_up(size, align);

	/* Pump up max_addr */
	if (end == MEMBLOCK_ALLOC_ACCESSIBLE)
		end = memblock.current_limit;

	/* We do a top-down search, this tends to limit memory
	 * fragmentation by keeping early boot allocs near the
	 * top of memory
	 */
	for (i = memblock.memory.cnt - 1; i >= 0; i--) {
		phys_addr_t memblockbase = memblock.memory.regions[i].base;
		phys_addr_t memblocksize = memblock.memory.regions[i].size;
		phys_addr_t bottom, top, found;

		if (memblocksize < size)
			continue;
		if ((memblockbase + memblocksize) <= start)
			break;
		bottom = max(memblockbase, start);
		top = min(memblockbase + memblocksize, end);
		if (bottom >= top)
			continue;
		found = memblock_find_region(bottom, top, size, align);
		if (found != MEMBLOCK_ERROR)
			return found;
	}
	return MEMBLOCK_ERROR;
}

/*
 * Find a free area with specified alignment in a specific range.
 */
u64 __init_memblock memblock_find_in_range(u64 start, u64 end, u64 size, u64 align)
{
	return memblock_find_base(size, align, start, end);
}

/*
 * Free memblock.reserved.regions
 */
int __init_memblock memblock_free_reserved_regions(void)
{
	if (memblock.reserved.regions == memblock_reserved_init_regions)
		return 0;

	return memblock_free(__pa(memblock.reserved.regions),
		 sizeof(struct memblock_region) * memblock.reserved.max);
}

/*
 * Reserve memblock.reserved.regions
 */
int __init_memblock memblock_reserve_reserved_regions(void)
{
	if (memblock.reserved.regions == memblock_reserved_init_regions)
		return 0;

	return memblock_reserve(__pa(memblock.reserved.regions),
		 sizeof(struct memblock_region) * memblock.reserved.max);
}

static void __init_memblock memblock_remove_region(struct memblock_type *type, unsigned long r)
{
	unsigned long i;

	for (i = r; i < type->cnt - 1; i++) {
		type->regions[i].base = type->regions[i + 1].base;
		type->regions[i].size = type->regions[i + 1].size;
	}
	type->cnt--;
}

/* Assumption: base addr of region 1 < base addr of region 2 */
static void __init_memblock memblock_coalesce_regions(struct memblock_type *type,
		unsigned long r1, unsigned long r2)
{
	type->regions[r1].size += type->regions[r2].size;
	memblock_remove_region(type, r2);
}

/* Defined below but needed now */
static long memblock_add_region(struct memblock_type *type, phys_addr_t base, phys_addr_t size);

static int __init_memblock memblock_double_array(struct memblock_type *type)
{
	struct memblock_region *new_array, *old_array;
	phys_addr_t old_size, new_size, addr;
	int use_slab = slab_is_available();

	/* We don't allow resizing until we know about the reserved regions
	 * of memory that aren't suitable for allocation
	 */
	if (!memblock_can_resize)
		return -1;

	/* Calculate new doubled size */
	old_size = type->max * sizeof(struct memblock_region);
	new_size = old_size << 1;

	/* Try to find some space for it.
	 *
	 * WARNING: We assume that either slab_is_available() and we use it or
	 * we use MEMBLOCK for allocations. That means that this is unsafe to use
	 * when bootmem is currently active (unless bootmem itself is implemented
	 * on top of MEMBLOCK which isn't the case yet)
	 *
	 * This should however not be an issue for now, as we currently only
	 * call into MEMBLOCK while it's still active, or much later when slab is
	 * active for memory hotplug operations
	 */
	if (use_slab) {
		new_array = kmalloc(new_size, GFP_KERNEL);
		addr = new_array == NULL ? MEMBLOCK_ERROR : __pa(new_array);
	} else
		addr = memblock_find_base(new_size, sizeof(phys_addr_t), 0, MEMBLOCK_ALLOC_ACCESSIBLE);
	if (addr == MEMBLOCK_ERROR) {
		pr_err("memblock: Failed to double %s array from %ld to %ld entries !\n",
		       memblock_type_name(type), type->max, type->max * 2);
		return -1;
	}
	new_array = __va(addr);

	memblock_dbg("memblock: %s array is doubled to %ld at [%#010llx-%#010llx]",
		 memblock_type_name(type), type->max * 2, (u64)addr, (u64)addr + new_size - 1);

	/* Found space, we now need to move the array over before
	 * we add the reserved region since it may be our reserved
	 * array itself that is full.
	 */
	memcpy(new_array, type->regions, old_size);
	memset(new_array + type->max, 0, old_size);
	old_array = type->regions;
	type->regions = new_array;
	type->max <<= 1;

	/* If we use SLAB that's it, we are done */
	if (use_slab)
		return 0;

	/* Add the new reserved region now. Should not fail ! */
	BUG_ON(memblock_add_region(&memblock.reserved, addr, new_size) < 0);

	/* If the array wasn't our static init one, then free it. We only do
	 * that before SLAB is available as later on, we don't know whether
	 * to use kfree or free_bootmem_pages(). Shouldn't be a big deal
	 * anyways
	 */
	if (old_array != memblock_memory_init_regions &&
	    old_array != memblock_reserved_init_regions)
		memblock_free(__pa(old_array), old_size);

	return 0;
}

extern int __init_memblock __weak memblock_memory_can_coalesce(phys_addr_t addr1, phys_addr_t size1,
					  phys_addr_t addr2, phys_addr_t size2)
{
	return 1;
}

static long __init_memblock memblock_add_region(struct memblock_type *type, phys_addr_t base, phys_addr_t size)
{
	unsigned long coalesced = 0;
	long adjacent, i;

	if ((type->cnt == 1) && (type->regions[0].size == 0)) {
		type->regions[0].base = base;
		type->regions[0].size = size;
		return 0;
	}

	/* First try and coalesce this MEMBLOCK with another. */
	for (i = 0; i < type->cnt; i++) {
		phys_addr_t rgnbase = type->regions[i].base;
		phys_addr_t rgnsize = type->regions[i].size;

		if ((rgnbase == base) && (rgnsize == size))
			/* Already have this region, so we're done */
			return 0;

		adjacent = memblock_addrs_adjacent(base, size, rgnbase, rgnsize);
		/* Check if arch allows coalescing */
		if (adjacent != 0 && type == &memblock.memory &&
		    !memblock_memory_can_coalesce(base, size, rgnbase, rgnsize))
			break;
		if (adjacent > 0) {
			type->regions[i].base -= size;
			type->regions[i].size += size;
			coalesced++;
			break;
		} else if (adjacent < 0) {
			type->regions[i].size += size;
			coalesced++;
			break;
		}
	}

	/* If we plugged a hole, we may want to also coalesce with the
	 * next region
	 */
	if ((i < type->cnt - 1) && memblock_regions_adjacent(type, i, i+1) &&
	    ((type != &memblock.memory || memblock_memory_can_coalesce(type->regions[i].base,
							     type->regions[i].size,
							     type->regions[i+1].base,
							     type->regions[i+1].size)))) {
		memblock_coalesce_regions(type, i, i+1);
		coalesced++;
	}

	if (coalesced)
		return coalesced;

	/* If we are out of space, we fail. It's too late to resize the array
	 * but then this shouldn't have happened in the first place.
	 */
	if (WARN_ON(type->cnt >= type->max))
		return -1;

	/* Couldn't coalesce the MEMBLOCK, so add it to the sorted table. */
	for (i = type->cnt - 1; i >= 0; i--) {
		if (base < type->regions[i].base) {
			type->regions[i+1].base = type->regions[i].base;
			type->regions[i+1].size = type->regions[i].size;
		} else {
			type->regions[i+1].base = base;
			type->regions[i+1].size = size;
			break;
		}
	}

	if (base < type->regions[0].base) {
		type->regions[0].base = base;
		type->regions[0].size = size;
	}
	type->cnt++;

	/* The array is full ? Try to resize it. If that fails, we undo
	 * our allocation and return an error
	 */
	if (type->cnt == type->max && memblock_double_array(type)) {
		type->cnt--;
		return -1;
	}

	return 0;
}

long __init_memblock memblock_add(phys_addr_t base, phys_addr_t size)
{
	return memblock_add_region(&memblock.memory, base, size);

}

static long __init_memblock __memblock_remove(struct memblock_type *type, phys_addr_t base, phys_addr_t size)
{
	phys_addr_t rgnbegin, rgnend;
	phys_addr_t end = base + size;
	int i;

	rgnbegin = rgnend = 0; /* supress gcc warnings */

	/* Find the region where (base, size) belongs to */
	for (i=0; i < type->cnt; i++) {
		rgnbegin = type->regions[i].base;
		rgnend = rgnbegin + type->regions[i].size;

		if ((rgnbegin <= base) && (end <= rgnend))
			break;
	}

	/* Didn't find the region */
	if (i == type->cnt)
		return -1;

	/* Check to see if we are removing entire region */
	if ((rgnbegin == base) && (rgnend == end)) {
		memblock_remove_region(type, i);
		return 0;
	}

	/* Check to see if region is matching at the front */
	if (rgnbegin == base) {
		type->regions[i].base = end;
		type->regions[i].size -= size;
		return 0;
	}

	/* Check to see if the region is matching at the end */
	if (rgnend == end) {
		type->regions[i].size -= size;
		return 0;
	}

	/*
	 * We need to split the entry -  adjust the current one to the
	 * beginging of the hole and add the region after hole.
	 */
	type->regions[i].size = base - type->regions[i].base;
	return memblock_add_region(type, end, rgnend - end);
}

long __init_memblock memblock_remove(phys_addr_t base, phys_addr_t size)
{
	return __memblock_remove(&memblock.memory, base, size);
}

long __init_memblock memblock_free(phys_addr_t base, phys_addr_t size)
{
	return __memblock_remove(&memblock.reserved, base, size);
}

long __init_memblock memblock_reserve(phys_addr_t base, phys_addr_t size)
{
	struct memblock_type *_rgn = &memblock.reserved;

	BUG_ON(0 == size);

	return memblock_add_region(_rgn, base, size);
}

phys_addr_t __init __memblock_alloc_base(phys_addr_t size, phys_addr_t align, phys_addr_t max_addr)
{
	phys_addr_t found;

	/* We align the size to limit fragmentation. Without this, a lot of
	 * small allocs quickly eat up the whole reserve array on sparc
	 */
	size = memblock_align_up(size, align);

	found = memblock_find_base(size, align, 0, max_addr);
	if (found != MEMBLOCK_ERROR &&
	    memblock_add_region(&memblock.reserved, found, size) >= 0)
		return found;

	return 0;
}

phys_addr_t __init memblock_alloc_base(phys_addr_t size, phys_addr_t align, phys_addr_t max_addr)
{
	phys_addr_t alloc;

	alloc = __memblock_alloc_base(size, align, max_addr);

	if (alloc == 0)
		panic("ERROR: Failed to allocate 0x%llx bytes below 0x%llx.\n",
		      (unsigned long long) size, (unsigned long long) max_addr);

	return alloc;
}

phys_addr_t __init memblock_alloc(phys_addr_t size, phys_addr_t align)
{
	return memblock_alloc_base(size, align, MEMBLOCK_ALLOC_ACCESSIBLE);
}


/*
 * Additional node-local allocators. Search for node memory is bottom up
 * and walks memblock regions within that node bottom-up as well, but allocation
 * within an memblock region is top-down. XXX I plan to fix that at some stage
 *
 * WARNING: Only available after early_node_map[] has been populated,
 * on some architectures, that is after all the calls to add_active_range()
 * have been done to populate it.
 */

phys_addr_t __weak __init memblock_nid_range(phys_addr_t start, phys_addr_t end, int *nid)
{
#ifdef CONFIG_ARCH_POPULATES_NODE_MAP
	/*
	 * This code originates from sparc which really wants use to walk by addresses
	 * and returns the nid. This is not very convenient for early_pfn_map[] users
	 * as the map isn't sorted yet, and it really wants to be walked by nid.
	 *
	 * For now, I implement the inefficient method below which walks the early
	 * map multiple times. Eventually we may want to use an ARCH config option
	 * to implement a completely different method for both case.
	 */
	unsigned long start_pfn, end_pfn;
	int i;

	for (i = 0; i < MAX_NUMNODES; i++) {
		get_pfn_range_for_nid(i, &start_pfn, &end_pfn);
		if (start < PFN_PHYS(start_pfn) || start >= PFN_PHYS(end_pfn))
			continue;
		*nid = i;
		return min(end, PFN_PHYS(end_pfn));
	}
#endif
	*nid = 0;

	return end;
}

static phys_addr_t __init memblock_alloc_nid_region(struct memblock_region *mp,
					       phys_addr_t size,
					       phys_addr_t align, int nid)
{
	phys_addr_t start, end;

	start = mp->base;
	end = start + mp->size;

	start = memblock_align_up(start, align);
	while (start < end) {
		phys_addr_t this_end;
		int this_nid;

		this_end = memblock_nid_range(start, end, &this_nid);
		if (this_nid == nid) {
			phys_addr_t ret = memblock_find_region(start, this_end, size, align);
			if (ret != MEMBLOCK_ERROR &&
			    memblock_add_region(&memblock.reserved, ret, size) >= 0)
				return ret;
		}
		start = this_end;
	}

	return MEMBLOCK_ERROR;
}

phys_addr_t __init memblock_alloc_nid(phys_addr_t size, phys_addr_t align, int nid)
{
	struct memblock_type *mem = &memblock.memory;
	int i;

	BUG_ON(0 == size);

	/* We align the size to limit fragmentation. Without this, a lot of
	 * small allocs quickly eat up the whole reserve array on sparc
	 */
	size = memblock_align_up(size, align);

	/* We do a bottom-up search for a region with the right
	 * nid since that's easier considering how memblock_nid_range()
	 * works
	 */
	for (i = 0; i < mem->cnt; i++) {
		phys_addr_t ret = memblock_alloc_nid_region(&mem->regions[i],
					       size, align, nid);
		if (ret != MEMBLOCK_ERROR)
			return ret;
	}

	return 0;
}

phys_addr_t __init memblock_alloc_try_nid(phys_addr_t size, phys_addr_t align, int nid)
{
	phys_addr_t res = memblock_alloc_nid(size, align, nid);

	if (res)
		return res;
	return memblock_alloc_base(size, align, MEMBLOCK_ALLOC_ANYWHERE);
}


/*
 * Remaining API functions
 */

/* You must call memblock_analyze() before this. */
phys_addr_t __init memblock_phys_mem_size(void)
{
	return memblock.memory_size;
}

phys_addr_t __init_memblock memblock_end_of_DRAM(void)
{
	int idx = memblock.memory.cnt - 1;

	return (memblock.memory.regions[idx].base + memblock.memory.regions[idx].size);
}

/* You must call memblock_analyze() after this. */
void __init memblock_enforce_memory_limit(phys_addr_t memory_limit)
{
	unsigned long i;
	phys_addr_t limit;
	struct memblock_region *p;

	if (!memory_limit)
		return;

	/* Truncate the memblock regions to satisfy the memory limit. */
	limit = memory_limit;
	for (i = 0; i < memblock.memory.cnt; i++) {
		if (limit > memblock.memory.regions[i].size) {
			limit -= memblock.memory.regions[i].size;
			continue;
		}

		memblock.memory.regions[i].size = limit;
		memblock.memory.cnt = i + 1;
		break;
	}

	memory_limit = memblock_end_of_DRAM();

	/* And truncate any reserves above the limit also. */
	for (i = 0; i < memblock.reserved.cnt; i++) {
		p = &memblock.reserved.regions[i];

		if (p->base > memory_limit)
			p->size = 0;
		else if ((p->base + p->size) > memory_limit)
			p->size = memory_limit - p->base;

		if (p->size == 0) {
			memblock_remove_region(&memblock.reserved, i);
			i--;
		}
	}
}

static int memblock_search(struct memblock_type *type, phys_addr_t addr)
{
	unsigned int left = 0, right = type->cnt;

	do {
		unsigned int mid = (right + left) / 2;

		if (addr < type->regions[mid].base)
			right = mid;
		else if (addr >= (type->regions[mid].base +
				  type->regions[mid].size))
			left = mid + 1;
		else
			return mid;
	} while (left < right);
	return -1;
}

int __init memblock_is_reserved(phys_addr_t addr)
{
	return memblock_search(&memblock.reserved, addr) != -1;
}

int __init_memblock memblock_is_memory(phys_addr_t addr)
{
	return memblock_search(&memblock.memory, addr) != -1;
}

int __init_memblock memblock_is_region_memory(phys_addr_t base, phys_addr_t size)
{
	int idx = memblock_search(&memblock.reserved, base);

	if (idx == -1)
		return 0;
	return memblock.reserved.regions[idx].base <= base &&
		(memblock.reserved.regions[idx].base +
		 memblock.reserved.regions[idx].size) >= (base + size);
}

int __init_memblock memblock_is_region_reserved(phys_addr_t base, phys_addr_t size)
{
	return memblock_overlaps_region(&memblock.reserved, base, size) >= 0;
}


void __init_memblock memblock_set_current_limit(phys_addr_t limit)
{
	memblock.current_limit = limit;
}

static void __init_memblock memblock_dump(struct memblock_type *region, char *name)
{
	unsigned long long base, size;
	int i;

	pr_info(" %s.cnt  = 0x%lx\n", name, region->cnt);

	for (i = 0; i < region->cnt; i++) {
		base = region->regions[i].base;
		size = region->regions[i].size;

		pr_info(" %s[%#x]\t[%#016llx-%#016llx], %#llx bytes\n",
		    name, i, base, base + size - 1, size);
	}
}

void __init_memblock memblock_dump_all(void)
{
	if (!memblock_debug)
		return;

	pr_info("MEMBLOCK configuration:\n");
	pr_info(" memory size = 0x%llx\n", (unsigned long long)memblock.memory_size);

	memblock_dump(&memblock.memory, "memory");
	memblock_dump(&memblock.reserved, "reserved");
}

void __init memblock_analyze(void)
{
	int i;

	/* Check marker in the unused last array entry */
	WARN_ON(memblock_memory_init_regions[INIT_MEMBLOCK_REGIONS].base
		!= (phys_addr_t)RED_INACTIVE);
	WARN_ON(memblock_reserved_init_regions[INIT_MEMBLOCK_REGIONS].base
		!= (phys_addr_t)RED_INACTIVE);

	memblock.memory_size = 0;

	for (i = 0; i < memblock.memory.cnt; i++)
		memblock.memory_size += memblock.memory.regions[i].size;

	/* We allow resizing from there */
	memblock_can_resize = 1;
}

void __init memblock_init(void)
{
	/* Hookup the initial arrays */
	memblock.memory.regions	= memblock_memory_init_regions;
	memblock.memory.max		= INIT_MEMBLOCK_REGIONS;
	memblock.reserved.regions	= memblock_reserved_init_regions;
	memblock.reserved.max	= INIT_MEMBLOCK_REGIONS;

	/* Write a marker in the unused last array entry */
	memblock.memory.regions[INIT_MEMBLOCK_REGIONS].base = (phys_addr_t)RED_INACTIVE;
	memblock.reserved.regions[INIT_MEMBLOCK_REGIONS].base = (phys_addr_t)RED_INACTIVE;

	/* Create a dummy zero size MEMBLOCK which will get coalesced away later.
	 * This simplifies the memblock_add() code below...
	 */
	memblock.memory.regions[0].base = 0;
	memblock.memory.regions[0].size = 0;
	memblock.memory.cnt = 1;

	/* Ditto. */
	memblock.reserved.regions[0].base = 0;
	memblock.reserved.regions[0].size = 0;
	memblock.reserved.cnt = 1;

	memblock.current_limit = MEMBLOCK_ALLOC_ANYWHERE;
}

static int __init early_memblock(char *p)
{
	if (p && strstr(p, "debug"))
		memblock_debug = 1;
	return 0;
}
early_param("memblock", early_memblock);

#if defined(CONFIG_DEBUG_FS) && !defined(ARCH_DISCARD_MEMBLOCK)

static int memblock_debug_show(struct seq_file *m, void *private)
{
	struct memblock_type *type = m->private;
	struct memblock_region *reg;
	int i;

	for (i = 0; i < type->cnt; i++) {
		reg = &type->regions[i];
		seq_printf(m, "%4d: ", i);
		if (sizeof(phys_addr_t) == 4)
			seq_printf(m, "0x%08lx..0x%08lx\n",
				   (unsigned long)reg->base,
				   (unsigned long)(reg->base + reg->size - 1));
		else
			seq_printf(m, "0x%016llx..0x%016llx\n",
				   (unsigned long long)reg->base,
				   (unsigned long long)(reg->base + reg->size - 1));

	}
	return 0;
}

static int memblock_debug_open(struct inode *inode, struct file *file)
{
	return single_open(file, memblock_debug_show, inode->i_private);
}

static const struct file_operations memblock_debug_fops = {
	.open = memblock_debug_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int __init memblock_init_debugfs(void)
{
	struct dentry *root = debugfs_create_dir("memblock", NULL);
	if (!root)
		return -ENXIO;
	debugfs_create_file("memory", S_IRUGO, root, &memblock.memory, &memblock_debug_fops);
	debugfs_create_file("reserved", S_IRUGO, root, &memblock.reserved, &memblock_debug_fops);

	return 0;
}
__initcall(memblock_init_debugfs);

#endif /* CONFIG_DEBUG_FS */
