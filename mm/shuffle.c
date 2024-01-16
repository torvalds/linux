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

static bool shuffle_param;

static __meminit int shuffle_param_set(const char *val,
		const struct kernel_param *kp)
{
	if (param_set_bool(val, kp))
		return -EINVAL;
	if (*(bool *)kp->arg)
		static_branch_enable(&page_alloc_shuffle_key);
	return 0;
}

static const struct kernel_param_ops shuffle_param_ops = {
	.set = shuffle_param_set,
	.get = param_get_bool,
};
module_param_cb(shuffle, &shuffle_param_ops, &shuffle_param, 0400);

/*
 * For two pages to be swapped in the shuffle, they must be free (on a
 * 'free_area' lru), have the same order, and have the same migratetype.
 */
static struct page * __meminit shuffle_valid_page(struct zone *zone,
						  unsigned long pfn, int order)
{
	struct page *page = pfn_to_online_page(pfn);

	/*
	 * Given we're dealing with randomly selected pfns in a zone we
	 * need to ask questions like...
	 */

	/* ... is the page managed by the buddy? */
	if (!page)
		return NULL;

	/* ... is the page assigned to the same zone? */
	if (page_zone(page) != zone)
		return NULL;

	/* ...is the page free and currently on a free_area list? */
	if (!PageBuddy(page))
		return NULL;

	/*
	 * ...is the page on the same list as the page we will
	 * shuffle it with?
	 */
	if (buddy_order(page) != order)
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
		page_i = shuffle_valid_page(z, i, order);
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
			page_j = shuffle_valid_page(z, j, order);
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

/*
 * __shuffle_free_memory - reduce the predictability of the page allocator
 * @pgdat: node page data
 */
void __meminit __shuffle_free_memory(pg_data_t *pgdat)
{
	struct zone *z;

	for (z = pgdat->node_zones; z < pgdat->node_zones + MAX_NR_ZONES; z++)
		shuffle_zone(z);
}

bool shuffle_pick_tail(void)
{
	static u64 rand;
	static u8 rand_bits;
	bool ret;

	/*
	 * The lack of locking is deliberate. If 2 threads race to
	 * update the rand state it just adds to the entropy.
	 */
	if (rand_bits == 0) {
		rand_bits = 64;
		rand = get_random_u64();
	}

	ret = rand & 1;

	rand_bits--;
	rand >>= 1;

	return ret;
}
