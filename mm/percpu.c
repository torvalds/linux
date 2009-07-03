/*
 * linux/mm/percpu.c - percpu memory allocator
 *
 * Copyright (C) 2009		SUSE Linux Products GmbH
 * Copyright (C) 2009		Tejun Heo <tj@kernel.org>
 *
 * This file is released under the GPLv2.
 *
 * This is percpu allocator which can handle both static and dynamic
 * areas.  Percpu areas are allocated in chunks in vmalloc area.  Each
 * chunk is consisted of boot-time determined number of units and the
 * first chunk is used for static percpu variables in the kernel image
 * (special boot time alloc/init handling necessary as these areas
 * need to be brought up before allocation services are running).
 * Unit grows as necessary and all units grow or shrink in unison.
 * When a chunk is filled up, another chunk is allocated.  ie. in
 * vmalloc area
 *
 *  c0                           c1                         c2
 *  -------------------          -------------------        ------------
 * | u0 | u1 | u2 | u3 |        | u0 | u1 | u2 | u3 |      | u0 | u1 | u
 *  -------------------  ......  -------------------  ....  ------------
 *
 * Allocation is done in offset-size areas of single unit space.  Ie,
 * an area of 512 bytes at 6k in c1 occupies 512 bytes at 6k of c1:u0,
 * c1:u1, c1:u2 and c1:u3.  On UMA, units corresponds directly to
 * cpus.  On NUMA, the mapping can be non-linear and even sparse.
 * Percpu access can be done by configuring percpu base registers
 * according to cpu to unit mapping and pcpu_unit_size.
 *
 * There are usually many small percpu allocations many of them being
 * as small as 4 bytes.  The allocator organizes chunks into lists
 * according to free size and tries to allocate from the fullest one.
 * Each chunk keeps the maximum contiguous area size hint which is
 * guaranteed to be eqaul to or larger than the maximum contiguous
 * area in the chunk.  This helps the allocator not to iterate the
 * chunk maps unnecessarily.
 *
 * Allocation state in each chunk is kept using an array of integers
 * on chunk->map.  A positive value in the map represents a free
 * region and negative allocated.  Allocation inside a chunk is done
 * by scanning this map sequentially and serving the first matching
 * entry.  This is mostly copied from the percpu_modalloc() allocator.
 * Chunks can be determined from the address using the index field
 * in the page struct. The index field contains a pointer to the chunk.
 *
 * To use this allocator, arch code should do the followings.
 *
 * - drop CONFIG_HAVE_LEGACY_PER_CPU_AREA
 *
 * - define __addr_to_pcpu_ptr() and __pcpu_ptr_to_addr() to translate
 *   regular address to percpu pointer and back if they need to be
 *   different from the default
 *
 * - use pcpu_setup_first_chunk() during percpu area initialization to
 *   setup the first chunk containing the kernel static percpu area
 */

#include <linux/bitmap.h>
#include <linux/bootmem.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/percpu.h>
#include <linux/pfn.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/vmalloc.h>
#include <linux/workqueue.h>

#include <asm/cacheflush.h>
#include <asm/sections.h>
#include <asm/tlbflush.h>

#define PCPU_SLOT_BASE_SHIFT		5	/* 1-31 shares the same slot */
#define PCPU_DFL_MAP_ALLOC		16	/* start a map with 16 ents */

/* default addr <-> pcpu_ptr mapping, override in asm/percpu.h if necessary */
#ifndef __addr_to_pcpu_ptr
#define __addr_to_pcpu_ptr(addr)					\
	(void *)((unsigned long)(addr) - (unsigned long)pcpu_base_addr	\
		 + (unsigned long)__per_cpu_start)
#endif
#ifndef __pcpu_ptr_to_addr
#define __pcpu_ptr_to_addr(ptr)						\
	(void *)((unsigned long)(ptr) + (unsigned long)pcpu_base_addr	\
		 - (unsigned long)__per_cpu_start)
#endif

struct pcpu_chunk {
	struct list_head	list;		/* linked to pcpu_slot lists */
	int			free_size;	/* free bytes in the chunk */
	int			contig_hint;	/* max contiguous size hint */
	struct vm_struct	*vm;		/* mapped vmalloc region */
	int			map_used;	/* # of map entries used */
	int			map_alloc;	/* # of map entries allocated */
	int			*map;		/* allocation map */
	bool			immutable;	/* no [de]population allowed */
	unsigned long		populated[];	/* populated bitmap */
};

static int pcpu_unit_pages __read_mostly;
static int pcpu_unit_size __read_mostly;
static int pcpu_nr_units __read_mostly;
static int pcpu_chunk_size __read_mostly;
static int pcpu_nr_slots __read_mostly;
static size_t pcpu_chunk_struct_size __read_mostly;

/* cpus with the lowest and highest unit numbers */
static unsigned int pcpu_first_unit_cpu __read_mostly;
static unsigned int pcpu_last_unit_cpu __read_mostly;

/* the address of the first chunk which starts with the kernel static area */
void *pcpu_base_addr __read_mostly;
EXPORT_SYMBOL_GPL(pcpu_base_addr);

/* cpu -> unit map */
const int *pcpu_unit_map __read_mostly;

/*
 * The first chunk which always exists.  Note that unlike other
 * chunks, this one can be allocated and mapped in several different
 * ways and thus often doesn't live in the vmalloc area.
 */
static struct pcpu_chunk *pcpu_first_chunk;

/*
 * Optional reserved chunk.  This chunk reserves part of the first
 * chunk and serves it for reserved allocations.  The amount of
 * reserved offset is in pcpu_reserved_chunk_limit.  When reserved
 * area doesn't exist, the following variables contain NULL and 0
 * respectively.
 */
static struct pcpu_chunk *pcpu_reserved_chunk;
static int pcpu_reserved_chunk_limit;

/*
 * Synchronization rules.
 *
 * There are two locks - pcpu_alloc_mutex and pcpu_lock.  The former
 * protects allocation/reclaim paths, chunks, populated bitmap and
 * vmalloc mapping.  The latter is a spinlock and protects the index
 * data structures - chunk slots, chunks and area maps in chunks.
 *
 * During allocation, pcpu_alloc_mutex is kept locked all the time and
 * pcpu_lock is grabbed and released as necessary.  All actual memory
 * allocations are done using GFP_KERNEL with pcpu_lock released.
 *
 * Free path accesses and alters only the index data structures, so it
 * can be safely called from atomic context.  When memory needs to be
 * returned to the system, free path schedules reclaim_work which
 * grabs both pcpu_alloc_mutex and pcpu_lock, unlinks chunks to be
 * reclaimed, release both locks and frees the chunks.  Note that it's
 * necessary to grab both locks to remove a chunk from circulation as
 * allocation path might be referencing the chunk with only
 * pcpu_alloc_mutex locked.
 */
static DEFINE_MUTEX(pcpu_alloc_mutex);	/* protects whole alloc and reclaim */
static DEFINE_SPINLOCK(pcpu_lock);	/* protects index data structures */

static struct list_head *pcpu_slot __read_mostly; /* chunk list slots */

/* reclaim work to release fully free chunks, scheduled from free path */
static void pcpu_reclaim(struct work_struct *work);
static DECLARE_WORK(pcpu_reclaim_work, pcpu_reclaim);

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
	if (chunk->free_size < sizeof(int) || chunk->contig_hint < sizeof(int))
		return 0;

	return pcpu_size_to_slot(chunk->free_size);
}

static int pcpu_page_idx(unsigned int cpu, int page_idx)
{
	return pcpu_unit_map[cpu] * pcpu_unit_pages + page_idx;
}

static unsigned long pcpu_chunk_addr(struct pcpu_chunk *chunk,
				     unsigned int cpu, int page_idx)
{
	return (unsigned long)chunk->vm->addr +
		(pcpu_page_idx(cpu, page_idx) << PAGE_SHIFT);
}

static struct page *pcpu_chunk_page(struct pcpu_chunk *chunk,
				    unsigned int cpu, int page_idx)
{
	/* must not be used on pre-mapped chunk */
	WARN_ON(chunk->immutable);

	return vmalloc_to_page((void *)pcpu_chunk_addr(chunk, cpu, page_idx));
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

static void pcpu_next_unpop(struct pcpu_chunk *chunk, int *rs, int *re, int end)
{
	*rs = find_next_zero_bit(chunk->populated, end, *rs);
	*re = find_next_bit(chunk->populated, end, *rs + 1);
}

static void pcpu_next_pop(struct pcpu_chunk *chunk, int *rs, int *re, int end)
{
	*rs = find_next_bit(chunk->populated, end, *rs);
	*re = find_next_zero_bit(chunk->populated, end, *rs + 1);
}

/*
 * (Un)populated page region iterators.  Iterate over (un)populated
 * page regions betwen @start and @end in @chunk.  @rs and @re should
 * be integer variables and will be set to start and end page index of
 * the current region.
 */
#define pcpu_for_each_unpop_region(chunk, rs, re, start, end)		    \
	for ((rs) = (start), pcpu_next_unpop((chunk), &(rs), &(re), (end)); \
	     (rs) < (re);						    \
	     (rs) = (re) + 1, pcpu_next_unpop((chunk), &(rs), &(re), (end)))

#define pcpu_for_each_pop_region(chunk, rs, re, start, end)		    \
	for ((rs) = (start), pcpu_next_pop((chunk), &(rs), &(re), (end));   \
	     (rs) < (re);						    \
	     (rs) = (re) + 1, pcpu_next_pop((chunk), &(rs), &(re), (end)))

/**
 * pcpu_mem_alloc - allocate memory
 * @size: bytes to allocate
 *
 * Allocate @size bytes.  If @size is smaller than PAGE_SIZE,
 * kzalloc() is used; otherwise, vmalloc() is used.  The returned
 * memory is always zeroed.
 *
 * CONTEXT:
 * Does GFP_KERNEL allocation.
 *
 * RETURNS:
 * Pointer to the allocated area on success, NULL on failure.
 */
static void *pcpu_mem_alloc(size_t size)
{
	if (size <= PAGE_SIZE)
		return kzalloc(size, GFP_KERNEL);
	else {
		void *ptr = vmalloc(size);
		if (ptr)
			memset(ptr, 0, size);
		return ptr;
	}
}

/**
 * pcpu_mem_free - free memory
 * @ptr: memory to free
 * @size: size of the area
 *
 * Free @ptr.  @ptr should have been allocated using pcpu_mem_alloc().
 */
static void pcpu_mem_free(void *ptr, size_t size)
{
	if (size <= PAGE_SIZE)
		kfree(ptr);
	else
		vfree(ptr);
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

	if (chunk != pcpu_reserved_chunk && oslot != nslot) {
		if (oslot < nslot)
			list_move(&chunk->list, &pcpu_slot[nslot]);
		else
			list_move_tail(&chunk->list, &pcpu_slot[nslot]);
	}
}

/**
 * pcpu_chunk_addr_search - determine chunk containing specified address
 * @addr: address for which the chunk needs to be determined.
 *
 * RETURNS:
 * The address of the found chunk.
 */
static struct pcpu_chunk *pcpu_chunk_addr_search(void *addr)
{
	void *first_start = pcpu_first_chunk->vm->addr;

	/* is it in the first chunk? */
	if (addr >= first_start && addr < first_start + pcpu_unit_size) {
		/* is it in the reserved area? */
		if (addr < first_start + pcpu_reserved_chunk_limit)
			return pcpu_reserved_chunk;
		return pcpu_first_chunk;
	}

	/*
	 * The address is relative to unit0 which might be unused and
	 * thus unmapped.  Offset the address to the unit space of the
	 * current processor before looking it up in the vmalloc
	 * space.  Note that any possible cpu id can be used here, so
	 * there's no need to worry about preemption or cpu hotplug.
	 */
	addr += pcpu_unit_map[smp_processor_id()] * pcpu_unit_size;
	return pcpu_get_page_chunk(vmalloc_to_page(addr));
}

/**
 * pcpu_extend_area_map - extend area map for allocation
 * @chunk: target chunk
 *
 * Extend area map of @chunk so that it can accomodate an allocation.
 * A single allocation can split an area into three areas, so this
 * function makes sure that @chunk->map has at least two extra slots.
 *
 * CONTEXT:
 * pcpu_alloc_mutex, pcpu_lock.  pcpu_lock is released and reacquired
 * if area map is extended.
 *
 * RETURNS:
 * 0 if noop, 1 if successfully extended, -errno on failure.
 */
static int pcpu_extend_area_map(struct pcpu_chunk *chunk)
{
	int new_alloc;
	int *new;
	size_t size;

	/* has enough? */
	if (chunk->map_alloc >= chunk->map_used + 2)
		return 0;

	spin_unlock_irq(&pcpu_lock);

	new_alloc = PCPU_DFL_MAP_ALLOC;
	while (new_alloc < chunk->map_used + 2)
		new_alloc *= 2;

	new = pcpu_mem_alloc(new_alloc * sizeof(new[0]));
	if (!new) {
		spin_lock_irq(&pcpu_lock);
		return -ENOMEM;
	}

	/*
	 * Acquire pcpu_lock and switch to new area map.  Only free
	 * could have happened inbetween, so map_used couldn't have
	 * grown.
	 */
	spin_lock_irq(&pcpu_lock);
	BUG_ON(new_alloc < chunk->map_used + 2);

	size = chunk->map_alloc * sizeof(chunk->map[0]);
	memcpy(new, chunk->map, size);

	/*
	 * map_alloc < PCPU_DFL_MAP_ALLOC indicates that the chunk is
	 * one of the first chunks and still using static map.
	 */
	if (chunk->map_alloc >= PCPU_DFL_MAP_ALLOC)
		pcpu_mem_free(chunk->map, size);

	chunk->map_alloc = new_alloc;
	chunk->map = new;
	return 0;
}

/**
 * pcpu_split_block - split a map block
 * @chunk: chunk of interest
 * @i: index of map block to split
 * @head: head size in bytes (can be 0)
 * @tail: tail size in bytes (can be 0)
 *
 * Split the @i'th map block into two or three blocks.  If @head is
 * non-zero, @head bytes block is inserted before block @i moving it
 * to @i+1 and reducing its size by @head bytes.
 *
 * If @tail is non-zero, the target block, which can be @i or @i+1
 * depending on @head, is reduced by @tail bytes and @tail byte block
 * is inserted after the target block.
 *
 * @chunk->map must have enough free slots to accomodate the split.
 *
 * CONTEXT:
 * pcpu_lock.
 */
static void pcpu_split_block(struct pcpu_chunk *chunk, int i,
			     int head, int tail)
{
	int nr_extra = !!head + !!tail;

	BUG_ON(chunk->map_alloc < chunk->map_used + nr_extra);

	/* insert new subblocks */
	memmove(&chunk->map[i + nr_extra], &chunk->map[i],
		sizeof(chunk->map[0]) * (chunk->map_used - i));
	chunk->map_used += nr_extra;

	if (head) {
		chunk->map[i + 1] = chunk->map[i] - head;
		chunk->map[i++] = head;
	}
	if (tail) {
		chunk->map[i++] -= tail;
		chunk->map[i] = tail;
	}
}

/**
 * pcpu_alloc_area - allocate area from a pcpu_chunk
 * @chunk: chunk of interest
 * @size: wanted size in bytes
 * @align: wanted align
 *
 * Try to allocate @size bytes area aligned at @align from @chunk.
 * Note that this function only allocates the offset.  It doesn't
 * populate or map the area.
 *
 * @chunk->map must have at least two free slots.
 *
 * CONTEXT:
 * pcpu_lock.
 *
 * RETURNS:
 * Allocated offset in @chunk on success, -1 if no matching area is
 * found.
 */
static int pcpu_alloc_area(struct pcpu_chunk *chunk, int size, int align)
{
	int oslot = pcpu_chunk_slot(chunk);
	int max_contig = 0;
	int i, off;

	for (i = 0, off = 0; i < chunk->map_used; off += abs(chunk->map[i++])) {
		bool is_last = i + 1 == chunk->map_used;
		int head, tail;

		/* extra for alignment requirement */
		head = ALIGN(off, align) - off;
		BUG_ON(i == 0 && head != 0);

		if (chunk->map[i] < 0)
			continue;
		if (chunk->map[i] < head + size) {
			max_contig = max(chunk->map[i], max_contig);
			continue;
		}

		/*
		 * If head is small or the previous block is free,
		 * merge'em.  Note that 'small' is defined as smaller
		 * than sizeof(int), which is very small but isn't too
		 * uncommon for percpu allocations.
		 */
		if (head && (head < sizeof(int) || chunk->map[i - 1] > 0)) {
			if (chunk->map[i - 1] > 0)
				chunk->map[i - 1] += head;
			else {
				chunk->map[i - 1] -= head;
				chunk->free_size -= head;
			}
			chunk->map[i] -= head;
			off += head;
			head = 0;
		}

		/* if tail is small, just keep it around */
		tail = chunk->map[i] - head - size;
		if (tail < sizeof(int))
			tail = 0;

		/* split if warranted */
		if (head || tail) {
			pcpu_split_block(chunk, i, head, tail);
			if (head) {
				i++;
				off += head;
				max_contig = max(chunk->map[i - 1], max_contig);
			}
			if (tail)
				max_contig = max(chunk->map[i + 1], max_contig);
		}

		/* update hint and mark allocated */
		if (is_last)
			chunk->contig_hint = max_contig; /* fully scanned */
		else
			chunk->contig_hint = max(chunk->contig_hint,
						 max_contig);

		chunk->free_size -= chunk->map[i];
		chunk->map[i] = -chunk->map[i];

		pcpu_chunk_relocate(chunk, oslot);
		return off;
	}

	chunk->contig_hint = max_contig;	/* fully scanned */
	pcpu_chunk_relocate(chunk, oslot);

	/* tell the upper layer that this chunk has no matching area */
	return -1;
}

/**
 * pcpu_free_area - free area to a pcpu_chunk
 * @chunk: chunk of interest
 * @freeme: offset of area to free
 *
 * Free area starting from @freeme to @chunk.  Note that this function
 * only modifies the allocation map.  It doesn't depopulate or unmap
 * the area.
 *
 * CONTEXT:
 * pcpu_lock.
 */
static void pcpu_free_area(struct pcpu_chunk *chunk, int freeme)
{
	int oslot = pcpu_chunk_slot(chunk);
	int i, off;

	for (i = 0, off = 0; i < chunk->map_used; off += abs(chunk->map[i++]))
		if (off == freeme)
			break;
	BUG_ON(off != freeme);
	BUG_ON(chunk->map[i] > 0);

	chunk->map[i] = -chunk->map[i];
	chunk->free_size += chunk->map[i];

	/* merge with previous? */
	if (i > 0 && chunk->map[i - 1] >= 0) {
		chunk->map[i - 1] += chunk->map[i];
		chunk->map_used--;
		memmove(&chunk->map[i], &chunk->map[i + 1],
			(chunk->map_used - i) * sizeof(chunk->map[0]));
		i--;
	}
	/* merge with next? */
	if (i + 1 < chunk->map_used && chunk->map[i + 1] >= 0) {
		chunk->map[i] += chunk->map[i + 1];
		chunk->map_used--;
		memmove(&chunk->map[i + 1], &chunk->map[i + 2],
			(chunk->map_used - (i + 1)) * sizeof(chunk->map[0]));
	}

	chunk->contig_hint = max(chunk->map[i], chunk->contig_hint);
	pcpu_chunk_relocate(chunk, oslot);
}

/**
 * pcpu_get_pages_and_bitmap - get temp pages array and bitmap
 * @chunk: chunk of interest
 * @bitmapp: output parameter for bitmap
 * @may_alloc: may allocate the array
 *
 * Returns pointer to array of pointers to struct page and bitmap,
 * both of which can be indexed with pcpu_page_idx().  The returned
 * array is cleared to zero and *@bitmapp is copied from
 * @chunk->populated.  Note that there is only one array and bitmap
 * and access exclusion is the caller's responsibility.
 *
 * CONTEXT:
 * pcpu_alloc_mutex and does GFP_KERNEL allocation if @may_alloc.
 * Otherwise, don't care.
 *
 * RETURNS:
 * Pointer to temp pages array on success, NULL on failure.
 */
static struct page **pcpu_get_pages_and_bitmap(struct pcpu_chunk *chunk,
					       unsigned long **bitmapp,
					       bool may_alloc)
{
	static struct page **pages;
	static unsigned long *bitmap;
	size_t pages_size = pcpu_nr_units * pcpu_unit_pages * sizeof(pages[0]);
	size_t bitmap_size = BITS_TO_LONGS(pcpu_unit_pages) *
			     sizeof(unsigned long);

	if (!pages || !bitmap) {
		if (may_alloc && !pages)
			pages = pcpu_mem_alloc(pages_size);
		if (may_alloc && !bitmap)
			bitmap = pcpu_mem_alloc(bitmap_size);
		if (!pages || !bitmap)
			return NULL;
	}

	memset(pages, 0, pages_size);
	bitmap_copy(bitmap, chunk->populated, pcpu_unit_pages);

	*bitmapp = bitmap;
	return pages;
}

/**
 * pcpu_free_pages - free pages which were allocated for @chunk
 * @chunk: chunk pages were allocated for
 * @pages: array of pages to be freed, indexed by pcpu_page_idx()
 * @populated: populated bitmap
 * @page_start: page index of the first page to be freed
 * @page_end: page index of the last page to be freed + 1
 *
 * Free pages [@page_start and @page_end) in @pages for all units.
 * The pages were allocated for @chunk.
 */
static void pcpu_free_pages(struct pcpu_chunk *chunk,
			    struct page **pages, unsigned long *populated,
			    int page_start, int page_end)
{
	unsigned int cpu;
	int i;

	for_each_possible_cpu(cpu) {
		for (i = page_start; i < page_end; i++) {
			struct page *page = pages[pcpu_page_idx(cpu, i)];

			if (page)
				__free_page(page);
		}
	}
}

/**
 * pcpu_alloc_pages - allocates pages for @chunk
 * @chunk: target chunk
 * @pages: array to put the allocated pages into, indexed by pcpu_page_idx()
 * @populated: populated bitmap
 * @page_start: page index of the first page to be allocated
 * @page_end: page index of the last page to be allocated + 1
 *
 * Allocate pages [@page_start,@page_end) into @pages for all units.
 * The allocation is for @chunk.  Percpu core doesn't care about the
 * content of @pages and will pass it verbatim to pcpu_map_pages().
 */
static int pcpu_alloc_pages(struct pcpu_chunk *chunk,
			    struct page **pages, unsigned long *populated,
			    int page_start, int page_end)
{
	const gfp_t gfp = GFP_KERNEL | __GFP_HIGHMEM | __GFP_COLD;
	unsigned int cpu;
	int i;

	for_each_possible_cpu(cpu) {
		for (i = page_start; i < page_end; i++) {
			struct page **pagep = &pages[pcpu_page_idx(cpu, i)];

			*pagep = alloc_pages_node(cpu_to_node(cpu), gfp, 0);
			if (!*pagep) {
				pcpu_free_pages(chunk, pages, populated,
						page_start, page_end);
				return -ENOMEM;
			}
		}
	}
	return 0;
}

/**
 * pcpu_pre_unmap_flush - flush cache prior to unmapping
 * @chunk: chunk the regions to be flushed belongs to
 * @page_start: page index of the first page to be flushed
 * @page_end: page index of the last page to be flushed + 1
 *
 * Pages in [@page_start,@page_end) of @chunk are about to be
 * unmapped.  Flush cache.  As each flushing trial can be very
 * expensive, issue flush on the whole region at once rather than
 * doing it for each cpu.  This could be an overkill but is more
 * scalable.
 */
static void pcpu_pre_unmap_flush(struct pcpu_chunk *chunk,
				 int page_start, int page_end)
{
	flush_cache_vunmap(
		pcpu_chunk_addr(chunk, pcpu_first_unit_cpu, page_start),
		pcpu_chunk_addr(chunk, pcpu_last_unit_cpu, page_end));
}

static void __pcpu_unmap_pages(unsigned long addr, int nr_pages)
{
	unmap_kernel_range_noflush(addr, nr_pages << PAGE_SHIFT);
}

/**
 * pcpu_unmap_pages - unmap pages out of a pcpu_chunk
 * @chunk: chunk of interest
 * @pages: pages array which can be used to pass information to free
 * @populated: populated bitmap
 * @page_start: page index of the first page to unmap
 * @page_end: page index of the last page to unmap + 1
 *
 * For each cpu, unmap pages [@page_start,@page_end) out of @chunk.
 * Corresponding elements in @pages were cleared by the caller and can
 * be used to carry information to pcpu_free_pages() which will be
 * called after all unmaps are finished.  The caller should call
 * proper pre/post flush functions.
 */
static void pcpu_unmap_pages(struct pcpu_chunk *chunk,
			     struct page **pages, unsigned long *populated,
			     int page_start, int page_end)
{
	unsigned int cpu;
	int i;

	for_each_possible_cpu(cpu) {
		for (i = page_start; i < page_end; i++) {
			struct page *page;

			page = pcpu_chunk_page(chunk, cpu, i);
			WARN_ON(!page);
			pages[pcpu_page_idx(cpu, i)] = page;
		}
		__pcpu_unmap_pages(pcpu_chunk_addr(chunk, cpu, page_start),
				   page_end - page_start);
	}

	for (i = page_start; i < page_end; i++)
		__clear_bit(i, populated);
}

/**
 * pcpu_post_unmap_tlb_flush - flush TLB after unmapping
 * @chunk: pcpu_chunk the regions to be flushed belong to
 * @page_start: page index of the first page to be flushed
 * @page_end: page index of the last page to be flushed + 1
 *
 * Pages [@page_start,@page_end) of @chunk have been unmapped.  Flush
 * TLB for the regions.  This can be skipped if the area is to be
 * returned to vmalloc as vmalloc will handle TLB flushing lazily.
 *
 * As with pcpu_pre_unmap_flush(), TLB flushing also is done at once
 * for the whole region.
 */
static void pcpu_post_unmap_tlb_flush(struct pcpu_chunk *chunk,
				      int page_start, int page_end)
{
	flush_tlb_kernel_range(
		pcpu_chunk_addr(chunk, pcpu_first_unit_cpu, page_start),
		pcpu_chunk_addr(chunk, pcpu_last_unit_cpu, page_end));
}

static int __pcpu_map_pages(unsigned long addr, struct page **pages,
			    int nr_pages)
{
	return map_kernel_range_noflush(addr, nr_pages << PAGE_SHIFT,
					PAGE_KERNEL, pages);
}

/**
 * pcpu_map_pages - map pages into a pcpu_chunk
 * @chunk: chunk of interest
 * @pages: pages array containing pages to be mapped
 * @populated: populated bitmap
 * @page_start: page index of the first page to map
 * @page_end: page index of the last page to map + 1
 *
 * For each cpu, map pages [@page_start,@page_end) into @chunk.  The
 * caller is responsible for calling pcpu_post_map_flush() after all
 * mappings are complete.
 *
 * This function is responsible for setting corresponding bits in
 * @chunk->populated bitmap and whatever is necessary for reverse
 * lookup (addr -> chunk).
 */
static int pcpu_map_pages(struct pcpu_chunk *chunk,
			  struct page **pages, unsigned long *populated,
			  int page_start, int page_end)
{
	unsigned int cpu, tcpu;
	int i, err;

	for_each_possible_cpu(cpu) {
		err = __pcpu_map_pages(pcpu_chunk_addr(chunk, cpu, page_start),
				       &pages[pcpu_page_idx(cpu, page_start)],
				       page_end - page_start);
		if (err < 0)
			goto err;
	}

	/* mapping successful, link chunk and mark populated */
	for (i = page_start; i < page_end; i++) {
		for_each_possible_cpu(cpu)
			pcpu_set_page_chunk(pages[pcpu_page_idx(cpu, i)],
					    chunk);
		__set_bit(i, populated);
	}

	return 0;

err:
	for_each_possible_cpu(tcpu) {
		if (tcpu == cpu)
			break;
		__pcpu_unmap_pages(pcpu_chunk_addr(chunk, tcpu, page_start),
				   page_end - page_start);
	}
	return err;
}

/**
 * pcpu_post_map_flush - flush cache after mapping
 * @chunk: pcpu_chunk the regions to be flushed belong to
 * @page_start: page index of the first page to be flushed
 * @page_end: page index of the last page to be flushed + 1
 *
 * Pages [@page_start,@page_end) of @chunk have been mapped.  Flush
 * cache.
 *
 * As with pcpu_pre_unmap_flush(), TLB flushing also is done at once
 * for the whole region.
 */
static void pcpu_post_map_flush(struct pcpu_chunk *chunk,
				int page_start, int page_end)
{
	flush_cache_vmap(
		pcpu_chunk_addr(chunk, pcpu_first_unit_cpu, page_start),
		pcpu_chunk_addr(chunk, pcpu_last_unit_cpu, page_end));
}

/**
 * pcpu_depopulate_chunk - depopulate and unmap an area of a pcpu_chunk
 * @chunk: chunk to depopulate
 * @off: offset to the area to depopulate
 * @size: size of the area to depopulate in bytes
 * @flush: whether to flush cache and tlb or not
 *
 * For each cpu, depopulate and unmap pages [@page_start,@page_end)
 * from @chunk.  If @flush is true, vcache is flushed before unmapping
 * and tlb after.
 *
 * CONTEXT:
 * pcpu_alloc_mutex.
 */
static void pcpu_depopulate_chunk(struct pcpu_chunk *chunk, int off, int size)
{
	int page_start = PFN_DOWN(off);
	int page_end = PFN_UP(off + size);
	struct page **pages;
	unsigned long *populated;
	int rs, re;

	/* quick path, check whether it's empty already */
	pcpu_for_each_unpop_region(chunk, rs, re, page_start, page_end) {
		if (rs == page_start && re == page_end)
			return;
		break;
	}

	/* immutable chunks can't be depopulated */
	WARN_ON(chunk->immutable);

	/*
	 * If control reaches here, there must have been at least one
	 * successful population attempt so the temp pages array must
	 * be available now.
	 */
	pages = pcpu_get_pages_and_bitmap(chunk, &populated, false);
	BUG_ON(!pages);

	/* unmap and free */
	pcpu_pre_unmap_flush(chunk, page_start, page_end);

	pcpu_for_each_pop_region(chunk, rs, re, page_start, page_end)
		pcpu_unmap_pages(chunk, pages, populated, rs, re);

	/* no need to flush tlb, vmalloc will handle it lazily */

	pcpu_for_each_pop_region(chunk, rs, re, page_start, page_end)
		pcpu_free_pages(chunk, pages, populated, rs, re);

	/* commit new bitmap */
	bitmap_copy(chunk->populated, populated, pcpu_unit_pages);
}

/**
 * pcpu_populate_chunk - populate and map an area of a pcpu_chunk
 * @chunk: chunk of interest
 * @off: offset to the area to populate
 * @size: size of the area to populate in bytes
 *
 * For each cpu, populate and map pages [@page_start,@page_end) into
 * @chunk.  The area is cleared on return.
 *
 * CONTEXT:
 * pcpu_alloc_mutex, does GFP_KERNEL allocation.
 */
static int pcpu_populate_chunk(struct pcpu_chunk *chunk, int off, int size)
{
	int page_start = PFN_DOWN(off);
	int page_end = PFN_UP(off + size);
	int free_end = page_start, unmap_end = page_start;
	struct page **pages;
	unsigned long *populated;
	unsigned int cpu;
	int rs, re, rc;

	/* quick path, check whether all pages are already there */
	pcpu_for_each_pop_region(chunk, rs, re, page_start, page_end) {
		if (rs == page_start && re == page_end)
			goto clear;
		break;
	}

	/* need to allocate and map pages, this chunk can't be immutable */
	WARN_ON(chunk->immutable);

	pages = pcpu_get_pages_and_bitmap(chunk, &populated, true);
	if (!pages)
		return -ENOMEM;

	/* alloc and map */
	pcpu_for_each_unpop_region(chunk, rs, re, page_start, page_end) {
		rc = pcpu_alloc_pages(chunk, pages, populated, rs, re);
		if (rc)
			goto err_free;
		free_end = re;
	}

	pcpu_for_each_unpop_region(chunk, rs, re, page_start, page_end) {
		rc = pcpu_map_pages(chunk, pages, populated, rs, re);
		if (rc)
			goto err_unmap;
		unmap_end = re;
	}
	pcpu_post_map_flush(chunk, page_start, page_end);

	/* commit new bitmap */
	bitmap_copy(chunk->populated, populated, pcpu_unit_pages);
clear:
	for_each_possible_cpu(cpu)
		memset((void *)pcpu_chunk_addr(chunk, cpu, 0) + off, 0, size);
	return 0;

err_unmap:
	pcpu_pre_unmap_flush(chunk, page_start, unmap_end);
	pcpu_for_each_unpop_region(chunk, rs, re, page_start, unmap_end)
		pcpu_unmap_pages(chunk, pages, populated, rs, re);
	pcpu_post_unmap_tlb_flush(chunk, page_start, unmap_end);
err_free:
	pcpu_for_each_unpop_region(chunk, rs, re, page_start, free_end)
		pcpu_free_pages(chunk, pages, populated, rs, re);
	return rc;
}

static void free_pcpu_chunk(struct pcpu_chunk *chunk)
{
	if (!chunk)
		return;
	if (chunk->vm)
		free_vm_area(chunk->vm);
	pcpu_mem_free(chunk->map, chunk->map_alloc * sizeof(chunk->map[0]));
	kfree(chunk);
}

static struct pcpu_chunk *alloc_pcpu_chunk(void)
{
	struct pcpu_chunk *chunk;

	chunk = kzalloc(pcpu_chunk_struct_size, GFP_KERNEL);
	if (!chunk)
		return NULL;

	chunk->map = pcpu_mem_alloc(PCPU_DFL_MAP_ALLOC * sizeof(chunk->map[0]));
	chunk->map_alloc = PCPU_DFL_MAP_ALLOC;
	chunk->map[chunk->map_used++] = pcpu_unit_size;

	chunk->vm = get_vm_area(pcpu_chunk_size, GFP_KERNEL);
	if (!chunk->vm) {
		free_pcpu_chunk(chunk);
		return NULL;
	}

	INIT_LIST_HEAD(&chunk->list);
	chunk->free_size = pcpu_unit_size;
	chunk->contig_hint = pcpu_unit_size;

	return chunk;
}

/**
 * pcpu_alloc - the percpu allocator
 * @size: size of area to allocate in bytes
 * @align: alignment of area (max PAGE_SIZE)
 * @reserved: allocate from the reserved chunk if available
 *
 * Allocate percpu area of @size bytes aligned at @align.
 *
 * CONTEXT:
 * Does GFP_KERNEL allocation.
 *
 * RETURNS:
 * Percpu pointer to the allocated area on success, NULL on failure.
 */
static void *pcpu_alloc(size_t size, size_t align, bool reserved)
{
	struct pcpu_chunk *chunk;
	int slot, off;

	if (unlikely(!size || size > PCPU_MIN_UNIT_SIZE || align > PAGE_SIZE)) {
		WARN(true, "illegal size (%zu) or align (%zu) for "
		     "percpu allocation\n", size, align);
		return NULL;
	}

	mutex_lock(&pcpu_alloc_mutex);
	spin_lock_irq(&pcpu_lock);

	/* serve reserved allocations from the reserved chunk if available */
	if (reserved && pcpu_reserved_chunk) {
		chunk = pcpu_reserved_chunk;
		if (size > chunk->contig_hint ||
		    pcpu_extend_area_map(chunk) < 0)
			goto fail_unlock;
		off = pcpu_alloc_area(chunk, size, align);
		if (off >= 0)
			goto area_found;
		goto fail_unlock;
	}

restart:
	/* search through normal chunks */
	for (slot = pcpu_size_to_slot(size); slot < pcpu_nr_slots; slot++) {
		list_for_each_entry(chunk, &pcpu_slot[slot], list) {
			if (size > chunk->contig_hint)
				continue;

			switch (pcpu_extend_area_map(chunk)) {
			case 0:
				break;
			case 1:
				goto restart;	/* pcpu_lock dropped, restart */
			default:
				goto fail_unlock;
			}

			off = pcpu_alloc_area(chunk, size, align);
			if (off >= 0)
				goto area_found;
		}
	}

	/* hmmm... no space left, create a new chunk */
	spin_unlock_irq(&pcpu_lock);

	chunk = alloc_pcpu_chunk();
	if (!chunk)
		goto fail_unlock_mutex;

	spin_lock_irq(&pcpu_lock);
	pcpu_chunk_relocate(chunk, -1);
	goto restart;

area_found:
	spin_unlock_irq(&pcpu_lock);

	/* populate, map and clear the area */
	if (pcpu_populate_chunk(chunk, off, size)) {
		spin_lock_irq(&pcpu_lock);
		pcpu_free_area(chunk, off);
		goto fail_unlock;
	}

	mutex_unlock(&pcpu_alloc_mutex);

	/* return address relative to unit0 */
	return __addr_to_pcpu_ptr(chunk->vm->addr + off);

fail_unlock:
	spin_unlock_irq(&pcpu_lock);
fail_unlock_mutex:
	mutex_unlock(&pcpu_alloc_mutex);
	return NULL;
}

/**
 * __alloc_percpu - allocate dynamic percpu area
 * @size: size of area to allocate in bytes
 * @align: alignment of area (max PAGE_SIZE)
 *
 * Allocate percpu area of @size bytes aligned at @align.  Might
 * sleep.  Might trigger writeouts.
 *
 * CONTEXT:
 * Does GFP_KERNEL allocation.
 *
 * RETURNS:
 * Percpu pointer to the allocated area on success, NULL on failure.
 */
void *__alloc_percpu(size_t size, size_t align)
{
	return pcpu_alloc(size, align, false);
}
EXPORT_SYMBOL_GPL(__alloc_percpu);

/**
 * __alloc_reserved_percpu - allocate reserved percpu area
 * @size: size of area to allocate in bytes
 * @align: alignment of area (max PAGE_SIZE)
 *
 * Allocate percpu area of @size bytes aligned at @align from reserved
 * percpu area if arch has set it up; otherwise, allocation is served
 * from the same dynamic area.  Might sleep.  Might trigger writeouts.
 *
 * CONTEXT:
 * Does GFP_KERNEL allocation.
 *
 * RETURNS:
 * Percpu pointer to the allocated area on success, NULL on failure.
 */
void *__alloc_reserved_percpu(size_t size, size_t align)
{
	return pcpu_alloc(size, align, true);
}

/**
 * pcpu_reclaim - reclaim fully free chunks, workqueue function
 * @work: unused
 *
 * Reclaim all fully free chunks except for the first one.
 *
 * CONTEXT:
 * workqueue context.
 */
static void pcpu_reclaim(struct work_struct *work)
{
	LIST_HEAD(todo);
	struct list_head *head = &pcpu_slot[pcpu_nr_slots - 1];
	struct pcpu_chunk *chunk, *next;

	mutex_lock(&pcpu_alloc_mutex);
	spin_lock_irq(&pcpu_lock);

	list_for_each_entry_safe(chunk, next, head, list) {
		WARN_ON(chunk->immutable);

		/* spare the first one */
		if (chunk == list_first_entry(head, struct pcpu_chunk, list))
			continue;

		list_move(&chunk->list, &todo);
	}

	spin_unlock_irq(&pcpu_lock);
	mutex_unlock(&pcpu_alloc_mutex);

	list_for_each_entry_safe(chunk, next, &todo, list) {
		pcpu_depopulate_chunk(chunk, 0, pcpu_unit_size);
		free_pcpu_chunk(chunk);
	}
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
void free_percpu(void *ptr)
{
	void *addr = __pcpu_ptr_to_addr(ptr);
	struct pcpu_chunk *chunk;
	unsigned long flags;
	int off;

	if (!ptr)
		return;

	spin_lock_irqsave(&pcpu_lock, flags);

	chunk = pcpu_chunk_addr_search(addr);
	off = addr - chunk->vm->addr;

	pcpu_free_area(chunk, off);

	/* if there are more than one fully free chunks, wake up grim reaper */
	if (chunk->free_size == pcpu_unit_size) {
		struct pcpu_chunk *pos;

		list_for_each_entry(pos, &pcpu_slot[pcpu_nr_slots - 1], list)
			if (pos != chunk) {
				schedule_work(&pcpu_reclaim_work);
				break;
			}
	}

	spin_unlock_irqrestore(&pcpu_lock, flags);
}
EXPORT_SYMBOL_GPL(free_percpu);

/**
 * pcpu_setup_first_chunk - initialize the first percpu chunk
 * @static_size: the size of static percpu area in bytes
 * @reserved_size: the size of reserved percpu area in bytes, 0 for none
 * @dyn_size: free size for dynamic allocation in bytes, -1 for auto
 * @unit_size: unit size in bytes, must be multiple of PAGE_SIZE
 * @base_addr: mapped address
 * @unit_map: cpu -> unit map, NULL for sequential mapping
 *
 * Initialize the first percpu chunk which contains the kernel static
 * perpcu area.  This function is to be called from arch percpu area
 * setup path.
 *
 * @reserved_size, if non-zero, specifies the amount of bytes to
 * reserve after the static area in the first chunk.  This reserves
 * the first chunk such that it's available only through reserved
 * percpu allocation.  This is primarily used to serve module percpu
 * static areas on architectures where the addressing model has
 * limited offset range for symbol relocations to guarantee module
 * percpu symbols fall inside the relocatable range.
 *
 * @dyn_size, if non-negative, determines the number of bytes
 * available for dynamic allocation in the first chunk.  Specifying
 * non-negative value makes percpu leave alone the area beyond
 * @static_size + @reserved_size + @dyn_size.
 *
 * @unit_size specifies unit size and must be aligned to PAGE_SIZE and
 * equal to or larger than @static_size + @reserved_size + if
 * non-negative, @dyn_size.
 *
 * The caller should have mapped the first chunk at @base_addr and
 * copied static data to each unit.
 *
 * If the first chunk ends up with both reserved and dynamic areas, it
 * is served by two chunks - one to serve the core static and reserved
 * areas and the other for the dynamic area.  They share the same vm
 * and page map but uses different area allocation map to stay away
 * from each other.  The latter chunk is circulated in the chunk slots
 * and available for dynamic allocation like any other chunks.
 *
 * RETURNS:
 * The determined pcpu_unit_size which can be used to initialize
 * percpu access.
 */
size_t __init pcpu_setup_first_chunk(size_t static_size, size_t reserved_size,
				     ssize_t dyn_size, size_t unit_size,
				     void *base_addr, const int *unit_map)
{
	static struct vm_struct first_vm;
	static int smap[2], dmap[2];
	size_t size_sum = static_size + reserved_size +
			  (dyn_size >= 0 ? dyn_size : 0);
	struct pcpu_chunk *schunk, *dchunk = NULL;
	unsigned int cpu, tcpu;
	int i;

	/* sanity checks */
	BUILD_BUG_ON(ARRAY_SIZE(smap) >= PCPU_DFL_MAP_ALLOC ||
		     ARRAY_SIZE(dmap) >= PCPU_DFL_MAP_ALLOC);
	BUG_ON(!static_size);
	BUG_ON(!base_addr);
	BUG_ON(unit_size < size_sum);
	BUG_ON(unit_size & ~PAGE_MASK);
	BUG_ON(unit_size < PCPU_MIN_UNIT_SIZE);

	/* determine number of units and verify and initialize pcpu_unit_map */
	if (unit_map) {
		int first_unit = INT_MAX, last_unit = INT_MIN;

		for_each_possible_cpu(cpu) {
			int unit = unit_map[cpu];

			BUG_ON(unit < 0);
			for_each_possible_cpu(tcpu) {
				if (tcpu == cpu)
					break;
				/* the mapping should be one-to-one */
				BUG_ON(unit_map[tcpu] == unit);
			}

			if (unit < first_unit) {
				pcpu_first_unit_cpu = cpu;
				first_unit = unit;
			}
			if (unit > last_unit) {
				pcpu_last_unit_cpu = cpu;
				last_unit = unit;
			}
		}
		pcpu_nr_units = last_unit + 1;
		pcpu_unit_map = unit_map;
	} else {
		int *identity_map;

		/* #units == #cpus, identity mapped */
		identity_map = alloc_bootmem(num_possible_cpus() *
					     sizeof(identity_map[0]));

		for_each_possible_cpu(cpu)
			identity_map[cpu] = cpu;

		pcpu_first_unit_cpu = 0;
		pcpu_last_unit_cpu = pcpu_nr_units - 1;
		pcpu_nr_units = num_possible_cpus();
		pcpu_unit_map = identity_map;
	}

	/* determine basic parameters */
	pcpu_unit_pages = unit_size >> PAGE_SHIFT;
	pcpu_unit_size = pcpu_unit_pages << PAGE_SHIFT;
	pcpu_chunk_size = pcpu_nr_units * pcpu_unit_size;
	pcpu_chunk_struct_size = sizeof(struct pcpu_chunk) +
		BITS_TO_LONGS(pcpu_unit_pages) * sizeof(unsigned long);

	if (dyn_size < 0)
		dyn_size = pcpu_unit_size - static_size - reserved_size;

	first_vm.flags = VM_ALLOC;
	first_vm.size = pcpu_chunk_size;
	first_vm.addr = base_addr;

	/*
	 * Allocate chunk slots.  The additional last slot is for
	 * empty chunks.
	 */
	pcpu_nr_slots = __pcpu_size_to_slot(pcpu_unit_size) + 2;
	pcpu_slot = alloc_bootmem(pcpu_nr_slots * sizeof(pcpu_slot[0]));
	for (i = 0; i < pcpu_nr_slots; i++)
		INIT_LIST_HEAD(&pcpu_slot[i]);

	/*
	 * Initialize static chunk.  If reserved_size is zero, the
	 * static chunk covers static area + dynamic allocation area
	 * in the first chunk.  If reserved_size is not zero, it
	 * covers static area + reserved area (mostly used for module
	 * static percpu allocation).
	 */
	schunk = alloc_bootmem(pcpu_chunk_struct_size);
	INIT_LIST_HEAD(&schunk->list);
	schunk->vm = &first_vm;
	schunk->map = smap;
	schunk->map_alloc = ARRAY_SIZE(smap);
	schunk->immutable = true;
	bitmap_fill(schunk->populated, pcpu_unit_pages);

	if (reserved_size) {
		schunk->free_size = reserved_size;
		pcpu_reserved_chunk = schunk;
		pcpu_reserved_chunk_limit = static_size + reserved_size;
	} else {
		schunk->free_size = dyn_size;
		dyn_size = 0;			/* dynamic area covered */
	}
	schunk->contig_hint = schunk->free_size;

	schunk->map[schunk->map_used++] = -static_size;
	if (schunk->free_size)
		schunk->map[schunk->map_used++] = schunk->free_size;

	/* init dynamic chunk if necessary */
	if (dyn_size) {
		dchunk = alloc_bootmem(pcpu_chunk_struct_size);
		INIT_LIST_HEAD(&dchunk->list);
		dchunk->vm = &first_vm;
		dchunk->map = dmap;
		dchunk->map_alloc = ARRAY_SIZE(dmap);
		dchunk->immutable = true;
		bitmap_fill(dchunk->populated, pcpu_unit_pages);

		dchunk->contig_hint = dchunk->free_size = dyn_size;
		dchunk->map[dchunk->map_used++] = -pcpu_reserved_chunk_limit;
		dchunk->map[dchunk->map_used++] = dchunk->free_size;
	}

	/* link the first chunk in */
	pcpu_first_chunk = dchunk ?: schunk;
	pcpu_chunk_relocate(pcpu_first_chunk, -1);

	/* we're done */
	pcpu_base_addr = schunk->vm->addr;
	return pcpu_unit_size;
}

static size_t pcpu_calc_fc_sizes(size_t static_size, size_t reserved_size,
				 ssize_t *dyn_sizep)
{
	size_t size_sum;

	size_sum = PFN_ALIGN(static_size + reserved_size +
			     (*dyn_sizep >= 0 ? *dyn_sizep : 0));
	if (*dyn_sizep != 0)
		*dyn_sizep = size_sum - static_size - reserved_size;

	return size_sum;
}

/**
 * pcpu_embed_first_chunk - embed the first percpu chunk into bootmem
 * @static_size: the size of static percpu area in bytes
 * @reserved_size: the size of reserved percpu area in bytes
 * @dyn_size: free size for dynamic allocation in bytes, -1 for auto
 *
 * This is a helper to ease setting up embedded first percpu chunk and
 * can be called where pcpu_setup_first_chunk() is expected.
 *
 * If this function is used to setup the first chunk, it is allocated
 * as a contiguous area using bootmem allocator and used as-is without
 * being mapped into vmalloc area.  This enables the first chunk to
 * piggy back on the linear physical mapping which often uses larger
 * page size.
 *
 * When @dyn_size is positive, dynamic area might be larger than
 * specified to fill page alignment.  When @dyn_size is auto,
 * @dyn_size is just big enough to fill page alignment after static
 * and reserved areas.
 *
 * If the needed size is smaller than the minimum or specified unit
 * size, the leftover is returned to the bootmem allocator.
 *
 * RETURNS:
 * The determined pcpu_unit_size which can be used to initialize
 * percpu access on success, -errno on failure.
 */
ssize_t __init pcpu_embed_first_chunk(size_t static_size, size_t reserved_size,
				      ssize_t dyn_size)
{
	size_t size_sum, unit_size, chunk_size;
	void *base;
	unsigned int cpu;

	/* determine parameters and allocate */
	size_sum = pcpu_calc_fc_sizes(static_size, reserved_size, &dyn_size);

	unit_size = max_t(size_t, size_sum, PCPU_MIN_UNIT_SIZE);
	chunk_size = unit_size * num_possible_cpus();

	base = __alloc_bootmem_nopanic(chunk_size, PAGE_SIZE,
				       __pa(MAX_DMA_ADDRESS));
	if (!base) {
		pr_warning("PERCPU: failed to allocate %zu bytes for "
			   "embedding\n", chunk_size);
		return -ENOMEM;
	}

	/* return the leftover and copy */
	for_each_possible_cpu(cpu) {
		void *ptr = base + cpu * unit_size;

		free_bootmem(__pa(ptr + size_sum), unit_size - size_sum);
		memcpy(ptr, __per_cpu_load, static_size);
	}

	/* we're ready, commit */
	pr_info("PERCPU: Embedded %zu pages at %p, static data %zu bytes\n",
		size_sum >> PAGE_SHIFT, base, static_size);

	return pcpu_setup_first_chunk(static_size, reserved_size, dyn_size,
				      unit_size, base, NULL);
}

/**
 * pcpu_4k_first_chunk - map the first chunk using PAGE_SIZE pages
 * @static_size: the size of static percpu area in bytes
 * @reserved_size: the size of reserved percpu area in bytes
 * @alloc_fn: function to allocate percpu page, always called with PAGE_SIZE
 * @free_fn: funtion to free percpu page, always called with PAGE_SIZE
 * @populate_pte_fn: function to populate pte
 *
 * This is a helper to ease setting up embedded first percpu chunk and
 * can be called where pcpu_setup_first_chunk() is expected.
 *
 * This is the basic allocator.  Static percpu area is allocated
 * page-by-page into vmalloc area.
 *
 * RETURNS:
 * The determined pcpu_unit_size which can be used to initialize
 * percpu access on success, -errno on failure.
 */
ssize_t __init pcpu_4k_first_chunk(size_t static_size, size_t reserved_size,
				   pcpu_fc_alloc_fn_t alloc_fn,
				   pcpu_fc_free_fn_t free_fn,
				   pcpu_fc_populate_pte_fn_t populate_pte_fn)
{
	static struct vm_struct vm;
	int unit_pages;
	size_t pages_size;
	struct page **pages;
	unsigned int cpu;
	int i, j;
	ssize_t ret;

	unit_pages = PFN_UP(max_t(size_t, static_size + reserved_size,
				  PCPU_MIN_UNIT_SIZE));

	/* unaligned allocations can't be freed, round up to page size */
	pages_size = PFN_ALIGN(unit_pages * num_possible_cpus() *
			       sizeof(pages[0]));
	pages = alloc_bootmem(pages_size);

	/* allocate pages */
	j = 0;
	for_each_possible_cpu(cpu)
		for (i = 0; i < unit_pages; i++) {
			void *ptr;

			ptr = alloc_fn(cpu, PAGE_SIZE);
			if (!ptr) {
				pr_warning("PERCPU: failed to allocate "
					   "4k page for cpu%u\n", cpu);
				goto enomem;
			}
			pages[j++] = virt_to_page(ptr);
		}

	/* allocate vm area, map the pages and copy static data */
	vm.flags = VM_ALLOC;
	vm.size = num_possible_cpus() * unit_pages << PAGE_SHIFT;
	vm_area_register_early(&vm, PAGE_SIZE);

	for_each_possible_cpu(cpu) {
		unsigned long unit_addr = (unsigned long)vm.addr +
			(cpu * unit_pages << PAGE_SHIFT);

		for (i = 0; i < unit_pages; i++)
			populate_pte_fn(unit_addr + (i << PAGE_SHIFT));

		/* pte already populated, the following shouldn't fail */
		ret = __pcpu_map_pages(unit_addr, &pages[cpu * unit_pages],
				       unit_pages);
		if (ret < 0)
			panic("failed to map percpu area, err=%zd\n", ret);

		/*
		 * FIXME: Archs with virtual cache should flush local
		 * cache for the linear mapping here - something
		 * equivalent to flush_cache_vmap() on the local cpu.
		 * flush_cache_vmap() can't be used as most supporting
		 * data structures are not set up yet.
		 */

		/* copy static data */
		memcpy((void *)unit_addr, __per_cpu_load, static_size);
	}

	/* we're ready, commit */
	pr_info("PERCPU: %d 4k pages per cpu, static data %zu bytes\n",
		unit_pages, static_size);

	ret = pcpu_setup_first_chunk(static_size, reserved_size, -1,
				     unit_pages << PAGE_SHIFT, vm.addr, NULL);
	goto out_free_ar;

enomem:
	while (--j >= 0)
		free_fn(page_address(pages[j]), PAGE_SIZE);
	ret = -ENOMEM;
out_free_ar:
	free_bootmem(__pa(pages), pages_size);
	return ret;
}

/*
 * Large page remapping first chunk setup helper
 */
#ifdef CONFIG_NEED_MULTIPLE_NODES
struct pcpul_ent {
	unsigned int	cpu;
	void		*ptr;
};

static size_t pcpul_size;
static size_t pcpul_unit_size;
static struct pcpul_ent *pcpul_map;
static struct vm_struct pcpul_vm;

/**
 * pcpu_lpage_first_chunk - remap the first percpu chunk using large page
 * @static_size: the size of static percpu area in bytes
 * @reserved_size: the size of reserved percpu area in bytes
 * @dyn_size: free size for dynamic allocation in bytes, -1 for auto
 * @lpage_size: the size of a large page
 * @alloc_fn: function to allocate percpu lpage, always called with lpage_size
 * @free_fn: function to free percpu memory, @size <= lpage_size
 * @map_fn: function to map percpu lpage, always called with lpage_size
 *
 * This allocator uses large page as unit.  A large page is allocated
 * for each cpu and each is remapped into vmalloc area using large
 * page mapping.  As large page can be quite large, only part of it is
 * used for the first chunk.  Unused part is returned to the bootmem
 * allocator.
 *
 * So, the large pages are mapped twice - once to the physical mapping
 * and to the vmalloc area for the first percpu chunk.  The double
 * mapping does add one more large TLB entry pressure but still is
 * much better than only using 4k mappings while still being NUMA
 * friendly.
 *
 * RETURNS:
 * The determined pcpu_unit_size which can be used to initialize
 * percpu access on success, -errno on failure.
 */
ssize_t __init pcpu_lpage_first_chunk(size_t static_size, size_t reserved_size,
				      ssize_t dyn_size, size_t lpage_size,
				      pcpu_fc_alloc_fn_t alloc_fn,
				      pcpu_fc_free_fn_t free_fn,
				      pcpu_fc_map_fn_t map_fn)
{
	size_t size_sum;
	size_t map_size;
	unsigned int cpu;
	int i, j;
	ssize_t ret;

	/*
	 * Currently supports only single page.  Supporting multiple
	 * pages won't be too difficult if it ever becomes necessary.
	 */
	size_sum = pcpu_calc_fc_sizes(static_size, reserved_size, &dyn_size);

	pcpul_unit_size = lpage_size;
	pcpul_size = max_t(size_t, size_sum, PCPU_MIN_UNIT_SIZE);
	if (pcpul_size > pcpul_unit_size) {
		pr_warning("PERCPU: static data is larger than large page, "
			   "can't use large page\n");
		return -EINVAL;
	}

	/* allocate pointer array and alloc large pages */
	map_size = PFN_ALIGN(num_possible_cpus() * sizeof(pcpul_map[0]));
	pcpul_map = alloc_bootmem(map_size);

	for_each_possible_cpu(cpu) {
		void *ptr;

		ptr = alloc_fn(cpu, lpage_size);
		if (!ptr) {
			pr_warning("PERCPU: failed to allocate large page "
				   "for cpu%u\n", cpu);
			goto enomem;
		}

		/*
		 * Only use pcpul_size bytes and give back the rest.
		 *
		 * Ingo: The lpage_size up-rounding bootmem is needed
		 * to make sure the partial lpage is still fully RAM -
		 * it's not well-specified to have a incompatible area
		 * (unmapped RAM, device memory, etc.) in that hole.
		 */
		free_fn(ptr + pcpul_size, lpage_size - pcpul_size);

		pcpul_map[cpu].cpu = cpu;
		pcpul_map[cpu].ptr = ptr;

		memcpy(ptr, __per_cpu_load, static_size);
	}

	/* allocate address and map */
	pcpul_vm.flags = VM_ALLOC;
	pcpul_vm.size = num_possible_cpus() * pcpul_unit_size;
	vm_area_register_early(&pcpul_vm, pcpul_unit_size);

	for_each_possible_cpu(cpu)
		map_fn(pcpul_map[cpu].ptr, pcpul_unit_size,
		       pcpul_vm.addr + cpu * pcpul_unit_size);

	/* we're ready, commit */
	pr_info("PERCPU: Remapped at %p with large pages, static data "
		"%zu bytes\n", pcpul_vm.addr, static_size);

	ret = pcpu_setup_first_chunk(static_size, reserved_size, dyn_size,
				     pcpul_unit_size, pcpul_vm.addr, NULL);

	/* sort pcpul_map array for pcpu_lpage_remapped() */
	for (i = 0; i < num_possible_cpus() - 1; i++)
		for (j = i + 1; j < num_possible_cpus(); j++)
			if (pcpul_map[i].ptr > pcpul_map[j].ptr) {
				struct pcpul_ent tmp = pcpul_map[i];
				pcpul_map[i] = pcpul_map[j];
				pcpul_map[j] = tmp;
			}

	return ret;

enomem:
	for_each_possible_cpu(cpu)
		if (pcpul_map[cpu].ptr)
			free_fn(pcpul_map[cpu].ptr, pcpul_size);
	free_bootmem(__pa(pcpul_map), map_size);
	return -ENOMEM;
}

/**
 * pcpu_lpage_remapped - determine whether a kaddr is in pcpul recycled area
 * @kaddr: the kernel address in question
 *
 * Determine whether @kaddr falls in the pcpul recycled area.  This is
 * used by pageattr to detect VM aliases and break up the pcpu large
 * page mapping such that the same physical page is not mapped under
 * different attributes.
 *
 * The recycled area is always at the tail of a partially used large
 * page.
 *
 * RETURNS:
 * Address of corresponding remapped pcpu address if match is found;
 * otherwise, NULL.
 */
void *pcpu_lpage_remapped(void *kaddr)
{
	unsigned long unit_mask = pcpul_unit_size - 1;
	void *lpage_addr = (void *)((unsigned long)kaddr & ~unit_mask);
	unsigned long offset = (unsigned long)kaddr & unit_mask;
	int left = 0, right = num_possible_cpus() - 1;
	int pos;

	/* pcpul in use at all? */
	if (!pcpul_map)
		return NULL;

	/* okay, perform binary search */
	while (left <= right) {
		pos = (left + right) / 2;

		if (pcpul_map[pos].ptr < lpage_addr)
			left = pos + 1;
		else if (pcpul_map[pos].ptr > lpage_addr)
			right = pos - 1;
		else {
			/* it shouldn't be in the area for the first chunk */
			WARN_ON(offset < pcpul_size);

			return pcpul_vm.addr +
				pcpul_map[pos].cpu * pcpul_unit_size + offset;
		}
	}

	return NULL;
}
#endif

/*
 * Generic percpu area setup.
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
#ifndef CONFIG_HAVE_SETUP_PER_CPU_AREA
unsigned long __per_cpu_offset[NR_CPUS] __read_mostly;
EXPORT_SYMBOL(__per_cpu_offset);

void __init setup_per_cpu_areas(void)
{
	size_t static_size = __per_cpu_end - __per_cpu_start;
	ssize_t unit_size;
	unsigned long delta;
	unsigned int cpu;

	/*
	 * Always reserve area for module percpu variables.  That's
	 * what the legacy allocator did.
	 */
	unit_size = pcpu_embed_first_chunk(static_size, PERCPU_MODULE_RESERVE,
					   PERCPU_DYNAMIC_RESERVE);
	if (unit_size < 0)
		panic("Failed to initialized percpu areas.");

	delta = (unsigned long)pcpu_base_addr - (unsigned long)__per_cpu_start;
	for_each_possible_cpu(cpu)
		__per_cpu_offset[cpu] = delta + cpu * unit_size;
}
#endif /* CONFIG_HAVE_SETUP_PER_CPU_AREA */
