/*
 * linux/kernel/power/snapshot.c
 *
 * This file provides system snapshot/restore functionality for swsusp.
 *
 * Copyright (C) 1998-2005 Pavel Machek <pavel@ucw.cz>
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
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/compiler.h>
#include <linux/ktime.h>

#include <asm/uaccess.h>
#include <asm/mmu_context.h>
#include <asm/pgtable.h>
#include <asm/tlbflush.h>
#include <asm/io.h>

#include "power.h"

static int swsusp_page_is_free(struct page *);
static void swsusp_set_page_forbidden(struct page *);
static void swsusp_unset_page_forbidden(struct page *);

/*
 * Number of bytes to reserve for memory allocations made by device drivers
 * from their ->freeze() and ->freeze_noirq() callbacks so that they don't
 * cause image creation to fail (tunable via /sys/power/reserved_size).
 */
unsigned long reserved_size;

void __init hibernate_reserved_size_init(void)
{
	reserved_size = SPARE_PAGES * PAGE_SIZE;
}

/*
 * Preferred image size in bytes (tunable via /sys/power/image_size).
 * When it is set to N, swsusp will do its best to ensure the image
 * size will not exceed N bytes, but if that is impossible, it will
 * try to create the smallest image possible.
 */
unsigned long image_size;

void __init hibernate_image_size_init(void)
{
	image_size = ((totalram_pages * 2) / 5) * PAGE_SIZE;
}

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
} __packed;

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

/**
 *	Data types related to memory bitmaps.
 *
 *	Memory bitmap is a structure consiting of many linked lists of
 *	objects.  The main list's elements are of type struct zone_bitmap
 *	and each of them corresonds to one zone.  For each zone bitmap
 *	object there is a list of objects of type struct bm_block that
 *	represent each blocks of bitmap in which information is stored.
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
 *	information is stored (in the form of a block of bitmap)
 *	It also contains the pfns that correspond to the start and end of
 *	the represented memory area.
 *
 *	The memory bitmap is organized as a radix tree to guarantee fast random
 *	access to the bits. There is one radix tree for each zone (as returned
 *	from create_mem_extents).
 *
 *	One radix tree is represented by one struct mem_zone_bm_rtree. There are
 *	two linked lists for the nodes of the tree, one for the inner nodes and
 *	one for the leave nodes. The linked leave nodes are used for fast linear
 *	access of the memory bitmap.
 *
 *	The struct rtree_node represents one node of the radix tree.
 */

#define BM_END_OF_MAP	(~0UL)

#define BM_BITS_PER_BLOCK	(PAGE_SIZE * BITS_PER_BYTE)
#define BM_BLOCK_SHIFT		(PAGE_SHIFT + 3)
#define BM_BLOCK_MASK		((1UL << BM_BLOCK_SHIFT) - 1)

/*
 * struct rtree_node is a wrapper struct to link the nodes
 * of the rtree together for easy linear iteration over
 * bits and easy freeing
 */
struct rtree_node {
	struct list_head list;
	unsigned long *data;
};

/*
 * struct mem_zone_bm_rtree represents a bitmap used for one
 * populated memory zone.
 */
struct mem_zone_bm_rtree {
	struct list_head list;		/* Link Zones together         */
	struct list_head nodes;		/* Radix Tree inner nodes      */
	struct list_head leaves;	/* Radix Tree leaves           */
	unsigned long start_pfn;	/* Zone start page frame       */
	unsigned long end_pfn;		/* Zone end page frame + 1     */
	struct rtree_node *rtree;	/* Radix Tree Root             */
	int levels;			/* Number of Radix Tree Levels */
	unsigned int blocks;		/* Number of Bitmap Blocks     */
};

/* strcut bm_position is used for browsing memory bitmaps */

struct bm_position {
	struct mem_zone_bm_rtree *zone;
	struct rtree_node *node;
	unsigned long node_pfn;
	int node_bit;
};

struct memory_bitmap {
	struct list_head zones;
	struct linked_page *p_list;	/* list of pages used to store zone
					 * bitmap objects and bitmap block
					 * objects
					 */
	struct bm_position cur;	/* most recently used bit position */
};

/* Functions that operate on memory bitmaps */

#define BM_ENTRIES_PER_LEVEL	(PAGE_SIZE / sizeof(unsigned long))
#if BITS_PER_LONG == 32
#define BM_RTREE_LEVEL_SHIFT	(PAGE_SHIFT - 2)
#else
#define BM_RTREE_LEVEL_SHIFT	(PAGE_SHIFT - 3)
#endif
#define BM_RTREE_LEVEL_MASK	((1UL << BM_RTREE_LEVEL_SHIFT) - 1)

/*
 *	alloc_rtree_node - Allocate a new node and add it to the radix tree.
 *
 *	This function is used to allocate inner nodes as well as the
 *	leave nodes of the radix tree. It also adds the node to the
 *	corresponding linked list passed in by the *list parameter.
 */
static struct rtree_node *alloc_rtree_node(gfp_t gfp_mask, int safe_needed,
					   struct chain_allocator *ca,
					   struct list_head *list)
{
	struct rtree_node *node;

	node = chain_alloc(ca, sizeof(struct rtree_node));
	if (!node)
		return NULL;

	node->data = get_image_page(gfp_mask, safe_needed);
	if (!node->data)
		return NULL;

	list_add_tail(&node->list, list);

	return node;
}

/*
 *	add_rtree_block - Add a new leave node to the radix tree
 *
 *	The leave nodes need to be allocated in order to keep the leaves
 *	linked list in order. This is guaranteed by the zone->blocks
 *	counter.
 */
static int add_rtree_block(struct mem_zone_bm_rtree *zone, gfp_t gfp_mask,
			   int safe_needed, struct chain_allocator *ca)
{
	struct rtree_node *node, *block, **dst;
	unsigned int levels_needed, block_nr;
	int i;

	block_nr = zone->blocks;
	levels_needed = 0;

	/* How many levels do we need for this block nr? */
	while (block_nr) {
		levels_needed += 1;
		block_nr >>= BM_RTREE_LEVEL_SHIFT;
	}

	/* Make sure the rtree has enough levels */
	for (i = zone->levels; i < levels_needed; i++) {
		node = alloc_rtree_node(gfp_mask, safe_needed, ca,
					&zone->nodes);
		if (!node)
			return -ENOMEM;

		node->data[0] = (unsigned long)zone->rtree;
		zone->rtree = node;
		zone->levels += 1;
	}

	/* Allocate new block */
	block = alloc_rtree_node(gfp_mask, safe_needed, ca, &zone->leaves);
	if (!block)
		return -ENOMEM;

	/* Now walk the rtree to insert the block */
	node = zone->rtree;
	dst = &zone->rtree;
	block_nr = zone->blocks;
	for (i = zone->levels; i > 0; i--) {
		int index;

		if (!node) {
			node = alloc_rtree_node(gfp_mask, safe_needed, ca,
						&zone->nodes);
			if (!node)
				return -ENOMEM;
			*dst = node;
		}

		index = block_nr >> ((i - 1) * BM_RTREE_LEVEL_SHIFT);
		index &= BM_RTREE_LEVEL_MASK;
		dst = (struct rtree_node **)&((*dst)->data[index]);
		node = *dst;
	}

	zone->blocks += 1;
	*dst = block;

	return 0;
}

static void free_zone_bm_rtree(struct mem_zone_bm_rtree *zone,
			       int clear_nosave_free);

/*
 *	create_zone_bm_rtree - create a radix tree for one zone
 *
 *	Allocated the mem_zone_bm_rtree structure and initializes it.
 *	This function also allocated and builds the radix tree for the
 *	zone.
 */
static struct mem_zone_bm_rtree *
create_zone_bm_rtree(gfp_t gfp_mask, int safe_needed,
		     struct chain_allocator *ca,
		     unsigned long start, unsigned long end)
{
	struct mem_zone_bm_rtree *zone;
	unsigned int i, nr_blocks;
	unsigned long pages;

	pages = end - start;
	zone  = chain_alloc(ca, sizeof(struct mem_zone_bm_rtree));
	if (!zone)
		return NULL;

	INIT_LIST_HEAD(&zone->nodes);
	INIT_LIST_HEAD(&zone->leaves);
	zone->start_pfn = start;
	zone->end_pfn = end;
	nr_blocks = DIV_ROUND_UP(pages, BM_BITS_PER_BLOCK);

	for (i = 0; i < nr_blocks; i++) {
		if (add_rtree_block(zone, gfp_mask, safe_needed, ca)) {
			free_zone_bm_rtree(zone, PG_UNSAFE_CLEAR);
			return NULL;
		}
	}

	return zone;
}

/*
 *	free_zone_bm_rtree - Free the memory of the radix tree
 *
 *	Free all node pages of the radix tree. The mem_zone_bm_rtree
 *	structure itself is not freed here nor are the rtree_node
 *	structs.
 */
static void free_zone_bm_rtree(struct mem_zone_bm_rtree *zone,
			       int clear_nosave_free)
{
	struct rtree_node *node;

	list_for_each_entry(node, &zone->nodes, list)
		free_image_page(node->data, clear_nosave_free);

	list_for_each_entry(node, &zone->leaves, list)
		free_image_page(node->data, clear_nosave_free);
}

static void memory_bm_position_reset(struct memory_bitmap *bm)
{
	bm->cur.zone = list_entry(bm->zones.next, struct mem_zone_bm_rtree,
				  list);
	bm->cur.node = list_entry(bm->cur.zone->leaves.next,
				  struct rtree_node, list);
	bm->cur.node_pfn = 0;
	bm->cur.node_bit = 0;
}

static void memory_bm_free(struct memory_bitmap *bm, int clear_nosave_free);

struct mem_extent {
	struct list_head hook;
	unsigned long start;
	unsigned long end;
};

/**
 *	free_mem_extents - free a list of memory extents
 *	@list - list of extents to empty
 */
static void free_mem_extents(struct list_head *list)
{
	struct mem_extent *ext, *aux;

	list_for_each_entry_safe(ext, aux, list, hook) {
		list_del(&ext->hook);
		kfree(ext);
	}
}

/**
 *	create_mem_extents - create a list of memory extents representing
 *	                     contiguous ranges of PFNs
 *	@list - list to put the extents into
 *	@gfp_mask - mask to use for memory allocations
 */
static int create_mem_extents(struct list_head *list, gfp_t gfp_mask)
{
	struct zone *zone;

	INIT_LIST_HEAD(list);

	for_each_populated_zone(zone) {
		unsigned long zone_start, zone_end;
		struct mem_extent *ext, *cur, *aux;

		zone_start = zone->zone_start_pfn;
		zone_end = zone_end_pfn(zone);

		list_for_each_entry(ext, list, hook)
			if (zone_start <= ext->end)
				break;

		if (&ext->hook == list || zone_end < ext->start) {
			/* New extent is necessary */
			struct mem_extent *new_ext;

			new_ext = kzalloc(sizeof(struct mem_extent), gfp_mask);
			if (!new_ext) {
				free_mem_extents(list);
				return -ENOMEM;
			}
			new_ext->start = zone_start;
			new_ext->end = zone_end;
			list_add_tail(&new_ext->hook, &ext->hook);
			continue;
		}

		/* Merge this zone's range of PFNs with the existing one */
		if (zone_start < ext->start)
			ext->start = zone_start;
		if (zone_end > ext->end)
			ext->end = zone_end;

		/* More merging may be possible */
		cur = ext;
		list_for_each_entry_safe_continue(cur, aux, list, hook) {
			if (zone_end < cur->start)
				break;
			if (zone_end < cur->end)
				ext->end = cur->end;
			list_del(&cur->hook);
			kfree(cur);
		}
	}

	return 0;
}

/**
  *	memory_bm_create - allocate memory for a memory bitmap
  */
static int
memory_bm_create(struct memory_bitmap *bm, gfp_t gfp_mask, int safe_needed)
{
	struct chain_allocator ca;
	struct list_head mem_extents;
	struct mem_extent *ext;
	int error;

	chain_init(&ca, gfp_mask, safe_needed);
	INIT_LIST_HEAD(&bm->zones);

	error = create_mem_extents(&mem_extents, gfp_mask);
	if (error)
		return error;

	list_for_each_entry(ext, &mem_extents, hook) {
		struct mem_zone_bm_rtree *zone;

		zone = create_zone_bm_rtree(gfp_mask, safe_needed, &ca,
					    ext->start, ext->end);
		if (!zone) {
			error = -ENOMEM;
			goto Error;
		}
		list_add_tail(&zone->list, &bm->zones);
	}

	bm->p_list = ca.chain;
	memory_bm_position_reset(bm);
 Exit:
	free_mem_extents(&mem_extents);
	return error;

 Error:
	bm->p_list = ca.chain;
	memory_bm_free(bm, PG_UNSAFE_CLEAR);
	goto Exit;
}

/**
  *	memory_bm_free - free memory occupied by the memory bitmap @bm
  */
static void memory_bm_free(struct memory_bitmap *bm, int clear_nosave_free)
{
	struct mem_zone_bm_rtree *zone;

	list_for_each_entry(zone, &bm->zones, list)
		free_zone_bm_rtree(zone, clear_nosave_free);

	free_list_of_pages(bm->p_list, clear_nosave_free);

	INIT_LIST_HEAD(&bm->zones);
}

/**
 *	memory_bm_find_bit - Find the bit for pfn in the memory
 *			     bitmap
 *
 *	Find the bit in the bitmap @bm that corresponds to given pfn.
 *	The cur.zone, cur.block and cur.node_pfn member of @bm are
 *	updated.
 *	It walks the radix tree to find the page which contains the bit for
 *	pfn and returns the bit position in **addr and *bit_nr.
 */
static int memory_bm_find_bit(struct memory_bitmap *bm, unsigned long pfn,
			      void **addr, unsigned int *bit_nr)
{
	struct mem_zone_bm_rtree *curr, *zone;
	struct rtree_node *node;
	int i, block_nr;

	zone = bm->cur.zone;

	if (pfn >= zone->start_pfn && pfn < zone->end_pfn)
		goto zone_found;

	zone = NULL;

	/* Find the right zone */
	list_for_each_entry(curr, &bm->zones, list) {
		if (pfn >= curr->start_pfn && pfn < curr->end_pfn) {
			zone = curr;
			break;
		}
	}

	if (!zone)
		return -EFAULT;

zone_found:
	/*
	 * We have a zone. Now walk the radix tree to find the leave
	 * node for our pfn.
	 */

	node = bm->cur.node;
	if (((pfn - zone->start_pfn) & ~BM_BLOCK_MASK) == bm->cur.node_pfn)
		goto node_found;

	node      = zone->rtree;
	block_nr  = (pfn - zone->start_pfn) >> BM_BLOCK_SHIFT;

	for (i = zone->levels; i > 0; i--) {
		int index;

		index = block_nr >> ((i - 1) * BM_RTREE_LEVEL_SHIFT);
		index &= BM_RTREE_LEVEL_MASK;
		BUG_ON(node->data[index] == 0);
		node = (struct rtree_node *)node->data[index];
	}

node_found:
	/* Update last position */
	bm->cur.zone = zone;
	bm->cur.node = node;
	bm->cur.node_pfn = (pfn - zone->start_pfn) & ~BM_BLOCK_MASK;

	/* Set return values */
	*addr = node->data;
	*bit_nr = (pfn - zone->start_pfn) & BM_BLOCK_MASK;

	return 0;
}

static void memory_bm_set_bit(struct memory_bitmap *bm, unsigned long pfn)
{
	void *addr;
	unsigned int bit;
	int error;

	error = memory_bm_find_bit(bm, pfn, &addr, &bit);
	BUG_ON(error);
	set_bit(bit, addr);
}

static int mem_bm_set_bit_check(struct memory_bitmap *bm, unsigned long pfn)
{
	void *addr;
	unsigned int bit;
	int error;

	error = memory_bm_find_bit(bm, pfn, &addr, &bit);
	if (!error)
		set_bit(bit, addr);

	return error;
}

static void memory_bm_clear_bit(struct memory_bitmap *bm, unsigned long pfn)
{
	void *addr;
	unsigned int bit;
	int error;

	error = memory_bm_find_bit(bm, pfn, &addr, &bit);
	BUG_ON(error);
	clear_bit(bit, addr);
}

static void memory_bm_clear_current(struct memory_bitmap *bm)
{
	int bit;

	bit = max(bm->cur.node_bit - 1, 0);
	clear_bit(bit, bm->cur.node->data);
}

static int memory_bm_test_bit(struct memory_bitmap *bm, unsigned long pfn)
{
	void *addr;
	unsigned int bit;
	int error;

	error = memory_bm_find_bit(bm, pfn, &addr, &bit);
	BUG_ON(error);
	return test_bit(bit, addr);
}

static bool memory_bm_pfn_present(struct memory_bitmap *bm, unsigned long pfn)
{
	void *addr;
	unsigned int bit;

	return !memory_bm_find_bit(bm, pfn, &addr, &bit);
}

/*
 *	rtree_next_node - Jumps to the next leave node
 *
 *	Sets the position to the beginning of the next node in the
 *	memory bitmap. This is either the next node in the current
 *	zone's radix tree or the first node in the radix tree of the
 *	next zone.
 *
 *	Returns true if there is a next node, false otherwise.
 */
static bool rtree_next_node(struct memory_bitmap *bm)
{
	bm->cur.node = list_entry(bm->cur.node->list.next,
				  struct rtree_node, list);
	if (&bm->cur.node->list != &bm->cur.zone->leaves) {
		bm->cur.node_pfn += BM_BITS_PER_BLOCK;
		bm->cur.node_bit  = 0;
		touch_softlockup_watchdog();
		return true;
	}

	/* No more nodes, goto next zone */
	bm->cur.zone = list_entry(bm->cur.zone->list.next,
				  struct mem_zone_bm_rtree, list);
	if (&bm->cur.zone->list != &bm->zones) {
		bm->cur.node = list_entry(bm->cur.zone->leaves.next,
					  struct rtree_node, list);
		bm->cur.node_pfn = 0;
		bm->cur.node_bit = 0;
		return true;
	}

	/* No more zones */
	return false;
}

/**
 *	memory_bm_rtree_next_pfn - Find the next set bit in the bitmap @bm
 *
 *	Starting from the last returned position this function searches
 *	for the next set bit in the memory bitmap and returns its
 *	number. If no more bit is set BM_END_OF_MAP is returned.
 *
 *	It is required to run memory_bm_position_reset() before the
 *	first call to this function.
 */
static unsigned long memory_bm_next_pfn(struct memory_bitmap *bm)
{
	unsigned long bits, pfn, pages;
	int bit;

	do {
		pages	  = bm->cur.zone->end_pfn - bm->cur.zone->start_pfn;
		bits      = min(pages - bm->cur.node_pfn, BM_BITS_PER_BLOCK);
		bit	  = find_next_bit(bm->cur.node->data, bits,
					  bm->cur.node_bit);
		if (bit < bits) {
			pfn = bm->cur.zone->start_pfn + bm->cur.node_pfn + bit;
			bm->cur.node_bit = bit + 1;
			return pfn;
		}
	} while (rtree_next_node(bm));

	return BM_END_OF_MAP;
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
		region = memblock_virt_alloc(sizeof(struct nosave_region), 0);
	region->start_pfn = start_pfn;
	region->end_pfn = end_pfn;
	list_add_tail(&region->list, &nosave_regions);
 Report:
	printk(KERN_INFO "PM: Registered nosave memory: [mem %#010llx-%#010llx]\n",
		(unsigned long long) start_pfn << PAGE_SHIFT,
		((unsigned long long) end_pfn << PAGE_SHIFT) - 1);
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

		pr_debug("PM: Marking nosave pages: [mem %#010llx-%#010llx]\n",
			 (unsigned long long) region->start_pfn << PAGE_SHIFT,
			 ((unsigned long long) region->end_pfn << PAGE_SHIFT)
				- 1);

		for (pfn = region->start_pfn; pfn < region->end_pfn; pfn++)
			if (pfn_valid(pfn)) {
				/*
				 * It is safe to ignore the result of
				 * mem_bm_set_bit_check() here, since we won't
				 * touch the PFNs for which the error is
				 * returned anyway.
				 */
				mem_bm_set_bit_check(bm, pfn);
			}
	}
}

static bool is_nosave_page(unsigned long pfn)
{
	struct nosave_region *region;

	list_for_each_entry(region, &nosave_regions, list) {
		if (pfn >= region->start_pfn && pfn < region->end_pfn) {
			pr_err("PM: %#010llx in e820 nosave region: "
			       "[mem %#010llx-%#010llx]\n",
			       (unsigned long long) pfn << PAGE_SHIFT,
			       (unsigned long long) region->start_pfn << PAGE_SHIFT,
			       ((unsigned long long) region->end_pfn << PAGE_SHIFT)
					- 1);
			return true;
		}
	}

	return false;
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

	if (forbidden_pages_map && free_pages_map)
		return 0;
	else
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

	pr_debug("PM: Basic memory bitmaps created\n");

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

	if (WARN_ON(!(forbidden_pages_map && free_pages_map)))
		return;

	bm1 = forbidden_pages_map;
	bm2 = free_pages_map;
	forbidden_pages_map = NULL;
	free_pages_map = NULL;
	memory_bm_free(bm1, PG_UNSAFE_CLEAR);
	kfree(bm1);
	memory_bm_free(bm2, PG_UNSAFE_CLEAR);
	kfree(bm2);

	pr_debug("PM: Basic memory bitmaps freed\n");
}

/**
 *	snapshot_additional_pages - estimate the number of additional pages
 *	be needed for setting up the suspend image data structures for given
 *	zone (usually the returned value is greater than the exact number)
 */

unsigned int snapshot_additional_pages(struct zone *zone)
{
	unsigned int rtree, nodes;

	rtree = nodes = DIV_ROUND_UP(zone->spanned_pages, BM_BITS_PER_BLOCK);
	rtree += DIV_ROUND_UP(rtree * sizeof(struct rtree_node),
			      LINKED_PAGE_DATA_SIZE);
	while (nodes > 1) {
		nodes = DIV_ROUND_UP(nodes, BM_ENTRIES_PER_LEVEL);
		rtree += nodes;
	}

	return 2 * rtree;
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

	for_each_populated_zone(zone)
		if (is_highmem(zone))
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
static struct page *saveable_highmem_page(struct zone *zone, unsigned long pfn)
{
	struct page *page;

	if (!pfn_valid(pfn))
		return NULL;

	page = pfn_to_page(pfn);
	if (page_zone(page) != zone)
		return NULL;

	BUG_ON(!PageHighMem(page));

	if (swsusp_page_is_forbidden(page) ||  swsusp_page_is_free(page) ||
	    PageReserved(page))
		return NULL;

	if (page_is_guard(page))
		return NULL;

	return page;
}

/**
 *	count_highmem_pages - compute the total number of saveable highmem
 *	pages.
 */

static unsigned int count_highmem_pages(void)
{
	struct zone *zone;
	unsigned int n = 0;

	for_each_populated_zone(zone) {
		unsigned long pfn, max_zone_pfn;

		if (!is_highmem(zone))
			continue;

		mark_free_pages(zone);
		max_zone_pfn = zone_end_pfn(zone);
		for (pfn = zone->zone_start_pfn; pfn < max_zone_pfn; pfn++)
			if (saveable_highmem_page(zone, pfn))
				n++;
	}
	return n;
}
#else
static inline void *saveable_highmem_page(struct zone *z, unsigned long p)
{
	return NULL;
}
#endif /* CONFIG_HIGHMEM */

/**
 *	saveable_page - Determine whether a non-highmem page should be included
 *	in the suspend image.
 *
 *	We should save the page if it isn't Nosave, and is not in the range
 *	of pages statically defined as 'unsaveable', and it isn't a part of
 *	a free chunk of pages.
 */
static struct page *saveable_page(struct zone *zone, unsigned long pfn)
{
	struct page *page;

	if (!pfn_valid(pfn))
		return NULL;

	page = pfn_to_page(pfn);
	if (page_zone(page) != zone)
		return NULL;

	BUG_ON(PageHighMem(page));

	if (swsusp_page_is_forbidden(page) || swsusp_page_is_free(page))
		return NULL;

	if (PageReserved(page)
	    && (!kernel_page_present(page) || pfn_is_nosave(pfn)))
		return NULL;

	if (page_is_guard(page))
		return NULL;

	return page;
}

/**
 *	count_data_pages - compute the total number of saveable non-highmem
 *	pages.
 */

static unsigned int count_data_pages(void)
{
	struct zone *zone;
	unsigned long pfn, max_zone_pfn;
	unsigned int n = 0;

	for_each_populated_zone(zone) {
		if (is_highmem(zone))
			continue;

		mark_free_pages(zone);
		max_zone_pfn = zone_end_pfn(zone);
		for (pfn = zone->zone_start_pfn; pfn < max_zone_pfn; pfn++)
			if (saveable_page(zone, pfn))
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


/**
 *	safe_copy_page - check if the page we are going to copy is marked as
 *		present in the kernel page tables (this always is the case if
 *		CONFIG_DEBUG_PAGEALLOC is not set and in that case
 *		kernel_page_present() always returns 'true').
 */
static void safe_copy_page(void *dst, struct page *s_page)
{
	if (kernel_page_present(s_page)) {
		do_copy_page(dst, page_address(s_page));
	} else {
		kernel_map_pages(s_page, 1, 1);
		do_copy_page(dst, page_address(s_page));
		kernel_map_pages(s_page, 1, 0);
	}
}


#ifdef CONFIG_HIGHMEM
static inline struct page *
page_is_saveable(struct zone *zone, unsigned long pfn)
{
	return is_highmem(zone) ?
		saveable_highmem_page(zone, pfn) : saveable_page(zone, pfn);
}

static void copy_data_page(unsigned long dst_pfn, unsigned long src_pfn)
{
	struct page *s_page, *d_page;
	void *src, *dst;

	s_page = pfn_to_page(src_pfn);
	d_page = pfn_to_page(dst_pfn);
	if (PageHighMem(s_page)) {
		src = kmap_atomic(s_page);
		dst = kmap_atomic(d_page);
		do_copy_page(dst, src);
		kunmap_atomic(dst);
		kunmap_atomic(src);
	} else {
		if (PageHighMem(d_page)) {
			/* Page pointed to by src may contain some kernel
			 * data modified by kmap_atomic()
			 */
			safe_copy_page(buffer, s_page);
			dst = kmap_atomic(d_page);
			copy_page(dst, buffer);
			kunmap_atomic(dst);
		} else {
			safe_copy_page(page_address(d_page), s_page);
		}
	}
}
#else
#define page_is_saveable(zone, pfn)	saveable_page(zone, pfn)

static inline void copy_data_page(unsigned long dst_pfn, unsigned long src_pfn)
{
	safe_copy_page(page_address(pfn_to_page(dst_pfn)),
				pfn_to_page(src_pfn));
}
#endif /* CONFIG_HIGHMEM */

static void
copy_data_pages(struct memory_bitmap *copy_bm, struct memory_bitmap *orig_bm)
{
	struct zone *zone;
	unsigned long pfn;

	for_each_populated_zone(zone) {
		unsigned long max_zone_pfn;

		mark_free_pages(zone);
		max_zone_pfn = zone_end_pfn(zone);
		for (pfn = zone->zone_start_pfn; pfn < max_zone_pfn; pfn++)
			if (page_is_saveable(zone, pfn))
				memory_bm_set_bit(orig_bm, pfn);
	}
	memory_bm_position_reset(orig_bm);
	memory_bm_position_reset(copy_bm);
	for(;;) {
		pfn = memory_bm_next_pfn(orig_bm);
		if (unlikely(pfn == BM_END_OF_MAP))
			break;
		copy_data_page(memory_bm_next_pfn(copy_bm), pfn);
	}
}

/* Total number of image pages */
static unsigned int nr_copy_pages;
/* Number of pages needed for saving the original pfns of the image pages */
static unsigned int nr_meta_pages;
/*
 * Numbers of normal and highmem page frames allocated for hibernation image
 * before suspending devices.
 */
unsigned int alloc_normal, alloc_highmem;
/*
 * Memory bitmap used for marking saveable pages (during hibernation) or
 * hibernation image pages (during restore)
 */
static struct memory_bitmap orig_bm;
/*
 * Memory bitmap used during hibernation for marking allocated page frames that
 * will contain copies of saveable pages.  During restore it is initially used
 * for marking hibernation image pages, but then the set bits from it are
 * duplicated in @orig_bm and it is released.  On highmem systems it is next
 * used for marking "safe" highmem pages, but it has to be reinitialized for
 * this purpose.
 */
static struct memory_bitmap copy_bm;

/**
 *	swsusp_free - free pages allocated for the suspend.
 *
 *	Suspend pages are alocated before the atomic copy is made, so we
 *	need to release them after the resume.
 */

void swsusp_free(void)
{
	unsigned long fb_pfn, fr_pfn;

	if (!forbidden_pages_map || !free_pages_map)
		goto out;

	memory_bm_position_reset(forbidden_pages_map);
	memory_bm_position_reset(free_pages_map);

loop:
	fr_pfn = memory_bm_next_pfn(free_pages_map);
	fb_pfn = memory_bm_next_pfn(forbidden_pages_map);

	/*
	 * Find the next bit set in both bitmaps. This is guaranteed to
	 * terminate when fb_pfn == fr_pfn == BM_END_OF_MAP.
	 */
	do {
		if (fb_pfn < fr_pfn)
			fb_pfn = memory_bm_next_pfn(forbidden_pages_map);
		if (fr_pfn < fb_pfn)
			fr_pfn = memory_bm_next_pfn(free_pages_map);
	} while (fb_pfn != fr_pfn);

	if (fr_pfn != BM_END_OF_MAP && pfn_valid(fr_pfn)) {
		struct page *page = pfn_to_page(fr_pfn);

		memory_bm_clear_current(forbidden_pages_map);
		memory_bm_clear_current(free_pages_map);
		__free_page(page);
		goto loop;
	}

out:
	nr_copy_pages = 0;
	nr_meta_pages = 0;
	restore_pblist = NULL;
	buffer = NULL;
	alloc_normal = 0;
	alloc_highmem = 0;
}

/* Helper functions used for the shrinking of memory. */

#define GFP_IMAGE	(GFP_KERNEL | __GFP_NOWARN)

/**
 * preallocate_image_pages - Allocate a number of pages for hibernation image
 * @nr_pages: Number of page frames to allocate.
 * @mask: GFP flags to use for the allocation.
 *
 * Return value: Number of page frames actually allocated
 */
static unsigned long preallocate_image_pages(unsigned long nr_pages, gfp_t mask)
{
	unsigned long nr_alloc = 0;

	while (nr_pages > 0) {
		struct page *page;

		page = alloc_image_page(mask);
		if (!page)
			break;
		memory_bm_set_bit(&copy_bm, page_to_pfn(page));
		if (PageHighMem(page))
			alloc_highmem++;
		else
			alloc_normal++;
		nr_pages--;
		nr_alloc++;
	}

	return nr_alloc;
}

static unsigned long preallocate_image_memory(unsigned long nr_pages,
					      unsigned long avail_normal)
{
	unsigned long alloc;

	if (avail_normal <= alloc_normal)
		return 0;

	alloc = avail_normal - alloc_normal;
	if (nr_pages < alloc)
		alloc = nr_pages;

	return preallocate_image_pages(alloc, GFP_IMAGE);
}

#ifdef CONFIG_HIGHMEM
static unsigned long preallocate_image_highmem(unsigned long nr_pages)
{
	return preallocate_image_pages(nr_pages, GFP_IMAGE | __GFP_HIGHMEM);
}

/**
 *  __fraction - Compute (an approximation of) x * (multiplier / base)
 */
static unsigned long __fraction(u64 x, u64 multiplier, u64 base)
{
	x *= multiplier;
	do_div(x, base);
	return (unsigned long)x;
}

static unsigned long preallocate_highmem_fraction(unsigned long nr_pages,
						unsigned long highmem,
						unsigned long total)
{
	unsigned long alloc = __fraction(nr_pages, highmem, total);

	return preallocate_image_pages(alloc, GFP_IMAGE | __GFP_HIGHMEM);
}
#else /* CONFIG_HIGHMEM */
static inline unsigned long preallocate_image_highmem(unsigned long nr_pages)
{
	return 0;
}

static inline unsigned long preallocate_highmem_fraction(unsigned long nr_pages,
						unsigned long highmem,
						unsigned long total)
{
	return 0;
}
#endif /* CONFIG_HIGHMEM */

/**
 * free_unnecessary_pages - Release preallocated pages not needed for the image
 */
static unsigned long free_unnecessary_pages(void)
{
	unsigned long save, to_free_normal, to_free_highmem, free;

	save = count_data_pages();
	if (alloc_normal >= save) {
		to_free_normal = alloc_normal - save;
		save = 0;
	} else {
		to_free_normal = 0;
		save -= alloc_normal;
	}
	save += count_highmem_pages();
	if (alloc_highmem >= save) {
		to_free_highmem = alloc_highmem - save;
	} else {
		to_free_highmem = 0;
		save -= alloc_highmem;
		if (to_free_normal > save)
			to_free_normal -= save;
		else
			to_free_normal = 0;
	}
	free = to_free_normal + to_free_highmem;

	memory_bm_position_reset(&copy_bm);

	while (to_free_normal > 0 || to_free_highmem > 0) {
		unsigned long pfn = memory_bm_next_pfn(&copy_bm);
		struct page *page = pfn_to_page(pfn);

		if (PageHighMem(page)) {
			if (!to_free_highmem)
				continue;
			to_free_highmem--;
			alloc_highmem--;
		} else {
			if (!to_free_normal)
				continue;
			to_free_normal--;
			alloc_normal--;
		}
		memory_bm_clear_bit(&copy_bm, pfn);
		swsusp_unset_page_forbidden(page);
		swsusp_unset_page_free(page);
		__free_page(page);
	}

	return free;
}

/**
 * minimum_image_size - Estimate the minimum acceptable size of an image
 * @saveable: Number of saveable pages in the system.
 *
 * We want to avoid attempting to free too much memory too hard, so estimate the
 * minimum acceptable size of a hibernation image to use as the lower limit for
 * preallocating memory.
 *
 * We assume that the minimum image size should be proportional to
 *
 * [number of saveable pages] - [number of pages that can be freed in theory]
 *
 * where the second term is the sum of (1) reclaimable slab pages, (2) active
 * and (3) inactive anonymous pages, (4) active and (5) inactive file pages,
 * minus mapped file pages.
 */
static unsigned long minimum_image_size(unsigned long saveable)
{
	unsigned long size;

	size = global_page_state(NR_SLAB_RECLAIMABLE)
		+ global_page_state(NR_ACTIVE_ANON)
		+ global_page_state(NR_INACTIVE_ANON)
		+ global_page_state(NR_ACTIVE_FILE)
		+ global_page_state(NR_INACTIVE_FILE)
		- global_page_state(NR_FILE_MAPPED);

	return saveable <= size ? 0 : saveable - size;
}

/**
 * hibernate_preallocate_memory - Preallocate memory for hibernation image
 *
 * To create a hibernation image it is necessary to make a copy of every page
 * frame in use.  We also need a number of page frames to be free during
 * hibernation for allocations made while saving the image and for device
 * drivers, in case they need to allocate memory from their hibernation
 * callbacks (these two numbers are given by PAGES_FOR_IO (which is a rough
 * estimate) and reserverd_size divided by PAGE_SIZE (which is tunable through
 * /sys/power/reserved_size, respectively).  To make this happen, we compute the
 * total number of available page frames and allocate at least
 *
 * ([page frames total] + PAGES_FOR_IO + [metadata pages]) / 2
 *  + 2 * DIV_ROUND_UP(reserved_size, PAGE_SIZE)
 *
 * of them, which corresponds to the maximum size of a hibernation image.
 *
 * If image_size is set below the number following from the above formula,
 * the preallocation of memory is continued until the total number of saveable
 * pages in the system is below the requested image size or the minimum
 * acceptable image size returned by minimum_image_size(), whichever is greater.
 */
int hibernate_preallocate_memory(void)
{
	struct zone *zone;
	unsigned long saveable, size, max_size, count, highmem, pages = 0;
	unsigned long alloc, save_highmem, pages_highmem, avail_normal;
	ktime_t start, stop;
	int error;

	printk(KERN_INFO "PM: Preallocating image memory... ");
	start = ktime_get();

	error = memory_bm_create(&orig_bm, GFP_IMAGE, PG_ANY);
	if (error)
		goto err_out;

	error = memory_bm_create(&copy_bm, GFP_IMAGE, PG_ANY);
	if (error)
		goto err_out;

	alloc_normal = 0;
	alloc_highmem = 0;

	/* Count the number of saveable data pages. */
	save_highmem = count_highmem_pages();
	saveable = count_data_pages();

	/*
	 * Compute the total number of page frames we can use (count) and the
	 * number of pages needed for image metadata (size).
	 */
	count = saveable;
	saveable += save_highmem;
	highmem = save_highmem;
	size = 0;
	for_each_populated_zone(zone) {
		size += snapshot_additional_pages(zone);
		if (is_highmem(zone))
			highmem += zone_page_state(zone, NR_FREE_PAGES);
		else
			count += zone_page_state(zone, NR_FREE_PAGES);
	}
	avail_normal = count;
	count += highmem;
	count -= totalreserve_pages;

	/* Add number of pages required for page keys (s390 only). */
	size += page_key_additional_pages(saveable);

	/* Compute the maximum number of saveable pages to leave in memory. */
	max_size = (count - (size + PAGES_FOR_IO)) / 2
			- 2 * DIV_ROUND_UP(reserved_size, PAGE_SIZE);
	/* Compute the desired number of image pages specified by image_size. */
	size = DIV_ROUND_UP(image_size, PAGE_SIZE);
	if (size > max_size)
		size = max_size;
	/*
	 * If the desired number of image pages is at least as large as the
	 * current number of saveable pages in memory, allocate page frames for
	 * the image and we're done.
	 */
	if (size >= saveable) {
		pages = preallocate_image_highmem(save_highmem);
		pages += preallocate_image_memory(saveable - pages, avail_normal);
		goto out;
	}

	/* Estimate the minimum size of the image. */
	pages = minimum_image_size(saveable);
	/*
	 * To avoid excessive pressure on the normal zone, leave room in it to
	 * accommodate an image of the minimum size (unless it's already too
	 * small, in which case don't preallocate pages from it at all).
	 */
	if (avail_normal > pages)
		avail_normal -= pages;
	else
		avail_normal = 0;
	if (size < pages)
		size = min_t(unsigned long, pages, max_size);

	/*
	 * Let the memory management subsystem know that we're going to need a
	 * large number of page frames to allocate and make it free some memory.
	 * NOTE: If this is not done, performance will be hurt badly in some
	 * test cases.
	 */
	shrink_all_memory(saveable - size);

	/*
	 * The number of saveable pages in memory was too high, so apply some
	 * pressure to decrease it.  First, make room for the largest possible
	 * image and fail if that doesn't work.  Next, try to decrease the size
	 * of the image as much as indicated by 'size' using allocations from
	 * highmem and non-highmem zones separately.
	 */
	pages_highmem = preallocate_image_highmem(highmem / 2);
	alloc = count - max_size;
	if (alloc > pages_highmem)
		alloc -= pages_highmem;
	else
		alloc = 0;
	pages = preallocate_image_memory(alloc, avail_normal);
	if (pages < alloc) {
		/* We have exhausted non-highmem pages, try highmem. */
		alloc -= pages;
		pages += pages_highmem;
		pages_highmem = preallocate_image_highmem(alloc);
		if (pages_highmem < alloc)
			goto err_out;
		pages += pages_highmem;
		/*
		 * size is the desired number of saveable pages to leave in
		 * memory, so try to preallocate (all memory - size) pages.
		 */
		alloc = (count - pages) - size;
		pages += preallocate_image_highmem(alloc);
	} else {
		/*
		 * There are approximately max_size saveable pages at this point
		 * and we want to reduce this number down to size.
		 */
		alloc = max_size - size;
		size = preallocate_highmem_fraction(alloc, highmem, count);
		pages_highmem += size;
		alloc -= size;
		size = preallocate_image_memory(alloc, avail_normal);
		pages_highmem += preallocate_image_highmem(alloc - size);
		pages += pages_highmem + size;
	}

	/*
	 * We only need as many page frames for the image as there are saveable
	 * pages in memory, but we have allocated more.  Release the excessive
	 * ones now.
	 */
	pages -= free_unnecessary_pages();

 out:
	stop = ktime_get();
	printk(KERN_CONT "done (allocated %lu pages)\n", pages);
	swsusp_show_speed(start, stop, pages, "Allocated");

	return 0;

 err_out:
	printk(KERN_CONT "\n");
	swsusp_free();
	return -ENOMEM;
}

#ifdef CONFIG_HIGHMEM
/**
  *	count_pages_for_highmem - compute the number of non-highmem pages
  *	that will be necessary for creating copies of highmem pages.
  */

static unsigned int count_pages_for_highmem(unsigned int nr_highmem)
{
	unsigned int free_highmem = count_free_highmem_pages() + alloc_highmem;

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
	unsigned int free = alloc_normal;

	for_each_populated_zone(zone)
		if (!is_highmem(zone))
			free += zone_page_state(zone, NR_FREE_PAGES);

	nr_pages += count_pages_for_highmem(nr_highmem);
	pr_debug("PM: Normal pages needed: %u + %u, available pages: %u\n",
		nr_pages, PAGES_FOR_IO, free);

	return free > nr_pages + PAGES_FOR_IO;
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
alloc_highmem_pages(struct memory_bitmap *bm, unsigned int nr_highmem)
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
alloc_highmem_pages(struct memory_bitmap *bm, unsigned int n) { return 0; }
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
	if (nr_highmem > 0) {
		if (get_highmem_buffer(PG_ANY))
			goto err_out;
		if (nr_highmem > alloc_highmem) {
			nr_highmem -= alloc_highmem;
			nr_pages += alloc_highmem_pages(copy_bm, nr_highmem);
		}
	}
	if (nr_pages > alloc_normal) {
		nr_pages -= alloc_normal;
		while (nr_pages-- > 0) {
			struct page *page;

			page = alloc_image_page(GFP_ATOMIC | __GFP_COLD);
			if (!page)
				goto err_out;
			memory_bm_set_bit(copy_bm, page_to_pfn(page));
		}
	}

	return 0;

 err_out:
	swsusp_free();
	return -ENOMEM;
}

asmlinkage __visible int swsusp_save(void)
{
	unsigned int nr_pages, nr_highmem;

	printk(KERN_INFO "PM: Creating hibernation image:\n");

	drain_local_pages(NULL);
	nr_pages = count_data_pages();
	nr_highmem = count_highmem_pages();
	printk(KERN_INFO "PM: Need to copy %u pages\n", nr_pages + nr_highmem);

	if (!enough_free_mem(nr_pages, nr_highmem)) {
		printk(KERN_ERR "PM: Not enough free memory\n");
		return -ENOMEM;
	}

	if (swsusp_alloc(&orig_bm, &copy_bm, nr_pages, nr_highmem)) {
		printk(KERN_ERR "PM: Memory allocation failed\n");
		return -ENOMEM;
	}

	/* During allocating of suspend pagedir, new cold pages may appear.
	 * Kill them.
	 */
	drain_local_pages(NULL);
	copy_data_pages(&copy_bm, &orig_bm);

	/*
	 * End of critical section. From now on, we can write to memory,
	 * but we should not touch disk. This specially means we must _not_
	 * touch swap space! Except we must write out our image of course.
	 */

	nr_pages += nr_highmem;
	nr_copy_pages = nr_pages;
	nr_meta_pages = DIV_ROUND_UP(nr_pages * sizeof(long), PAGE_SIZE);

	printk(KERN_INFO "PM: Hibernation image created (%d pages copied)\n",
		nr_pages);

	return 0;
}

#ifndef CONFIG_ARCH_HIBERNATION_HEADER
static int init_header_complete(struct swsusp_info *info)
{
	memcpy(&info->uts, init_utsname(), sizeof(struct new_utsname));
	info->version_code = LINUX_VERSION_CODE;
	return 0;
}

static char *check_image_kernel(struct swsusp_info *info)
{
	if (info->version_code != LINUX_VERSION_CODE)
		return "kernel version";
	if (strcmp(info->uts.sysname,init_utsname()->sysname))
		return "system type";
	if (strcmp(info->uts.release,init_utsname()->release))
		return "kernel release";
	if (strcmp(info->uts.version,init_utsname()->version))
		return "version";
	if (strcmp(info->uts.machine,init_utsname()->machine))
		return "machine";
	return NULL;
}
#endif /* CONFIG_ARCH_HIBERNATION_HEADER */

unsigned long snapshot_get_image_size(void)
{
	return nr_copy_pages + nr_meta_pages + 1;
}

static int init_header(struct swsusp_info *info)
{
	memset(info, 0, sizeof(struct swsusp_info));
	info->num_physpages = get_num_physpages();
	info->image_pages = nr_copy_pages;
	info->pages = snapshot_get_image_size();
	info->size = info->pages;
	info->size <<= PAGE_SHIFT;
	return init_header_complete(info);
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
		/* Save page key for data page (s390 only). */
		page_key_read(buf + j);
	}
}

/**
 *	snapshot_read_next - used for reading the system memory snapshot.
 *
 *	On the first call to it @handle should point to a zeroed
 *	snapshot_handle structure.  The structure gets updated and a pointer
 *	to it should be passed to this function every next time.
 *
 *	On success the function returns a positive number.  Then, the caller
 *	is allowed to read up to the returned number of bytes from the memory
 *	location computed by the data_of() macro.
 *
 *	The function returns 0 to indicate the end of data stream condition,
 *	and a negative number is returned on error.  In such cases the
 *	structure pointed to by @handle is not updated and should not be used
 *	any more.
 */

int snapshot_read_next(struct snapshot_handle *handle)
{
	if (handle->cur > nr_meta_pages + nr_copy_pages)
		return 0;

	if (!buffer) {
		/* This makes the buffer be freed by swsusp_free() */
		buffer = get_image_page(GFP_ATOMIC, PG_ANY);
		if (!buffer)
			return -ENOMEM;
	}
	if (!handle->cur) {
		int error;

		error = init_header((struct swsusp_info *)buffer);
		if (error)
			return error;
		handle->buffer = buffer;
		memory_bm_position_reset(&orig_bm);
		memory_bm_position_reset(&copy_bm);
	} else if (handle->cur <= nr_meta_pages) {
		clear_page(buffer);
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

			kaddr = kmap_atomic(page);
			copy_page(buffer, kaddr);
			kunmap_atomic(kaddr);
			handle->buffer = buffer;
		} else {
			handle->buffer = page_address(page);
		}
	}
	handle->cur++;
	return PAGE_SIZE;
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
	for_each_populated_zone(zone) {
		max_zone_pfn = zone_end_pfn(zone);
		for (pfn = zone->zone_start_pfn; pfn < max_zone_pfn; pfn++)
			if (pfn_valid(pfn))
				swsusp_unset_page_free(pfn_to_page(pfn));
	}

	/* Mark pages that correspond to the "original" pfns as "unsafe" */
	memory_bm_position_reset(bm);
	do {
		pfn = memory_bm_next_pfn(bm);
		if (likely(pfn != BM_END_OF_MAP)) {
			if (likely(pfn_valid(pfn)) && !is_nosave_page(pfn))
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

static int check_header(struct swsusp_info *info)
{
	char *reason;

	reason = check_image_kernel(info);
	if (!reason && info->num_physpages != get_num_physpages())
		reason = "memory size";
	if (reason) {
		printk(KERN_ERR "PM: Image mismatch: %s\n", reason);
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
static int unpack_orig_pfns(unsigned long *buf, struct memory_bitmap *bm)
{
	int j;

	for (j = 0; j < PAGE_SIZE / sizeof(long); j++) {
		if (unlikely(buf[j] == BM_END_OF_MAP))
			break;

		/* Extract and buffer page key for data page (s390 only). */
		page_key_memorize(buf + j);

		if (memory_bm_pfn_present(bm, buf[j]))
			memory_bm_set_bit(bm, buf[j]);
		else
			return -EFAULT;
	}

	return 0;
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
		return ERR_PTR(-ENOMEM);
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

		dst = kmap_atomic(last_highmem_page);
		copy_page(dst, buffer);
		kunmap_atomic(dst);
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
	return ERR_PTR(-EINVAL);
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
	struct page *page;
	unsigned long pfn = memory_bm_next_pfn(bm);

	if (pfn == BM_END_OF_MAP)
		return ERR_PTR(-EFAULT);

	page = pfn_to_page(pfn);
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
		return ERR_PTR(-ENOMEM);
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
 *	On success the function returns a positive number.  Then, the caller
 *	is allowed to write up to the returned number of bytes to the memory
 *	location computed by the data_of() macro.
 *
 *	The function returns 0 to indicate the "end of file" condition,
 *	and a negative number is returned on error.  In such cases the
 *	structure pointed to by @handle is not updated and should not be used
 *	any more.
 */

int snapshot_write_next(struct snapshot_handle *handle)
{
	static struct chain_allocator ca;
	int error = 0;

	/* Check if we have already loaded the entire image */
	if (handle->cur > 1 && handle->cur > nr_meta_pages + nr_copy_pages)
		return 0;

	handle->sync_read = 1;

	if (!handle->cur) {
		if (!buffer)
			/* This makes the buffer be freed by swsusp_free() */
			buffer = get_image_page(GFP_ATOMIC, PG_ANY);

		if (!buffer)
			return -ENOMEM;

		handle->buffer = buffer;
	} else if (handle->cur == 1) {
		error = load_header(buffer);
		if (error)
			return error;

		error = memory_bm_create(&copy_bm, GFP_ATOMIC, PG_ANY);
		if (error)
			return error;

		/* Allocate buffer for page keys. */
		error = page_key_alloc(nr_copy_pages);
		if (error)
			return error;

	} else if (handle->cur <= nr_meta_pages + 1) {
		error = unpack_orig_pfns(buffer, &copy_bm);
		if (error)
			return error;

		if (handle->cur == nr_meta_pages + 1) {
			error = prepare_image(&orig_bm, &copy_bm);
			if (error)
				return error;

			chain_init(&ca, GFP_ATOMIC, PG_SAFE);
			memory_bm_position_reset(&orig_bm);
			restore_pblist = NULL;
			handle->buffer = get_buffer(&orig_bm, &ca);
			handle->sync_read = 0;
			if (IS_ERR(handle->buffer))
				return PTR_ERR(handle->buffer);
		}
	} else {
		copy_last_highmem_page();
		/* Restore page key for data page (s390 only). */
		page_key_write(handle->buffer);
		handle->buffer = get_buffer(&orig_bm, &ca);
		if (IS_ERR(handle->buffer))
			return PTR_ERR(handle->buffer);
		if (handle->buffer != buffer)
			handle->sync_read = 0;
	}
	handle->cur++;
	return PAGE_SIZE;
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
	/* Restore page key for data page (s390 only). */
	page_key_write(handle->buffer);
	page_key_free();
	/* Free only if we have loaded the image entirely */
	if (handle->cur > 1 && handle->cur > nr_meta_pages + nr_copy_pages) {
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

	kaddr1 = kmap_atomic(p1);
	kaddr2 = kmap_atomic(p2);
	copy_page(buf, kaddr1);
	copy_page(kaddr1, kaddr2);
	copy_page(kaddr2, buf);
	kunmap_atomic(kaddr2);
	kunmap_atomic(kaddr1);
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
