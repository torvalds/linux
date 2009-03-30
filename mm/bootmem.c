/*
 *  bootmem - A boot-time physical memory allocator and configurator
 *
 *  Copyright (C) 1999 Ingo Molnar
 *                1999 Kanoj Sarcar, SGI
 *                2008 Johannes Weiner
 *
 * Access to this subsystem has to be serialized externally (which is true
 * for the boot process anyway).
 */
#include <linux/init.h>
#include <linux/pfn.h>
#include <linux/bootmem.h>
#include <linux/module.h>

#include <asm/bug.h>
#include <asm/io.h>
#include <asm/processor.h>

#include "internal.h"

unsigned long max_low_pfn;
unsigned long min_low_pfn;
unsigned long max_pfn;

#ifdef CONFIG_CRASH_DUMP
/*
 * If we have booted due to a crash, max_pfn will be a very low value. We need
 * to know the amount of memory that the previous kernel used.
 */
unsigned long saved_max_pfn;
#endif

bootmem_data_t bootmem_node_data[MAX_NUMNODES] __initdata;

static struct list_head bdata_list __initdata = LIST_HEAD_INIT(bdata_list);

static int bootmem_debug;

static int __init bootmem_debug_setup(char *buf)
{
	bootmem_debug = 1;
	return 0;
}
early_param("bootmem_debug", bootmem_debug_setup);

#define bdebug(fmt, args...) ({				\
	if (unlikely(bootmem_debug))			\
		printk(KERN_INFO			\
			"bootmem::%s " fmt,		\
			__func__, ## args);		\
})

static unsigned long __init bootmap_bytes(unsigned long pages)
{
	unsigned long bytes = (pages + 7) / 8;

	return ALIGN(bytes, sizeof(long));
}

/**
 * bootmem_bootmap_pages - calculate bitmap size in pages
 * @pages: number of pages the bitmap has to represent
 */
unsigned long __init bootmem_bootmap_pages(unsigned long pages)
{
	unsigned long bytes = bootmap_bytes(pages);

	return PAGE_ALIGN(bytes) >> PAGE_SHIFT;
}

/*
 * link bdata in order
 */
static void __init link_bootmem(bootmem_data_t *bdata)
{
	struct list_head *iter;

	list_for_each(iter, &bdata_list) {
		bootmem_data_t *ent;

		ent = list_entry(iter, bootmem_data_t, list);
		if (bdata->node_min_pfn < ent->node_min_pfn)
			break;
	}
	list_add_tail(&bdata->list, iter);
}

/*
 * Called once to set up the allocator itself.
 */
static unsigned long __init init_bootmem_core(bootmem_data_t *bdata,
	unsigned long mapstart, unsigned long start, unsigned long end)
{
	unsigned long mapsize;

	mminit_validate_memmodel_limits(&start, &end);
	bdata->node_bootmem_map = phys_to_virt(PFN_PHYS(mapstart));
	bdata->node_min_pfn = start;
	bdata->node_low_pfn = end;
	link_bootmem(bdata);

	/*
	 * Initially all pages are reserved - setup_arch() has to
	 * register free RAM areas explicitly.
	 */
	mapsize = bootmap_bytes(end - start);
	memset(bdata->node_bootmem_map, 0xff, mapsize);

	bdebug("nid=%td start=%lx map=%lx end=%lx mapsize=%lx\n",
		bdata - bootmem_node_data, start, mapstart, end, mapsize);

	return mapsize;
}

/**
 * init_bootmem_node - register a node as boot memory
 * @pgdat: node to register
 * @freepfn: pfn where the bitmap for this node is to be placed
 * @startpfn: first pfn on the node
 * @endpfn: first pfn after the node
 *
 * Returns the number of bytes needed to hold the bitmap for this node.
 */
unsigned long __init init_bootmem_node(pg_data_t *pgdat, unsigned long freepfn,
				unsigned long startpfn, unsigned long endpfn)
{
	return init_bootmem_core(pgdat->bdata, freepfn, startpfn, endpfn);
}

/**
 * init_bootmem - register boot memory
 * @start: pfn where the bitmap is to be placed
 * @pages: number of available physical pages
 *
 * Returns the number of bytes needed to hold the bitmap.
 */
unsigned long __init init_bootmem(unsigned long start, unsigned long pages)
{
	max_low_pfn = pages;
	min_low_pfn = start;
	return init_bootmem_core(NODE_DATA(0)->bdata, start, 0, pages);
}

static unsigned long __init free_all_bootmem_core(bootmem_data_t *bdata)
{
	int aligned;
	struct page *page;
	unsigned long start, end, pages, count = 0;

	if (!bdata->node_bootmem_map)
		return 0;

	start = bdata->node_min_pfn;
	end = bdata->node_low_pfn;

	/*
	 * If the start is aligned to the machines wordsize, we might
	 * be able to free pages in bulks of that order.
	 */
	aligned = !(start & (BITS_PER_LONG - 1));

	bdebug("nid=%td start=%lx end=%lx aligned=%d\n",
		bdata - bootmem_node_data, start, end, aligned);

	while (start < end) {
		unsigned long *map, idx, vec;

		map = bdata->node_bootmem_map;
		idx = start - bdata->node_min_pfn;
		vec = ~map[idx / BITS_PER_LONG];

		if (aligned && vec == ~0UL && start + BITS_PER_LONG < end) {
			int order = ilog2(BITS_PER_LONG);

			__free_pages_bootmem(pfn_to_page(start), order);
			count += BITS_PER_LONG;
		} else {
			unsigned long off = 0;

			while (vec && off < BITS_PER_LONG) {
				if (vec & 1) {
					page = pfn_to_page(start + off);
					__free_pages_bootmem(page, 0);
					count++;
				}
				vec >>= 1;
				off++;
			}
		}
		start += BITS_PER_LONG;
	}

	page = virt_to_page(bdata->node_bootmem_map);
	pages = bdata->node_low_pfn - bdata->node_min_pfn;
	pages = bootmem_bootmap_pages(pages);
	count += pages;
	while (pages--)
		__free_pages_bootmem(page++, 0);

	bdebug("nid=%td released=%lx\n", bdata - bootmem_node_data, count);

	return count;
}

/**
 * free_all_bootmem_node - release a node's free pages to the buddy allocator
 * @pgdat: node to be released
 *
 * Returns the number of pages actually released.
 */
unsigned long __init free_all_bootmem_node(pg_data_t *pgdat)
{
	register_page_bootmem_info_node(pgdat);
	return free_all_bootmem_core(pgdat->bdata);
}

/**
 * free_all_bootmem - release free pages to the buddy allocator
 *
 * Returns the number of pages actually released.
 */
unsigned long __init free_all_bootmem(void)
{
	return free_all_bootmem_core(NODE_DATA(0)->bdata);
}

static void __init __free(bootmem_data_t *bdata,
			unsigned long sidx, unsigned long eidx)
{
	unsigned long idx;

	bdebug("nid=%td start=%lx end=%lx\n", bdata - bootmem_node_data,
		sidx + bdata->node_min_pfn,
		eidx + bdata->node_min_pfn);

	if (bdata->hint_idx > sidx)
		bdata->hint_idx = sidx;

	for (idx = sidx; idx < eidx; idx++)
		if (!test_and_clear_bit(idx, bdata->node_bootmem_map))
			BUG();
}

static int __init __reserve(bootmem_data_t *bdata, unsigned long sidx,
			unsigned long eidx, int flags)
{
	unsigned long idx;
	int exclusive = flags & BOOTMEM_EXCLUSIVE;

	bdebug("nid=%td start=%lx end=%lx flags=%x\n",
		bdata - bootmem_node_data,
		sidx + bdata->node_min_pfn,
		eidx + bdata->node_min_pfn,
		flags);

	for (idx = sidx; idx < eidx; idx++)
		if (test_and_set_bit(idx, bdata->node_bootmem_map)) {
			if (exclusive) {
				__free(bdata, sidx, idx);
				return -EBUSY;
			}
			bdebug("silent double reserve of PFN %lx\n",
				idx + bdata->node_min_pfn);
		}
	return 0;
}

static int __init mark_bootmem_node(bootmem_data_t *bdata,
				unsigned long start, unsigned long end,
				int reserve, int flags)
{
	unsigned long sidx, eidx;

	bdebug("nid=%td start=%lx end=%lx reserve=%d flags=%x\n",
		bdata - bootmem_node_data, start, end, reserve, flags);

	BUG_ON(start < bdata->node_min_pfn);
	BUG_ON(end > bdata->node_low_pfn);

	sidx = start - bdata->node_min_pfn;
	eidx = end - bdata->node_min_pfn;

	if (reserve)
		return __reserve(bdata, sidx, eidx, flags);
	else
		__free(bdata, sidx, eidx);
	return 0;
}

static int __init mark_bootmem(unsigned long start, unsigned long end,
				int reserve, int flags)
{
	unsigned long pos;
	bootmem_data_t *bdata;

	pos = start;
	list_for_each_entry(bdata, &bdata_list, list) {
		int err;
		unsigned long max;

		if (pos < bdata->node_min_pfn ||
		    pos >= bdata->node_low_pfn) {
			BUG_ON(pos != start);
			continue;
		}

		max = min(bdata->node_low_pfn, end);

		err = mark_bootmem_node(bdata, pos, max, reserve, flags);
		if (reserve && err) {
			mark_bootmem(start, pos, 0, 0);
			return err;
		}

		if (max == end)
			return 0;
		pos = bdata->node_low_pfn;
	}
	BUG();
}

/**
 * free_bootmem_node - mark a page range as usable
 * @pgdat: node the range resides on
 * @physaddr: starting address of the range
 * @size: size of the range in bytes
 *
 * Partial pages will be considered reserved and left as they are.
 *
 * The range must reside completely on the specified node.
 */
void __init free_bootmem_node(pg_data_t *pgdat, unsigned long physaddr,
			      unsigned long size)
{
	unsigned long start, end;

	start = PFN_UP(physaddr);
	end = PFN_DOWN(physaddr + size);

	mark_bootmem_node(pgdat->bdata, start, end, 0, 0);
}

/**
 * free_bootmem - mark a page range as usable
 * @addr: starting address of the range
 * @size: size of the range in bytes
 *
 * Partial pages will be considered reserved and left as they are.
 *
 * The range must be contiguous but may span node boundaries.
 */
void __init free_bootmem(unsigned long addr, unsigned long size)
{
	unsigned long start, end;

	start = PFN_UP(addr);
	end = PFN_DOWN(addr + size);

	mark_bootmem(start, end, 0, 0);
}

/**
 * reserve_bootmem_node - mark a page range as reserved
 * @pgdat: node the range resides on
 * @physaddr: starting address of the range
 * @size: size of the range in bytes
 * @flags: reservation flags (see linux/bootmem.h)
 *
 * Partial pages will be reserved.
 *
 * The range must reside completely on the specified node.
 */
int __init reserve_bootmem_node(pg_data_t *pgdat, unsigned long physaddr,
				 unsigned long size, int flags)
{
	unsigned long start, end;

	start = PFN_DOWN(physaddr);
	end = PFN_UP(physaddr + size);

	return mark_bootmem_node(pgdat->bdata, start, end, 1, flags);
}

/**
 * reserve_bootmem - mark a page range as usable
 * @addr: starting address of the range
 * @size: size of the range in bytes
 * @flags: reservation flags (see linux/bootmem.h)
 *
 * Partial pages will be reserved.
 *
 * The range must be contiguous but may span node boundaries.
 */
int __init reserve_bootmem(unsigned long addr, unsigned long size,
			    int flags)
{
	unsigned long start, end;

	start = PFN_DOWN(addr);
	end = PFN_UP(addr + size);

	return mark_bootmem(start, end, 1, flags);
}

static unsigned long align_idx(struct bootmem_data *bdata, unsigned long idx,
			unsigned long step)
{
	unsigned long base = bdata->node_min_pfn;

	/*
	 * Align the index with respect to the node start so that the
	 * combination of both satisfies the requested alignment.
	 */

	return ALIGN(base + idx, step) - base;
}

static unsigned long align_off(struct bootmem_data *bdata, unsigned long off,
			unsigned long align)
{
	unsigned long base = PFN_PHYS(bdata->node_min_pfn);

	/* Same as align_idx for byte offsets */

	return ALIGN(base + off, align) - base;
}

static void * __init alloc_bootmem_core(struct bootmem_data *bdata,
					unsigned long size, unsigned long align,
					unsigned long goal, unsigned long limit)
{
	unsigned long fallback = 0;
	unsigned long min, max, start, sidx, midx, step;

	bdebug("nid=%td size=%lx [%lu pages] align=%lx goal=%lx limit=%lx\n",
		bdata - bootmem_node_data, size, PAGE_ALIGN(size) >> PAGE_SHIFT,
		align, goal, limit);

	BUG_ON(!size);
	BUG_ON(align & (align - 1));
	BUG_ON(limit && goal + size > limit);

	if (!bdata->node_bootmem_map)
		return NULL;

	min = bdata->node_min_pfn;
	max = bdata->node_low_pfn;

	goal >>= PAGE_SHIFT;
	limit >>= PAGE_SHIFT;

	if (limit && max > limit)
		max = limit;
	if (max <= min)
		return NULL;

	step = max(align >> PAGE_SHIFT, 1UL);

	if (goal && min < goal && goal < max)
		start = ALIGN(goal, step);
	else
		start = ALIGN(min, step);

	sidx = start - bdata->node_min_pfn;
	midx = max - bdata->node_min_pfn;

	if (bdata->hint_idx > sidx) {
		/*
		 * Handle the valid case of sidx being zero and still
		 * catch the fallback below.
		 */
		fallback = sidx + 1;
		sidx = align_idx(bdata, bdata->hint_idx, step);
	}

	while (1) {
		int merge;
		void *region;
		unsigned long eidx, i, start_off, end_off;
find_block:
		sidx = find_next_zero_bit(bdata->node_bootmem_map, midx, sidx);
		sidx = align_idx(bdata, sidx, step);
		eidx = sidx + PFN_UP(size);

		if (sidx >= midx || eidx > midx)
			break;

		for (i = sidx; i < eidx; i++)
			if (test_bit(i, bdata->node_bootmem_map)) {
				sidx = align_idx(bdata, i, step);
				if (sidx == i)
					sidx += step;
				goto find_block;
			}

		if (bdata->last_end_off & (PAGE_SIZE - 1) &&
				PFN_DOWN(bdata->last_end_off) + 1 == sidx)
			start_off = align_off(bdata, bdata->last_end_off, align);
		else
			start_off = PFN_PHYS(sidx);

		merge = PFN_DOWN(start_off) < sidx;
		end_off = start_off + size;

		bdata->last_end_off = end_off;
		bdata->hint_idx = PFN_UP(end_off);

		/*
		 * Reserve the area now:
		 */
		if (__reserve(bdata, PFN_DOWN(start_off) + merge,
				PFN_UP(end_off), BOOTMEM_EXCLUSIVE))
			BUG();

		region = phys_to_virt(PFN_PHYS(bdata->node_min_pfn) +
				start_off);
		memset(region, 0, size);
		return region;
	}

	if (fallback) {
		sidx = align_idx(bdata, fallback - 1, step);
		fallback = 0;
		goto find_block;
	}

	return NULL;
}

static void * __init alloc_arch_preferred_bootmem(bootmem_data_t *bdata,
					unsigned long size, unsigned long align,
					unsigned long goal, unsigned long limit)
{
#ifdef CONFIG_HAVE_ARCH_BOOTMEM
	bootmem_data_t *p_bdata;

	p_bdata = bootmem_arch_preferred_node(bdata, size, align, goal, limit);
	if (p_bdata)
		return alloc_bootmem_core(p_bdata, size, align, goal, limit);
#endif
	return NULL;
}

static void * __init ___alloc_bootmem_nopanic(unsigned long size,
					unsigned long align,
					unsigned long goal,
					unsigned long limit)
{
	bootmem_data_t *bdata;
	void *region;

restart:
	region = alloc_arch_preferred_bootmem(NULL, size, align, goal, limit);
	if (region)
		return region;

	list_for_each_entry(bdata, &bdata_list, list) {
		if (goal && bdata->node_low_pfn <= PFN_DOWN(goal))
			continue;
		if (limit && bdata->node_min_pfn >= PFN_DOWN(limit))
			break;

		region = alloc_bootmem_core(bdata, size, align, goal, limit);
		if (region)
			return region;
	}

	if (goal) {
		goal = 0;
		goto restart;
	}

	return NULL;
}

/**
 * __alloc_bootmem_nopanic - allocate boot memory without panicking
 * @size: size of the request in bytes
 * @align: alignment of the region
 * @goal: preferred starting address of the region
 *
 * The goal is dropped if it can not be satisfied and the allocation will
 * fall back to memory below @goal.
 *
 * Allocation may happen on any node in the system.
 *
 * Returns NULL on failure.
 */
void * __init __alloc_bootmem_nopanic(unsigned long size, unsigned long align,
					unsigned long goal)
{
	return ___alloc_bootmem_nopanic(size, align, goal, 0);
}

static void * __init ___alloc_bootmem(unsigned long size, unsigned long align,
					unsigned long goal, unsigned long limit)
{
	void *mem = ___alloc_bootmem_nopanic(size, align, goal, limit);

	if (mem)
		return mem;
	/*
	 * Whoops, we cannot satisfy the allocation request.
	 */
	printk(KERN_ALERT "bootmem alloc of %lu bytes failed!\n", size);
	panic("Out of memory");
	return NULL;
}

/**
 * __alloc_bootmem - allocate boot memory
 * @size: size of the request in bytes
 * @align: alignment of the region
 * @goal: preferred starting address of the region
 *
 * The goal is dropped if it can not be satisfied and the allocation will
 * fall back to memory below @goal.
 *
 * Allocation may happen on any node in the system.
 *
 * The function panics if the request can not be satisfied.
 */
void * __init __alloc_bootmem(unsigned long size, unsigned long align,
			      unsigned long goal)
{
	return ___alloc_bootmem(size, align, goal, 0);
}

static void * __init ___alloc_bootmem_node(bootmem_data_t *bdata,
				unsigned long size, unsigned long align,
				unsigned long goal, unsigned long limit)
{
	void *ptr;

	ptr = alloc_arch_preferred_bootmem(bdata, size, align, goal, limit);
	if (ptr)
		return ptr;

	ptr = alloc_bootmem_core(bdata, size, align, goal, limit);
	if (ptr)
		return ptr;

	return ___alloc_bootmem(size, align, goal, limit);
}

/**
 * __alloc_bootmem_node - allocate boot memory from a specific node
 * @pgdat: node to allocate from
 * @size: size of the request in bytes
 * @align: alignment of the region
 * @goal: preferred starting address of the region
 *
 * The goal is dropped if it can not be satisfied and the allocation will
 * fall back to memory below @goal.
 *
 * Allocation may fall back to any node in the system if the specified node
 * can not hold the requested memory.
 *
 * The function panics if the request can not be satisfied.
 */
void * __init __alloc_bootmem_node(pg_data_t *pgdat, unsigned long size,
				   unsigned long align, unsigned long goal)
{
	return ___alloc_bootmem_node(pgdat->bdata, size, align, goal, 0);
}

#ifdef CONFIG_SPARSEMEM
/**
 * alloc_bootmem_section - allocate boot memory from a specific section
 * @size: size of the request in bytes
 * @section_nr: sparse map section to allocate from
 *
 * Return NULL on failure.
 */
void * __init alloc_bootmem_section(unsigned long size,
				    unsigned long section_nr)
{
	bootmem_data_t *bdata;
	unsigned long pfn, goal, limit;

	pfn = section_nr_to_pfn(section_nr);
	goal = pfn << PAGE_SHIFT;
	limit = section_nr_to_pfn(section_nr + 1) << PAGE_SHIFT;
	bdata = &bootmem_node_data[early_pfn_to_nid(pfn)];

	return alloc_bootmem_core(bdata, size, SMP_CACHE_BYTES, goal, limit);
}
#endif

void * __init __alloc_bootmem_node_nopanic(pg_data_t *pgdat, unsigned long size,
				   unsigned long align, unsigned long goal)
{
	void *ptr;

	ptr = alloc_arch_preferred_bootmem(pgdat->bdata, size, align, goal, 0);
	if (ptr)
		return ptr;

	ptr = alloc_bootmem_core(pgdat->bdata, size, align, goal, 0);
	if (ptr)
		return ptr;

	return __alloc_bootmem_nopanic(size, align, goal);
}

#ifndef ARCH_LOW_ADDRESS_LIMIT
#define ARCH_LOW_ADDRESS_LIMIT	0xffffffffUL
#endif

/**
 * __alloc_bootmem_low - allocate low boot memory
 * @size: size of the request in bytes
 * @align: alignment of the region
 * @goal: preferred starting address of the region
 *
 * The goal is dropped if it can not be satisfied and the allocation will
 * fall back to memory below @goal.
 *
 * Allocation may happen on any node in the system.
 *
 * The function panics if the request can not be satisfied.
 */
void * __init __alloc_bootmem_low(unsigned long size, unsigned long align,
				  unsigned long goal)
{
	return ___alloc_bootmem(size, align, goal, ARCH_LOW_ADDRESS_LIMIT);
}

/**
 * __alloc_bootmem_low_node - allocate low boot memory from a specific node
 * @pgdat: node to allocate from
 * @size: size of the request in bytes
 * @align: alignment of the region
 * @goal: preferred starting address of the region
 *
 * The goal is dropped if it can not be satisfied and the allocation will
 * fall back to memory below @goal.
 *
 * Allocation may fall back to any node in the system if the specified node
 * can not hold the requested memory.
 *
 * The function panics if the request can not be satisfied.
 */
void * __init __alloc_bootmem_low_node(pg_data_t *pgdat, unsigned long size,
				       unsigned long align, unsigned long goal)
{
	return ___alloc_bootmem_node(pgdat->bdata, size, align,
				goal, ARCH_LOW_ADDRESS_LIMIT);
}
