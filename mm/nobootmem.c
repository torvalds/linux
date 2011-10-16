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
#include <linux/slab.h>
#include <linux/bootmem.h>
#include <linux/export.h>
#include <linux/kmemleak.h>
#include <linux/range.h>
#include <linux/memblock.h>

#include <asm/bug.h>
#include <asm/io.h>
#include <asm/processor.h>

#include "internal.h"

#ifndef CONFIG_NEED_MULTIPLE_NODES
struct pglist_data __refdata contig_page_data;
EXPORT_SYMBOL(contig_page_data);
#endif

unsigned long max_low_pfn;
unsigned long min_low_pfn;
unsigned long max_pfn;

static void * __init __alloc_memory_core_early(int nid, u64 size, u64 align,
					u64 goal, u64 limit)
{
	void *ptr;
	u64 addr;

	if (limit > memblock.current_limit)
		limit = memblock.current_limit;

	addr = find_memory_core_early(nid, size, align, goal, limit);

	if (addr == MEMBLOCK_ERROR)
		return NULL;

	ptr = phys_to_virt(addr);
	memset(ptr, 0, size);
	memblock_x86_reserve_range(addr, addr + size, "BOOTMEM");
	/*
	 * The min_count is set to 0 so that bootmem allocated blocks
	 * are never reported as leaks.
	 */
	kmemleak_alloc(ptr, size, 0, 0);
	return ptr;
}

/*
 * free_bootmem_late - free bootmem pages directly to page allocator
 * @addr: starting address of the range
 * @size: size of the range in bytes
 *
 * This is only useful when the bootmem allocator has already been torn
 * down, but we are still initializing the system.  Pages are given directly
 * to the page allocator, no bootmem metadata is updated because it is gone.
 */
void __init free_bootmem_late(unsigned long addr, unsigned long size)
{
	unsigned long cursor, end;

	kmemleak_free_part(__va(addr), size);

	cursor = PFN_UP(addr);
	end = PFN_DOWN(addr + size);

	for (; cursor < end; cursor++) {
		__free_pages_bootmem(pfn_to_page(cursor), 0);
		totalram_pages++;
	}
}

static void __init __free_pages_memory(unsigned long start, unsigned long end)
{
	int i;
	unsigned long start_aligned, end_aligned;
	int order = ilog2(BITS_PER_LONG);

	start_aligned = (start + (BITS_PER_LONG - 1)) & ~(BITS_PER_LONG - 1);
	end_aligned = end & ~(BITS_PER_LONG - 1);

	if (end_aligned <= start_aligned) {
		for (i = start; i < end; i++)
			__free_pages_bootmem(pfn_to_page(i), 0);

		return;
	}

	for (i = start; i < start_aligned; i++)
		__free_pages_bootmem(pfn_to_page(i), 0);

	for (i = start_aligned; i < end_aligned; i += BITS_PER_LONG)
		__free_pages_bootmem(pfn_to_page(i), order);

	for (i = end_aligned; i < end; i++)
		__free_pages_bootmem(pfn_to_page(i), 0);
}

unsigned long __init free_all_memory_core_early(int nodeid)
{
	int i;
	u64 start, end;
	unsigned long count = 0;
	struct range *range = NULL;
	int nr_range;

	nr_range = get_free_all_memory_range(&range, nodeid);

	for (i = 0; i < nr_range; i++) {
		start = range[i].start;
		end = range[i].end;
		count += end - start;
		__free_pages_memory(start, end);
	}

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

	/* free_all_memory_core_early(MAX_NUMNODES) will be called later */
	return 0;
}

/**
 * free_all_bootmem - release free pages to the buddy allocator
 *
 * Returns the number of pages actually released.
 */
unsigned long __init free_all_bootmem(void)
{
	/*
	 * We need to use MAX_NUMNODES instead of NODE_DATA(0)->node_id
	 *  because in some case like Node0 doesn't have RAM installed
	 *  low ram will be on Node1
	 * Use MAX_NUMNODES will make sure all ranges in early_node_map[]
	 *  will be used instead of only Node0 related
	 */
	return free_all_memory_core_early(MAX_NUMNODES);
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
	kmemleak_free_part(__va(physaddr), size);
	memblock_x86_free_range(physaddr, physaddr + size);
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
	kmemleak_free_part(__va(addr), size);
	memblock_x86_free_range(addr, addr + size);
}

static void * __init ___alloc_bootmem_nopanic(unsigned long size,
					unsigned long align,
					unsigned long goal,
					unsigned long limit)
{
	void *ptr;

	if (WARN_ON_ONCE(slab_is_available()))
		return kzalloc(size, GFP_NOWAIT);

restart:

	ptr = __alloc_memory_core_early(MAX_NUMNODES, size, align, goal, limit);

	if (ptr)
		return ptr;

	if (goal != 0) {
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
	unsigned long limit = -1UL;

	return ___alloc_bootmem_nopanic(size, align, goal, limit);
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
	unsigned long limit = -1UL;

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
	void *ptr;

	if (WARN_ON_ONCE(slab_is_available()))
		return kzalloc_node(size, GFP_NOWAIT, pgdat->node_id);

	ptr = __alloc_memory_core_early(pgdat->node_id, size, align,
					 goal, -1ULL);
	if (ptr)
		return ptr;

	return __alloc_memory_core_early(MAX_NUMNODES, size, align,
					 goal, -1ULL);
}

void * __init __alloc_bootmem_node_high(pg_data_t *pgdat, unsigned long size,
				   unsigned long align, unsigned long goal)
{
	return __alloc_bootmem_node(pgdat, size, align, goal);
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
	unsigned long pfn, goal, limit;

	pfn = section_nr_to_pfn(section_nr);
	goal = pfn << PAGE_SHIFT;
	limit = section_nr_to_pfn(section_nr + 1) << PAGE_SHIFT;

	return __alloc_memory_core_early(early_pfn_to_nid(pfn), size,
					 SMP_CACHE_BYTES, goal, limit);
}
#endif

void * __init __alloc_bootmem_node_nopanic(pg_data_t *pgdat, unsigned long size,
				   unsigned long align, unsigned long goal)
{
	void *ptr;

	if (WARN_ON_ONCE(slab_is_available()))
		return kzalloc_node(size, GFP_NOWAIT, pgdat->node_id);

	ptr =  __alloc_memory_core_early(pgdat->node_id, size, align,
						 goal, -1ULL);
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
	void *ptr;

	if (WARN_ON_ONCE(slab_is_available()))
		return kzalloc_node(size, GFP_NOWAIT, pgdat->node_id);

	ptr = __alloc_memory_core_early(pgdat->node_id, size, align,
				goal, ARCH_LOW_ADDRESS_LIMIT);
	if (ptr)
		return ptr;

	return  __alloc_memory_core_early(MAX_NUMNODES, size, align,
				goal, ARCH_LOW_ADDRESS_LIMIT);
}
