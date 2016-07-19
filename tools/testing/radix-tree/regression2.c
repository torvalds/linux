/*
 * Regression2
 * Description:
 * Toshiyuki Okajima describes the following radix-tree bug:
 *
 * In the following case, we can get a hangup on
 *   radix_radix_tree_gang_lookup_tag_slot.
 *
 * 0.  The radix tree contains RADIX_TREE_MAP_SIZE items. And the tag of
 *     a certain item has PAGECACHE_TAG_DIRTY.
 * 1.  radix_tree_range_tag_if_tagged(, start, end, , PAGECACHE_TAG_DIRTY,
 *     PAGECACHE_TAG_TOWRITE) is called to add PAGECACHE_TAG_TOWRITE tag
 *     for the tag which has PAGECACHE_TAG_DIRTY. However, there is no tag with
 *     PAGECACHE_TAG_DIRTY within the range from start to end. As the result,
 *     There is no tag with PAGECACHE_TAG_TOWRITE but the root tag has
 *     PAGECACHE_TAG_TOWRITE.
 * 2.  An item is added into the radix tree and then the level of it is
 *     extended into 2 from 1. At that time, the new radix tree node succeeds
 *     the tag status of the root tag. Therefore the tag of the new radix tree
 *     node has PAGECACHE_TAG_TOWRITE but there is not slot with
 *     PAGECACHE_TAG_TOWRITE tag in the child node of the new radix tree node.
 * 3.  The tag of a certain item is cleared with PAGECACHE_TAG_DIRTY.
 * 4.  All items within the index range from 0 to RADIX_TREE_MAP_SIZE - 1 are
 *     released. (Only the item which index is RADIX_TREE_MAP_SIZE exist in the
 *     radix tree.) As the result, the slot of the radix tree node is NULL but
 *     the tag which corresponds to the slot has PAGECACHE_TAG_TOWRITE.
 * 5.  radix_tree_gang_lookup_tag_slot(PAGECACHE_TAG_TOWRITE) calls
 *     __lookup_tag. __lookup_tag returns with 0. And __lookup_tag doesn't
 *     change the index that is the input and output parameter. Because the 1st
 *     slot of the radix tree node is NULL, but the tag which corresponds to
 *     the slot has PAGECACHE_TAG_TOWRITE.
 *     Therefore radix_tree_gang_lookup_tag_slot tries to get some items by
 *     calling __lookup_tag, but it cannot get any items forever.
 *
 * The fix is to change that radix_tree_tag_if_tagged doesn't tag the root tag
 * if it doesn't set any tags within the specified range.
 *
 * Running:
 * This test should run to completion immediately. The above bug would cause it
 * to hang indefinitely.
 *
 * Upstream commit:
 * Not yet
 */
#include <linux/kernel.h>
#include <linux/gfp.h>
#include <linux/slab.h>
#include <linux/radix-tree.h>
#include <stdlib.h>
#include <stdio.h>

#include "regression.h"

#ifdef __KERNEL__
#define RADIX_TREE_MAP_SHIFT    (CONFIG_BASE_SMALL ? 4 : 6)
#else
#define RADIX_TREE_MAP_SHIFT    3       /* For more stressful testing */
#endif

#define RADIX_TREE_MAP_SIZE     (1UL << RADIX_TREE_MAP_SHIFT)
#define PAGECACHE_TAG_DIRTY     0
#define PAGECACHE_TAG_WRITEBACK 1
#define PAGECACHE_TAG_TOWRITE   2

static RADIX_TREE(mt_tree, GFP_KERNEL);
unsigned long page_count = 0;

struct page {
	unsigned long index;
};

static struct page *page_alloc(void)
{
	struct page *p;
	p = malloc(sizeof(struct page));
	p->index = page_count++;

	return p;
}

void regression2_test(void)
{
	int i;
	struct page *p;
	int max_slots = RADIX_TREE_MAP_SIZE;
	unsigned long int start, end;
	struct page *pages[1];

	printf("running regression test 2 (should take milliseconds)\n");
	/* 0. */
	for (i = 0; i <= max_slots - 1; i++) {
		p = page_alloc();
		radix_tree_insert(&mt_tree, i, p);
	}
	radix_tree_tag_set(&mt_tree, max_slots - 1, PAGECACHE_TAG_DIRTY);

	/* 1. */
	start = 0;
	end = max_slots - 2;
	radix_tree_range_tag_if_tagged(&mt_tree, &start, end, 1,
				PAGECACHE_TAG_DIRTY, PAGECACHE_TAG_TOWRITE);

	/* 2. */
	p = page_alloc();
	radix_tree_insert(&mt_tree, max_slots, p);

	/* 3. */
	radix_tree_tag_clear(&mt_tree, max_slots - 1, PAGECACHE_TAG_DIRTY);

	/* 4. */
	for (i = max_slots - 1; i >= 0; i--)
		radix_tree_delete(&mt_tree, i);

	/* 5. */
	// NOTE: start should not be 0 because radix_tree_gang_lookup_tag_slot
	//       can return.
	start = 1;
	end = max_slots - 2;
	radix_tree_gang_lookup_tag_slot(&mt_tree, (void ***)pages, start, end,
		PAGECACHE_TAG_TOWRITE);

	/* We remove all the remained nodes */
	radix_tree_delete(&mt_tree, max_slots);

	printf("regression test 2, done\n");
}
