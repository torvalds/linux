/*
 * linux/kernel/power/snapshot.c
 *
 * This file provide system snapshot/restore functionality.
 *
 * Copyright (C) 1998-2005 Pavel Machek <pavel@suse.cz>
 *
 * This file is released under the GPLv2, and is based on swsusp.c.
 *
 */


#include <linux/version.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/suspend.h>
#include <linux/smp_lock.h>
#include <linux/delay.h>
#include <linux/bitops.h>
#include <linux/spinlock.h>
#include <linux/kernel.h>
#include <linux/pm.h>
#include <linux/device.h>
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

/* List of PBEs used for creating and restoring the suspend image */
struct pbe *restore_pblist;

static unsigned int nr_copy_pages;
static unsigned int nr_meta_pages;
static void *buffer;

#ifdef CONFIG_HIGHMEM
unsigned int count_highmem_pages(void)
{
	struct zone *zone;
	unsigned long zone_pfn;
	unsigned int n = 0;

	for_each_zone (zone)
		if (is_highmem(zone)) {
			mark_free_pages(zone);
			for (zone_pfn = 0; zone_pfn < zone->spanned_pages; zone_pfn++) {
				struct page *page;
				unsigned long pfn = zone_pfn + zone->zone_start_pfn;
				if (!pfn_valid(pfn))
					continue;
				page = pfn_to_page(pfn);
				if (PageReserved(page))
					continue;
				if (PageNosaveFree(page))
					continue;
				n++;
			}
		}
	return n;
}

struct highmem_page {
	char *data;
	struct page *page;
	struct highmem_page *next;
};

static struct highmem_page *highmem_copy;

static int save_highmem_zone(struct zone *zone)
{
	unsigned long zone_pfn;
	mark_free_pages(zone);
	for (zone_pfn = 0; zone_pfn < zone->spanned_pages; ++zone_pfn) {
		struct page *page;
		struct highmem_page *save;
		void *kaddr;
		unsigned long pfn = zone_pfn + zone->zone_start_pfn;

		if (!(pfn%10000))
			printk(".");
		if (!pfn_valid(pfn))
			continue;
		page = pfn_to_page(pfn);
		/*
		 * This condition results from rvmalloc() sans vmalloc_32()
		 * and architectural memory reservations. This should be
		 * corrected eventually when the cases giving rise to this
		 * are better understood.
		 */
		if (PageReserved(page))
			continue;
		BUG_ON(PageNosave(page));
		if (PageNosaveFree(page))
			continue;
		save = kmalloc(sizeof(struct highmem_page), GFP_ATOMIC);
		if (!save)
			return -ENOMEM;
		save->next = highmem_copy;
		save->page = page;
		save->data = (void *) get_zeroed_page(GFP_ATOMIC);
		if (!save->data) {
			kfree(save);
			return -ENOMEM;
		}
		kaddr = kmap_atomic(page, KM_USER0);
		memcpy(save->data, kaddr, PAGE_SIZE);
		kunmap_atomic(kaddr, KM_USER0);
		highmem_copy = save;
	}
	return 0;
}

int save_highmem(void)
{
	struct zone *zone;
	int res = 0;

	pr_debug("swsusp: Saving Highmem");
	drain_local_pages();
	for_each_zone (zone) {
		if (is_highmem(zone))
			res = save_highmem_zone(zone);
		if (res)
			return res;
	}
	printk("\n");
	return 0;
}

int restore_highmem(void)
{
	printk("swsusp: Restoring Highmem\n");
	while (highmem_copy) {
		struct highmem_page *save = highmem_copy;
		void *kaddr;
		highmem_copy = save->next;

		kaddr = kmap_atomic(save->page, KM_USER0);
		memcpy(kaddr, save->data, PAGE_SIZE);
		kunmap_atomic(kaddr, KM_USER0);
		free_page((long) save->data);
		kfree(save);
	}
	return 0;
}
#else
static inline unsigned int count_highmem_pages(void) {return 0;}
static inline int save_highmem(void) {return 0;}
static inline int restore_highmem(void) {return 0;}
#endif

/**
 *	@safe_needed - on resume, for storing the PBE list and the image,
 *	we can only use memory pages that do not conflict with the pages
 *	used before suspend.
 *
 *	The unsafe pages are marked with the PG_nosave_free flag
 *	and we count them using unsafe_pages
 */

#define PG_ANY		0
#define PG_SAFE		1
#define PG_UNSAFE_CLEAR	1
#define PG_UNSAFE_KEEP	0

static unsigned int allocated_unsafe_pages;

static void *alloc_image_page(gfp_t gfp_mask, int safe_needed)
{
	void *res;

	res = (void *)get_zeroed_page(gfp_mask);
	if (safe_needed)
		while (res && PageNosaveFree(virt_to_page(res))) {
			/* The page is unsafe, mark it for swsusp_free() */
			SetPageNosave(virt_to_page(res));
			allocated_unsafe_pages++;
			res = (void *)get_zeroed_page(gfp_mask);
		}
	if (res) {
		SetPageNosave(virt_to_page(res));
		SetPageNosaveFree(virt_to_page(res));
	}
	return res;
}

unsigned long get_safe_page(gfp_t gfp_mask)
{
	return (unsigned long)alloc_image_page(gfp_mask, PG_SAFE);
}

/**
 *	free_image_page - free page represented by @addr, allocated with
 *	alloc_image_page (page flags set by it must be cleared)
 */

static inline void free_image_page(void *addr, int clear_nosave_free)
{
	ClearPageNosave(virt_to_page(addr));
	if (clear_nosave_free)
		ClearPageNosaveFree(virt_to_page(addr));
	free_page((unsigned long)addr);
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

		lp = alloc_image_page(ca->gfp_mask, ca->safe_needed);
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
 *
 *	NOTE: Memory bitmaps are used for two types of operations only:
 *	"set a bit" and "find the next bit set".  Moreover, the searching
 *	is always carried out after all of the "set a bit" operations
 *	on given bitmap.
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
	for_each_zone (zone)
		if (populated_zone(zone) && !is_highmem(zone))
			nr++;

	/* Allocate the list of zones bitmap objects */
	zone_bm = create_zone_bm_list(nr, &ca);
	bm->zone_bm_list = zone_bm;
	if (!zone_bm) {
		chain_free(&ca, PG_UNSAFE_CLEAR);
		return -ENOMEM;
	}

	/* Initialize the zone bitmap objects */
	for_each_zone (zone) {
		unsigned long pfn;

		if (!populated_zone(zone) || is_highmem(zone))
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

			ptr = alloc_image_page(gfp_mask, safe_needed);
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
 *	memory_bm_set_bit - set the bit in the bitmap @bm that corresponds
 *	to given pfn.  The cur_zone_bm member of @bm and the cur_block member
 *	of @bm->cur_zone_bm are updated.
 *
 *	If the bit cannot be set, the function returns -EINVAL .
 */

static int
memory_bm_set_bit(struct memory_bitmap *bm, unsigned long pfn)
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
			if (unlikely(!zone_bm))
				return -EINVAL;
		}
		bm->cur.zone_bm = zone_bm;
	}
	/* Check if the pfn corresponds to the current bitmap block */
	bb = zone_bm->cur_block;
	if (pfn < bb->start_pfn)
		bb = zone_bm->bm_blocks;

	while (pfn >= bb->end_pfn) {
		bb = bb->next;
		if (unlikely(!bb))
			return -EINVAL;
	}
	zone_bm->cur_block = bb;
	pfn -= bb->start_pfn;
	set_bit(pfn % BM_BITS_PER_CHUNK, bb->data + pfn / BM_BITS_PER_CHUNK);
	return 0;
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
 *	snapshot_additional_pages - estimate the number of additional pages
 *	be needed for setting up the suspend image data structures for given
 *	zone (usually the returned value is greater than the exact number)
 */

unsigned int snapshot_additional_pages(struct zone *zone)
{
	unsigned int res;

	res = DIV_ROUND_UP(zone->spanned_pages, BM_BITS_PER_BLOCK);
	res += DIV_ROUND_UP(res * sizeof(struct bm_block), PAGE_SIZE);
	return res;
}

/**
 *	pfn_is_nosave - check if given pfn is in the 'nosave' section
 */

static inline int pfn_is_nosave(unsigned long pfn)
{
	unsigned long nosave_begin_pfn = __pa(&__nosave_begin) >> PAGE_SHIFT;
	unsigned long nosave_end_pfn = PAGE_ALIGN(__pa(&__nosave_end)) >> PAGE_SHIFT;
	return (pfn >= nosave_begin_pfn) && (pfn < nosave_end_pfn);
}

/**
 *	saveable - Determine whether a page should be cloned or not.
 *	@pfn:	The page
 *
 *	We save a page if it isn't Nosave, and is not in the range of pages
 *	statically defined as 'unsaveable', and it
 *	isn't a part of a free chunk of pages.
 */

static struct page *saveable_page(unsigned long pfn)
{
	struct page *page;

	if (!pfn_valid(pfn))
		return NULL;

	page = pfn_to_page(pfn);

	if (PageNosave(page))
		return NULL;
	if (PageReserved(page) && pfn_is_nosave(pfn))
		return NULL;
	if (PageNosaveFree(page))
		return NULL;

	return page;
}

unsigned int count_data_pages(void)
{
	struct zone *zone;
	unsigned long pfn, max_zone_pfn;
	unsigned int n = 0;

	for_each_zone (zone) {
		if (is_highmem(zone))
			continue;
		mark_free_pages(zone);
		max_zone_pfn = zone->zone_start_pfn + zone->spanned_pages;
		for (pfn = zone->zone_start_pfn; pfn < max_zone_pfn; pfn++)
			n += !!saveable_page(pfn);
	}
	return n;
}

static inline void copy_data_page(long *dst, long *src)
{
	int n;

	/* copy_page and memcpy are not usable for copying task structs. */
	for (n = PAGE_SIZE / sizeof(long); n; n--)
		*dst++ = *src++;
}

static void
copy_data_pages(struct memory_bitmap *copy_bm, struct memory_bitmap *orig_bm)
{
	struct zone *zone;
	unsigned long pfn;

	for_each_zone (zone) {
		unsigned long max_zone_pfn;

		if (is_highmem(zone))
			continue;

		mark_free_pages(zone);
		max_zone_pfn = zone->zone_start_pfn + zone->spanned_pages;
		for (pfn = zone->zone_start_pfn; pfn < max_zone_pfn; pfn++)
			if (saveable_page(pfn))
				memory_bm_set_bit(orig_bm, pfn);
	}
	memory_bm_position_reset(orig_bm);
	memory_bm_position_reset(copy_bm);
	do {
		pfn = memory_bm_next_pfn(orig_bm);
		if (likely(pfn != BM_END_OF_MAP)) {
			struct page *page;
			void *src;

			page = pfn_to_page(pfn);
			src = page_address(page);
			page = pfn_to_page(memory_bm_next_pfn(copy_bm));
			copy_data_page(page_address(page), src);
		}
	} while (pfn != BM_END_OF_MAP);
}

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

				if (PageNosave(page) && PageNosaveFree(page)) {
					ClearPageNosave(page);
					ClearPageNosaveFree(page);
					free_page((long) page_address(page));
				}
			}
	}
	nr_copy_pages = 0;
	nr_meta_pages = 0;
	restore_pblist = NULL;
	buffer = NULL;
}


/**
 *	enough_free_mem - Make sure we enough free memory to snapshot.
 *
 *	Returns TRUE or FALSE after checking the number of available
 *	free pages.
 */

static int enough_free_mem(unsigned int nr_pages)
{
	struct zone *zone;
	unsigned int free = 0, meta = 0;

	for_each_zone (zone)
		if (!is_highmem(zone)) {
			free += zone->free_pages;
			meta += snapshot_additional_pages(zone);
		}

	pr_debug("swsusp: pages needed: %u + %u + %u, available pages: %u\n",
		nr_pages, PAGES_FOR_IO, meta, free);

	return free > nr_pages + PAGES_FOR_IO + meta;
}

static int
swsusp_alloc(struct memory_bitmap *orig_bm, struct memory_bitmap *copy_bm,
		unsigned int nr_pages)
{
	int error;

	error = memory_bm_create(orig_bm, GFP_ATOMIC | __GFP_COLD, PG_ANY);
	if (error)
		goto Free;

	error = memory_bm_create(copy_bm, GFP_ATOMIC | __GFP_COLD, PG_ANY);
	if (error)
		goto Free;

	while (nr_pages-- > 0) {
		struct page *page = alloc_page(GFP_ATOMIC | __GFP_COLD);
		if (!page)
			goto Free;

		SetPageNosave(page);
		SetPageNosaveFree(page);
		memory_bm_set_bit(copy_bm, page_to_pfn(page));
	}
	return 0;

Free:
	swsusp_free();
	return -ENOMEM;
}

/* Memory bitmap used for marking saveable pages */
static struct memory_bitmap orig_bm;
/* Memory bitmap used for marking allocated pages that will contain the copies
 * of saveable pages
 */
static struct memory_bitmap copy_bm;

asmlinkage int swsusp_save(void)
{
	unsigned int nr_pages;

	pr_debug("swsusp: critical section: \n");

	drain_local_pages();
	nr_pages = count_data_pages();
	printk("swsusp: Need to copy %u pages\n", nr_pages);

	if (!enough_free_mem(nr_pages)) {
		printk(KERN_ERR "swsusp: Not enough free memory\n");
		return -ENOMEM;
	}

	if (swsusp_alloc(&orig_bm, &copy_bm, nr_pages))
		return -ENOMEM;

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

	nr_copy_pages = nr_pages;
	nr_meta_pages = (nr_pages * sizeof(long) + PAGE_SIZE - 1) >> PAGE_SHIFT;

	printk("swsusp: critical section/: done (%d pages copied)\n", nr_pages);
	return 0;
}

static void init_header(struct swsusp_info *info)
{
	memset(info, 0, sizeof(struct swsusp_info));
	info->version_code = LINUX_VERSION_CODE;
	info->num_physpages = num_physpages;
	memcpy(&info->uts, &system_utsname, sizeof(system_utsname));
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
		buffer = alloc_image_page(GFP_ATOMIC, PG_ANY);
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
			unsigned long pfn = memory_bm_next_pfn(&copy_bm);

			handle->buffer = page_address(pfn_to_page(pfn));
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
	for_each_zone (zone) {
		max_zone_pfn = zone->zone_start_pfn + zone->spanned_pages;
		for (pfn = zone->zone_start_pfn; pfn < max_zone_pfn; pfn++)
			if (pfn_valid(pfn))
				ClearPageNosaveFree(pfn_to_page(pfn));
	}

	/* Mark pages that correspond to the "original" pfns as "unsafe" */
	memory_bm_position_reset(bm);
	do {
		pfn = memory_bm_next_pfn(bm);
		if (likely(pfn != BM_END_OF_MAP)) {
			if (likely(pfn_valid(pfn)))
				SetPageNosaveFree(pfn_to_page(pfn));
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
	if (strcmp(info->uts.sysname,system_utsname.sysname))
		reason = "system type";
	if (strcmp(info->uts.release,system_utsname.release))
		reason = "kernel release";
	if (strcmp(info->uts.version,system_utsname.version))
		reason = "version";
	if (strcmp(info->uts.machine,system_utsname.machine))
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

/**
 *	prepare_image - use the memory bitmap @bm to mark the pages that will
 *	be overwritten in the process of restoring the system memory state
 *	from the suspend image ("unsafe" pages) and allocate memory for the
 *	image.
 *
 *	The idea is to allocate a new memory bitmap first and then allocate
 *	as many pages as needed for the image data, but not to assign these
 *	pages to specific tasks initially.  Instead, we just mark them as
 *	allocated and create a list of "safe" pages that will be used later.
 */

#define PBES_PER_LINKED_PAGE	(LINKED_PAGE_DATA_SIZE / sizeof(struct pbe))

static struct linked_page *safe_pages_list;

static int
prepare_image(struct memory_bitmap *new_bm, struct memory_bitmap *bm)
{
	unsigned int nr_pages;
	struct linked_page *sp_list, *lp;
	int error;

	error = mark_unsafe_pages(bm);
	if (error)
		goto Free;

	error = memory_bm_create(new_bm, GFP_ATOMIC, PG_SAFE);
	if (error)
		goto Free;

	duplicate_memory_bitmap(new_bm, bm);
	memory_bm_free(bm, PG_UNSAFE_KEEP);
	/* Reserve some safe pages for potential later use.
	 *
	 * NOTE: This way we make sure there will be enough safe pages for the
	 * chain_alloc() in get_buffer().  It is a bit wasteful, but
	 * nr_copy_pages cannot be greater than 50% of the memory anyway.
	 */
	sp_list = NULL;
	/* nr_copy_pages cannot be lesser than allocated_unsafe_pages */
	nr_pages = nr_copy_pages - allocated_unsafe_pages;
	nr_pages = DIV_ROUND_UP(nr_pages, PBES_PER_LINKED_PAGE);
	while (nr_pages > 0) {
		lp = alloc_image_page(GFP_ATOMIC, PG_SAFE);
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
	nr_pages = nr_copy_pages - allocated_unsafe_pages;
	while (nr_pages > 0) {
		lp = (struct linked_page *)get_zeroed_page(GFP_ATOMIC);
		if (!lp) {
			error = -ENOMEM;
			goto Free;
		}
		if (!PageNosaveFree(virt_to_page(lp))) {
			/* The page is "safe", add it to the list */
			lp->next = safe_pages_list;
			safe_pages_list = lp;
		}
		/* Mark the page as allocated */
		SetPageNosave(virt_to_page(lp));
		SetPageNosaveFree(virt_to_page(lp));
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

	if (PageNosave(page) && PageNosaveFree(page))
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
	pbe->orig_address = (unsigned long)page_address(page);
	pbe->address = (unsigned long)safe_pages_list;
	safe_pages_list = safe_pages_list->next;
	pbe->next = restore_pblist;
	restore_pblist = pbe;
	return (void *)pbe->address;
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

	if (!buffer) {
		/* This makes the buffer be freed by swsusp_free() */
		buffer = alloc_image_page(GFP_ATOMIC, PG_ANY);
		if (!buffer)
			return -ENOMEM;
	}
	if (!handle->offset)
		handle->buffer = buffer;
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
			handle->buffer = get_buffer(&orig_bm, &ca);
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

int snapshot_image_loaded(struct snapshot_handle *handle)
{
	return !(!nr_copy_pages ||
			handle->cur <= nr_meta_pages + nr_copy_pages);
}

void snapshot_free_unused_memory(struct snapshot_handle *handle)
{
	/* Free only if we have loaded the image entirely */
	if (handle->prev && handle->cur > nr_meta_pages + nr_copy_pages)
		memory_bm_free(&orig_bm, PG_UNSAFE_CLEAR);
}
