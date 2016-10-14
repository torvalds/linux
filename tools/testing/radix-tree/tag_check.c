#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <linux/slab.h>
#include <linux/radix-tree.h>

#include "test.h"


static void
__simple_checks(struct radix_tree_root *tree, unsigned long index, int tag)
{
	unsigned long first = 0;
	int ret;

	item_check_absent(tree, index);
	assert(item_tag_get(tree, index, tag) == 0);

	item_insert(tree, index);
	assert(item_tag_get(tree, index, tag) == 0);
	item_tag_set(tree, index, tag);
	ret = item_tag_get(tree, index, tag);
	assert(ret != 0);
	ret = radix_tree_range_tag_if_tagged(tree, &first, ~0UL, 10, tag, !tag);
	assert(ret == 1);
	ret = item_tag_get(tree, index, !tag);
	assert(ret != 0);
	ret = item_delete(tree, index);
	assert(ret != 0);
	item_insert(tree, index);
	ret = item_tag_get(tree, index, tag);
	assert(ret == 0);
	ret = item_delete(tree, index);
	assert(ret != 0);
	ret = item_delete(tree, index);
	assert(ret == 0);
}

void simple_checks(void)
{
	unsigned long index;
	RADIX_TREE(tree, GFP_KERNEL);

	for (index = 0; index < 10000; index++) {
		__simple_checks(&tree, index, 0);
		__simple_checks(&tree, index, 1);
	}
	verify_tag_consistency(&tree, 0);
	verify_tag_consistency(&tree, 1);
	printf("before item_kill_tree: %d allocated\n", nr_allocated);
	item_kill_tree(&tree);
	printf("after item_kill_tree: %d allocated\n", nr_allocated);
}

/*
 * Check that tags propagate correctly when extending a tree.
 */
static void extend_checks(void)
{
	RADIX_TREE(tree, GFP_KERNEL);

	item_insert(&tree, 43);
	assert(item_tag_get(&tree, 43, 0) == 0);
	item_tag_set(&tree, 43, 0);
	assert(item_tag_get(&tree, 43, 0) == 1);
	item_insert(&tree, 1000000);
	assert(item_tag_get(&tree, 43, 0) == 1);

	item_insert(&tree, 0);
	item_tag_set(&tree, 0, 0);
	item_delete(&tree, 1000000);
	assert(item_tag_get(&tree, 43, 0) != 0);
	item_delete(&tree, 43);
	assert(item_tag_get(&tree, 43, 0) == 0);	/* crash */
	assert(item_tag_get(&tree, 0, 0) == 1);

	verify_tag_consistency(&tree, 0);

	item_kill_tree(&tree);
}

/*
 * Check that tags propagate correctly when contracting a tree.
 */
static void contract_checks(void)
{
	struct item *item;
	int tmp;
	RADIX_TREE(tree, GFP_KERNEL);

	tmp = 1<<RADIX_TREE_MAP_SHIFT;
	item_insert(&tree, tmp);
	item_insert(&tree, tmp+1);
	item_tag_set(&tree, tmp, 0);
	item_tag_set(&tree, tmp, 1);
	item_tag_set(&tree, tmp+1, 0);
	item_delete(&tree, tmp+1);
	item_tag_clear(&tree, tmp, 1);

	assert(radix_tree_gang_lookup_tag(&tree, (void **)&item, 0, 1, 0) == 1);
	assert(radix_tree_gang_lookup_tag(&tree, (void **)&item, 0, 1, 1) == 0);

	assert(item_tag_get(&tree, tmp, 0) == 1);
	assert(item_tag_get(&tree, tmp, 1) == 0);

	verify_tag_consistency(&tree, 0);
	item_kill_tree(&tree);
}

/*
 * Stupid tag thrasher
 *
 * Create a large linear array corresponding to the tree.   Each element in
 * the array is coherent with each node in the tree
 */

enum {
	NODE_ABSENT = 0,
	NODE_PRESENT = 1,
	NODE_TAGGED = 2,
};

#define THRASH_SIZE		(1000 * 1000)
#define N 127
#define BATCH	33

static void gang_check(struct radix_tree_root *tree,
			char *thrash_state, int tag)
{
	struct item *items[BATCH];
	int nr_found;
	unsigned long index = 0;
	unsigned long last_index = 0;

	while ((nr_found = radix_tree_gang_lookup_tag(tree, (void **)items,
					index, BATCH, tag))) {
		int i;

		for (i = 0; i < nr_found; i++) {
			struct item *item = items[i];

			while (last_index < item->index) {
				assert(thrash_state[last_index] != NODE_TAGGED);
				last_index++;
			}
			assert(thrash_state[last_index] == NODE_TAGGED);
			last_index++;
		}
		index = items[nr_found - 1]->index + 1;
	}
}

static void do_thrash(struct radix_tree_root *tree, char *thrash_state, int tag)
{
	int insert_chunk;
	int delete_chunk;
	int tag_chunk;
	int untag_chunk;
	int total_tagged = 0;
	int total_present = 0;

	for (insert_chunk = 1; insert_chunk < THRASH_SIZE; insert_chunk *= N)
	for (delete_chunk = 1; delete_chunk < THRASH_SIZE; delete_chunk *= N)
	for (tag_chunk = 1; tag_chunk < THRASH_SIZE; tag_chunk *= N)
	for (untag_chunk = 1; untag_chunk < THRASH_SIZE; untag_chunk *= N) {
		int i;
		unsigned long index;
		int nr_inserted = 0;
		int nr_deleted = 0;
		int nr_tagged = 0;
		int nr_untagged = 0;
		int actual_total_tagged;
		int actual_total_present;

		for (i = 0; i < insert_chunk; i++) {
			index = rand() % THRASH_SIZE;
			if (thrash_state[index] != NODE_ABSENT)
				continue;
			item_check_absent(tree, index);
			item_insert(tree, index);
			assert(thrash_state[index] != NODE_PRESENT);
			thrash_state[index] = NODE_PRESENT;
			nr_inserted++;
			total_present++;
		}

		for (i = 0; i < delete_chunk; i++) {
			index = rand() % THRASH_SIZE;
			if (thrash_state[index] == NODE_ABSENT)
				continue;
			item_check_present(tree, index);
			if (item_tag_get(tree, index, tag)) {
				assert(thrash_state[index] == NODE_TAGGED);
				total_tagged--;
			} else {
				assert(thrash_state[index] == NODE_PRESENT);
			}
			item_delete(tree, index);
			assert(thrash_state[index] != NODE_ABSENT);
			thrash_state[index] = NODE_ABSENT;
			nr_deleted++;
			total_present--;
		}

		for (i = 0; i < tag_chunk; i++) {
			index = rand() % THRASH_SIZE;
			if (thrash_state[index] != NODE_PRESENT) {
				if (item_lookup(tree, index))
					assert(item_tag_get(tree, index, tag));
				continue;
			}
			item_tag_set(tree, index, tag);
			item_tag_set(tree, index, tag);
			assert(thrash_state[index] != NODE_TAGGED);
			thrash_state[index] = NODE_TAGGED;
			nr_tagged++;
			total_tagged++;
		}

		for (i = 0; i < untag_chunk; i++) {
			index = rand() % THRASH_SIZE;
			if (thrash_state[index] != NODE_TAGGED)
				continue;
			item_check_present(tree, index);
			assert(item_tag_get(tree, index, tag));
			item_tag_clear(tree, index, tag);
			item_tag_clear(tree, index, tag);
			assert(thrash_state[index] != NODE_PRESENT);
			thrash_state[index] = NODE_PRESENT;
			nr_untagged++;
			total_tagged--;
		}

		actual_total_tagged = 0;
		actual_total_present = 0;
		for (index = 0; index < THRASH_SIZE; index++) {
			switch (thrash_state[index]) {
			case NODE_ABSENT:
				item_check_absent(tree, index);
				break;
			case NODE_PRESENT:
				item_check_present(tree, index);
				assert(!item_tag_get(tree, index, tag));
				actual_total_present++;
				break;
			case NODE_TAGGED:
				item_check_present(tree, index);
				assert(item_tag_get(tree, index, tag));
				actual_total_present++;
				actual_total_tagged++;
				break;
			}
		}

		gang_check(tree, thrash_state, tag);

		printf("%d(%d) %d(%d) %d(%d) %d(%d) / "
				"%d(%d) present, %d(%d) tagged\n",
			insert_chunk, nr_inserted,
			delete_chunk, nr_deleted,
			tag_chunk, nr_tagged,
			untag_chunk, nr_untagged,
			total_present, actual_total_present,
			total_tagged, actual_total_tagged);
	}
}

static void thrash_tags(void)
{
	RADIX_TREE(tree, GFP_KERNEL);
	char *thrash_state;

	thrash_state = malloc(THRASH_SIZE);
	memset(thrash_state, 0, THRASH_SIZE);

	do_thrash(&tree, thrash_state, 0);

	verify_tag_consistency(&tree, 0);
	item_kill_tree(&tree);
	free(thrash_state);
}

static void leak_check(void)
{
	RADIX_TREE(tree, GFP_KERNEL);

	item_insert(&tree, 1000000);
	item_delete(&tree, 1000000);
	item_kill_tree(&tree);
}

static void __leak_check(void)
{
	RADIX_TREE(tree, GFP_KERNEL);

	printf("%d: nr_allocated=%d\n", __LINE__, nr_allocated);
	item_insert(&tree, 1000000);
	printf("%d: nr_allocated=%d\n", __LINE__, nr_allocated);
	item_delete(&tree, 1000000);
	printf("%d: nr_allocated=%d\n", __LINE__, nr_allocated);
	item_kill_tree(&tree);
	printf("%d: nr_allocated=%d\n", __LINE__, nr_allocated);
}

static void single_check(void)
{
	struct item *items[BATCH];
	RADIX_TREE(tree, GFP_KERNEL);
	int ret;
	unsigned long first = 0;

	item_insert(&tree, 0);
	item_tag_set(&tree, 0, 0);
	ret = radix_tree_gang_lookup_tag(&tree, (void **)items, 0, BATCH, 0);
	assert(ret == 1);
	ret = radix_tree_gang_lookup_tag(&tree, (void **)items, 1, BATCH, 0);
	assert(ret == 0);
	verify_tag_consistency(&tree, 0);
	verify_tag_consistency(&tree, 1);
	ret = radix_tree_range_tag_if_tagged(&tree, &first, 10, 10, 0, 1);
	assert(ret == 1);
	ret = radix_tree_gang_lookup_tag(&tree, (void **)items, 0, BATCH, 1);
	assert(ret == 1);
	item_kill_tree(&tree);
}

void tag_check(void)
{
	single_check();
	extend_checks();
	contract_checks();
	printf("after extend_checks: %d allocated\n", nr_allocated);
	__leak_check();
	leak_check();
	printf("after leak_check: %d allocated\n", nr_allocated);
	simple_checks();
	printf("after simple_checks: %d allocated\n", nr_allocated);
	thrash_tags();
	printf("after thrash_tags: %d allocated\n", nr_allocated);
}
