// SPDX-License-Identifier: GPL-2.0-only
/*
 * mm/percpu.c - percpu memory allocator
 *
 * Copyright (C) 2009		SUSE Linux Products GmbH
 * Copyright (C) 2009		Tejun Heo <tj@kernel.org>
 *
 * Copyright (C) 2017		Facebook Inc.
 * Copyright (C) 2017		Dennis Zhou <dennis@kernel.org>
 *
 * The percpu allocator handles both static and dynamic areas.  Percpu
 * areas are allocated in chunks which are divided into units.  There is
 * a 1-to-1 mapping for units to possible cpus.  These units are grouped
 * based on NUMA properties of the machine.
 *
 *  c0                           c1                         c2
 *  -------------------          -------------------        ------------
 * | u0 | u1 | u2 | u3 |        | u0 | u1 | u2 | u3 |      | u0 | u1 | u
 *  -------------------  ......  -------------------  ....  ------------
 *
 * Allocation is done by offsets into a unit's address space.  Ie., an
 * area of 512 bytes at 6k in c1 occupies 512 bytes at 6k in c1:u0,
 * c1:u1, c1:u2, etc.  On NUMA machines, the mapping may be non-linear
 * and even sparse.  Access is handled by configuring percpu base
 * registers according to the cpu to unit mappings and offsetting the
 * base address using pcpu_unit_size.
 *
 * There is special consideration for the first chunk which must handle
 * the static percpu variables in the kernel image as allocation services
 * are not online yet.  In short, the first chunk is structured like so:
 *
 *                  <Static | [Reserved] | Dynamic>
 *
 * The static data is copied from the original section managed by the
 * linker.  The reserved section, if non-zero, primarily manages static
 * percpu variables from kernel modules.  Finally, the dynamic section
 * takes care of normal allocations.
 *
 * The allocator organizes chunks into lists according to free size and
 * tries to allocate from the fullest chunk first.  Each chunk is managed
 * by a bitmap with metadata blocks.  The allocation map is updated on
 * every allocation and free to reflect the current state while the boundary
 * map is only updated on allocation.  Each metadata block contains
 * information to help mitigate the need to iterate over large portions
 * of the bitmap.  The reverse mapping from page to chunk is stored in
 * the page's index.  Lastly, units are lazily backed and grow in unison.
 *
 * There is a unique conversion that goes on here between bytes and bits.
 * Each bit represents a fragment of size PCPU_MIN_ALLOC_SIZE.  The chunk
 * tracks the number of pages it is responsible for in nr_pages.  Helper
 * functions are used to convert from between the bytes, bits, and blocks.
 * All hints are managed in bits unless explicitly stated.
 *
 * To use this allocator, arch code should do the following:
 *
 * - define __addr_to_pcpu_ptr() and __pcpu_ptr_to_addr() to translate
 *   regular address to percpu pointer and back if they need to be
 *   different from the default
 *
 * - use pcpu_setup_first_chunk() during percpu area initialization to
 *   setup the first chunk containing the kernel static percpu area
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/bitmap.h>
#include <linux/memblock.h>
#include <linux/err.h>
#include <linux/lcm.h>
#include <linux/list.h>
#include <linux/log2.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/percpu.h>
#include <linux/pfn.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/vmalloc.h>
#include <linux/workqueue.h>
#include <linux/kmemleak.h>
#include <linux/sched.h>
#include <linux/sched/mm.h>

#include <asm/cacheflush.h>
#include <asm/sections.h>
#include <asm/tlbflush.h>
#include <asm/io.h>

#define CREATE_TRACE_POINTS
#include <trace/events/percpu.h>

#include "percpu-internal.h"

/* the slots are sorted by free bytes left, 1-31 bytes share the same slot */
#define PCPU_SLOT_BASE_SHIFT		5
/* chunks in slots below this are subject to being sidelined on failed alloc */
#define PCPU_SLOT_FAIL_THRESHOLD	3

#define PCPU_EMPTY_POP_PAGES_LOW	2
#define PCPU_EMPTY_POP_PAGES_HIGH	4

#ifdef CONFIG_SMP
/* default addr <-> pcpu_ptr mapping, override in asm/percpu.h if necessary */
#ifndef __addr_to_pcpu_ptr
#define __addr_to_pcpu_ptr(addr)					\
	(void __percpu *)((unsigned long)(addr) -			\
			  (unsigned long)pcpu_base_addr	+		\
			  (unsigned long)__per_cpu_start)
#endif
#ifndef __pcpu_ptr_to_addr
#define __pcpu_ptr_to_addr(ptr)						\
	(void __force *)((unsigned long)(ptr) +				\
			 (unsigned long)pcpu_base_addr -		\
			 (unsigned long)__per_cpu_start)
#endif
#else	/* CONFIG_SMP */
/* on UP, it's always identity mapped */
#define __addr_to_pcpu_ptr(addr)	(void __percpu *)(addr)
#define __pcpu_ptr_to_addr(ptr)		(void __force *)(ptr)
#endif	/* CONFIG_SMP */

static int pcpu_unit_pages __ro_after_init;
static int pcpu_unit_size __ro_after_init;
static int pcpu_nr_units __ro_after_init;
static int pcpu_atom_size __ro_after_init;
int pcpu_nr_slots __ro_after_init;
static size_t pcpu_chunk_struct_size __ro_after_init;

/* cpus with the lowest and highest unit addresses */
static unsigned int pcpu_low_unit_cpu __ro_after_init;
static unsigned int pcpu_high_unit_cpu __ro_after_init;

/* the address of the first chunk which starts with the kernel static area */
void *pcpu_base_addr __ro_after_init;
EXPORT_SYMBOL_GPL(pcpu_base_addr);

static const int *pcpu_unit_map __ro_after_init;		/* cpu -> unit */
const unsigned long *pcpu_unit_offsets __ro_after_init;	/* cpu -> unit offset */

/* group information, used for vm allocation */
static int pcpu_nr_groups __ro_after_init;
static const unsigned long *pcpu_group_offsets __ro_after_init;
static const size_t *pcpu_group_sizes __ro_after_init;

/*
 * The first chunk which always exists.  Note that unlike other
 * chunks, this one can be allocated and mapped in several different
 * ways and thus often doesn't live in the vmalloc area.
 */
struct pcpu_chunk *pcpu_first_chunk __ro_after_init;

/*
 * Optional reserved chunk.  This chunk reserves part of the first
 * chunk and serves it for reserved allocations.  When the reserved
 * region doesn't exist, the following variable is NULL.
 */
struct pcpu_chunk *pcpu_reserved_chunk __ro_after_init;

DEFINE_SPINLOCK(pcpu_lock);	/* all internal data structures */
static DEFINE_MUTEX(pcpu_alloc_mutex);	/* chunk create/destroy, [de]pop, map ext */

struct list_head *pcpu_slot __ro_after_init; /* chunk list slots */

/* chunks which need their map areas extended, protected by pcpu_lock */
static LIST_HEAD(pcpu_map_extend_chunks);

/*
 * The number of empty populated pages, protected by pcpu_lock.  The
 * reserved chunk doesn't contribute to the count.
 */
int pcpu_nr_empty_pop_pages;

/*
 * The number of populated pages in use by the allocator, protected by
 * pcpu_lock.  This number is kept per a unit per chunk (i.e. when a page gets
 * allocated/deallocated, it is allocated/deallocated in all units of a chunk
 * and increments/decrements this count by 1).
 */
static unsigned long pcpu_nr_populated;

/*
 * Balance work is used to populate or destroy chunks asynchronously.  We
 * try to keep the number of populated free pages between
 * PCPU_EMPTY_POP_PAGES_LOW and HIGH for atomic allocations and at most one
 * empty chunk.
 */
static void pcpu_balance_workfn(struct work_struct *work);
static DECLARE_WORK(pcpu_balance_work, pcpu_balance_workfn);
static bool pcpu_async_enabled __read_mostly;
static bool pcpu_atomic_alloc_failed;

static void pcpu_schedule_balance_work(void)
{
	if (pcpu_async_enabled)
		schedule_work(&pcpu_balance_work);
}

/**
 * pcpu_addr_in_chunk - check if the address is served from this chunk
 * @chunk: chunk of interest
 * @addr: percpu address
 *
 * RETURNS:
 * True if the address is served from this chunk.
 */
static bool pcpu_addr_in_chunk(struct pcpu_chunk *chunk, void *addr)
{
	void *start_addr, *end_addr;

	if (!chunk)
		return false;

	start_addr = chunk->base_addr + chunk->start_offset;
	end_addr = chunk->base_addr + chunk->nr_pages * PAGE_SIZE -
		   chunk->end_offset;

	return addr >= start_addr && addr < end_addr;
}

static int __pcpu_size_to_slot(int size)
{
	int highbit = fls(size);	/* size is in bytes */
	return max(highbit - PCPU_SLOT_BASE_SHIFT + 2, 1);
}

static int pcpu_size_to_slot(int size)
{
	if (size == pcpu_unit_size)
		return pcpu_nr_slots - 1;
	return __pcpu_size_to_slot(size);
}

static int pcpu_chunk_slot(const struct pcpu_chunk *chunk)
{
	const struct pcpu_block_md *chunk_md = &chunk->chunk_md;

	if (chunk->free_bytes < PCPU_MIN_ALLOC_SIZE ||
	    chunk_md->contig_hint == 0)
		return 0;

	return pcpu_size_to_slot(chunk_md->contig_hint * PCPU_MIN_ALLOC_SIZE);
}

/* set the pointer to a chunk in a page struct */
static void pcpu_set_page_chunk(struct page *page, struct pcpu_chunk *pcpu)
{
	page->index = (unsigned long)pcpu;
}

/* obtain pointer to a chunk from a page struct */
static struct pcpu_chunk *pcpu_get_page_chunk(struct page *page)
{
	return (struct pcpu_chunk *)page->index;
}

static int __maybe_unused pcpu_page_idx(unsigned int cpu, int page_idx)
{
	return pcpu_unit_map[cpu] * pcpu_unit_pages + page_idx;
}

static unsigned long pcpu_unit_page_offset(unsigned int cpu, int page_idx)
{
	return pcpu_unit_offsets[cpu] + (page_idx << PAGE_SHIFT);
}

static unsigned long pcpu_chunk_addr(struct pcpu_chunk *chunk,
				     unsigned int cpu, int page_idx)
{
	return (unsigned long)chunk->base_addr +
	       pcpu_unit_page_offset(cpu, page_idx);
}

/*
 * The following are helper functions to help access bitmaps and convert
 * between bitmap offsets to address offsets.
 */
static unsigned long *pcpu_index_alloc_map(struct pcpu_chunk *chunk, int index)
{
	return chunk->alloc_map +
	       (index * PCPU_BITMAP_BLOCK_BITS / BITS_PER_LONG);
}

static unsigned long pcpu_off_to_block_index(int off)
{
	return off / PCPU_BITMAP_BLOCK_BITS;
}

static unsigned long pcpu_off_to_block_off(int off)
{
	return off & (PCPU_BITMAP_BLOCK_BITS - 1);
}

static unsigned long pcpu_block_off_to_off(int index, int off)
{
	return index * PCPU_BITMAP_BLOCK_BITS + off;
}

/*
 * pcpu_next_hint - determine which hint to use
 * @block: block of interest
 * @alloc_bits: size of allocation
 *
 * This determines if we should scan based on the scan_hint or first_free.
 * In general, we want to scan from first_free to fulfill allocations by
 * first fit.  However, if we know a scan_hint at position scan_hint_start
 * cannot fulfill an allocation, we can begin scanning from there knowing
 * the contig_hint will be our fallback.
 */
static int pcpu_next_hint(struct pcpu_block_md *block, int alloc_bits)
{
	/*
	 * The three conditions below determine if we can skip past the
	 * scan_hint.  First, does the scan hint exist.  Second, is the
	 * contig_hint after the scan_hint (possibly not true iff
	 * contig_hint == scan_hint).  Third, is the allocation request
	 * larger than the scan_hint.
	 */
	if (block->scan_hint &&
	    block->contig_hint_start > block->scan_hint_start &&
	    alloc_bits > block->scan_hint)
		return block->scan_hint_start + block->scan_hint;

	return block->first_free;
}

/**
 * pcpu_next_md_free_region - finds the next hint free area
 * @chunk: chunk of interest
 * @bit_off: chunk offset
 * @bits: size of free area
 *
 * Helper function for pcpu_for_each_md_free_region.  It checks
 * block->contig_hint and performs aggregation across blocks to find the
 * next hint.  It modifies bit_off and bits in-place to be consumed in the
 * loop.
 */
static void pcpu_next_md_free_region(struct pcpu_chunk *chunk, int *bit_off,
				     int *bits)
{
	int i = pcpu_off_to_block_index(*bit_off);
	int block_off = pcpu_off_to_block_off(*bit_off);
	struct pcpu_block_md *block;

	*bits = 0;
	for (block = chunk->md_blocks + i; i < pcpu_chunk_nr_blocks(chunk);
	     block++, i++) {
		/* handles contig area across blocks */
		if (*bits) {
			*bits += block->left_free;
			if (block->left_free == PCPU_BITMAP_BLOCK_BITS)
				continue;
			return;
		}

		/*
		 * This checks three things.  First is there a contig_hint to
		 * check.  Second, have we checked this hint before by
		 * comparing the block_off.  Third, is this the same as the
		 * right contig hint.  In the last case, it spills over into
		 * the next block and should be handled by the contig area
		 * across blocks code.
		 */
		*bits = block->contig_hint;
		if (*bits && block->contig_hint_start >= block_off &&
		    *bits + block->contig_hint_start < PCPU_BITMAP_BLOCK_BITS) {
			*bit_off = pcpu_block_off_to_off(i,
					block->contig_hint_start);
			return;
		}
		/* reset to satisfy the second predicate above */
		block_off = 0;

		*bits = block->right_free;
		*bit_off = (i + 1) * PCPU_BITMAP_BLOCK_BITS - block->right_free;
	}
}

/**
 * pcpu_next_fit_region - finds fit areas for a given allocation request
 * @chunk: chunk of interest
 * @alloc_bits: size of allocation
 * @align: alignment of area (max PAGE_SIZE)
 * @bit_off: chunk offset
 * @bits: size of free area
 *
 * Finds the next free region that is viable for use with a given size and
 * alignment.  This only returns if there is a valid area to be used for this
 * allocation.  block->first_free is returned if the allocation request fits
 * within the block to see if the request can be fulfilled prior to the contig
 * hint.
 */
static void pcpu_next_fit_region(struct pcpu_chunk *chunk, int alloc_bits,
				 int align, int *bit_off, int *bits)
{
	int i = pcpu_off_to_block_index(*bit_off);
	int block_off = pcpu_off_to_block_off(*bit_off);
	struct pcpu_block_md *block;

	*bits = 0;
	for (block = chunk->md_blocks + i; i < pcpu_chunk_nr_blocks(chunk);
	     block++, i++) {
		/* handles contig area across blocks */
		if (*bits) {
			*bits += block->left_free;
			if (*bits >= alloc_bits)
				return;
			if (block->left_free == PCPU_BITMAP_BLOCK_BITS)
				continue;
		}

		/* check block->contig_hint */
		*bits = ALIGN(block->contig_hint_start, align) -
			block->contig_hint_start;
		/*
		 * This uses the block offset to determine if this has been
		 * checked in the prior iteration.
		 */
		if (block->contig_hint &&
		    block->contig_hint_start >= block_off &&
		    block->contig_hint >= *bits + alloc_bits) {
			int start = pcpu_next_hint(block, alloc_bits);

			*bits += alloc_bits + block->contig_hint_start -
				 start;
			*bit_off = pcpu_block_off_to_off(i, start);
			return;
		}
		/* reset to satisfy the second predicate above */
		block_off = 0;

		*bit_off = ALIGN(PCPU_BITMAP_BLOCK_BITS - block->right_free,
				 align);
		*bits = PCPU_BITMAP_BLOCK_BITS - *bit_off;
		*bit_off = pcpu_block_off_to_off(i, *bit_off);
		if (*bits >= alloc_bits)
			return;
	}

	/* no valid offsets were found - fail condition */
	*bit_off = pcpu_chunk_map_bits(chunk);
}

/*
 * Metadata free area iterators.  These perform aggregation of free areas
 * based on the metadata blocks and return the offset @bit_off and size in
 * bits of the free area @bits.  pcpu_for_each_fit_region only returns when
 * a fit is found for the allocation request.
 */
#define pcpu_for_each_md_free_region(chunk, bit_off, bits)		\
	for (pcpu_next_md_free_region((chunk), &(bit_off), &(bits));	\
	     (bit_off) < pcpu_chunk_map_bits((chunk));			\
	     (bit_off) += (bits) + 1,					\
	     pcpu_next_md_free_region((chunk), &(bit_off), &(bits)))

#define pcpu_for_each_fit_region(chunk, alloc_bits, align, bit_off, bits)     \
	for (pcpu_next_fit_region((chunk), (alloc_bits), (align), &(bit_off), \
				  &(bits));				      \
	     (bit_off) < pcpu_chunk_map_bits((chunk));			      \
	     (bit_off) += (bits),					      \
	     pcpu_next_fit_region((chunk), (alloc_bits), (align), &(bit_off), \
				  &(bits)))

/**
 * pcpu_mem_zalloc - allocate memory
 * @size: bytes to allocate
 * @gfp: allocation flags
 *
 * Allocate @size bytes.  If @size is smaller than PAGE_SIZE,
 * kzalloc() is used; otherwise, the equivalent of vzalloc() is used.
 * This is to facilitate passing through whitelisted flags.  The
 * returned memory is always zeroed.
 *
 * RETURNS:
 * Pointer to the allocated area on success, NULL on failure.
 */
static void *pcpu_mem_zalloc(size_t size, gfp_t gfp)
{
	if (WARN_ON_ONCE(!slab_is_available()))
		return NULL;

	if (size <= PAGE_SIZE)
		return kzalloc(size, gfp);
	else
		return __vmalloc(size, gfp | __GFP_ZERO, PAGE_KERNEL);
}

/**
 * pcpu_mem_free - free memory
 * @ptr: memory to free
 *
 * Free @ptr.  @ptr should have been allocated using pcpu_mem_zalloc().
 */
static void pcpu_mem_free(void *ptr)
{
	kvfree(ptr);
}

static void __pcpu_chunk_move(struct pcpu_chunk *chunk, int slot,
			      bool move_front)
{
	if (chunk != pcpu_reserved_chunk) {
		if (move_front)
			list_move(&chunk->list, &pcpu_slot[slot]);
		else
			list_move_tail(&chunk->list, &pcpu_slot[slot]);
	}
}

static void pcpu_chunk_move(struct pcpu_chunk *chunk, int slot)
{
	__pcpu_chunk_move(chunk, slot, true);
}

/**
 * pcpu_chunk_relocate - put chunk in the appropriate chunk slot
 * @chunk: chunk of interest
 * @oslot: the previous slot it was on
 *
 * This function is called after an allocation or free changed @chunk.
 * New slot according to the changed state is determined and @chunk is
 * moved to the slot.  Note that the reserved chunk is never put on
 * chunk slots.
 *
 * CONTEXT:
 * pcpu_lock.
 */
static void pcpu_chunk_relocate(struct pcpu_chunk *chunk, int oslot)
{
	int nslot = pcpu_chunk_slot(chunk);

	if (oslot != nslot)
		__pcpu_chunk_move(chunk, nslot, oslot < nslot);
}

/*
 * pcpu_update_empty_pages - update empty page counters
 * @chunk: chunk of interest
 * @nr: nr of empty pages
 *
 * This is used to keep track of the empty pages now based on the premise
 * a md_block covers a page.  The hint update functions recognize if a block
 * is made full or broken to calculate deltas for keeping track of free pages.
 */
static inline void pcpu_update_empty_pages(struct pcpu_chunk *chunk, int nr)
{
	chunk->nr_empty_pop_pages += nr;
	if (chunk != pcpu_reserved_chunk)
		pcpu_nr_empty_pop_pages += nr;
}

/*
 * pcpu_region_overlap - determines if two regions overlap
 * @a: start of first region, inclusive
 * @b: end of first region, exclusive
 * @x: start of second region, inclusive
 * @y: end of second region, exclusive
 *
 * This is used to determine if the hint region [a, b) overlaps with the
 * allocated region [x, y).
 */
static inline bool pcpu_region_overlap(int a, int b, int x, int y)
{
	return (a < y) && (x < b);
}

/**
 * pcpu_block_update - updates a block given a free area
 * @block: block of interest
 * @start: start offset in block
 * @end: end offset in block
 *
 * Updates a block given a known free area.  The region [start, end) is
 * expected to be the entirety of the free area within a block.  Chooses
 * the best starting offset if the contig hints are equal.
 */
static void pcpu_block_update(struct pcpu_block_md *block, int start, int end)
{
	int contig = end - start;

	block->first_free = min(block->first_free, start);
	if (start == 0)
		block->left_free = contig;

	if (end == block->nr_bits)
		block->right_free = contig;

	if (contig > block->contig_hint) {
		/* promote the old contig_hint to be the new scan_hint */
		if (start > block->contig_hint_start) {
			if (block->contig_hint > block->scan_hint) {
				block->scan_hint_start =
					block->contig_hint_start;
				block->scan_hint = block->contig_hint;
			} else if (start < block->scan_hint_start) {
				/*
				 * The old contig_hint == scan_hint.  But, the
				 * new contig is larger so hold the invariant
				 * scan_hint_start < contig_hint_start.
				 */
				block->scan_hint = 0;
			}
		} else {
			block->scan_hint = 0;
		}
		block->contig_hint_start = start;
		block->contig_hint = contig;
	} else if (contig == block->contig_hint) {
		if (block->contig_hint_start &&
		    (!start ||
		     __ffs(start) > __ffs(block->contig_hint_start))) {
			/* start has a better alignment so use it */
			block->contig_hint_start = start;
			if (start < block->scan_hint_start &&
			    block->contig_hint > block->scan_hint)
				block->scan_hint = 0;
		} else if (start > block->scan_hint_start ||
			   block->contig_hint > block->scan_hint) {
			/*
			 * Knowing contig == contig_hint, update the scan_hint
			 * if it is farther than or larger than the current
			 * scan_hint.
			 */
			block->scan_hint_start = start;
			block->scan_hint = contig;
		}
	} else {
		/*
		 * The region is smaller than the contig_hint.  So only update
		 * the scan_hint if it is larger than or equal and farther than
		 * the current scan_hint.
		 */
		if ((start < block->contig_hint_start &&
		     (contig > block->scan_hint ||
		      (contig == block->scan_hint &&
		       start > block->scan_hint_start)))) {
			block->scan_hint_start = start;
			block->scan_hint = contig;
		}
	}
}

/*
 * pcpu_block_update_scan - update a block given a free area from a scan
 * @chunk: chunk of interest
 * @bit_off: chunk offset
 * @bits: size of free area
 *
 * Finding the final allocation spot first goes through pcpu_find_block_fit()
 * to find a block that can hold the allocation and then pcpu_alloc_area()
 * where a scan is used.  When allocations require specific alignments,
 * we can inadvertently create holes which will not be seen in the alloc
 * or free paths.
 *
 * This takes a given free area hole and updates a block as it may change the
 * scan_hint.  We need to scan backwards to ensure we don't miss free bits
 * from alignment.
 */
static void pcpu_block_update_scan(struct pcpu_chunk *chunk, int bit_off,
				   int bits)
{
	int s_off = pcpu_off_to_block_off(bit_off);
	int e_off = s_off + bits;
	int s_index, l_bit;
	struct pcpu_block_md *block;

	if (e_off > PCPU_BITMAP_BLOCK_BITS)
		return;

	s_index = pcpu_off_to_block_index(bit_off);
	block = chunk->md_blocks + s_index;

	/* scan backwards in case of alignment skipping free bits */
	l_bit = find_last_bit(pcpu_index_alloc_map(chunk, s_index), s_off);
	s_off = (s_off == l_bit) ? 0 : l_bit + 1;

	pcpu_block_update(block, s_off, e_off);
}

/**
 * pcpu_chunk_refresh_hint - updates metadata about a chunk
 * @chunk: chunk of interest
 * @full_scan: if we should scan from the beginning
 *
 * Iterates over the metadata blocks to find the largest contig area.
 * A full scan can be avoided on the allocation path as this is triggered
 * if we broke the contig_hint.  In doing so, the scan_hint will be before
 * the contig_hint or after if the scan_hint == contig_hint.  This cannot
 * be prevented on freeing as we want to find the largest area possibly
 * spanning blocks.
 */
static void pcpu_chunk_refresh_hint(struct pcpu_chunk *chunk, bool full_scan)
{
	struct pcpu_block_md *chunk_md = &chunk->chunk_md;
	int bit_off, bits;

	/* promote scan_hint to contig_hint */
	if (!full_scan && chunk_md->scan_hint) {
		bit_off = chunk_md->scan_hint_start + chunk_md->scan_hint;
		chunk_md->contig_hint_start = chunk_md->scan_hint_start;
		chunk_md->contig_hint = chunk_md->scan_hint;
		chunk_md->scan_hint = 0;
	} else {
		bit_off = chunk_md->first_free;
		chunk_md->contig_hint = 0;
	}

	bits = 0;
	pcpu_for_each_md_free_region(chunk, bit_off, bits)
		pcpu_block_update(chunk_md, bit_off, bit_off + bits);
}

/**
 * pcpu_block_refresh_hint
 * @chunk: chunk of interest
 * @index: index of the metadata block
 *
 * Scans over the block beginning at first_free and updates the block
 * metadata accordingly.
 */
static void pcpu_block_refresh_hint(struct pcpu_chunk *chunk, int index)
{
	struct pcpu_block_md *block = chunk->md_blocks + index;
	unsigned long *alloc_map = pcpu_index_alloc_map(chunk, index);
	unsigned int rs, re, start;	/* region start, region end */

	/* promote scan_hint to contig_hint */
	if (block->scan_hint) {
		start = block->scan_hint_start + block->scan_hint;
		block->contig_hint_start = block->scan_hint_start;
		block->contig_hint = block->scan_hint;
		block->scan_hint = 0;
	} else {
		start = block->first_free;
		block->contig_hint = 0;
	}

	block->right_free = 0;

	/* iterate over free areas and update the contig hints */
	bitmap_for_each_clear_region(alloc_map, rs, re, start,
				     PCPU_BITMAP_BLOCK_BITS)
		pcpu_block_update(block, rs, re);
}

/**
 * pcpu_block_update_hint_alloc - update hint on allocation path
 * @chunk: chunk of interest
 * @bit_off: chunk offset
 * @bits: size of request
 *
 * Updates metadata for the allocation path.  The metadata only has to be
 * refreshed by a full scan iff the chunk's contig hint is broken.  Block level
 * scans are required if the block's contig hint is broken.
 */
static void pcpu_block_update_hint_alloc(struct pcpu_chunk *chunk, int bit_off,
					 int bits)
{
	struct pcpu_block_md *chunk_md = &chunk->chunk_md;
	int nr_empty_pages = 0;
	struct pcpu_block_md *s_block, *e_block, *block;
	int s_index, e_index;	/* block indexes of the freed allocation */
	int s_off, e_off;	/* block offsets of the freed allocation */

	/*
	 * Calculate per block offsets.
	 * The calculation uses an inclusive range, but the resulting offsets
	 * are [start, end).  e_index always points to the last block in the
	 * range.
	 */
	s_index = pcpu_off_to_block_index(bit_off);
	e_index = pcpu_off_to_block_index(bit_off + bits - 1);
	s_off = pcpu_off_to_block_off(bit_off);
	e_off = pcpu_off_to_block_off(bit_off + bits - 1) + 1;

	s_block = chunk->md_blocks + s_index;
	e_block = chunk->md_blocks + e_index;

	/*
	 * Update s_block.
	 * block->first_free must be updated if the allocation takes its place.
	 * If the allocation breaks the contig_hint, a scan is required to
	 * restore this hint.
	 */
	if (s_block->contig_hint == PCPU_BITMAP_BLOCK_BITS)
		nr_empty_pages++;

	if (s_off == s_block->first_free)
		s_block->first_free = find_next_zero_bit(
					pcpu_index_alloc_map(chunk, s_index),
					PCPU_BITMAP_BLOCK_BITS,
					s_off + bits);

	if (pcpu_region_overlap(s_block->scan_hint_start,
				s_block->scan_hint_start + s_block->scan_hint,
				s_off,
				s_off + bits))
		s_block->scan_hint = 0;

	if (pcpu_region_overlap(s_block->contig_hint_start,
				s_block->contig_hint_start +
				s_block->contig_hint,
				s_off,
				s_off + bits)) {
		/* block contig hint is broken - scan to fix it */
		if (!s_off)
			s_block->left_free = 0;
		pcpu_block_refresh_hint(chunk, s_index);
	} else {
		/* update left and right contig manually */
		s_block->left_free = min(s_block->left_free, s_off);
		if (s_index == e_index)
			s_block->right_free = min_t(int, s_block->right_free,
					PCPU_BITMAP_BLOCK_BITS - e_off);
		else
			s_block->right_free = 0;
	}

	/*
	 * Update e_block.
	 */
	if (s_index != e_index) {
		if (e_block->contig_hint == PCPU_BITMAP_BLOCK_BITS)
			nr_empty_pages++;

		/*
		 * When the allocation is across blocks, the end is along
		 * the left part of the e_block.
		 */
		e_block->first_free = find_next_zero_bit(
				pcpu_index_alloc_map(chunk, e_index),
				PCPU_BITMAP_BLOCK_BITS, e_off);

		if (e_off == PCPU_BITMAP_BLOCK_BITS) {
			/* reset the block */
			e_block++;
		} else {
			if (e_off > e_block->scan_hint_start)
				e_block->scan_hint = 0;

			e_block->left_free = 0;
			if (e_off > e_block->contig_hint_start) {
				/* contig hint is broken - scan to fix it */
				pcpu_block_refresh_hint(chunk, e_index);
			} else {
				e_block->right_free =
					min_t(int, e_block->right_free,
					      PCPU_BITMAP_BLOCK_BITS - e_off);
			}
		}

		/* update in-between md_blocks */
		nr_empty_pages += (e_index - s_index - 1);
		for (block = s_block + 1; block < e_block; block++) {
			block->scan_hint = 0;
			block->contig_hint = 0;
			block->left_free = 0;
			block->right_free = 0;
		}
	}

	if (nr_empty_pages)
		pcpu_update_empty_pages(chunk, -nr_empty_pages);

	if (pcpu_region_overlap(chunk_md->scan_hint_start,
				chunk_md->scan_hint_start +
				chunk_md->scan_hint,
				bit_off,
				bit_off + bits))
		chunk_md->scan_hint = 0;

	/*
	 * The only time a full chunk scan is required is if the chunk
	 * contig hint is broken.  Otherwise, it means a smaller space
	 * was used and therefore the chunk contig hint is still correct.
	 */
	if (pcpu_region_overlap(chunk_md->contig_hint_start,
				chunk_md->contig_hint_start +
				chunk_md->contig_hint,
				bit_off,
				bit_off + bits))
		pcpu_chunk_refresh_hint(chunk, false);
}

/**
 * pcpu_block_update_hint_free - updates the block hints on the free path
 * @chunk: chunk of interest
 * @bit_off: chunk offset
 * @bits: size of request
 *
 * Updates metadata for the allocation path.  This avoids a blind block
 * refresh by making use of the block contig hints.  If this fails, it scans
 * forward and backward to determine the extent of the free area.  This is
 * capped at the boundary of blocks.
 *
 * A chunk update is triggered if a page becomes free, a block becomes free,
 * or the free spans across blocks.  This tradeoff is to minimize iterating
 * over the block metadata to update chunk_md->contig_hint.
 * chunk_md->contig_hint may be off by up to a page, but it will never be more
 * than the available space.  If the contig hint is contained in one block, it
 * will be accurate.
 */
static void pcpu_block_update_hint_free(struct pcpu_chunk *chunk, int bit_off,
					int bits)
{
	int nr_empty_pages = 0;
	struct pcpu_block_md *s_block, *e_block, *block;
	int s_index, e_index;	/* block indexes of the freed allocation */
	int s_off, e_off;	/* block offsets of the freed allocation */
	int start, end;		/* start and end of the whole free area */

	/*
	 * Calculate per block offsets.
	 * The calculation uses an inclusive range, but the resulting offsets
	 * are [start, end).  e_index always points to the last block in the
	 * range.
	 */
	s_index = pcpu_off_to_block_index(bit_off);
	e_index = pcpu_off_to_block_index(bit_off + bits - 1);
	s_off = pcpu_off_to_block_off(bit_off);
	e_off = pcpu_off_to_block_off(bit_off + bits - 1) + 1;

	s_block = chunk->md_blocks + s_index;
	e_block = chunk->md_blocks + e_index;

	/*
	 * Check if the freed area aligns with the block->contig_hint.
	 * If it does, then the scan to find the beginning/end of the
	 * larger free area can be avoided.
	 *
	 * start and end refer to beginning and end of the free area
	 * within each their respective blocks.  This is not necessarily
	 * the entire free area as it may span blocks past the beginning
	 * or end of the block.
	 */
	start = s_off;
	if (s_off == s_block->contig_hint + s_block->contig_hint_start) {
		start = s_block->contig_hint_start;
	} else {
		/*
		 * Scan backwards to find the extent of the free area.
		 * find_last_bit returns the starting bit, so if the start bit
		 * is returned, that means there was no last bit and the
		 * remainder of the chunk is free.
		 */
		int l_bit = find_last_bit(pcpu_index_alloc_map(chunk, s_index),
					  start);
		start = (start == l_bit) ? 0 : l_bit + 1;
	}

	end = e_off;
	if (e_off == e_block->contig_hint_start)
		end = e_block->contig_hint_start + e_block->contig_hint;
	else
		end = find_next_bit(pcpu_index_alloc_map(chunk, e_index),
				    PCPU_BITMAP_BLOCK_BITS, end);

	/* update s_block */
	e_off = (s_index == e_index) ? end : PCPU_BITMAP_BLOCK_BITS;
	if (!start && e_off == PCPU_BITMAP_BLOCK_BITS)
		nr_empty_pages++;
	pcpu_block_update(s_block, start, e_off);

	/* freeing in the same block */
	if (s_index != e_index) {
		/* update e_block */
		if (end == PCPU_BITMAP_BLOCK_BITS)
			nr_empty_pages++;
		pcpu_block_update(e_block, 0, end);

		/* reset md_blocks in the middle */
		nr_empty_pages += (e_index - s_index - 1);
		for (block = s_block + 1; block < e_block; block++) {
			block->first_free = 0;
			block->scan_hint = 0;
			block->contig_hint_start = 0;
			block->contig_hint = PCPU_BITMAP_BLOCK_BITS;
			block->left_free = PCPU_BITMAP_BLOCK_BITS;
			block->right_free = PCPU_BITMAP_BLOCK_BITS;
		}
	}

	if (nr_empty_pages)
		pcpu_update_empty_pages(chunk, nr_empty_pages);

	/*
	 * Refresh chunk metadata when the free makes a block free or spans
	 * across blocks.  The contig_hint may be off by up to a page, but if
	 * the contig_hint is contained in a block, it will be accurate with
	 * the else condition below.
	 */
	if (((end - start) >= PCPU_BITMAP_BLOCK_BITS) || s_index != e_index)
		pcpu_chunk_refresh_hint(chunk, true);
	else
		pcpu_block_update(&chunk->chunk_md,
				  pcpu_block_off_to_off(s_index, start),
				  end);
}

/**
 * pcpu_is_populated - determines if the region is populated
 * @chunk: chunk of interest
 * @bit_off: chunk offset
 * @bits: size of area
 * @next_off: return value for the next offset to start searching
 *
 * For atomic allocations, check if the backing pages are populated.
 *
 * RETURNS:
 * Bool if the backing pages are populated.
 * next_index is to skip over unpopulated blocks in pcpu_find_block_fit.
 */
static bool pcpu_is_populated(struct pcpu_chunk *chunk, int bit_off, int bits,
			      int *next_off)
{
	unsigned int page_start, page_end, rs, re;

	page_start = PFN_DOWN(bit_off * PCPU_MIN_ALLOC_SIZE);
	page_end = PFN_UP((bit_off + bits) * PCPU_MIN_ALLOC_SIZE);

	rs = page_start;
	bitmap_next_clear_region(chunk->populated, &rs, &re, page_end);
	if (rs >= page_end)
		return true;

	*next_off = re * PAGE_SIZE / PCPU_MIN_ALLOC_SIZE;
	return false;
}

/**
 * pcpu_find_block_fit - finds the block index to start searching
 * @chunk: chunk of interest
 * @alloc_bits: size of request in allocation units
 * @align: alignment of area (max PAGE_SIZE bytes)
 * @pop_only: use populated regions only
 *
 * Given a chunk and an allocation spec, find the offset to begin searching
 * for a free region.  This iterates over the bitmap metadata blocks to
 * find an offset that will be guaranteed to fit the requirements.  It is
 * not quite first fit as if the allocation does not fit in the contig hint
 * of a block or chunk, it is skipped.  This errs on the side of caution
 * to prevent excess iteration.  Poor alignment can cause the allocator to
 * skip over blocks and chunks that have valid free areas.
 *
 * RETURNS:
 * The offset in the bitmap to begin searching.
 * -1 if no offset is found.
 */
static int pcpu_find_block_fit(struct pcpu_chunk *chunk, int alloc_bits,
			       size_t align, bool pop_only)
{
	struct pcpu_block_md *chunk_md = &chunk->chunk_md;
	int bit_off, bits, next_off;

	/*
	 * Check to see if the allocation can fit in the chunk's contig hint.
	 * This is an optimization to prevent scanning by assuming if it
	 * cannot fit in the global hint, there is memory pressure and creating
	 * a new chunk would happen soon.
	 */
	bit_off = ALIGN(chunk_md->contig_hint_start, align) -
		  chunk_md->contig_hint_start;
	if (bit_off + alloc_bits > chunk_md->contig_hint)
		return -1;

	bit_off = pcpu_next_hint(chunk_md, alloc_bits);
	bits = 0;
	pcpu_for_each_fit_region(chunk, alloc_bits, align, bit_off, bits) {
		if (!pop_only || pcpu_is_populated(chunk, bit_off, bits,
						   &next_off))
			break;

		bit_off = next_off;
		bits = 0;
	}

	if (bit_off == pcpu_chunk_map_bits(chunk))
		return -1;

	return bit_off;
}

/*
 * pcpu_find_zero_area - modified from bitmap_find_next_zero_area_off()
 * @map: the address to base the search on
 * @size: the bitmap size in bits
 * @start: the bitnumber to start searching at
 * @nr: the number of zeroed bits we're looking for
 * @align_mask: alignment mask for zero area
 * @largest_off: offset of the largest area skipped
 * @largest_bits: size of the largest area skipped
 *
 * The @align_mask should be one less than a power of 2.
 *
 * This is a modified version of bitmap_find_next_zero_area_off() to remember
 * the largest area that was skipped.  This is imperfect, but in general is
 * good enough.  The largest remembered region is the largest failed region
 * seen.  This does not include anything we possibly skipped due to alignment.
 * pcpu_block_update_scan() does scan backwards to try and recover what was
 * lost to alignment.  While this can cause scanning to miss earlier possible
 * free areas, smaller allocations will eventually fill those holes.
 */
static unsigned long pcpu_find_zero_area(unsigned long *map,
					 unsigned long size,
					 unsigned long start,
					 unsigned long nr,
					 unsigned long align_mask,
					 unsigned long *largest_off,
					 unsigned long *largest_bits)
{
	unsigned long index, end, i, area_off, area_bits;
again:
	index = find_next_zero_bit(map, size, start);

	/* Align allocation */
	index = __ALIGN_MASK(index, align_mask);
	area_off = index;

	end = index + nr;
	if (end > size)
		return end;
	i = find_next_bit(map, end, index);
	if (i < end) {
		area_bits = i - area_off;
		/* remember largest unused area with best alignment */
		if (area_bits > *largest_bits ||
		    (area_bits == *largest_bits && *largest_off &&
		     (!area_off || __ffs(area_off) > __ffs(*largest_off)))) {
			*largest_off = area_off;
			*largest_bits = area_bits;
		}

		start = i + 1;
		goto again;
	}
	return index;
}

/**
 * pcpu_alloc_area - allocates an area from a pcpu_chunk
 * @chunk: chunk of interest
 * @alloc_bits: size of request in allocation units
 * @align: alignment of area (max PAGE_SIZE)
 * @start: bit_off to start searching
 *
 * This function takes in a @start offset to begin searching to fit an
 * allocation of @alloc_bits with alignment @align.  It needs to scan
 * the allocation map because if it fits within the block's contig hint,
 * @start will be block->first_free. This is an attempt to fill the
 * allocation prior to breaking the contig hint.  The allocation and
 * boundary maps are updated accordingly if it confirms a valid
 * free area.
 *
 * RETURNS:
 * Allocated addr offset in @chunk on success.
 * -1 if no matching area is found.
 */
static int pcpu_alloc_area(struct pcpu_chunk *chunk, int alloc_bits,
			   size_t align, int start)
{
	struct pcpu_block_md *chunk_md = &chunk->chunk_md;
	size_t align_mask = (align) ? (align - 1) : 0;
	unsigned long area_off = 0, area_bits = 0;
	int bit_off, end, oslot;

	lockdep_assert_held(&pcpu_lock);

	oslot = pcpu_chunk_slot(chunk);

	/*
	 * Search to find a fit.
	 */
	end = min_t(int, start + alloc_bits + PCPU_BITMAP_BLOCK_BITS,
		    pcpu_chunk_map_bits(chunk));
	bit_off = pcpu_find_zero_area(chunk->alloc_map, end, start, alloc_bits,
				      align_mask, &area_off, &area_bits);
	if (bit_off >= end)
		return -1;

	if (area_bits)
		pcpu_block_update_scan(chunk, area_off, area_bits);

	/* update alloc map */
	bitmap_set(chunk->alloc_map, bit_off, alloc_bits);

	/* update boundary map */
	set_bit(bit_off, chunk->bound_map);
	bitmap_clear(chunk->bound_map, bit_off + 1, alloc_bits - 1);
	set_bit(bit_off + alloc_bits, chunk->bound_map);

	chunk->free_bytes -= alloc_bits * PCPU_MIN_ALLOC_SIZE;

	/* update first free bit */
	if (bit_off == chunk_md->first_free)
		chunk_md->first_free = find_next_zero_bit(
					chunk->alloc_map,
					pcpu_chunk_map_bits(chunk),
					bit_off + alloc_bits);

	pcpu_block_update_hint_alloc(chunk, bit_off, alloc_bits);

	pcpu_chunk_relocate(chunk, oslot);

	return bit_off * PCPU_MIN_ALLOC_SIZE;
}

/**
 * pcpu_free_area - frees the corresponding offset
 * @chunk: chunk of interest
 * @off: addr offset into chunk
 *
 * This function determines the size of an allocation to free using
 * the boundary bitmap and clears the allocation map.
 */
static void pcpu_free_area(struct pcpu_chunk *chunk, int off)
{
	struct pcpu_block_md *chunk_md = &chunk->chunk_md;
	int bit_off, bits, end, oslot;

	lockdep_assert_held(&pcpu_lock);
	pcpu_stats_area_dealloc(chunk);

	oslot = pcpu_chunk_slot(chunk);

	bit_off = off / PCPU_MIN_ALLOC_SIZE;

	/* find end index */
	end = find_next_bit(chunk->bound_map, pcpu_chunk_map_bits(chunk),
			    bit_off + 1);
	bits = end - bit_off;
	bitmap_clear(chunk->alloc_map, bit_off, bits);

	/* update metadata */
	chunk->free_bytes += bits * PCPU_MIN_ALLOC_SIZE;

	/* update first free bit */
	chunk_md->first_free = min(chunk_md->first_free, bit_off);

	pcpu_block_update_hint_free(chunk, bit_off, bits);

	pcpu_chunk_relocate(chunk, oslot);
}

static void pcpu_init_md_block(struct pcpu_block_md *block, int nr_bits)
{
	block->scan_hint = 0;
	block->contig_hint = nr_bits;
	block->left_free = nr_bits;
	block->right_free = nr_bits;
	block->first_free = 0;
	block->nr_bits = nr_bits;
}

static void pcpu_init_md_blocks(struct pcpu_chunk *chunk)
{
	struct pcpu_block_md *md_block;

	/* init the chunk's block */
	pcpu_init_md_block(&chunk->chunk_md, pcpu_chunk_map_bits(chunk));

	for (md_block = chunk->md_blocks;
	     md_block != chunk->md_blocks + pcpu_chunk_nr_blocks(chunk);
	     md_block++)
		pcpu_init_md_block(md_block, PCPU_BITMAP_BLOCK_BITS);
}

/**
 * pcpu_alloc_first_chunk - creates chunks that serve the first chunk
 * @tmp_addr: the start of the region served
 * @map_size: size of the region served
 *
 * This is responsible for creating the chunks that serve the first chunk.  The
 * base_addr is page aligned down of @tmp_addr while the region end is page
 * aligned up.  Offsets are kept track of to determine the region served. All
 * this is done to appease the bitmap allocator in avoiding partial blocks.
 *
 * RETURNS:
 * Chunk serving the region at @tmp_addr of @map_size.
 */
static struct pcpu_chunk * __init pcpu_alloc_first_chunk(unsigned long tmp_addr,
							 int map_size)
{
	struct pcpu_chunk *chunk;
	unsigned long aligned_addr, lcm_align;
	int start_offset, offset_bits, region_size, region_bits;
	size_t alloc_size;

	/* region calculations */
	aligned_addr = tmp_addr & PAGE_MASK;

	start_offset = tmp_addr - aligned_addr;

	/*
	 * Align the end of the region with the LCM of PAGE_SIZE and
	 * PCPU_BITMAP_BLOCK_SIZE.  One of these constants is a multiple of
	 * the other.
	 */
	lcm_align = lcm(PAGE_SIZE, PCPU_BITMAP_BLOCK_SIZE);
	region_size = ALIGN(start_offset + map_size, lcm_align);

	/* allocate chunk */
	alloc_size = sizeof(struct pcpu_chunk) +
		BITS_TO_LONGS(region_size >> PAGE_SHIFT);
	chunk = memblock_alloc(alloc_size, SMP_CACHE_BYTES);
	if (!chunk)
		panic("%s: Failed to allocate %zu bytes\n", __func__,
		      alloc_size);

	INIT_LIST_HEAD(&chunk->list);

	chunk->base_addr = (void *)aligned_addr;
	chunk->start_offset = start_offset;
	chunk->end_offset = region_size - chunk->start_offset - map_size;

	chunk->nr_pages = region_size >> PAGE_SHIFT;
	region_bits = pcpu_chunk_map_bits(chunk);

	alloc_size = BITS_TO_LONGS(region_bits) * sizeof(chunk->alloc_map[0]);
	chunk->alloc_map = memblock_alloc(alloc_size, SMP_CACHE_BYTES);
	if (!chunk->alloc_map)
		panic("%s: Failed to allocate %zu bytes\n", __func__,
		      alloc_size);

	alloc_size =
		BITS_TO_LONGS(region_bits + 1) * sizeof(chunk->bound_map[0]);
	chunk->bound_map = memblock_alloc(alloc_size, SMP_CACHE_BYTES);
	if (!chunk->bound_map)
		panic("%s: Failed to allocate %zu bytes\n", __func__,
		      alloc_size);

	alloc_size = pcpu_chunk_nr_blocks(chunk) * sizeof(chunk->md_blocks[0]);
	chunk->md_blocks = memblock_alloc(alloc_size, SMP_CACHE_BYTES);
	if (!chunk->md_blocks)
		panic("%s: Failed to allocate %zu bytes\n", __func__,
		      alloc_size);

	pcpu_init_md_blocks(chunk);

	/* manage populated page bitmap */
	chunk->immutable = true;
	bitmap_fill(chunk->populated, chunk->nr_pages);
	chunk->nr_populated = chunk->nr_pages;
	chunk->nr_empty_pop_pages = chunk->nr_pages;

	chunk->free_bytes = map_size;

	if (chunk->start_offset) {
		/* hide the beginning of the bitmap */
		offset_bits = chunk->start_offset / PCPU_MIN_ALLOC_SIZE;
		bitmap_set(chunk->alloc_map, 0, offset_bits);
		set_bit(0, chunk->bound_map);
		set_bit(offset_bits, chunk->bound_map);

		chunk->chunk_md.first_free = offset_bits;

		pcpu_block_update_hint_alloc(chunk, 0, offset_bits);
	}

	if (chunk->end_offset) {
		/* hide the end of the bitmap */
		offset_bits = chunk->end_offset / PCPU_MIN_ALLOC_SIZE;
		bitmap_set(chunk->alloc_map,
			   pcpu_chunk_map_bits(chunk) - offset_bits,
			   offset_bits);
		set_bit((start_offset + map_size) / PCPU_MIN_ALLOC_SIZE,
			chunk->bound_map);
		set_bit(region_bits, chunk->bound_map);

		pcpu_block_update_hint_alloc(chunk, pcpu_chunk_map_bits(chunk)
					     - offset_bits, offset_bits);
	}

	return chunk;
}

static struct pcpu_chunk *pcpu_alloc_chunk(gfp_t gfp)
{
	struct pcpu_chunk *chunk;
	int region_bits;

	chunk = pcpu_mem_zalloc(pcpu_chunk_struct_size, gfp);
	if (!chunk)
		return NULL;

	INIT_LIST_HEAD(&chunk->list);
	chunk->nr_pages = pcpu_unit_pages;
	region_bits = pcpu_chunk_map_bits(chunk);

	chunk->alloc_map = pcpu_mem_zalloc(BITS_TO_LONGS(region_bits) *
					   sizeof(chunk->alloc_map[0]), gfp);
	if (!chunk->alloc_map)
		goto alloc_map_fail;

	chunk->bound_map = pcpu_mem_zalloc(BITS_TO_LONGS(region_bits + 1) *
					   sizeof(chunk->bound_map[0]), gfp);
	if (!chunk->bound_map)
		goto bound_map_fail;

	chunk->md_blocks = pcpu_mem_zalloc(pcpu_chunk_nr_blocks(chunk) *
					   sizeof(chunk->md_blocks[0]), gfp);
	if (!chunk->md_blocks)
		goto md_blocks_fail;

	pcpu_init_md_blocks(chunk);

	/* init metadata */
	chunk->free_bytes = chunk->nr_pages * PAGE_SIZE;

	return chunk;

md_blocks_fail:
	pcpu_mem_free(chunk->bound_map);
bound_map_fail:
	pcpu_mem_free(chunk->alloc_map);
alloc_map_fail:
	pcpu_mem_free(chunk);

	return NULL;
}

static void pcpu_free_chunk(struct pcpu_chunk *chunk)
{
	if (!chunk)
		return;
	pcpu_mem_free(chunk->md_blocks);
	pcpu_mem_free(chunk->bound_map);
	pcpu_mem_free(chunk->alloc_map);
	pcpu_mem_free(chunk);
}

/**
 * pcpu_chunk_populated - post-population bookkeeping
 * @chunk: pcpu_chunk which got populated
 * @page_start: the start page
 * @page_end: the end page
 *
 * Pages in [@page_start,@page_end) have been populated to @chunk.  Update
 * the bookkeeping information accordingly.  Must be called after each
 * successful population.
 *
 * If this is @for_alloc, do not increment pcpu_nr_empty_pop_pages because it
 * is to serve an allocation in that area.
 */
static void pcpu_chunk_populated(struct pcpu_chunk *chunk, int page_start,
				 int page_end)
{
	int nr = page_end - page_start;

	lockdep_assert_held(&pcpu_lock);

	bitmap_set(chunk->populated, page_start, nr);
	chunk->nr_populated += nr;
	pcpu_nr_populated += nr;

	pcpu_update_empty_pages(chunk, nr);
}

/**
 * pcpu_chunk_depopulated - post-depopulation bookkeeping
 * @chunk: pcpu_chunk which got depopulated
 * @page_start: the start page
 * @page_end: the end page
 *
 * Pages in [@page_start,@page_end) have been depopulated from @chunk.
 * Update the bookkeeping information accordingly.  Must be called after
 * each successful depopulation.
 */
static void pcpu_chunk_depopulated(struct pcpu_chunk *chunk,
				   int page_start, int page_end)
{
	int nr = page_end - page_start;

	lockdep_assert_held(&pcpu_lock);

	bitmap_clear(chunk->populated, page_start, nr);
	chunk->nr_populated -= nr;
	pcpu_nr_populated -= nr;

	pcpu_update_empty_pages(chunk, -nr);
}

/*
 * Chunk management implementation.
 *
 * To allow different implementations, chunk alloc/free and
 * [de]population are implemented in a separate file which is pulled
 * into this file and compiled together.  The following functions
 * should be implemented.
 *
 * pcpu_populate_chunk		- populate the specified range of a chunk
 * pcpu_depopulate_chunk	- depopulate the specified range of a chunk
 * pcpu_create_chunk		- create a new chunk
 * pcpu_destroy_chunk		- destroy a chunk, always preceded by full depop
 * pcpu_addr_to_page		- translate address to physical address
 * pcpu_verify_alloc_info	- check alloc_info is acceptable during init
 */
static int pcpu_populate_chunk(struct pcpu_chunk *chunk,
			       int page_start, int page_end, gfp_t gfp);
static void pcpu_depopulate_chunk(struct pcpu_chunk *chunk,
				  int page_start, int page_end);
static struct pcpu_chunk *pcpu_create_chunk(gfp_t gfp);
static void pcpu_destroy_chunk(struct pcpu_chunk *chunk);
static struct page *pcpu_addr_to_page(void *addr);
static int __init pcpu_verify_alloc_info(const struct pcpu_alloc_info *ai);

#ifdef CONFIG_NEED_PER_CPU_KM
#include "percpu-km.c"
#else
#include "percpu-vm.c"
#endif

/**
 * pcpu_chunk_addr_search - determine chunk containing specified address
 * @addr: address for which the chunk needs to be determined.
 *
 * This is an internal function that handles all but static allocations.
 * Static percpu address values should never be passed into the allocator.
 *
 * RETURNS:
 * The address of the found chunk.
 */
static struct pcpu_chunk *pcpu_chunk_addr_search(void *addr)
{
	/* is it in the dynamic region (first chunk)? */
	if (pcpu_addr_in_chunk(pcpu_first_chunk, addr))
		return pcpu_first_chunk;

	/* is it in the reserved region? */
	if (pcpu_addr_in_chunk(pcpu_reserved_chunk, addr))
		return pcpu_reserved_chunk;

	/*
	 * The address is relative to unit0 which might be unused and
	 * thus unmapped.  Offset the address to the unit space of the
	 * current processor before looking it up in the vmalloc
	 * space.  Note that any possible cpu id can be used here, so
	 * there's no need to worry about preemption or cpu hotplug.
	 */
	addr += pcpu_unit_offsets[raw_smp_processor_id()];
	return pcpu_get_page_chunk(pcpu_addr_to_page(addr));
}

/**
 * pcpu_alloc - the percpu allocator
 * @size: size of area to allocate in bytes
 * @align: alignment of area (max PAGE_SIZE)
 * @reserved: allocate from the reserved chunk if available
 * @gfp: allocation flags
 *
 * Allocate percpu area of @size bytes aligned at @align.  If @gfp doesn't
 * contain %GFP_KERNEL, the allocation is atomic. If @gfp has __GFP_NOWARN
 * then no warning will be triggered on invalid or failed allocation
 * requests.
 *
 * RETURNS:
 * Percpu pointer to the allocated area on success, NULL on failure.
 */
static void __percpu *pcpu_alloc(size_t size, size_t align, bool reserved,
				 gfp_t gfp)
{
	gfp_t pcpu_gfp;
	bool is_atomic;
	bool do_warn;
	static int warn_limit = 10;
	struct pcpu_chunk *chunk, *next;
	const char *err;
	int slot, off, cpu, ret;
	unsigned long flags;
	void __percpu *ptr;
	size_t bits, bit_align;

	gfp = current_gfp_context(gfp);
	/* whitelisted flags that can be passed to the backing allocators */
	pcpu_gfp = gfp & (GFP_KERNEL | __GFP_NORETRY | __GFP_NOWARN);
	is_atomic = (gfp & GFP_KERNEL) != GFP_KERNEL;
	do_warn = !(gfp & __GFP_NOWARN);

	/*
	 * There is now a minimum allocation size of PCPU_MIN_ALLOC_SIZE,
	 * therefore alignment must be a minimum of that many bytes.
	 * An allocation may have internal fragmentation from rounding up
	 * of up to PCPU_MIN_ALLOC_SIZE - 1 bytes.
	 */
	if (unlikely(align < PCPU_MIN_ALLOC_SIZE))
		align = PCPU_MIN_ALLOC_SIZE;

	size = ALIGN(size, PCPU_MIN_ALLOC_SIZE);
	bits = size >> PCPU_MIN_ALLOC_SHIFT;
	bit_align = align >> PCPU_MIN_ALLOC_SHIFT;

	if (unlikely(!size || size > PCPU_MIN_UNIT_SIZE || align > PAGE_SIZE ||
		     !is_power_of_2(align))) {
		WARN(do_warn, "illegal size (%zu) or align (%zu) for percpu allocation\n",
		     size, align);
		return NULL;
	}

	if (!is_atomic) {
		/*
		 * pcpu_balance_workfn() allocates memory under this mutex,
		 * and it may wait for memory reclaim. Allow current task
		 * to become OOM victim, in case of memory pressure.
		 */
		if (gfp & __GFP_NOFAIL)
			mutex_lock(&pcpu_alloc_mutex);
		else if (mutex_lock_killable(&pcpu_alloc_mutex))
			return NULL;
	}

	spin_lock_irqsave(&pcpu_lock, flags);

	/* serve reserved allocations from the reserved chunk if available */
	if (reserved && pcpu_reserved_chunk) {
		chunk = pcpu_reserved_chunk;

		off = pcpu_find_block_fit(chunk, bits, bit_align, is_atomic);
		if (off < 0) {
			err = "alloc from reserved chunk failed";
			goto fail_unlock;
		}

		off = pcpu_alloc_area(chunk, bits, bit_align, off);
		if (off >= 0)
			goto area_found;

		err = "alloc from reserved chunk failed";
		goto fail_unlock;
	}

restart:
	/* search through normal chunks */
	for (slot = pcpu_size_to_slot(size); slot < pcpu_nr_slots; slot++) {
		list_for_each_entry_safe(chunk, next, &pcpu_slot[slot], list) {
			off = pcpu_find_block_fit(chunk, bits, bit_align,
						  is_atomic);
			if (off < 0) {
				if (slot < PCPU_SLOT_FAIL_THRESHOLD)
					pcpu_chunk_move(chunk, 0);
				continue;
			}

			off = pcpu_alloc_area(chunk, bits, bit_align, off);
			if (off >= 0)
				goto area_found;

		}
	}

	spin_unlock_irqrestore(&pcpu_lock, flags);

	/*
	 * No space left.  Create a new chunk.  We don't want multiple
	 * tasks to create chunks simultaneously.  Serialize and create iff
	 * there's still no empty chunk after grabbing the mutex.
	 */
	if (is_atomic) {
		err = "atomic alloc failed, no space left";
		goto fail;
	}

	if (list_empty(&pcpu_slot[pcpu_nr_slots - 1])) {
		chunk = pcpu_create_chunk(pcpu_gfp);
		if (!chunk) {
			err = "failed to allocate new chunk";
			goto fail;
		}

		spin_lock_irqsave(&pcpu_lock, flags);
		pcpu_chunk_relocate(chunk, -1);
	} else {
		spin_lock_irqsave(&pcpu_lock, flags);
	}

	goto restart;

area_found:
	pcpu_stats_area_alloc(chunk, size);
	spin_unlock_irqrestore(&pcpu_lock, flags);

	/* populate if not all pages are already there */
	if (!is_atomic) {
		unsigned int page_start, page_end, rs, re;

		page_start = PFN_DOWN(off);
		page_end = PFN_UP(off + size);

		bitmap_for_each_clear_region(chunk->populated, rs, re,
					     page_start, page_end) {
			WARN_ON(chunk->immutable);

			ret = pcpu_populate_chunk(chunk, rs, re, pcpu_gfp);

			spin_lock_irqsave(&pcpu_lock, flags);
			if (ret) {
				pcpu_free_area(chunk, off);
				err = "failed to populate";
				goto fail_unlock;
			}
			pcpu_chunk_populated(chunk, rs, re);
			spin_unlock_irqrestore(&pcpu_lock, flags);
		}

		mutex_unlock(&pcpu_alloc_mutex);
	}

	if (pcpu_nr_empty_pop_pages < PCPU_EMPTY_POP_PAGES_LOW)
		pcpu_schedule_balance_work();

	/* clear the areas and return address relative to base address */
	for_each_possible_cpu(cpu)
		memset((void *)pcpu_chunk_addr(chunk, cpu, 0) + off, 0, size);

	ptr = __addr_to_pcpu_ptr(chunk->base_addr + off);
	kmemleak_alloc_percpu(ptr, size, gfp);

	trace_percpu_alloc_percpu(reserved, is_atomic, size, align,
			chunk->base_addr, off, ptr);

	return ptr;

fail_unlock:
	spin_unlock_irqrestore(&pcpu_lock, flags);
fail:
	trace_percpu_alloc_percpu_fail(reserved, is_atomic, size, align);

	if (!is_atomic && do_warn && warn_limit) {
		pr_warn("allocation failed, size=%zu align=%zu atomic=%d, %s\n",
			size, align, is_atomic, err);
		dump_stack();
		if (!--warn_limit)
			pr_info("limit reached, disable warning\n");
	}
	if (is_atomic) {
		/* see the flag handling in pcpu_blance_workfn() */
		pcpu_atomic_alloc_failed = true;
		pcpu_schedule_balance_work();
	} else {
		mutex_unlock(&pcpu_alloc_mutex);
	}
	return NULL;
}

/**
 * __alloc_percpu_gfp - allocate dynamic percpu area
 * @size: size of area to allocate in bytes
 * @align: alignment of area (max PAGE_SIZE)
 * @gfp: allocation flags
 *
 * Allocate zero-filled percpu area of @size bytes aligned at @align.  If
 * @gfp doesn't contain %GFP_KERNEL, the allocation doesn't block and can
 * be called from any context but is a lot more likely to fail. If @gfp
 * has __GFP_NOWARN then no warning will be triggered on invalid or failed
 * allocation requests.
 *
 * RETURNS:
 * Percpu pointer to the allocated area on success, NULL on failure.
 */
void __percpu *__alloc_percpu_gfp(size_t size, size_t align, gfp_t gfp)
{
	return pcpu_alloc(size, align, false, gfp);
}
EXPORT_SYMBOL_GPL(__alloc_percpu_gfp);

/**
 * __alloc_percpu - allocate dynamic percpu area
 * @size: size of area to allocate in bytes
 * @align: alignment of area (max PAGE_SIZE)
 *
 * Equivalent to __alloc_percpu_gfp(size, align, %GFP_KERNEL).
 */
void __percpu *__alloc_percpu(size_t size, size_t align)
{
	return pcpu_alloc(size, align, false, GFP_KERNEL);
}
EXPORT_SYMBOL_GPL(__alloc_percpu);

/**
 * __alloc_reserved_percpu - allocate reserved percpu area
 * @size: size of area to allocate in bytes
 * @align: alignment of area (max PAGE_SIZE)
 *
 * Allocate zero-filled percpu area of @size bytes aligned at @align
 * from reserved percpu area if arch has set it up; otherwise,
 * allocation is served from the same dynamic area.  Might sleep.
 * Might trigger writeouts.
 *
 * CONTEXT:
 * Does GFP_KERNEL allocation.
 *
 * RETURNS:
 * Percpu pointer to the allocated area on success, NULL on failure.
 */
void __percpu *__alloc_reserved_percpu(size_t size, size_t align)
{
	return pcpu_alloc(size, align, true, GFP_KERNEL);
}

/**
 * pcpu_balance_workfn - manage the amount of free chunks and populated pages
 * @work: unused
 *
 * Reclaim all fully free chunks except for the first one.  This is also
 * responsible for maintaining the pool of empty populated pages.  However,
 * it is possible that this is called when physical memory is scarce causing
 * OOM killer to be triggered.  We should avoid doing so until an actual
 * allocation causes the failure as it is possible that requests can be
 * serviced from already backed regions.
 */
static void pcpu_balance_workfn(struct work_struct *work)
{
	/* gfp flags passed to underlying allocators */
	const gfp_t gfp = GFP_KERNEL | __GFP_NORETRY | __GFP_NOWARN;
	LIST_HEAD(to_free);
	struct list_head *free_head = &pcpu_slot[pcpu_nr_slots - 1];
	struct pcpu_chunk *chunk, *next;
	int slot, nr_to_pop, ret;

	/*
	 * There's no reason to keep around multiple unused chunks and VM
	 * areas can be scarce.  Destroy all free chunks except for one.
	 */
	mutex_lock(&pcpu_alloc_mutex);
	spin_lock_irq(&pcpu_lock);

	list_for_each_entry_safe(chunk, next, free_head, list) {
		WARN_ON(chunk->immutable);

		/* spare the first one */
		if (chunk == list_first_entry(free_head, struct pcpu_chunk, list))
			continue;

		list_move(&chunk->list, &to_free);
	}

	spin_unlock_irq(&pcpu_lock);

	list_for_each_entry_safe(chunk, next, &to_free, list) {
		unsigned int rs, re;

		bitmap_for_each_set_region(chunk->populated, rs, re, 0,
					   chunk->nr_pages) {
			pcpu_depopulate_chunk(chunk, rs, re);
			spin_lock_irq(&pcpu_lock);
			pcpu_chunk_depopulated(chunk, rs, re);
			spin_unlock_irq(&pcpu_lock);
		}
		pcpu_destroy_chunk(chunk);
		cond_resched();
	}

	/*
	 * Ensure there are certain number of free populated pages for
	 * atomic allocs.  Fill up from the most packed so that atomic
	 * allocs don't increase fragmentation.  If atomic allocation
	 * failed previously, always populate the maximum amount.  This
	 * should prevent atomic allocs larger than PAGE_SIZE from keeping
	 * failing indefinitely; however, large atomic allocs are not
	 * something we support properly and can be highly unreliable and
	 * inefficient.
	 */
retry_pop:
	if (pcpu_atomic_alloc_failed) {
		nr_to_pop = PCPU_EMPTY_POP_PAGES_HIGH;
		/* best effort anyway, don't worry about synchronization */
		pcpu_atomic_alloc_failed = false;
	} else {
		nr_to_pop = clamp(PCPU_EMPTY_POP_PAGES_HIGH -
				  pcpu_nr_empty_pop_pages,
				  0, PCPU_EMPTY_POP_PAGES_HIGH);
	}

	for (slot = pcpu_size_to_slot(PAGE_SIZE); slot < pcpu_nr_slots; slot++) {
		unsigned int nr_unpop = 0, rs, re;

		if (!nr_to_pop)
			break;

		spin_lock_irq(&pcpu_lock);
		list_for_each_entry(chunk, &pcpu_slot[slot], list) {
			nr_unpop = chunk->nr_pages - chunk->nr_populated;
			if (nr_unpop)
				break;
		}
		spin_unlock_irq(&pcpu_lock);

		if (!nr_unpop)
			continue;

		/* @chunk can't go away while pcpu_alloc_mutex is held */
		bitmap_for_each_clear_region(chunk->populated, rs, re, 0,
					     chunk->nr_pages) {
			int nr = min_t(int, re - rs, nr_to_pop);

			ret = pcpu_populate_chunk(chunk, rs, rs + nr, gfp);
			if (!ret) {
				nr_to_pop -= nr;
				spin_lock_irq(&pcpu_lock);
				pcpu_chunk_populated(chunk, rs, rs + nr);
				spin_unlock_irq(&pcpu_lock);
			} else {
				nr_to_pop = 0;
			}

			if (!nr_to_pop)
				break;
		}
	}

	if (nr_to_pop) {
		/* ran out of chunks to populate, create a new one and retry */
		chunk = pcpu_create_chunk(gfp);
		if (chunk) {
			spin_lock_irq(&pcpu_lock);
			pcpu_chunk_relocate(chunk, -1);
			spin_unlock_irq(&pcpu_lock);
			goto retry_pop;
		}
	}

	mutex_unlock(&pcpu_alloc_mutex);
}

/**
 * free_percpu - free percpu area
 * @ptr: pointer to area to free
 *
 * Free percpu area @ptr.
 *
 * CONTEXT:
 * Can be called from atomic context.
 */
void free_percpu(void __percpu *ptr)
{
	void *addr;
	struct pcpu_chunk *chunk;
	unsigned long flags;
	int off;
	bool need_balance = false;

	if (!ptr)
		return;

	kmemleak_free_percpu(ptr);

	addr = __pcpu_ptr_to_addr(ptr);

	spin_lock_irqsave(&pcpu_lock, flags);

	chunk = pcpu_chunk_addr_search(addr);
	off = addr - chunk->base_addr;

	pcpu_free_area(chunk, off);

	/* if there are more than one fully free chunks, wake up grim reaper */
	if (chunk->free_bytes == pcpu_unit_size) {
		struct pcpu_chunk *pos;

		list_for_each_entry(pos, &pcpu_slot[pcpu_nr_slots - 1], list)
			if (pos != chunk) {
				need_balance = true;
				break;
			}
	}

	trace_percpu_free_percpu(chunk->base_addr, off, ptr);

	spin_unlock_irqrestore(&pcpu_lock, flags);

	if (need_balance)
		pcpu_schedule_balance_work();
}
EXPORT_SYMBOL_GPL(free_percpu);

bool __is_kernel_percpu_address(unsigned long addr, unsigned long *can_addr)
{
#ifdef CONFIG_SMP
	const size_t static_size = __per_cpu_end - __per_cpu_start;
	void __percpu *base = __addr_to_pcpu_ptr(pcpu_base_addr);
	unsigned int cpu;

	for_each_possible_cpu(cpu) {
		void *start = per_cpu_ptr(base, cpu);
		void *va = (void *)addr;

		if (va >= start && va < start + static_size) {
			if (can_addr) {
				*can_addr = (unsigned long) (va - start);
				*can_addr += (unsigned long)
					per_cpu_ptr(base, get_boot_cpu_id());
			}
			return true;
		}
	}
#endif
	/* on UP, can't distinguish from other static vars, always false */
	return false;
}

/**
 * is_kernel_percpu_address - test whether address is from static percpu area
 * @addr: address to test
 *
 * Test whether @addr belongs to in-kernel static percpu area.  Module
 * static percpu areas are not considered.  For those, use
 * is_module_percpu_address().
 *
 * RETURNS:
 * %true if @addr is from in-kernel static percpu area, %false otherwise.
 */
bool is_kernel_percpu_address(unsigned long addr)
{
	return __is_kernel_percpu_address(addr, NULL);
}

/**
 * per_cpu_ptr_to_phys - convert translated percpu address to physical address
 * @addr: the address to be converted to physical address
 *
 * Given @addr which is dereferenceable address obtained via one of
 * percpu access macros, this function translates it into its physical
 * address.  The caller is responsible for ensuring @addr stays valid
 * until this function finishes.
 *
 * percpu allocator has special setup for the first chunk, which currently
 * supports either embedding in linear address space or vmalloc mapping,
 * and, from the second one, the backing allocator (currently either vm or
 * km) provides translation.
 *
 * The addr can be translated simply without checking if it falls into the
 * first chunk. But the current code reflects better how percpu allocator
 * actually works, and the verification can discover both bugs in percpu
 * allocator itself and per_cpu_ptr_to_phys() callers. So we keep current
 * code.
 *
 * RETURNS:
 * The physical address for @addr.
 */
phys_addr_t per_cpu_ptr_to_phys(void *addr)
{
	void __percpu *base = __addr_to_pcpu_ptr(pcpu_base_addr);
	bool in_first_chunk = false;
	unsigned long first_low, first_high;
	unsigned int cpu;

	/*
	 * The following test on unit_low/high isn't strictly
	 * necessary but will speed up lookups of addresses which
	 * aren't in the first chunk.
	 *
	 * The address check is against full chunk sizes.  pcpu_base_addr
	 * points to the beginning of the first chunk including the
	 * static region.  Assumes good intent as the first chunk may
	 * not be full (ie. < pcpu_unit_pages in size).
	 */
	first_low = (unsigned long)pcpu_base_addr +
		    pcpu_unit_page_offset(pcpu_low_unit_cpu, 0);
	first_high = (unsigned long)pcpu_base_addr +
		     pcpu_unit_page_offset(pcpu_high_unit_cpu, pcpu_unit_pages);
	if ((unsigned long)addr >= first_low &&
	    (unsigned long)addr < first_high) {
		for_each_possible_cpu(cpu) {
			void *start = per_cpu_ptr(base, cpu);

			if (addr >= start && addr < start + pcpu_unit_size) {
				in_first_chunk = true;
				break;
			}
		}
	}

	if (in_first_chunk) {
		if (!is_vmalloc_addr(addr))
			return __pa(addr);
		else
			return page_to_phys(vmalloc_to_page(addr)) +
			       offset_in_page(addr);
	} else
		return page_to_phys(pcpu_addr_to_page(addr)) +
		       offset_in_page(addr);
}

/**
 * pcpu_alloc_alloc_info - allocate percpu allocation info
 * @nr_groups: the number of groups
 * @nr_units: the number of units
 *
 * Allocate ai which is large enough for @nr_groups groups containing
 * @nr_units units.  The returned ai's groups[0].cpu_map points to the
 * cpu_map array which is long enough for @nr_units and filled with
 * NR_CPUS.  It's the caller's responsibility to initialize cpu_map
 * pointer of other groups.
 *
 * RETURNS:
 * Pointer to the allocated pcpu_alloc_info on success, NULL on
 * failure.
 */
struct pcpu_alloc_info * __init pcpu_alloc_alloc_info(int nr_groups,
						      int nr_units)
{
	struct pcpu_alloc_info *ai;
	size_t base_size, ai_size;
	void *ptr;
	int unit;

	base_size = ALIGN(struct_size(ai, groups, nr_groups),
			  __alignof__(ai->groups[0].cpu_map[0]));
	ai_size = base_size + nr_units * sizeof(ai->groups[0].cpu_map[0]);

	ptr = memblock_alloc(PFN_ALIGN(ai_size), PAGE_SIZE);
	if (!ptr)
		return NULL;
	ai = ptr;
	ptr += base_size;

	ai->groups[0].cpu_map = ptr;

	for (unit = 0; unit < nr_units; unit++)
		ai->groups[0].cpu_map[unit] = NR_CPUS;

	ai->nr_groups = nr_groups;
	ai->__ai_size = PFN_ALIGN(ai_size);

	return ai;
}

/**
 * pcpu_free_alloc_info - free percpu allocation info
 * @ai: pcpu_alloc_info to free
 *
 * Free @ai which was allocated by pcpu_alloc_alloc_info().
 */
void __init pcpu_free_alloc_info(struct pcpu_alloc_info *ai)
{
	memblock_free_early(__pa(ai), ai->__ai_size);
}

/**
 * pcpu_dump_alloc_info - print out information about pcpu_alloc_info
 * @lvl: loglevel
 * @ai: allocation info to dump
 *
 * Print out information about @ai using loglevel @lvl.
 */
static void pcpu_dump_alloc_info(const char *lvl,
				 const struct pcpu_alloc_info *ai)
{
	int group_width = 1, cpu_width = 1, width;
	char empty_str[] = "--------";
	int alloc = 0, alloc_end = 0;
	int group, v;
	int upa, apl;	/* units per alloc, allocs per line */

	v = ai->nr_groups;
	while (v /= 10)
		group_width++;

	v = num_possible_cpus();
	while (v /= 10)
		cpu_width++;
	empty_str[min_t(int, cpu_width, sizeof(empty_str) - 1)] = '\0';

	upa = ai->alloc_size / ai->unit_size;
	width = upa * (cpu_width + 1) + group_width + 3;
	apl = rounddown_pow_of_two(max(60 / width, 1));

	printk("%spcpu-alloc: s%zu r%zu d%zu u%zu alloc=%zu*%zu",
	       lvl, ai->static_size, ai->reserved_size, ai->dyn_size,
	       ai->unit_size, ai->alloc_size / ai->atom_size, ai->atom_size);

	for (group = 0; group < ai->nr_groups; group++) {
		const struct pcpu_group_info *gi = &ai->groups[group];
		int unit = 0, unit_end = 0;

		BUG_ON(gi->nr_units % upa);
		for (alloc_end += gi->nr_units / upa;
		     alloc < alloc_end; alloc++) {
			if (!(alloc % apl)) {
				pr_cont("\n");
				printk("%spcpu-alloc: ", lvl);
			}
			pr_cont("[%0*d] ", group_width, group);

			for (unit_end += upa; unit < unit_end; unit++)
				if (gi->cpu_map[unit] != NR_CPUS)
					pr_cont("%0*d ",
						cpu_width, gi->cpu_map[unit]);
				else
					pr_cont("%s ", empty_str);
		}
	}
	pr_cont("\n");
}

/**
 * pcpu_setup_first_chunk - initialize the first percpu chunk
 * @ai: pcpu_alloc_info describing how to percpu area is shaped
 * @base_addr: mapped address
 *
 * Initialize the first percpu chunk which contains the kernel static
 * percpu area.  This function is to be called from arch percpu area
 * setup path.
 *
 * @ai contains all information necessary to initialize the first
 * chunk and prime the dynamic percpu allocator.
 *
 * @ai->static_size is the size of static percpu area.
 *
 * @ai->reserved_size, if non-zero, specifies the amount of bytes to
 * reserve after the static area in the first chunk.  This reserves
 * the first chunk such that it's available only through reserved
 * percpu allocation.  This is primarily used to serve module percpu
 * static areas on architectures where the addressing model has
 * limited offset range for symbol relocations to guarantee module
 * percpu symbols fall inside the relocatable range.
 *
 * @ai->dyn_size determines the number of bytes available for dynamic
 * allocation in the first chunk.  The area between @ai->static_size +
 * @ai->reserved_size + @ai->dyn_size and @ai->unit_size is unused.
 *
 * @ai->unit_size specifies unit size and must be aligned to PAGE_SIZE
 * and equal to or larger than @ai->static_size + @ai->reserved_size +
 * @ai->dyn_size.
 *
 * @ai->atom_size is the allocation atom size and used as alignment
 * for vm areas.
 *
 * @ai->alloc_size is the allocation size and always multiple of
 * @ai->atom_size.  This is larger than @ai->atom_size if
 * @ai->unit_size is larger than @ai->atom_size.
 *
 * @ai->nr_groups and @ai->groups describe virtual memory layout of
 * percpu areas.  Units which should be colocated are put into the
 * same group.  Dynamic VM areas will be allocated according to these
 * groupings.  If @ai->nr_groups is zero, a single group containing
 * all units is assumed.
 *
 * The caller should have mapped the first chunk at @base_addr and
 * copied static data to each unit.
 *
 * The first chunk will always contain a static and a dynamic region.
 * However, the static region is not managed by any chunk.  If the first
 * chunk also contains a reserved region, it is served by two chunks -
 * one for the reserved region and one for the dynamic region.  They
 * share the same vm, but use offset regions in the area allocation map.
 * The chunk serving the dynamic region is circulated in the chunk slots
 * and available for dynamic allocation like any other chunk.
 */
void __init pcpu_setup_first_chunk(const struct pcpu_alloc_info *ai,
				   void *base_addr)
{
	size_t size_sum = ai->static_size + ai->reserved_size + ai->dyn_size;
	size_t static_size, dyn_size;
	struct pcpu_chunk *chunk;
	unsigned long *group_offsets;
	size_t *group_sizes;
	unsigned long *unit_off;
	unsigned int cpu;
	int *unit_map;
	int group, unit, i;
	int map_size;
	unsigned long tmp_addr;
	size_t alloc_size;

#define PCPU_SETUP_BUG_ON(cond)	do {					\
	if (unlikely(cond)) {						\
		pr_emerg("failed to initialize, %s\n", #cond);		\
		pr_emerg("cpu_possible_mask=%*pb\n",			\
			 cpumask_pr_args(cpu_possible_mask));		\
		pcpu_dump_alloc_info(KERN_EMERG, ai);			\
		BUG();							\
	}								\
} while (0)

	/* sanity checks */
	PCPU_SETUP_BUG_ON(ai->nr_groups <= 0);
#ifdef CONFIG_SMP
	PCPU_SETUP_BUG_ON(!ai->static_size);
	PCPU_SETUP_BUG_ON(offset_in_page(__per_cpu_start));
#endif
	PCPU_SETUP_BUG_ON(!base_addr);
	PCPU_SETUP_BUG_ON(offset_in_page(base_addr));
	PCPU_SETUP_BUG_ON(ai->unit_size < size_sum);
	PCPU_SETUP_BUG_ON(offset_in_page(ai->unit_size));
	PCPU_SETUP_BUG_ON(ai->unit_size < PCPU_MIN_UNIT_SIZE);
	PCPU_SETUP_BUG_ON(!IS_ALIGNED(ai->unit_size, PCPU_BITMAP_BLOCK_SIZE));
	PCPU_SETUP_BUG_ON(ai->dyn_size < PERCPU_DYNAMIC_EARLY_SIZE);
	PCPU_SETUP_BUG_ON(!ai->dyn_size);
	PCPU_SETUP_BUG_ON(!IS_ALIGNED(ai->reserved_size, PCPU_MIN_ALLOC_SIZE));
	PCPU_SETUP_BUG_ON(!(IS_ALIGNED(PCPU_BITMAP_BLOCK_SIZE, PAGE_SIZE) ||
			    IS_ALIGNED(PAGE_SIZE, PCPU_BITMAP_BLOCK_SIZE)));
	PCPU_SETUP_BUG_ON(pcpu_verify_alloc_info(ai) < 0);

	/* process group information and build config tables accordingly */
	alloc_size = ai->nr_groups * sizeof(group_offsets[0]);
	group_offsets = memblock_alloc(alloc_size, SMP_CACHE_BYTES);
	if (!group_offsets)
		panic("%s: Failed to allocate %zu bytes\n", __func__,
		      alloc_size);

	alloc_size = ai->nr_groups * sizeof(group_sizes[0]);
	group_sizes = memblock_alloc(alloc_size, SMP_CACHE_BYTES);
	if (!group_sizes)
		panic("%s: Failed to allocate %zu bytes\n", __func__,
		      alloc_size);

	alloc_size = nr_cpu_ids * sizeof(unit_map[0]);
	unit_map = memblock_alloc(alloc_size, SMP_CACHE_BYTES);
	if (!unit_map)
		panic("%s: Failed to allocate %zu bytes\n", __func__,
		      alloc_size);

	alloc_size = nr_cpu_ids * sizeof(unit_off[0]);
	unit_off = memblock_alloc(alloc_size, SMP_CACHE_BYTES);
	if (!unit_off)
		panic("%s: Failed to allocate %zu bytes\n", __func__,
		      alloc_size);

	for (cpu = 0; cpu < nr_cpu_ids; cpu++)
		unit_map[cpu] = UINT_MAX;

	pcpu_low_unit_cpu = NR_CPUS;
	pcpu_high_unit_cpu = NR_CPUS;

	for (group = 0, unit = 0; group < ai->nr_groups; group++, unit += i) {
		const struct pcpu_group_info *gi = &ai->groups[group];

		group_offsets[group] = gi->base_offset;
		group_sizes[group] = gi->nr_units * ai->unit_size;

		for (i = 0; i < gi->nr_units; i++) {
			cpu = gi->cpu_map[i];
			if (cpu == NR_CPUS)
				continue;

			PCPU_SETUP_BUG_ON(cpu >= nr_cpu_ids);
			PCPU_SETUP_BUG_ON(!cpu_possible(cpu));
			PCPU_SETUP_BUG_ON(unit_map[cpu] != UINT_MAX);

			unit_map[cpu] = unit + i;
			unit_off[cpu] = gi->base_offset + i * ai->unit_size;

			/* determine low/high unit_cpu */
			if (pcpu_low_unit_cpu == NR_CPUS ||
			    unit_off[cpu] < unit_off[pcpu_low_unit_cpu])
				pcpu_low_unit_cpu = cpu;
			if (pcpu_high_unit_cpu == NR_CPUS ||
			    unit_off[cpu] > unit_off[pcpu_high_unit_cpu])
				pcpu_high_unit_cpu = cpu;
		}
	}
	pcpu_nr_units = unit;

	for_each_possible_cpu(cpu)
		PCPU_SETUP_BUG_ON(unit_map[cpu] == UINT_MAX);

	/* we're done parsing the input, undefine BUG macro and dump config */
#undef PCPU_SETUP_BUG_ON
	pcpu_dump_alloc_info(KERN_DEBUG, ai);

	pcpu_nr_groups = ai->nr_groups;
	pcpu_group_offsets = group_offsets;
	pcpu_group_sizes = group_sizes;
	pcpu_unit_map = unit_map;
	pcpu_unit_offsets = unit_off;

	/* determine basic parameters */
	pcpu_unit_pages = ai->unit_size >> PAGE_SHIFT;
	pcpu_unit_size = pcpu_unit_pages << PAGE_SHIFT;
	pcpu_atom_size = ai->atom_size;
	pcpu_chunk_struct_size = sizeof(struct pcpu_chunk) +
		BITS_TO_LONGS(pcpu_unit_pages) * sizeof(unsigned long);

	pcpu_stats_save_ai(ai);

	/*
	 * Allocate chunk slots.  The additional last slot is for
	 * empty chunks.
	 */
	pcpu_nr_slots = __pcpu_size_to_slot(pcpu_unit_size) + 2;
	pcpu_slot = memblock_alloc(pcpu_nr_slots * sizeof(pcpu_slot[0]),
				   SMP_CACHE_BYTES);
	if (!pcpu_slot)
		panic("%s: Failed to allocate %zu bytes\n", __func__,
		      pcpu_nr_slots * sizeof(pcpu_slot[0]));
	for (i = 0; i < pcpu_nr_slots; i++)
		INIT_LIST_HEAD(&pcpu_slot[i]);

	/*
	 * The end of the static region needs to be aligned with the
	 * minimum allocation size as this offsets the reserved and
	 * dynamic region.  The first chunk ends page aligned by
	 * expanding the dynamic region, therefore the dynamic region
	 * can be shrunk to compensate while still staying above the
	 * configured sizes.
	 */
	static_size = ALIGN(ai->static_size, PCPU_MIN_ALLOC_SIZE);
	dyn_size = ai->dyn_size - (static_size - ai->static_size);

	/*
	 * Initialize first chunk.
	 * If the reserved_size is non-zero, this initializes the reserved
	 * chunk.  If the reserved_size is zero, the reserved chunk is NULL
	 * and the dynamic region is initialized here.  The first chunk,
	 * pcpu_first_chunk, will always point to the chunk that serves
	 * the dynamic region.
	 */
	tmp_addr = (unsigned long)base_addr + static_size;
	map_size = ai->reserved_size ?: dyn_size;
	chunk = pcpu_alloc_first_chunk(tmp_addr, map_size);

	/* init dynamic chunk if necessary */
	if (ai->reserved_size) {
		pcpu_reserved_chunk = chunk;

		tmp_addr = (unsigned long)base_addr + static_size +
			   ai->reserved_size;
		map_size = dyn_size;
		chunk = pcpu_alloc_first_chunk(tmp_addr, map_size);
	}

	/* link the first chunk in */
	pcpu_first_chunk = chunk;
	pcpu_nr_empty_pop_pages = pcpu_first_chunk->nr_empty_pop_pages;
	pcpu_chunk_relocate(pcpu_first_chunk, -1);

	/* include all regions of the first chunk */
	pcpu_nr_populated += PFN_DOWN(size_sum);

	pcpu_stats_chunk_alloc();
	trace_percpu_create_chunk(base_addr);

	/* we're done */
	pcpu_base_addr = base_addr;
}

#ifdef CONFIG_SMP

const char * const pcpu_fc_names[PCPU_FC_NR] __initconst = {
	[PCPU_FC_AUTO]	= "auto",
	[PCPU_FC_EMBED]	= "embed",
	[PCPU_FC_PAGE]	= "page",
};

enum pcpu_fc pcpu_chosen_fc __initdata = PCPU_FC_AUTO;

static int __init percpu_alloc_setup(char *str)
{
	if (!str)
		return -EINVAL;

	if (0)
		/* nada */;
#ifdef CONFIG_NEED_PER_CPU_EMBED_FIRST_CHUNK
	else if (!strcmp(str, "embed"))
		pcpu_chosen_fc = PCPU_FC_EMBED;
#endif
#ifdef CONFIG_NEED_PER_CPU_PAGE_FIRST_CHUNK
	else if (!strcmp(str, "page"))
		pcpu_chosen_fc = PCPU_FC_PAGE;
#endif
	else
		pr_warn("unknown allocator %s specified\n", str);

	return 0;
}
early_param("percpu_alloc", percpu_alloc_setup);

/*
 * pcpu_embed_first_chunk() is used by the generic percpu setup.
 * Build it if needed by the arch config or the generic setup is going
 * to be used.
 */
#if defined(CONFIG_NEED_PER_CPU_EMBED_FIRST_CHUNK) || \
	!defined(CONFIG_HAVE_SETUP_PER_CPU_AREA)
#define BUILD_EMBED_FIRST_CHUNK
#endif

/* build pcpu_page_first_chunk() iff needed by the arch config */
#if defined(CONFIG_NEED_PER_CPU_PAGE_FIRST_CHUNK)
#define BUILD_PAGE_FIRST_CHUNK
#endif

/* pcpu_build_alloc_info() is used by both embed and page first chunk */
#if defined(BUILD_EMBED_FIRST_CHUNK) || defined(BUILD_PAGE_FIRST_CHUNK)
/**
 * pcpu_build_alloc_info - build alloc_info considering distances between CPUs
 * @reserved_size: the size of reserved percpu area in bytes
 * @dyn_size: minimum free size for dynamic allocation in bytes
 * @atom_size: allocation atom size
 * @cpu_distance_fn: callback to determine distance between cpus, optional
 *
 * This function determines grouping of units, their mappings to cpus
 * and other parameters considering needed percpu size, allocation
 * atom size and distances between CPUs.
 *
 * Groups are always multiples of atom size and CPUs which are of
 * LOCAL_DISTANCE both ways are grouped together and share space for
 * units in the same group.  The returned configuration is guaranteed
 * to have CPUs on different nodes on different groups and >=75% usage
 * of allocated virtual address space.
 *
 * RETURNS:
 * On success, pointer to the new allocation_info is returned.  On
 * failure, ERR_PTR value is returned.
 */
static struct pcpu_alloc_info * __init pcpu_build_alloc_info(
				size_t reserved_size, size_t dyn_size,
				size_t atom_size,
				pcpu_fc_cpu_distance_fn_t cpu_distance_fn)
{
	static int group_map[NR_CPUS] __initdata;
	static int group_cnt[NR_CPUS] __initdata;
	const size_t static_size = __per_cpu_end - __per_cpu_start;
	int nr_groups = 1, nr_units = 0;
	size_t size_sum, min_unit_size, alloc_size;
	int upa, max_upa, uninitialized_var(best_upa);	/* units_per_alloc */
	int last_allocs, group, unit;
	unsigned int cpu, tcpu;
	struct pcpu_alloc_info *ai;
	unsigned int *cpu_map;

	/* this function may be called multiple times */
	memset(group_map, 0, sizeof(group_map));
	memset(group_cnt, 0, sizeof(group_cnt));

	/* calculate size_sum and ensure dyn_size is enough for early alloc */
	size_sum = PFN_ALIGN(static_size + reserved_size +
			    max_t(size_t, dyn_size, PERCPU_DYNAMIC_EARLY_SIZE));
	dyn_size = size_sum - static_size - reserved_size;

	/*
	 * Determine min_unit_size, alloc_size and max_upa such that
	 * alloc_size is multiple of atom_size and is the smallest
	 * which can accommodate 4k aligned segments which are equal to
	 * or larger than min_unit_size.
	 */
	min_unit_size = max_t(size_t, size_sum, PCPU_MIN_UNIT_SIZE);

	/* determine the maximum # of units that can fit in an allocation */
	alloc_size = roundup(min_unit_size, atom_size);
	upa = alloc_size / min_unit_size;
	while (alloc_size % upa || (offset_in_page(alloc_size / upa)))
		upa--;
	max_upa = upa;

	/* group cpus according to their proximity */
	for_each_possible_cpu(cpu) {
		group = 0;
	next_group:
		for_each_possible_cpu(tcpu) {
			if (cpu == tcpu)
				break;
			if (group_map[tcpu] == group && cpu_distance_fn &&
			    (cpu_distance_fn(cpu, tcpu) > LOCAL_DISTANCE ||
			     cpu_distance_fn(tcpu, cpu) > LOCAL_DISTANCE)) {
				group++;
				nr_groups = max(nr_groups, group + 1);
				goto next_group;
			}
		}
		group_map[cpu] = group;
		group_cnt[group]++;
	}

	/*
	 * Wasted space is caused by a ratio imbalance of upa to group_cnt.
	 * Expand the unit_size until we use >= 75% of the units allocated.
	 * Related to atom_size, which could be much larger than the unit_size.
	 */
	last_allocs = INT_MAX;
	for (upa = max_upa; upa; upa--) {
		int allocs = 0, wasted = 0;

		if (alloc_size % upa || (offset_in_page(alloc_size / upa)))
			continue;

		for (group = 0; group < nr_groups; group++) {
			int this_allocs = DIV_ROUND_UP(group_cnt[group], upa);
			allocs += this_allocs;
			wasted += this_allocs * upa - group_cnt[group];
		}

		/*
		 * Don't accept if wastage is over 1/3.  The
		 * greater-than comparison ensures upa==1 always
		 * passes the following check.
		 */
		if (wasted > num_possible_cpus() / 3)
			continue;

		/* and then don't consume more memory */
		if (allocs > last_allocs)
			break;
		last_allocs = allocs;
		best_upa = upa;
	}
	upa = best_upa;

	/* allocate and fill alloc_info */
	for (group = 0; group < nr_groups; group++)
		nr_units += roundup(group_cnt[group], upa);

	ai = pcpu_alloc_alloc_info(nr_groups, nr_units);
	if (!ai)
		return ERR_PTR(-ENOMEM);
	cpu_map = ai->groups[0].cpu_map;

	for (group = 0; group < nr_groups; group++) {
		ai->groups[group].cpu_map = cpu_map;
		cpu_map += roundup(group_cnt[group], upa);
	}

	ai->static_size = static_size;
	ai->reserved_size = reserved_size;
	ai->dyn_size = dyn_size;
	ai->unit_size = alloc_size / upa;
	ai->atom_size = atom_size;
	ai->alloc_size = alloc_size;

	for (group = 0, unit = 0; group < nr_groups; group++) {
		struct pcpu_group_info *gi = &ai->groups[group];

		/*
		 * Initialize base_offset as if all groups are located
		 * back-to-back.  The caller should update this to
		 * reflect actual allocation.
		 */
		gi->base_offset = unit * ai->unit_size;

		for_each_possible_cpu(cpu)
			if (group_map[cpu] == group)
				gi->cpu_map[gi->nr_units++] = cpu;
		gi->nr_units = roundup(gi->nr_units, upa);
		unit += gi->nr_units;
	}
	BUG_ON(unit != nr_units);

	return ai;
}
#endif /* BUILD_EMBED_FIRST_CHUNK || BUILD_PAGE_FIRST_CHUNK */

#if defined(BUILD_EMBED_FIRST_CHUNK)
/**
 * pcpu_embed_first_chunk - embed the first percpu chunk into bootmem
 * @reserved_size: the size of reserved percpu area in bytes
 * @dyn_size: minimum free size for dynamic allocation in bytes
 * @atom_size: allocation atom size
 * @cpu_distance_fn: callback to determine distance between cpus, optional
 * @alloc_fn: function to allocate percpu page
 * @free_fn: function to free percpu page
 *
 * This is a helper to ease setting up embedded first percpu chunk and
 * can be called where pcpu_setup_first_chunk() is expected.
 *
 * If this function is used to setup the first chunk, it is allocated
 * by calling @alloc_fn and used as-is without being mapped into
 * vmalloc area.  Allocations are always whole multiples of @atom_size
 * aligned to @atom_size.
 *
 * This enables the first chunk to piggy back on the linear physical
 * mapping which often uses larger page size.  Please note that this
 * can result in very sparse cpu->unit mapping on NUMA machines thus
 * requiring large vmalloc address space.  Don't use this allocator if
 * vmalloc space is not orders of magnitude larger than distances
 * between node memory addresses (ie. 32bit NUMA machines).
 *
 * @dyn_size specifies the minimum dynamic area size.
 *
 * If the needed size is smaller than the minimum or specified unit
 * size, the leftover is returned using @free_fn.
 *
 * RETURNS:
 * 0 on success, -errno on failure.
 */
int __init pcpu_embed_first_chunk(size_t reserved_size, size_t dyn_size,
				  size_t atom_size,
				  pcpu_fc_cpu_distance_fn_t cpu_distance_fn,
				  pcpu_fc_alloc_fn_t alloc_fn,
				  pcpu_fc_free_fn_t free_fn)
{
	void *base = (void *)ULONG_MAX;
	void **areas = NULL;
	struct pcpu_alloc_info *ai;
	size_t size_sum, areas_size;
	unsigned long max_distance;
	int group, i, highest_group, rc = 0;

	ai = pcpu_build_alloc_info(reserved_size, dyn_size, atom_size,
				   cpu_distance_fn);
	if (IS_ERR(ai))
		return PTR_ERR(ai);

	size_sum = ai->static_size + ai->reserved_size + ai->dyn_size;
	areas_size = PFN_ALIGN(ai->nr_groups * sizeof(void *));

	areas = memblock_alloc(areas_size, SMP_CACHE_BYTES);
	if (!areas) {
		rc = -ENOMEM;
		goto out_free;
	}

	/* allocate, copy and determine base address & max_distance */
	highest_group = 0;
	for (group = 0; group < ai->nr_groups; group++) {
		struct pcpu_group_info *gi = &ai->groups[group];
		unsigned int cpu = NR_CPUS;
		void *ptr;

		for (i = 0; i < gi->nr_units && cpu == NR_CPUS; i++)
			cpu = gi->cpu_map[i];
		BUG_ON(cpu == NR_CPUS);

		/* allocate space for the whole group */
		ptr = alloc_fn(cpu, gi->nr_units * ai->unit_size, atom_size);
		if (!ptr) {
			rc = -ENOMEM;
			goto out_free_areas;
		}
		/* kmemleak tracks the percpu allocations separately */
		kmemleak_free(ptr);
		areas[group] = ptr;

		base = min(ptr, base);
		if (ptr > areas[highest_group])
			highest_group = group;
	}
	max_distance = areas[highest_group] - base;
	max_distance += ai->unit_size * ai->groups[highest_group].nr_units;

	/* warn if maximum distance is further than 75% of vmalloc space */
	if (max_distance > VMALLOC_TOTAL * 3 / 4) {
		pr_warn("max_distance=0x%lx too large for vmalloc space 0x%lx\n",
				max_distance, VMALLOC_TOTAL);
#ifdef CONFIG_NEED_PER_CPU_PAGE_FIRST_CHUNK
		/* and fail if we have fallback */
		rc = -EINVAL;
		goto out_free_areas;
#endif
	}

	/*
	 * Copy data and free unused parts.  This should happen after all
	 * allocations are complete; otherwise, we may end up with
	 * overlapping groups.
	 */
	for (group = 0; group < ai->nr_groups; group++) {
		struct pcpu_group_info *gi = &ai->groups[group];
		void *ptr = areas[group];

		for (i = 0; i < gi->nr_units; i++, ptr += ai->unit_size) {
			if (gi->cpu_map[i] == NR_CPUS) {
				/* unused unit, free whole */
				free_fn(ptr, ai->unit_size);
				continue;
			}
			/* copy and return the unused part */
			memcpy(ptr, __per_cpu_load, ai->static_size);
			free_fn(ptr + size_sum, ai->unit_size - size_sum);
		}
	}

	/* base address is now known, determine group base offsets */
	for (group = 0; group < ai->nr_groups; group++) {
		ai->groups[group].base_offset = areas[group] - base;
	}

	pr_info("Embedded %zu pages/cpu s%zu r%zu d%zu u%zu\n",
		PFN_DOWN(size_sum), ai->static_size, ai->reserved_size,
		ai->dyn_size, ai->unit_size);

	pcpu_setup_first_chunk(ai, base);
	goto out_free;

out_free_areas:
	for (group = 0; group < ai->nr_groups; group++)
		if (areas[group])
			free_fn(areas[group],
				ai->groups[group].nr_units * ai->unit_size);
out_free:
	pcpu_free_alloc_info(ai);
	if (areas)
		memblock_free_early(__pa(areas), areas_size);
	return rc;
}
#endif /* BUILD_EMBED_FIRST_CHUNK */

#ifdef BUILD_PAGE_FIRST_CHUNK
/**
 * pcpu_page_first_chunk - map the first chunk using PAGE_SIZE pages
 * @reserved_size: the size of reserved percpu area in bytes
 * @alloc_fn: function to allocate percpu page, always called with PAGE_SIZE
 * @free_fn: function to free percpu page, always called with PAGE_SIZE
 * @populate_pte_fn: function to populate pte
 *
 * This is a helper to ease setting up page-remapped first percpu
 * chunk and can be called where pcpu_setup_first_chunk() is expected.
 *
 * This is the basic allocator.  Static percpu area is allocated
 * page-by-page into vmalloc area.
 *
 * RETURNS:
 * 0 on success, -errno on failure.
 */
int __init pcpu_page_first_chunk(size_t reserved_size,
				 pcpu_fc_alloc_fn_t alloc_fn,
				 pcpu_fc_free_fn_t free_fn,
				 pcpu_fc_populate_pte_fn_t populate_pte_fn)
{
	static struct vm_struct vm;
	struct pcpu_alloc_info *ai;
	char psize_str[16];
	int unit_pages;
	size_t pages_size;
	struct page **pages;
	int unit, i, j, rc = 0;
	int upa;
	int nr_g0_units;

	snprintf(psize_str, sizeof(psize_str), "%luK", PAGE_SIZE >> 10);

	ai = pcpu_build_alloc_info(reserved_size, 0, PAGE_SIZE, NULL);
	if (IS_ERR(ai))
		return PTR_ERR(ai);
	BUG_ON(ai->nr_groups != 1);
	upa = ai->alloc_size/ai->unit_size;
	nr_g0_units = roundup(num_possible_cpus(), upa);
	if (WARN_ON(ai->groups[0].nr_units != nr_g0_units)) {
		pcpu_free_alloc_info(ai);
		return -EINVAL;
	}

	unit_pages = ai->unit_size >> PAGE_SHIFT;

	/* unaligned allocations can't be freed, round up to page size */
	pages_size = PFN_ALIGN(unit_pages * num_possible_cpus() *
			       sizeof(pages[0]));
	pages = memblock_alloc(pages_size, SMP_CACHE_BYTES);
	if (!pages)
		panic("%s: Failed to allocate %zu bytes\n", __func__,
		      pages_size);

	/* allocate pages */
	j = 0;
	for (unit = 0; unit < num_possible_cpus(); unit++) {
		unsigned int cpu = ai->groups[0].cpu_map[unit];
		for (i = 0; i < unit_pages; i++) {
			void *ptr;

			ptr = alloc_fn(cpu, PAGE_SIZE, PAGE_SIZE);
			if (!ptr) {
				pr_warn("failed to allocate %s page for cpu%u\n",
						psize_str, cpu);
				goto enomem;
			}
			/* kmemleak tracks the percpu allocations separately */
			kmemleak_free(ptr);
			pages[j++] = virt_to_page(ptr);
		}
	}

	/* allocate vm area, map the pages and copy static data */
	vm.flags = VM_ALLOC;
	vm.size = num_possible_cpus() * ai->unit_size;
	vm_area_register_early(&vm, PAGE_SIZE);

	for (unit = 0; unit < num_possible_cpus(); unit++) {
		unsigned long unit_addr =
			(unsigned long)vm.addr + unit * ai->unit_size;

		for (i = 0; i < unit_pages; i++)
			populate_pte_fn(unit_addr + (i << PAGE_SHIFT));

		/* pte already populated, the following shouldn't fail */
		rc = __pcpu_map_pages(unit_addr, &pages[unit * unit_pages],
				      unit_pages);
		if (rc < 0)
			panic("failed to map percpu area, err=%d\n", rc);

		/*
		 * FIXME: Archs with virtual cache should flush local
		 * cache for the linear mapping here - something
		 * equivalent to flush_cache_vmap() on the local cpu.
		 * flush_cache_vmap() can't be used as most supporting
		 * data structures are not set up yet.
		 */

		/* copy static data */
		memcpy((void *)unit_addr, __per_cpu_load, ai->static_size);
	}

	/* we're ready, commit */
	pr_info("%d %s pages/cpu s%zu r%zu d%zu\n",
		unit_pages, psize_str, ai->static_size,
		ai->reserved_size, ai->dyn_size);

	pcpu_setup_first_chunk(ai, vm.addr);
	goto out_free_ar;

enomem:
	while (--j >= 0)
		free_fn(page_address(pages[j]), PAGE_SIZE);
	rc = -ENOMEM;
out_free_ar:
	memblock_free_early(__pa(pages), pages_size);
	pcpu_free_alloc_info(ai);
	return rc;
}
#endif /* BUILD_PAGE_FIRST_CHUNK */

#ifndef	CONFIG_HAVE_SETUP_PER_CPU_AREA
/*
 * Generic SMP percpu area setup.
 *
 * The embedding helper is used because its behavior closely resembles
 * the original non-dynamic generic percpu area setup.  This is
 * important because many archs have addressing restrictions and might
 * fail if the percpu area is located far away from the previous
 * location.  As an added bonus, in non-NUMA cases, embedding is
 * generally a good idea TLB-wise because percpu area can piggy back
 * on the physical linear memory mapping which uses large page
 * mappings on applicable archs.
 */
unsigned long __per_cpu_offset[NR_CPUS] __read_mostly;
EXPORT_SYMBOL(__per_cpu_offset);

static void * __init pcpu_dfl_fc_alloc(unsigned int cpu, size_t size,
				       size_t align)
{
	return  memblock_alloc_from(size, align, __pa(MAX_DMA_ADDRESS));
}

static void __init pcpu_dfl_fc_free(void *ptr, size_t size)
{
	memblock_free_early(__pa(ptr), size);
}

void __init setup_per_cpu_areas(void)
{
	unsigned long delta;
	unsigned int cpu;
	int rc;

	/*
	 * Always reserve area for module percpu variables.  That's
	 * what the legacy allocator did.
	 */
	rc = pcpu_embed_first_chunk(PERCPU_MODULE_RESERVE,
				    PERCPU_DYNAMIC_RESERVE, PAGE_SIZE, NULL,
				    pcpu_dfl_fc_alloc, pcpu_dfl_fc_free);
	if (rc < 0)
		panic("Failed to initialize percpu areas.");

	delta = (unsigned long)pcpu_base_addr - (unsigned long)__per_cpu_start;
	for_each_possible_cpu(cpu)
		__per_cpu_offset[cpu] = delta + pcpu_unit_offsets[cpu];
}
#endif	/* CONFIG_HAVE_SETUP_PER_CPU_AREA */

#else	/* CONFIG_SMP */

/*
 * UP percpu area setup.
 *
 * UP always uses km-based percpu allocator with identity mapping.
 * Static percpu variables are indistinguishable from the usual static
 * variables and don't require any special preparation.
 */
void __init setup_per_cpu_areas(void)
{
	const size_t unit_size =
		roundup_pow_of_two(max_t(size_t, PCPU_MIN_UNIT_SIZE,
					 PERCPU_DYNAMIC_RESERVE));
	struct pcpu_alloc_info *ai;
	void *fc;

	ai = pcpu_alloc_alloc_info(1, 1);
	fc = memblock_alloc_from(unit_size, PAGE_SIZE, __pa(MAX_DMA_ADDRESS));
	if (!ai || !fc)
		panic("Failed to allocate memory for percpu areas.");
	/* kmemleak tracks the percpu allocations separately */
	kmemleak_free(fc);

	ai->dyn_size = unit_size;
	ai->unit_size = unit_size;
	ai->atom_size = unit_size;
	ai->alloc_size = unit_size;
	ai->groups[0].nr_units = 1;
	ai->groups[0].cpu_map[0] = 0;

	pcpu_setup_first_chunk(ai, fc);
	pcpu_free_alloc_info(ai);
}

#endif	/* CONFIG_SMP */

/*
 * pcpu_nr_pages - calculate total number of populated backing pages
 *
 * This reflects the number of pages populated to back chunks.  Metadata is
 * excluded in the number exposed in meminfo as the number of backing pages
 * scales with the number of cpus and can quickly outweigh the memory used for
 * metadata.  It also keeps this calculation nice and simple.
 *
 * RETURNS:
 * Total number of populated backing pages in use by the allocator.
 */
unsigned long pcpu_nr_pages(void)
{
	return pcpu_nr_populated * pcpu_nr_units;
}

/*
 * Percpu allocator is initialized early during boot when neither slab or
 * workqueue is available.  Plug async management until everything is up
 * and running.
 */
static int __init percpu_enable_async(void)
{
	pcpu_async_enabled = true;
	return 0;
}
subsys_initcall(percpu_enable_async);
