/*
 * linux/kernel/power/snapshot.c
 *
 * This file provides system snapshot/restore functionality for swsusp.
 *
 * Copyright (C) 1998-2005 Pavel Machek <pavel@suse.cz>
 * Copyright (C) 2006 Rafael J. Wysocki <rjw@sisk.pl>
 *
 * This file is released under the GPLv2.
 *
 */

#include <linux/version.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/suspend.h>
#include <linux/delay.h>
#include <linux/bitops.h>
#include <linux/spinlock.h>
#include <linux/kernel.h>
#include <linux/pm.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/bootmem.h>
#include <linux/syscalls.h>
#include <linux/console.h>
#include <linux/highmem.h>

#include <asm/uaccess.h>
#include <asm/mmu_context.h>
#include <asm/pgtable.h>
#include <asm/tlbflush.h>
#include <asm/io.h>

#include "power.h"

static int swsusp_page_is_free(struct page *);
static void swsusp_set_page_forbidden(struct page *);
static void swsusp_unset_page_forbidden(struct page *);

/* List of PBEs needed for restoring the pages that were allocated before
 * the suspend and included in the suspend image, but have also been
 * allocated by the "resume" kernel, so their contents cannot be written
 * directly to their "original" page frames.
 */
struct pbe *restore_pblist;

/* Pointer to an auxiliary buffer (1 page) */
static void *buffer;

/**
 *	@safe_needed - on resume, for storing the PBE list and the image,
 *	we can only use memory pages that do not conflict with the pages
 *	used before suspend.  The unsafe pages have PageNosaveFree set
 *	and we count them using unsafe_pages.
 *
 *	Each allocated image page is marked as PageNosave and PageNosaveFree
 *	so that swsusp_free() can release it.
 */

#define PG_ANY		0
#define PG_SAFE		1
#define PG_UNSAFE_CLEAR	1
#define PG_UNSAFE_KEEP	0

static unsigned int allocated_unsafe_pages;

static void *get_image_page(gfp_t gfp_mask, int safe_needed)
{
	void *res;

	res = (void *)get_zeroed_page(gfp_mask);
	if (safe_needed)
		while (res && swsusp_page_is_free(virt_to_page(res))) {
			/* The page is unsafe, mark it for swsusp_free() */
			swsusp_set_page_forbidden(virt_to_page(res));
			allocated_unsafe_pages++;
			res = (void *)get_zeroed_page(gfp_mask);
		}
	if (res) {
		swsusp_set_page_forbidden(virt_to_page(res));
		swsusp_set_page_free(virt_to_page(res));
	}
	return res;
}

unsigned long get_safe_page(gfp_t gfp_mask)
{
	return (unsigned long)get_image_page(gfp_mask, PG_SAFE);
}

static struct page *alloc_image_page(gfp_t gfp_mask)
{
	struct page *page;

	page = alloc_page(gfp_mask);
	if (page) {
		swsusp_set_page_forbidden(page);
		swsusp_set_page_free(page);
	}
	return page;
}

/**
 *	free_image_page - free page represented by @addr, allocated with
 *	get_image_page (page flags set by it must be cleared)
 */

static inline void free_image_page(void *addr, int clear_nosave_free)
{
	struct page *page;

	BUG_ON(!virt_addr_valid(addr));

	page = virt_to_page(addr);

	swsusp_unset_page_forbidden(page);
	if (clear_nosave_free)
		swsusp_unset_page_free(page);

	__free_page(page);
}

/* struct linked_page is used to build chains of pages */

#define LINKED_PAGE_DATA_SIZE	(PAGE_SIZE - sizeof(void *))

struct linked_page {
	struct linked_page *next;
	char data[LINKED_PAGE_DATA_SIZE];
} __attribute__((packed));

static inline void
free_list_of_pages(struct linked_page *list, int clear_page_nosave)
{
	while (list) {
		struct linked_page *lp = list->next;

		free_image_page(list, clear_page_nosave);
		list = lp;
	}
}

/**
  *	struct chain_allocator is used for allocating small objects out of
  *	a linked list of pages called 'the chain'.
  *
  *	The chain grows each time when there is no room for a new object in
  *	the current page.  The allocated objects cannot be freed individually.
  *	It is only possible to free them all at once, by freeing the entire
  *	chain.
  *
  *	NOTE: The chain allocator may be inefficient if the allocated objects
  *	are not much smaller than PAGE_SIZE.
  */

struct chain_allocator {
	struct linked_page *chain;	/* the chain */
	unsigned int used_space;	/* total size of objects allocated out
					 * of the current page
					 */
	gfp_t gfp_mask;		/* mask for allocating pages */
	int safe_needed;	/* if set, only "safe" pages are allocated */
};

static void
chain_init(struct chain_allocator *ca, gfp_t gfp_mask, int safe_needed)
{
	ca->chain = NULL;
	ca->used_space = LINKED_PAGE_DATA_SIZE;
	ca->gfp_mask = gfp_mask;
	ca->safe_needed = safe_needed;
}

static void *chain_alloc(struct chain_allocator *ca, unsigned int size)
{
	void *ret;

	if (LINKED_PAGE_DATA_SIZE - ca->used_space < size) {
		struct linked_page *lp;

		lp = get_image_page(ca->gfp_mask, ca->safe_needed);
		if (!lp)
			return NULL;

		lp->next = ca->chain;
		ca->chain = lp;
		ca->used_space = 0;
	}
	ret = ca->chain->data + ca->used_space;
	ca->used_space += size;
	return ret;
}

static void chain_free(struct chain_allocator *ca, int clear_page_nosave)
{
	free_list_of_pages(ca->chain, clear_page_nosave);
	memset(ca, 0, sizeof(struct chain_allocator));
}

/**
 *	Data types related to memory bitmaps.
 *
 *	Memory bitmap is a structure consiting of many linked lists of
 *	objects.  The main list's elements are of type struct zone_bitmap
 *	and each of them corresonds to one zone.  For each zone bitmap
 *	object there is a list of objects of type struct bm_block that
 *	represent each blocks of bit chunks in which information is
 *	stored.
 *
 *	struct memory_bitmap contains a pointer to the main list of zone
 *	bitmap objects, a struct bm_position used for browsing the bitmap,
 *	and a pointer to the list of pages used for allocating all of the
 *	zone bitmap objects and bitmap block objects.
 *
 *	NOTE: It has to be possible to lay out the bitmap in memory
 *	using only allocations of order 0.  Additionally, the bitmap is
 *	designed to work with arbitrary number of zones (this is over the
 *	top for now, but let's avoid making unnecessary assumptions ;-).
 *
 *	struct zone_bitmap contains a pointer to a list of bitmap block
 *	objects and a pointer to the bitmap block object that has been
 *	most recently used for setting bits.  Additionally, it contains the
 *	pfns that correspond to the start and end of the represented zone.
 *
 *	struct bm_block contains a pointer to the memory page in which
 *	information is stored (in the form of a block of bit chunks
 *	of type unsigned long each).  It also contains the pfns that
 *	correspond to the start and end of the represented memory area and
 *	the number of bit chunks in the block.
 */

#define BM_END_OF_MAP	(~0UL)

#define BM_CHUNKS_PER_BLOCK	(PAGE_SIZE / sizeof(long))
#define BM_BITS_PER_CHUNK	(sizeof(long) << 3)
#define BM_BITS_PER_BLOCK	(PAGE_SIZE << 3)

struct bm_block {
	struct bm_block *next;		/* next element of the list */
	unsigned long start_pfn;	/* pfn represented by the first bit */
	unsigned long end_pfn;	/* pfn represented by the last bit plus 1 */
	unsigned int size;	/* number of bit chunks */
	unsigned long *data;	/* chunks of bits representing pages */
};

struct zone_bitmap {
	struct zone_bitmap *next;	/* next element of the list */
	unsigned long start_pfn;	/* minimal pfn in this zone */
	unsigned long end_pfn;		/* maximal pfn in this zone plus 1 */
	struct bm_block *bm_blocks;	/* list of bitmap blocks */
	struct bm_block *cur_block;	/* recently used bitmap block */
};

/* strcut bm_position is used for browsing memory bitmaps */

struct bm_position {
	struct zone_bitmap *zone_bm;
	struct bm_block *block;
	int chunk;
	int bit;
};

struct memory_bitmap {
	struct zone_bitmap *zone_bm_list;	/* list of zone bitmaps */
	struct linked_page *p_list;	/* list of pages used to store zone
					 * bitmap objects and bitmap block
					 * objects
					 */
	struct bm_position cur;	/* most recently used bit position */
};

/* Functions that operate on memory bitmaps */

static inline void memory_bm_reset_chunk(struct memory_bitmap *bm)
{
	bm->cur.chunk = 0;
	bm->cur.bit = -1;
}

static void memory_bm_position_reset(struct memory_bitmap *bm)
{
	struct zone_bitmap *zone_bm;

	zone_bm = bm->zone_bm_list;
	bm->cur.zone_bm = zone_bm;
	bm->cur.block = zone_bm->bm_blocks;
	memory_bm_reset_chunk(bm);
}

static void memory_bm_free(struct memory_bitmap *bm, int clear_nosave_free);

/**
 *	create_bm_block_list - create a list of block bitmap objects
 */

static inline struct bm_block *
create_bm_block_list(unsigned int nr_blocks, struct chain_allocator *ca)
{
	struct bm_block *bblist = NULL;

	while (nr_blocks-- > 0) {
		struct bm_block *bb;

		bb = chain_alloc(ca, sizeof(struct bm_block));
		if (!bb)
			return NULL;

		bb->next = bblist;
		bblist = bb;
	}
	return bblist;
}

/**
 *	create_zone_bm_list - create a list of zone bitmap objects
 */

static inline struct zone_bitmap *
create_zone_bm_list(unsigned int nr_zones, struct chain_allocator *ca)
{
	struct zone_bitmap *zbmlist = NULL;

	while (nr_zones-- > 0) {
		struct zone_bitmap *zbm;

		zbm = chain_alloc(ca, sizeof(struct zone_bitmap));
		if (!zbm)
			return NULL;

		zbm->next = zbmlist;
		zbmlist = zbm;
	}
	return zbmlist;
}

/**
  *	memory_bm_create - allocate memory for a memory bitmap
  */

static int
memory_bm_create(struct memory_bitmap *bm, gfp_t gfp_mask, int safe_needed)
{
	struct chain_allocator ca;
	struct zone *zone;
	struct zone_bitmap *zone_bm;
	struct bm_block *bb;
	unsigned int nr;

	chain_init(&ca, gfp_mask, safe_needed);

	/* Compute the number of zones */
	nr = 0;
	for_each_zone(zone)
		if (populated_zone(zone))
			nr++;

	/* Allocate the list of zones bitmap objects */
	zone_bm = create_zone_bm_list(nr, &ca);
	bm->zone_bm_list = zone_bm;
	if (!zone_bm) {
		chain_free(&ca, PG_UNSAFE_CLEAR);
		return -ENOMEM;
	}

	/* Initialize the zone bitmap objects */
	for_each_zone(zone) {
		unsigned long pfn;

		if (!populated_zone(zone))
			continue;

		zone_bm->start_pfn = zone->zone_start_pfn;
		zone_bm->end_pfn = zone->zone_start_pfn + zone->spanned_pages;
		/* Allocate the list of bitmap block objects */
		nr = DIV_ROUND_UP(zone->spanned_pages, BM_BITS_PER_BLOCK);
		bb = create_bm_block_list(nr, &ca);
		zone_bm->bm_blocks = bb;
		zone_bm->cur_block = bb;
		if (!bb)
			goto Free;

		nr = zone->spanned_pages;
		pfn = zone->zone_start_pfn;
		/* Initialize the bitmap block objects */
		while (bb) {
			unsigned long *ptr;

			ptr = get_image_page(gfp_mask, safe_needed);
			bb->data = ptr;
			if (!ptr)
				goto Free;

			bb->start_pfn = pfn;
			if (nr >= BM_BITS_PER_BLOCK) {
				pfn += BM_BITS_PER_BLOCK;
				bb->size = BM_CHUNKS_PER_BLOCK;
				nr -= BM_BITS_PER_BLOCK;
			} else {
				/* This is executed only once in the loop */
				pfn += nr;
				bb->size = DIV_ROUND_UP(nr, BM_BITS_PER_CHUNK);
			}
			bb->end_pfn = pfn;
			bb = bb->next;
		}
		zone_bm = zone_bm->next;
	}
	bm->p_list = ca.chain;
	memory_bm_position_reset(bm);
	return 0;

 Free:
	bm->p_list = ca.chain;
	memory_bm_free(bm, PG_UNSAFE_CLEAR);
	return -ENOMEM;
}

/**
  *	memory_bm_free - free memory occupied by the memory bitmap @bm
  */

static void memory_bm_free(struct memory_bitmap *bm, int clear_nosave_free)
{
	struct zone_bitmap *zone_bm;

	/* Free the list of bit blocks for each zone_bitmap object */
	zone_bm = bm->zone_bm_list;
	while (zone_bm) {
		struct bm_block *bb;

		bb = zone_bm->bm_blocks;
		while (bb) {
			if (bb->data)
				free_image_page(bb->data, clear_nosave_free);
			bb = bb->next;
		}
		zone_bm = zone_bm->next;
	}
	free_list_of_pages(bm->p_list, clear_nosave_free);
	bm->zone_bm_list = NULL;
}

/**
 *	memory_bm_find_bit - find the bit in the bitmap @bm that corresponds
 *	to given pfn.  The cur_zone_bm member of @bm and the cur_block member
 *	of @bm->cur_zone_bm are updated.
 */

static void memory_bm_find_bit(struct memory_bitmap *bm, unsigned long pfn,
				void **addr, unsigned int *bit_nr)
{
	struct zone_bitmap *zone_bm;
	struct bm_block *bb;

	/* Check if the pfn is from the current zone */
	zone_bm = bm->cur.zone_bm;
	if (pfn < zone_bm->start_pfn || pfn >= zone_bm->end_pfn) {
		zone_bm = bm->zone_bm_list;
		/* We don't assume that the zones are sorted by pfns */
		while (pfn < zone_bm->start_pfn || pfn >= zone_bm->end_pfn) {
			zone_bm = zone_bm->next;

			BUG_ON(!zone_bm);
		}
		bm->cur.zone_bm = zone_bm;
	}
	/* Check if the pfn corresponds to the current bitmap block */
	bb = zone_bm->cur_block;
	if (pfn < bb->start_pfn)
		bb = zone_bm->bm_blocks;

	while (pfn >= bb->end_pfn) {
		bb = bb->next;

		BUG_ON(!bb);
	}
	zone_bm->cur_block = bb;
	pfn -= bb->start_pfn;
	*bit_nr = pfn % BM_BITS_PER_CHUNK;
	*addr = bb->data + pfn / BM_BITS_PER_CHUNK;
}

static void memory_bm_set_bit(struct memory_bitmap *bm, unsigned long pfn)
{
	void *addr;
	unsigned int bit;

	memory_bm_find_bit(bm, pfn, &addr, &bit);
	set_bit(bit, addr);
}

static void memory_bm_clear_bit(struct memory_bitmap *bm, unsigned long pfn)
{
	void *addr;
	unsigned int bit;

	memory_bm_find_bit(bm, pfn, &addr, &bit);
	clear_bit(bit, addr);
}

static int memory_bm_test_bit(struct memory_bitmap *bm, unsigned long pfn)
{
	void *addr;
	unsigned int bit;

	memory_bm_find_bit(bm, pfn, &addr, &bit);
	return test_bit(bit, addr);
}

/* Two auxiliary functions for memory_bm_next_pfn */

/* Find the first set bit in the given chunk, if there is one */

static inline int next_bit_in_chunk(int bit, unsigned long *chunk_p)
{
	bit++;
	while (bit < BM_BITS_PER_CHUNK) {
		if (test_bit(bit, chunk_p))
			return bit;

		bit++;
	}
	return -1;
}

/* Find a chunk containing some bits set in given block of bits */

static inline int next_chunk_in_block(int n, struct bm_block *bb)
{
	n++;
	while (n < bb->size) {
		if (bb->data[n])
			return n;

		n++;
	}
	return -1;
}

/**
 *	memory_bm_next_pfn - find the pfn that corresponds to the next set bit
 *	in the bitmap @bm.  If the pfn cannot be found, BM_END_OF_MAP is
 *	returned.
 *
 *	It is required to run memory_bm_position_reset() before the first call to
 *	this function.
 */

static unsigned long memory_bm_next_pfn(struct memory_bitmap *bm)
{
	struct zone_bitmap *zone_bm;
	struct bm_block *bb;
	int chunk;
	int bit;

	do {
		bb = bm->cur.block;
		do {
			chunk = bm->cur.chunk;
			bit = bm->cur.bit;
			do {
				bit = next_bit_in_chunk(bit, bb->data + chunk);
				if (bit >= 0)
					goto Return_pfn;

				chunk = next_chunk_in_block(chunk, bb);
				bit = -1;
			} while (chunk >= 0);
			bb = bb->next;
			bm->cur.block = bb;
			memory_bm_reset_chunk(bm);
		} while (bb);
		zone_bm = bm->cur.zone_bm->next;
		if (zone_bm) {
			bm->cur.zone_bm = zone_bm;
			bm->cur.block = zone_bm->bm_blocks;
			memory_bm_reset_chunk(bm);
		}
	} while (zone_bm);
	memory_bm_position_reset(bm);
	return BM_END_OF_MAP;

 Return_pfn:
	bm->cur.chunk = chunk;
	bm->cur.bit = bit;
	return bb->start_pfn + chunk * BM_BITS_PER_CHUNK + bit;
}

/**
 *	This structure represents a range of page frames the contents of which
 *	should not be saved during the suspend.
 */

struct nosave_region {
	struct list_head list;
	unsigned long start_pfn;
	unsigned long end_pfn;
};

static LIST_HEAD(nosave_regions);

/**
 *	register_nosave_region - register a range of page frames the contents
 *	of which should not be saved during the suspend (to be used in the early
 *	initialization code)
 */

void __init
__register_nosave_region(unsigned long start_pfn, unsigned long end_pfn,
			 int use_kmalloc)
{
	struct nosave_region *region;

	if (start_pfn >= end_pfn)
		return;

	if (!list_empty(&nosave_regions)) {
		/* Try to extend the previous region (they should be sorted) */
		region = list_entry(nosave_regions.prev,
					struct nosave_region, list);
		if (region->end_pfn == start_pfn) {
			region->end_pfn = end_pfn;
			goto Report;
		}
	}
	if (use_kmalloc) {
		/* during init, this shouldn't fail */
		region = kmalloc(sizeof(struct nosave_region), GFP_KERNEL);
		BUG_ON(!region);
	} else
		/* This allocation cannot fail */
		region = alloc_bootmem_low(sizeof(struct nosave_region));
	region->start_pfn = start_pfn;
	region->end_pfn = end_pfn;
	list_add_tail(&region->list, &nosave_regions);
 Report:
	printk("swsusp: Registered nosave memory region: %016lx - %016lx\n",
		start_pfn << PAGE_SHIFT, end_pfn << PAGE_SHIFT);
}

/*
 * Set bits in this map correspond to the page frames the contents of which
 * should not be saved during the suspend.
 */
static struct memory_bitmap *forbidden_pages_map;

/* Set bits in this map correspond to free page frames. */
static struct memory_bitmap *free_pages_map;

/*
 * Each page frame allocated for creating the image is marked by setting the
 * corresponding bits in forbidden_pages_map and free_pages_map simultaneously
 */

void swsusp_set_page_free(struct page *page)
{
	if (free_pages_map)
		memory_bm_set_bit(free_pages_map, page_to_pfn(page));
}

static int swsusp_page_is_free(struct page *page)
{
	return free_pages_map ?
		memory_bm_test_bit(free_pages_map, page_to_pfn(page)) : 0;
}

void swsusp_unset_page_free(struct page *page)
{
	if (free_pages_map)
		memory_bm_clear_bit(free_pages_map, page_to_pfn(page));
}

static void swsusp_set_page_forbidden(struct page *page)
{
	if (forbidden_pages_map)
		memory_bm_set_bit(forbidden_pages_map, page_to_pfn(page));
}

int swsusp_page_is_forbidden(struct page *page)
{
	return forbidden_pages_map ?
		memory_bm_test_bit(forbidden_pages_map, page_to_pfn(page)) : 0;
}

static void swsusp_unset_page_forbidden(struct page *page)
{
	if (forbidden_pages_map)
		memory_bm_clear_bit(forbidden_pages_map, page_to_pfn(page));
}

/**
 *	mark_nosave_pages - set bits corresponding to the page frames the
 *	contents of which should not be saved in a given bitmap.
 */

static void mark_nosave_pages(struct memory_bitmap *bm)
{
	struct nosave_region *region;

	if (list_empty(&nosave_regions))
		return;

	list_for_each_entry(region, &nosave_regions, list) {
		unsigned long pfn;

		printk("swsusp: Marking nosave pages: %016lx - %016lx\n",
				region->start_pfn << PAGE_SHIFT,
				region->end_pfn << PAGE_SHIFT);

		for (pfn = region->start_pfn; pfn < region->end_pfn; pfn++)
			memory_bm_set_bit(bm, pfn);
	}
}

/**
 *	create_basic_memory_bitmaps - create bitmaps needed for marking page
 *	frames that should not be saved and free page frames.  The pointers
 *	forbidden_pages_map and free_pages_map are only modified if everything
 *	goes well, because we don't want the bits to be used before both bitmaps
 *	are set up.
 */

int create_basic_memory_bitmaps(void)
{
	struct memory_bitmap *bm1, *bm2;
	int error = 0;

	BUG_ON(forbidden_pages_map || free_pages_map);

	bm1 = kzalloc(sizeof(struct memory_bitmap), GFP_KERNEL);
	if (!bm1)
		return -ENOMEM;

	error = memory_bm_create(bm1, GFP_KERNEL, PG_ANY);
	if (error)
		goto Free_first_object;

	bm2 = kzalloc(sizeof(struct memory_bitmap), GFP_KERNEL);
	if (!bm2)
		goto Free_first_bitmap;

	error = memory_bm_create(bm2, GFP_KERNEL, PG_ANY);
	if (error)
		goto Free_second_object;

	forbidden_pages_map = bm1;
	free_pages_map = bm2;
	mark_nosave_pages(forbidden_pages_map);

	printk("swsusp: Basic memory bitmaps created\n");

	return 0;

 Free_second_object:
	kfree(bm2);
 Free_first_bitmap:
 	memory_bm_free(bm1, PG_UNSAFE_CLEAR);
 Free_first_object:
	kfree(bm1);
	return -ENOMEM;
}

/**
 *	free_basic_memory_bitmaps - free memory bitmaps allocated by
 *	create_basic_memory_bitmaps().  The auxiliary pointers are necessary
 *	so that the bitmaps themselves are not referred to while they are being
 *	freed.
 */

void free_basic_memory_bitmaps(void)
{
	struct memory_bitmap *bm1, *bm2;

	BUG_ON(!(forbidden_pages_map && free_pages_map));

	bm1 = forbidden_pages_map;
	bm2 = free_pages_map;
	forbidden_pages_map = NULL;
	free_pages_map = NULL;
	memory_bm_free(bm1, PG_UNSAFE_CLEAR);
	kfree(bm1);
	memory_bm_free(bm2, PG_UNSAFE_CLEAR);
	kfree(bm2);

	printk("swsusp: Basic memory bitmaps freed\n");
}

/**
 *	snapshot_additional_pages - estimate the number of additional pages
 *	be needed for setting up the suspend image data structures for given
 *	zone (usually the returned value is greater than the exact number)
 */

unsigned int snapshot_additional_pages(struct zone *zone)
{
	unsigned int res;

	res = DIV_ROUND_UP(zone->spanned_pages, BM_BITS_PER_BLOCK);
	res += DIV_ROUND_UP(res * sizeof(struct bm_block), PAGE_SIZE);
	return 2 * res;
}

#ifdef CONFIG_HIGHMEM
/**
 *	count_free_highmem_pages - compute the total number of free highmem
 *	pages, system-wide.
 */

static unsigned int count_free_highmem_pages(void)
{
	struct zone *zone;
	unsigned int cnt = 0;

	for_each_zone(zone)
		if (populated_zone(zone) && is_highmem(zone))
			cnt += zone_page_state(zone, NR_FREE_PAGES);

	return cnt;
}

/**
 *	saveable_highmem_page - Determine whether a highmem page should be
 *	included in the suspend image.
 *
 *	We should save the page if it isn't Nosave or NosaveFree, or Reserved,
 *	and it isn't a part of a free chunk of pages.
 */

static struct page *saveable_highmem_page(unsigned long pfn)
{
	struct page *page;

	if (!pfn_valid(pfn))
		return NULL;

	page = pfn_to_page(pfn);

	BUG_ON(!PageHighMem(page));

	if (swsusp_page_is_forbidden(page) ||  swsusp_page_is_free(page) ||
	    PageReserved(page))
		return NULL;

	return page;
}

/**
 *	count_highmem_pages - compute the total number of saveable highmem
 *	pages.
 */

unsigned int count_highmem_pages(void)
{
	struct zone *zone;
	unsigned int n = 0;

	for_each_zone(zone) {
		unsigned long pfn, max_zone_pfn;

		if (!is_highmem(zone))
			continue;

		mark_free_pages(zone);
		max_zone_pfn = zone->zone_start_pfn + zone->spanned_pages;
		for (pfn = zone->zone_start_pfn; pfn < max_zone_pfn; pfn++)
			if (saveable_highmem_page(pfn))
				n++;
	}
	return n;
}
#else
static inline void *saveable_highmem_page(unsigned long pfn) { return NULL; }
static inline unsigned int count_highmem_pages(void) { return 0; }
#endif /* CONFIG_HIGHMEM */

/**
 *	saveable - Determine whether a non-highmem page should be included in
 *	the suspend image.
 *
 *	We should save the page if it isn't Nosave, and is not in the range
 *	of pages statically defined as 'unsaveable', and it isn't a part of
 *	a free chunk of pages.
 */

static struct page *saveable_page(unsigned long pfn)
{
	struct page *page;

	if (!pfn_valid(pfn))
		return NULL;

	page = pfn_to_page(pfn);

	BUG_ON(PageHighMem(page));

	if (swsusp_page_is_forbidden(page) || swsusp_page_is_free(page))
		return NULL;

	if (PageReserved(page) && pfn_is_nosave(pfn))
		return NULL;

	return page;
}

/**
 *	count_data_pages - compute the total number of saveable non-highmem
 *	pages.
 */

unsigned int count_data_pages(void)
{
	struct zone *zone;
	unsigned long pfn, max_zone_pfn;
	unsigned int n = 0;

	for_each_zone(zone) {
		if (is_highmem(zone))
			continue;

		mark_free_pages(zone);
		max_zone_pfn = zone->zone_start_pfn + zone->spanned_pages;
		for (pfn = zone->zone_start_pfn; pfn < max_zone_pfn; pfn++)
			if(saveable_page(pfn))
				n++;
	}
	return n;
}

/* This is needed, because copy_page and memcpy are not usable for copying
 * task structs.
 */
static inline void do_copy_page(long *dst, long *src)
{
	int n;

	for (n = PAGE_SIZE / sizeof(long); n; n--)
		*dst++ = *src++;
}

#ifdef CONFIG_HIGHMEM
static inline struct page *
page_is_saveable(struct zone *zone, unsigned long pfn)
{
	return is_highmem(zone) ?
			saveable_highmem_page(pfn) : saveable_page(pfn);
}

static inline void
copy_data_page(unsigned long dst_pfn, unsigned long src_pfn)
{
	struct page *s_page, *d_page;
	void *src, *dst;

	s_page = pfn_to_page(src_pfn);
	d_page = pfn_to_page(dst_pfn);
	if (PageHighMem(s_page)) {
		src = kmap_atomic(s_page, KM_USER0);
		dst = kmap_atomic(d_page, KM_USER1);
		do_copy_page(dst, src);
		kunmap_atomic(src, KM_USER0);
		kunmap_atomic(dst, KM_USER1);
	} else {
		src = page_address(s_page);
		if (PageHighMem(d_page)) {
			/* Page pointed to by src may contain some kernel
			 * data modified by kmap_atomic()
			 */
			do_copy_page(buffer, src);
			dst = kmap_atomic(pfn_to_page(dst_pfn), KM_USER0);
			memcpy(dst, buffer, PAGE_SIZE);
			kunmap_atomic(dst, KM_USER0);
		} else {
			dst = page_address(d_page);
			do_copy_page(dst, src);
		}
	}
}
#else
#define page_is_saveable(zone, pfn)	saveable_page(pfn)

static inline void
copy_data_page(unsigned long dst_pfn, unsigned long src_pfn)
{
	do_copy_page(page_address(pfn_to_page(dst_pfn)),
			page_address(pfn_to_page(src_pfn)));
}
#endif /* CONFIG_HIGHMEM */

static void
copy_data_pages(struct memory_bitmap *copy_bm, struct memory_bitmap *orig_bm)
{
	struct zone *zone;
	unsigned long pfn;

	for_each_zone(zone) {
		unsigned long max_zone_pfn;

		mark_free_pages(zone);
		max_zone_pfn = zone->zone_start_pfn + zone->spanned_pages;
		for (pfn = zone->zone_start_pfn; pfn < max_zone_pfn; pfn++)
			if (page_is_saveable(zone, pfn))
				memory_bm_set_bit(orig_bm, pfn);
	}
	memory_bm_position_reset(orig_bm);
	memory_bm_position_reset(copy_bm);
	do {
		pfn = memory_bm_next_pfn(orig_bm);
		if (likely(pfn != BM_END_OF_MAP))
			copy_data_page(memory_bm_next_pfn(copy_bm), pfn);
	} while (pfn != BM_END_OF_MAP);
}

/* Total number of image pages */
static unsigned int nr_copy_pages;
/* Number of pages needed for saving the original pfns of the image pages */
static unsigned int nr_meta_pages;

/**
 *	swsusp_free - free pages allocated for the suspend.
 *
 *	Suspend pages are alocated before the atomic copy is made, so we
 *	need to release them after the resume.
 */

void swsusp_free(void)
{
	struct zone *zone;
	unsigned long pfn, max_zone_pfn;

	for_each_zone(zone) {
		max_zone_pfn = zone->zone_start_pfn + zone->spanned_pages;
		for (pfn = zone->zone_start_pfn; pfn < max_zone_pfn; pfn++)
			if (pfn_valid(pfn)) {
				struct page *page = pfn_to_page(pfn);

				if (swsusp_page_is_forbidden(page) &&
				    swsusp_page_is_free(page)) {
					swsusp_unset_page_forbidden(page);
					swsusp_unset_page_free(page);
					__free_page(page);
				}
			}
	}
	nr_copy_pages = 0;
	nr_meta_pages = 0;
	restore_pblist = NULL;
	buffer = NULL;
}

#ifdef CONFIG_HIGHMEM
/**
  *	count_pages_for_highmem - compute the number of non-highmem pages
  *	that will be necessary for creating copies of highmem pages.
  */

static unsigned int count_pages_for_highmem(unsigned int nr_highmem)
{
	unsigned int free_highmem = count_free_highmem_pages();

	if (free_highmem >= nr_highmem)
		nr_highmem = 0;
	else
		nr_highmem -= free_highmem;

	return nr_highmem;
}
#else
static unsigned int
count_pages_for_highmem(unsigned int nr_highmem) { return 0; }
#endif /* CONFIG_HIGHMEM */

/**
 *	enough_free_mem - Make sure we have enough free memory for the
 *	snapshot image.
 */

static int enough_free_mem(unsigned int nr_pages, unsigned int nr_highmem)
{
	struct zone *zone;
	unsigned int free = 0, meta = 0;

	for_each_zone(zone) {
		meta += snapshot_additional_pages(zone);
		if (!is_highmem(zone))
			free += zone_page_state(zone, NR_FREE_PAGES);
	}

	nr_pages += count_pages_for_highmem(nr_highmem);
	pr_debug("swsusp: Normal pages needed: %u + %u + %u, available pages: %u\n",
		nr_pages, PAGES_FOR_IO, meta, free);

	return free > nr_pages + PAGES_FOR_IO + meta;
}

#ifdef CONFIG_HIGHMEM
/**
 *	get_highmem_buffer - if there are some highmem pages in the suspend
 *	image, we may need the buffer to copy them and/or load their data.
 */

static inline int get_highmem_buffer(int safe_needed)
{
	buffer = get_image_page(GFP_ATOMIC | __GFP_COLD, safe_needed);
	return buffer ? 0 : -ENOMEM;
}

/**
 *	alloc_highmem_image_pages - allocate some highmem pages for the image.
 *	Try to allocate as many pages as needed, but if the number of free
 *	highmem pages is lesser than that, allocate them all.
 */

static inline unsigned int
alloc_highmem_image_pages(struct memory_bitmap *bm, unsigned int nr_highmem)
{
	unsigned int to_alloc = count_free_highmem_pages();

	if (to_alloc > nr_highmem)
		to_alloc = nr_highmem;

	nr_highmem -= to_alloc;
	while (to_alloc-- > 0) {
		struct page *page;

		page = alloc_image_page(__GFP_HIGHMEM);
		memory_bm_set_bit(bm, page_to_pfn(page));
	}
	return nr_highmem;
}
#else
static inline int get_highmem_buffer(int safe_needed) { return 0; }

static inline unsigned int
alloc_highmem_image_pages(struct memory_bitmap *bm, unsigned int n) { return 0; }
#endif /* CONFIG_HIGHMEM */

/**
 *	swsusp_alloc - allocate memory for the suspend image
 *
 *	We first try to allocate as many highmem pages as there are
 *	saveable highmem pages in the system.  If that fails, we allocate
 *	non-highmem pages for the copies of the remaining highmem ones.
 *
 *	In this approach it is likely that the copies of highmem pages will
 *	also be located in the high memory, because of the way in which
 *	copy_data_pages() works.
 */

static int
swsusp_alloc(struct memory_bitmap *orig_bm, struct memory_bitmap *copy_bm,
		unsigned int nr_pages, unsigned int nr_highmem)
{
	int error;

	error = memory_bm_create(orig_bm, GFP_ATOMIC | __GFP_COLD, PG_ANY);
	if (error)
		goto Free;

	error = memory_bm_create(copy_bm, GFP_ATOMIC | __GFP_COLD, PG_ANY);
	if (error)
		goto Free;

	if (nr_highmem > 0) {
		error = get_highmem_buffer(PG_ANY);
		if (error)
			goto Free;

		nr_pages += alloc_highmem_image_pages(copy_bm, nr_highmem);
	}
	while (nr_pages-- > 0) {
		struct page *page = alloc_image_page(GFP_ATOMIC | __GFP_COLD);

		if (!page)
			goto Free;

		memory_bm_set_bit(copy_bm, page_to_pfn(page));
	}
	return 0;

 Free:
	swsusp_free();
	return -ENOMEM;
}

/* Memory bitmap used for marking saveable pages (during suspend) or the
 * suspend image pages (during resume)
 */
static struct memory_bitmap orig_bm;
/* Memory bitmap used on suspend for marking allocated pages that will contain
 * the copies of saveable pages.  During resume it is initially used for
 * marking the suspend image pages, but then its set bits are duplicated in
 * @orig_bm and it is released.  Next, on systems with high memory, it may be
 * used for marking "safe" highmem pages, but it has to be reinitialized for
 * this purpose.
 */
static struct memory_bitmap copy_bm;

asmlinkage int swsusp_save(void)
{
	unsigned int nr_pages, nr_highmem;

	printk("swsusp: critical section: \n");

	drain_local_pages();
	nr_pages = count_data_pages();
	nr_highmem = count_highmem_pages();
	printk("swsusp: Need to copy %u pages\n", nr_pages + nr_highmem);

	if (!enough_free_mem(nr_pages, nr_highmem)) {
		printk(KERN_ERR "swsusp: Not enough free memory\n");
		return -ENOMEM;
	}

	if (swsusp_alloc(&orig_bm, &copy_bm, nr_pages, nr_highmem)) {
		printk(KERN_ERR "swsusp: Memory allocation failed\n");
		return -ENOMEM;
	}

	/* During allocating of suspend pagedir, new cold pages may appear.
	 * Kill them.
	 */
	drain_local_pages();
	copy_data_pages(&copy_bm, &orig_bm);

	/*
	 * End of critical section. From now on, we can write to memory,
	 * but we should not touch disk. This specially means we must _not_
	 * touch swap space! Except we must write out our image of course.
	 */

	nr_pages += nr_highmem;
	nr_copy_pages = nr_pages;
	nr_meta_pages = DIV_ROUND_UP(nr_pages * sizeof(long), PAGE_SIZE);

	printk("swsusp: critical section: done (%d pages copied)\n", nr_pages);

	return 0;
}

static void init_header(struct swsusp_info *info)
{
	memset(info, 0, sizeof(struct swsusp_info));
	info->version_code = LINUX_VERSION_CODE;
	info->num_physpages = num_physpages;
	memcpy(&info->uts, init_utsname(), sizeof(struct new_utsname));
	info->cpus = num_online_cpus();
	info->image_pages = nr_copy_pages;
	info->pages = nr_copy_pages + nr_meta_pages + 1;
	info->size = info->pages;
	info->size <<= PAGE_SHIFT;
}

/**
 *	pack_pfns - pfns corresponding to the set bits found in the bitmap @bm
 *	are stored in the array @buf[] (1 page at a time)
 */

static inline void
pack_pfns(unsigned long *buf, struct memory_bitmap *bm)
{
	int j;

	for (j = 0; j < PAGE_SIZE / sizeof(long); j++) {
		buf[j] = memory_bm_next_pfn(bm);
		if (unlikely(buf[j] == BM_END_OF_MAP))
			break;
	}
}

/**
 *	snapshot_read_next - used for reading the system memory snapshot.
 *
 *	On the first call to it @handle should point to a zeroed
 *	snapshot_handle structure.  The structure gets updated and a pointer
 *	to it should be passed to this function every next time.
 *
 *	The @count parameter should contain the number of bytes the caller
 *	wants to read from the snapshot.  It must not be zero.
 *
 *	On success the function returns a positive number.  Then, the caller
 *	is allowed to read up to the returned number of bytes from the memory
 *	location computed by the data_of() macro.  The number returned
 *	may be smaller than @count, but this only happens if the read would
 *	cross a page boundary otherwise.
 *
 *	The function returns 0 to indicate the end of data stream condition,
 *	and a negative number is returned on error.  In such cases the
 *	structure pointed to by @handle is not updated and should not be used
 *	any more.
 */

int snapshot_read_next(struct snapshot_handle *handle, size_t count)
{
	if (handle->cur > nr_meta_pages + nr_copy_pages)
		return 0;

	if (!buffer) {
		/* This makes the buffer be freed by swsusp_free() */
		buffer = get_image_page(GFP_ATOMIC, PG_ANY);
		if (!buffer)
			return -ENOMEM;
	}
	if (!handle->offset) {
		init_header((struct swsusp_info *)buffer);
		handle->buffer = buffer;
		memory_bm_position_reset(&orig_bm);
		memory_bm_position_reset(&copy_bm);
	}
	if (handle->prev < handle->cur) {
		if (handle->cur <= nr_meta_pages) {
			memset(buffer, 0, PAGE_SIZE);
			pack_pfns(buffer, &orig_bm);
		} else {
			struct page *page;

			page = pfn_to_page(memory_bm_next_pfn(&copy_bm));
			if (PageHighMem(page)) {
				/* Highmem pages are copied to the buffer,
				 * because we can't return with a kmapped
				 * highmem page (we may not be called again).
				 */
				void *kaddr;

				kaddr = kmap_atomic(page, KM_USER0);
				memcpy(buffer, kaddr, PAGE_SIZE);
				kunmap_atomic(kaddr, KM_USER0);
				handle->buffer = buffer;
			} else {
				handle->buffer = page_address(page);
			}
		}
		handle->prev = handle->cur;
	}
	handle->buf_offset = handle->cur_offset;
	if (handle->cur_offset + count >= PAGE_SIZE) {
		count = PAGE_SIZE - handle->cur_offset;
		handle->cur_offset = 0;
		handle->cur++;
	} else {
		handle->cur_offset += count;
	}
	handle->offset += count;
	return count;
}

/**
 *	mark_unsafe_pages - mark the pages that cannot be used for storing
 *	the image during resume, because they conflict with the pages that
 *	had been used before suspend
 */

static int mark_unsafe_pages(struct memory_bitmap *bm)
{
	struct zone *zone;
	unsigned long pfn, max_zone_pfn;

	/* Clear page flags */
	for_each_zone(zone) {
		max_zone_pfn = zone->zone_start_pfn + zone->spanned_pages;
		for (pfn = zone->zone_start_pfn; pfn < max_zone_pfn; pfn++)
			if (pfn_valid(pfn))
				swsusp_unset_page_free(pfn_to_page(pfn));
	}

	/* Mark pages that correspond to the "original" pfns as "unsafe" */
	memory_bm_position_reset(bm);
	do {
		pfn = memory_bm_next_pfn(bm);
		if (likely(pfn != BM_END_OF_MAP)) {
			if (likely(pfn_valid(pfn)))
				swsusp_set_page_free(pfn_to_page(pfn));
			else
				return -EFAULT;
		}
	} while (pfn != BM_END_OF_MAP);

	allocated_unsafe_pages = 0;

	return 0;
}

static void
duplicate_memory_bitmap(struct memory_bitmap *dst, struct memory_bitmap *src)
{
	unsigned long pfn;

	memory_bm_position_reset(src);
	pfn = memory_bm_next_pfn(src);
	while (pfn != BM_END_OF_MAP) {
		memory_bm_set_bit(dst, pfn);
		pfn = memory_bm_next_pfn(src);
	}
}

static inline int check_header(struct swsusp_info *info)
{
	char *reason = NULL;

	if (info->version_code != LINUX_VERSION_CODE)
		reason = "kernel version";
	if (info->num_physpages != num_physpages)
		reason = "memory size";
	if (strcmp(info->uts.sysname,init_utsname()->sysname))
		reason = "system type";
	if (strcmp(info->uts.release,init_utsname()->release))
		reason = "kernel release";
	if (strcmp(info->uts.version,init_utsname()->version))
		reason = "version";
	if (strcmp(info->uts.machine,init_utsname()->machine))
		reason = "machine";
	if (reason) {
		printk(KERN_ERR "swsusp: Resume mismatch: %s\n", reason);
		return -EPERM;
	}
	return 0;
}

/**
 *	load header - check the image header and copy data from it
 */

static int
load_header(struct swsusp_info *info)
{
	int error;

	restore_pblist = NULL;
	error = check_header(info);
	if (!error) {
		nr_copy_pages = info->image_pages;
		nr_meta_pages = info->pages - info->image_pages - 1;
	}
	return error;
}

/**
 *	unpack_orig_pfns - for each element of @buf[] (1 page at a time) set
 *	the corresponding bit in the memory bitmap @bm
 */

static inline void
unpack_orig_pfns(unsigned long *buf, struct memory_bitmap *bm)
{
	int j;

	for (j = 0; j < PAGE_SIZE / sizeof(long); j++) {
		if (unlikely(buf[j] == BM_END_OF_MAP))
			break;

		memory_bm_set_bit(bm, buf[j]);
	}
}

/* List of "safe" pages that may be used to store data loaded from the suspend
 * image
 */
static struct linked_page *safe_pages_list;

#ifdef CONFIG_HIGHMEM
/* struct highmem_pbe is used for creating the list of highmem pages that
 * should be restored atomically during the resume from disk, because the page
 * frames they have occupied before the suspend are in use.
 */
struct highmem_pbe {
	struct page *copy_page;	/* data is here now */
	struct page *orig_page;	/* data was here before the suspend */
	struct highmem_pbe *next;
};

/* List of highmem PBEs needed for restoring the highmem pages that were
 * allocated before the suspend and included in the suspend image, but have
 * also been allocated by the "resume" kernel, so their contents cannot be
 * written directly to their "original" page frames.
 */
static struct highmem_pbe *highmem_pblist;

/**
 *	count_highmem_image_pages - compute the number of highmem pages in the
 *	suspend image.  The bits in the memory bitmap @bm that correspond to the
 *	image pages are assumed to be set.
 */

static unsigned int count_highmem_image_pages(struct memory_bitmap *bm)
{
	unsigned long pfn;
	unsigned int cnt = 0;

	memory_bm_position_reset(bm);
	pfn = memory_bm_next_pfn(bm);
	while (pfn != BM_END_OF_MAP) {
		if (PageHighMem(pfn_to_page(pfn)))
			cnt++;

		pfn = memory_bm_next_pfn(bm);
	}
	return cnt;
}

/**
 *	prepare_highmem_image - try to allocate as many highmem pages as
 *	there are highmem image pages (@nr_highmem_p points to the variable
 *	containing the number of highmem image pages).  The pages that are
 *	"safe" (ie. will not be overwritten when the suspend image is
 *	restored) have the corresponding bits set in @bm (it must be
 *	unitialized).
 *
 *	NOTE: This function should not be called if there are no highmem
 *	image pages.
 */

static unsigned int safe_highmem_pages;

static struct memory_bitmap *safe_highmem_bm;

static int
prepare_highmem_image(struct memory_bitmap *bm, unsigned int *nr_highmem_p)
{
	unsigned int to_alloc;

	if (memory_bm_create(bm, GFP_ATOMIC, PG_SAFE))
		return -ENOMEM;

	if (get_highmem_buffer(PG_SAFE))
		return -ENOMEM;

	to_alloc = count_free_highmem_pages();
	if (to_alloc > *nr_highmem_p)
		to_alloc = *nr_highmem_p;
	else
		*nr_highmem_p = to_alloc;

	safe_highmem_pages = 0;
	while (to_alloc-- > 0) {
		struct page *page;

		page = alloc_page(__GFP_HIGHMEM);
		if (!swsusp_page_is_free(page)) {
			/* The page is "safe", set its bit the bitmap */
			memory_bm_set_bit(bm, page_to_pfn(page));
			safe_highmem_pages++;
		}
		/* Mark the page as allocated */
		swsusp_set_page_forbidden(page);
		swsusp_set_page_free(page);
	}
	memory_bm_position_reset(bm);
	safe_highmem_bm = bm;
	return 0;
}

/**
 *	get_highmem_page_buffer - for given highmem image page find the buffer
 *	that suspend_write_next() should set for its caller to write to.
 *
 *	If the page is to be saved to its "original" page frame or a copy of
 *	the page is to be made in the highmem, @buffer is returned.  Otherwise,
 *	the copy of the page is to be made in normal memory, so the address of
 *	the copy is returned.
 *
 *	If @buffer is returned, the caller of suspend_write_next() will write
 *	the page's contents to @buffer, so they will have to be copied to the
 *	right location on the next call to suspend_write_next() and it is done
 *	with the help of copy_last_highmem_page().  For this purpose, if
 *	@buffer is returned, @last_highmem page is set to the page to which
 *	the data will have to be copied from @buffer.
 */

static struct page *last_highmem_page;

static void *
get_highmem_page_buffer(struct page *page, struct chain_allocator *ca)
{
	struct highmem_pbe *pbe;
	void *kaddr;

	if (swsusp_page_is_forbidden(page) && swsusp_page_is_free(page)) {
		/* We have allocated the "original" page frame and we can
		 * use it directly to store the loaded page.
		 */
		last_highmem_page = page;
		return buffer;
	}
	/* The "original" page frame has not been allocated and we have to
	 * use a "safe" page frame to store the loaded page.
	 */
	pbe = chain_alloc(ca, sizeof(struct highmem_pbe));
	if (!pbe) {
		swsusp_free();
		return NULL;
	}
	pbe->orig_page = page;
	if (safe_highmem_pages > 0) {
		struct page *tmp;

		/* Copy of the page will be stored in high memory */
		kaddr = buffer;
		tmp = pfn_to_page(memory_bm_next_pfn(safe_highmem_bm));
		safe_highmem_pages--;
		last_highmem_page = tmp;
		pbe->copy_page = tmp;
	} else {
		/* Copy of the page will be stored in normal memory */
		kaddr = safe_pages_list;
		safe_pages_list = safe_pages_list->next;
		pbe->copy_page = virt_to_page(kaddr);
	}
	pbe->next = highmem_pblist;
	highmem_pblist = pbe;
	return kaddr;
}

/**
 *	copy_last_highmem_page - copy the contents of a highmem image from
 *	@buffer, where the caller of snapshot_write_next() has place them,
 *	to the right location represented by @last_highmem_page .
 */

static void copy_last_highmem_page(void)
{
	if (last_highmem_page) {
		void *dst;

		dst = kmap_atomic(last_highmem_page, KM_USER0);
		memcpy(dst, buffer, PAGE_SIZE);
		kunmap_atomic(dst, KM_USER0);
		last_highmem_page = NULL;
	}
}

static inline int last_highmem_page_copied(void)
{
	return !last_highmem_page;
}

static inline void free_highmem_data(void)
{
	if (safe_highmem_bm)
		memory_bm_free(safe_highmem_bm, PG_UNSAFE_CLEAR);

	if (buffer)
		free_image_page(buffer, PG_UNSAFE_CLEAR);
}
#else
static inline int get_safe_write_buffer(void) { return 0; }

static unsigned int
count_highmem_image_pages(struct memory_bitmap *bm) { return 0; }

static inline int
prepare_highmem_image(struct memory_bitmap *bm, unsigned int *nr_highmem_p)
{
	return 0;
}

static inline void *
get_highmem_page_buffer(struct page *page, struct chain_allocator *ca)
{
	return NULL;
}

static inline void copy_last_highmem_page(void) {}
static inline int last_highmem_page_copied(void) { return 1; }
static inline void free_highmem_data(void) {}
#endif /* CONFIG_HIGHMEM */

/**
 *	prepare_image - use the memory bitmap @bm to mark the pages that will
 *	be overwritten in the process of restoring the system memory state
 *	from the suspend image ("unsafe" pages) and allocate memory for the
 *	image.
 *
 *	The idea is to allocate a new memory bitmap first and then allocate
 *	as many pages as needed for the image data, but not to assign these
 *	pages to specific tasks initially.  Instead, we just mark them as
 *	allocated and create a lists of "safe" pages that will be used
 *	later.  On systems with high memory a list of "safe" highmem pages is
 *	also created.
 */

#define PBES_PER_LINKED_PAGE	(LINKED_PAGE_DATA_SIZE / sizeof(struct pbe))

static int
prepare_image(struct memory_bitmap *new_bm, struct memory_bitmap *bm)
{
	unsigned int nr_pages, nr_highmem;
	struct linked_page *sp_list, *lp;
	int error;

	/* If there is no highmem, the buffer will not be necessary */
	free_image_page(buffer, PG_UNSAFE_CLEAR);
	buffer = NULL;

	nr_highmem = count_highmem_image_pages(bm);
	error = mark_unsafe_pages(bm);
	if (error)
		goto Free;

	error = memory_bm_create(new_bm, GFP_ATOMIC, PG_SAFE);
	if (error)
		goto Free;

	duplicate_memory_bitmap(new_bm, bm);
	memory_bm_free(bm, PG_UNSAFE_KEEP);
	if (nr_highmem > 0) {
		error = prepare_highmem_image(bm, &nr_highmem);
		if (error)
			goto Free;
	}
	/* Reserve some safe pages for potential later use.
	 *
	 * NOTE: This way we make sure there will be enough safe pages for the
	 * chain_alloc() in get_buffer().  It is a bit wasteful, but
	 * nr_copy_pages cannot be greater than 50% of the memory anyway.
	 */
	sp_list = NULL;
	/* nr_copy_pages cannot be lesser than allocated_unsafe_pages */
	nr_pages = nr_copy_pages - nr_highmem - allocated_unsafe_pages;
	nr_pages = DIV_ROUND_UP(nr_pages, PBES_PER_LINKED_PAGE);
	while (nr_pages > 0) {
		lp = get_image_page(GFP_ATOMIC, PG_SAFE);
		if (!lp) {
			error = -ENOMEM;
			goto Free;
		}
		lp->next = sp_list;
		sp_list = lp;
		nr_pages--;
	}
	/* Preallocate memory for the image */
	safe_pages_list = NULL;
	nr_pages = nr_copy_pages - nr_highmem - allocated_unsafe_pages;
	while (nr_pages > 0) {
		lp = (struct linked_page *)get_zeroed_page(GFP_ATOMIC);
		if (!lp) {
			error = -ENOMEM;
			goto Free;
		}
		if (!swsusp_page_is_free(virt_to_page(lp))) {
			/* The page is "safe", add it to the list */
			lp->next = safe_pages_list;
			safe_pages_list = lp;
		}
		/* Mark the page as allocated */
		swsusp_set_page_forbidden(virt_to_page(lp));
		swsusp_set_page_free(virt_to_page(lp));
		nr_pages--;
	}
	/* Free the reserved safe pages so that chain_alloc() can use them */
	while (sp_list) {
		lp = sp_list->next;
		free_image_page(sp_list, PG_UNSAFE_CLEAR);
		sp_list = lp;
	}
	return 0;

 Free:
	swsusp_free();
	return error;
}

/**
 *	get_buffer - compute the address that snapshot_write_next() should
 *	set for its caller to write to.
 */

static void *get_buffer(struct memory_bitmap *bm, struct chain_allocator *ca)
{
	struct pbe *pbe;
	struct page *page = pfn_to_page(memory_bm_next_pfn(bm));

	if (PageHighMem(page))
		return get_highmem_page_buffer(page, ca);

	if (swsusp_page_is_forbidden(page) && swsusp_page_is_free(page))
		/* We have allocated the "original" page frame and we can
		 * use it directly to store the loaded page.
		 */
		return page_address(page);

	/* The "original" page frame has not been allocated and we have to
	 * use a "safe" page frame to store the loaded page.
	 */
	pbe = chain_alloc(ca, sizeof(struct pbe));
	if (!pbe) {
		swsusp_free();
		return NULL;
	}
	pbe->orig_address = page_address(page);
	pbe->address = safe_pages_list;
	safe_pages_list = safe_pages_list->next;
	pbe->next = restore_pblist;
	restore_pblist = pbe;
	return pbe->address;
}

/**
 *	snapshot_write_next - used for writing the system memory snapshot.
 *
 *	On the first call to it @handle should point to a zeroed
 *	snapshot_handle structure.  The structure gets updated and a pointer
 *	to it should be passed to this function every next time.
 *
 *	The @count parameter should contain the number of bytes the caller
 *	wants to write to the image.  It must not be zero.
 *
 *	On success the function returns a positive number.  Then, the caller
 *	is allowed to write up to the returned number of bytes to the memory
 *	location computed by the data_of() macro.  The number returned
 *	may be smaller than @count, but this only happens if the write would
 *	cross a page boundary otherwise.
 *
 *	The function returns 0 to indicate the "end of file" condition,
 *	and a negative number is returned on error.  In such cases the
 *	structure pointed to by @handle is not updated and should not be used
 *	any more.
 */

int snapshot_write_next(struct snapshot_handle *handle, size_t count)
{
	static struct chain_allocator ca;
	int error = 0;

	/* Check if we have already loaded the entire image */
	if (handle->prev && handle->cur > nr_meta_pages + nr_copy_pages)
		return 0;

	if (handle->offset == 0) {
		if (!buffer)
			/* This makes the buffer be freed by swsusp_free() */
			buffer = get_image_page(GFP_ATOMIC, PG_ANY);

		if (!buffer)
			return -ENOMEM;

		handle->buffer = buffer;
	}
	handle->sync_read = 1;
	if (handle->prev < handle->cur) {
		if (handle->prev == 0) {
			error = load_header(buffer);
			if (error)
				return error;

			error = memory_bm_create(&copy_bm, GFP_ATOMIC, PG_ANY);
			if (error)
				return error;

		} else if (handle->prev <= nr_meta_pages) {
			unpack_orig_pfns(buffer, &copy_bm);
			if (handle->prev == nr_meta_pages) {
				error = prepare_image(&orig_bm, &copy_bm);
				if (error)
					return error;

				chain_init(&ca, GFP_ATOMIC, PG_SAFE);
				memory_bm_position_reset(&orig_bm);
				restore_pblist = NULL;
				handle->buffer = get_buffer(&orig_bm, &ca);
				handle->sync_read = 0;
				if (!handle->buffer)
					return -ENOMEM;
			}
		} else {
			copy_last_highmem_page();
			handle->buffer = get_buffer(&orig_bm, &ca);
			if (handle->buffer != buffer)
				handle->sync_read = 0;
		}
		handle->prev = handle->cur;
	}
	handle->buf_offset = handle->cur_offset;
	if (handle->cur_offset + count >= PAGE_SIZE) {
		count = PAGE_SIZE - handle->cur_offset;
		handle->cur_offset = 0;
		handle->cur++;
	} else {
		handle->cur_offset += count;
	}
	handle->offset += count;
	return count;
}

/**
 *	snapshot_write_finalize - must be called after the last call to
 *	snapshot_write_next() in case the last page in the image happens
 *	to be a highmem page and its contents should be stored in the
 *	highmem.  Additionally, it releases the memory that will not be
 *	used any more.
 */

void snapshot_write_finalize(struct snapshot_handle *handle)
{
	copy_last_highmem_page();
	/* Free only if we have loaded the image entirely */
	if (handle->prev && handle->cur > nr_meta_pages + nr_copy_pages) {
		memory_bm_free(&orig_bm, PG_UNSAFE_CLEAR);
		free_highmem_data();
	}
}

int snapshot_image_loaded(struct snapshot_handle *handle)
{
	return !(!nr_copy_pages || !last_highmem_page_copied() ||
			handle->cur <= nr_meta_pages + nr_copy_pages);
}

#ifdef CONFIG_HIGHMEM
/* Assumes that @buf is ready and points to a "safe" page */
static inline void
swap_two_pages_data(struct page *p1, struct page *p2, void *buf)
{
	void *kaddr1, *kaddr2;

	kaddr1 = kmap_atomic(p1, KM_USER0);
	kaddr2 = kmap_atomic(p2, KM_USER1);
	memcpy(buf, kaddr1, PAGE_SIZE);
	memcpy(kaddr1, kaddr2, PAGE_SIZE);
	memcpy(kaddr2, buf, PAGE_SIZE);
	kunmap_atomic(kaddr1, KM_USER0);
	kunmap_atomic(kaddr2, KM_USER1);
}

/**
 *	restore_highmem - for each highmem page that was allocated before
 *	the suspend and included in the suspend image, and also has been
 *	allocated by the "resume" kernel swap its current (ie. "before
 *	resume") contents with the previous (ie. "before suspend") one.
 *
 *	If the resume eventually fails, we can call this function once
 *	again and restore the "before resume" highmem state.
 */

int restore_highmem(void)
{
	struct highmem_pbe *pbe = highmem_pblist;
	void *buf;

	if (!pbe)
		return 0;

	buf = get_image_page(GFP_ATOMIC, PG_SAFE);
	if (!buf)
		return -ENOMEM;

	while (pbe) {
		swap_two_pages_data(pbe->copy_page, pbe->orig_page, buf);
		pbe = pbe->next;
	}
	free_image_page(buf, PG_UNSAFE_CLEAR);
	return 0;
}
#endif /* CONFIG_HIGHMEM */
