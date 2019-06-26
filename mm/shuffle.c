// SPDX-License-Identifier: GPL-2.0
// Copyright(c) 2018 Intel Corporation. All rights reserved.

#include <linux/mm.h>
#include <linux/init.h>
#include <linux/mmzone.h>
#include <linux/random.h>
#include <linux/moduleparam.h>
#include "internal.h"
#include "shuffle.h"

DEFINE_STATIC_KEY_FALSE(page_alloc_shuffle_key);
static unsigned long shuffle_state __ro_after_init;

/*
 * Depending on the architecture, module parameter parsing may run
 * before, or after the cache detection. SHUFFLE_FORCE_DISABLE prevents,
 * or reverts the enabling of the shuffle implementation. SHUFFLE_ENABLE
 * attempts to turn on the implementation, but aborts if it finds
 * SHUFFLE_FORCE_DISABLE already set.
 */
__meminit void page_alloc_shuffle(enum mm_shuffle_ctl ctl)
{
	if (ctl == SHUFFLE_FORCE_DISABLE)
		set_bit(SHUFFLE_FORCE_DISABLE, &shuffle_state);

	if (test_bit(SHUFFLE_FORCE_DISABLE, &shuffle_state)) {
		if (test_and_clear_bit(SHUFFLE_ENABLE, &shuffle_state))
			static_branch_disable(&page_alloc_shuffle_key);
	} else if (ctl == SHUFFLE_ENABLE
			&& !test_and_set_bit(SHUFFLE_ENABLE, &shuffle_state))
		static_branch_enable(&page_alloc_shuffle_key);
}

static bool shuffle_param;
extern int shuffle_show(char *buffer, const struct kernel_param *kp)
{
	return sprintf(buffer, "%c\n", test_bit(SHUFFLE_ENABLE, &shuffle_state)
			? 'Y' : 'N');
}

static __meminit int shuffle_store(const char *val,
		const struct kernel_param *kp)
{
	int rc = param_set_bool(val, kp);

	if (rc < 0)
		return rc;
	if (shuffle_param)
		page_alloc_shuffle(SHUFFLE_ENABLE);
	else
		page_alloc_shuffle(SHUFFLE_FORCE_DISABLE);
	return 0;
}
module_param_call(shuffle, shuffle_store, shuffle_show, &shuffle_param, 0400);

/*
 * For two pages to be swapped in the shuffle, they must be free (on a
 * 'free_area' lru), have the same order, and have the same migratetype.
 */
static struct page * __meminit shuffle_valid_page(unsigned long pfn, int order)
{
	struct page *page;

	/*
	 * Given we're dealing with randomly selected pfns in a zone we
	 * need to ask questions like...
	 */

	/* ...is the pfn even in the memmap? */
	if (!pfn_valid_within(pfn))
		return NULL;

	/* ...is the pfn in a present section or a hole? */
	if (!pfn_present(pfn))
		return NULL;

	/* ...is the page free and currently on a free_area list? */
	page = pfn_to_page(pfn);
	if (!PageBuddy(page))
		return NULL;

	/*
	 * ...is the page on the same list as the page we will
	 * shuffle it with?
	 */
	if (page_order(page) != order)
		return NULL;

	return page;
}

/*
 * Fisher-Yates shuffle the freelist which prescribes iterating through an
 * array, pfns in this case, and randomly swapping each entry with another in
 * the span, end_pfn - start_pfn.
 *
 * To keep the implementation simple it does not attempt to correct for sources
 * of bias in the distribution, like modulo bias or pseudo-random number
 * generator bias. I.e. the expectation is that this shuffling raises the bar
 * for attacks that exploit the predictability of page allocations, but need not
 * be a perfect shuffle.
 */
#define SHUFFLE_RETRY 10
void __meminit __shuffle_zone(struct zone *z)
{
	unsigned long i, flags;
	unsigned long start_pfn = z->zone_start_pfn;
	unsigned long end_pfn = zone_end_pfn(z);
	const int order = SHUFFLE_ORDER;
	const int order_pages = 1 << order;

	spin_lock_irqsave(&z->lock, flags);
	start_pfn = ALIGN(start_pfn, order_pages);
	for (i = start_pfn; i < end_pfn; i += order_pages) {
		unsigned long j;
		int migratetype, retry;
		struct page *page_i, *page_j;

		/*
		 * We expect page_i, in the sub-range of a zone being added
		 * (@start_pfn to @end_pfn), to more likely be valid compared to
		 * page_j randomly selected in the span @zone_start_pfn to
		 * @spanned_pages.
		 */
		page_i = shuffle_valid_page(i, order);
		if (!page_i)
			continue;

		for (retry = 0; retry < SHUFFLE_RETRY; retry++) {
			/*
			 * Pick a random order aligned page in the zone span as
			 * a swap target. If the selected pfn is a hole, retry
			 * up to SHUFFLE_RETRY attempts find a random valid pfn
			 * in the zone.
			 */
			j = z->zone_start_pfn +
				ALIGN_DOWN(get_random_long() % z->spanned_pages,
						order_pages);
			page_j = shuffle_valid_page(j, order);
			if (page_j && page_j != page_i)
				break;
		}
		if (retry >= SHUFFLE_RETRY) {
			pr_debug("%s: failed to swap %#lx\n", __func__, i);
			continue;
		}

		/*
		 * Each migratetype corresponds to its own list, make sure the
		 * types match otherwise we're moving pages to lists where they
		 * do not belong.
		 */
		migratetype = get_pageblock_migratetype(page_i);
		if (get_pageblock_migratetype(page_j) != migratetype) {
			pr_debug("%s: migratetype mismatch %#lx\n", __func__, i);
			continue;
		}

		list_swap(&page_i->lru, &page_j->lru);

		pr_debug("%s: swap: %#lx -> %#lx\n", __func__, i, j);

		/* take it easy on the zone lock */
		if ((i % (100 * order_pages)) == 0) {
			spin_unlock_irqrestore(&z->lock, flags);
			cond_resched();
			spin_lock_irqsave(&z->lock, flags);
		}
	}
	spin_unlock_irqrestore(&z->lock, flags);
}

/**
 * shuffle_free_memory - reduce the predictability of the page allocator
 * @pgdat: node page data
 */
void __meminit __shuffle_free_memory(pg_data_t *pgdat)
{
	struct zone *z;

	for (z = pgdat->node_zones; z < pgdat->node_zones + MAX_NR_ZONES; z++)
		shuffle_zone(z);
}

void add_to_free_area_random(struct page *page, struct free_area *area,
		int migratetype)
{
	static u64 rand;
	static u8 rand_bits;

	/*
	 * The lack of locking is deliberate. If 2 threads race to
	 * update the rand state it just adds to the entropy.
	 */
	if (rand_bits == 0) {
		rand_bits = 64;
		rand = get_random_u64();
	}

	if (rand & 1)
		add_to_free_area(page, area, migratetype);
	else
		add_to_free_area_tail(page, area, migratetype);
	rand_bits--;
	rand >>= 1;
}
