// SPDX-License-Identifier: GPL-2.0
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/bitops.h>

#include "test.h"

struct item *
item_tag_set(struct radix_tree_root *root, unsigned long index, int tag)
{
	return radix_tree_tag_set(root, index, tag);
}

struct item *
item_tag_clear(struct radix_tree_root *root, unsigned long index, int tag)
{
	return radix_tree_tag_clear(root, index, tag);
}

int item_tag_get(struct radix_tree_root *root, unsigned long index, int tag)
{
	return radix_tree_tag_get(root, index, tag);
}

int __item_insert(struct radix_tree_root *root, struct item *item)
{
	return __radix_tree_insert(root, item->index, item->order, item);
}

struct item *item_create(unsigned long index, unsigned int order)
{
	struct item *ret = malloc(sizeof(*ret));

	ret->index = index;
	ret->order = order;
	return ret;
}

int item_insert_order(struct radix_tree_root *root, unsigned long index,
			unsigned order)
{
	struct item *item = item_create(index, order);
	int err = __item_insert(root, item);
	if (err)
		free(item);
	return err;
}

int item_insert(struct radix_tree_root *root, unsigned long index)
{
	return item_insert_order(root, index, 0);
}

void item_sanity(struct item *item, unsigned long index)
{
	unsigned long mask;
	assert(!radix_tree_is_internal_node(item));
	assert(item->order < BITS_PER_LONG);
	mask = (1UL << item->order) - 1;
	assert((item->index | mask) == (index | mask));
}

int item_delete(struct radix_tree_root *root, unsigned long index)
{
	struct item *item = radix_tree_delete(root, index);

	if (item) {
		item_sanity(item, index);
		free(item);
		return 1;
	}
	return 0;
}

static void item_free_rcu(struct rcu_head *head)
{
	struct item *item = container_of(head, struct item, rcu_head);

	free(item);
}

int item_delete_rcu(struct radix_tree_root *root, unsigned long index)
{
	struct item *item = radix_tree_delete(root, index);

	if (item) {
		item_sanity(item, index);
		call_rcu(&item->rcu_head, item_free_rcu);
		return 1;
	}
	return 0;
}

void item_check_present(struct radix_tree_root *root, unsigned long index)
{
	struct item *item;

	item = radix_tree_lookup(root, index);
	assert(item != NULL);
	item_sanity(item, index);
}

struct item *item_lookup(struct radix_tree_root *root, unsigned long index)
{
	return radix_tree_lookup(root, index);
}

void item_check_absent(struct radix_tree_root *root, unsigned long index)
{
	struct item *item;

	item = radix_tree_lookup(root, index);
	assert(item == NULL);
}

/*
 * Scan only the passed (start, start+nr] for present items
 */
void item_gang_check_present(struct radix_tree_root *root,
			unsigned long start, unsigned long nr,
			int chunk, int hop)
{
	struct item *items[chunk];
	unsigned long into;

	for (into = 0; into < nr; ) {
		int nfound;
		int nr_to_find = chunk;
		int i;

		if (nr_to_find > (nr - into))
			nr_to_find = nr - into;

		nfound = radix_tree_gang_lookup(root, (void **)items,
						start + into, nr_to_find);
		assert(nfound == nr_to_find);
		for (i = 0; i < nfound; i++)
			assert(items[i]->index == start + into + i);
		into += hop;
	}
}

/*
 * Scan the entire tree, only expecting present items (start, start+nr]
 */
void item_full_scan(struct radix_tree_root *root, unsigned long start,
			unsigned long nr, int chunk)
{
	struct item *items[chunk];
	unsigned long into = 0;
	unsigned long this_index = start;
	int nfound;
	int i;

//	printf("%s(0x%08lx, 0x%08lx, %d)\n", __FUNCTION__, start, nr, chunk);

	while ((nfound = radix_tree_gang_lookup(root, (void **)items, into,
					chunk))) {
//		printf("At 0x%08lx, nfound=%d\n", into, nfound);
		for (i = 0; i < nfound; i++) {
			assert(items[i]->index == this_index);
			this_index++;
		}
//		printf("Found 0x%08lx->0x%08lx\n",
//			items[0]->index, items[nfound-1]->index);
		into = this_index;
	}
	if (chunk)
		assert(this_index == start + nr);
	nfound = radix_tree_gang_lookup(root, (void **)items,
					this_index, chunk);
	assert(nfound == 0);
}

/* Use the same pattern as tag_pages_for_writeback() in mm/page-writeback.c */
int tag_tagged_items(struct radix_tree_root *root, pthread_mutex_t *lock,
			unsigned long start, unsigned long end, unsigned batch,
			unsigned iftag, unsigned thentag)
{
	unsigned long tagged = 0;
	struct radix_tree_iter iter;
	void **slot;

	if (batch == 0)
		batch = 1;

	if (lock)
		pthread_mutex_lock(lock);
	radix_tree_for_each_tagged(slot, root, &iter, start, iftag) {
		if (iter.index > end)
			break;
		radix_tree_iter_tag_set(root, &iter, thentag);
		tagged++;
		if ((tagged % batch) != 0)
			continue;
		slot = radix_tree_iter_resume(slot, &iter);
		if (lock) {
			pthread_mutex_unlock(lock);
			rcu_barrier();
			pthread_mutex_lock(lock);
		}
	}
	if (lock)
		pthread_mutex_unlock(lock);

	return tagged;
}

/* Use the same pattern as find_swap_entry() in mm/shmem.c */
unsigned long find_item(struct radix_tree_root *root, void *item)
{
	struct radix_tree_iter iter;
	void **slot;
	unsigned long found = -1;
	unsigned long checked = 0;

	radix_tree_for_each_slot(slot, root, &iter, 0) {
		if (*slot == item) {
			found = iter.index;
			break;
		}
		checked++;
		if ((checked % 4) != 0)
			continue;
		slot = radix_tree_iter_resume(slot, &iter);
	}

	return found;
}

static int verify_node(struct radix_tree_node *slot, unsigned int tag,
			int tagged)
{
	int anyset = 0;
	int i;
	int j;

	slot = entry_to_node(slot);

	/* Verify consistency at this level */
	for (i = 0; i < RADIX_TREE_TAG_LONGS; i++) {
		if (slot->tags[tag][i]) {
			anyset = 1;
			break;
		}
	}
	if (tagged != anyset) {
		printf("tag: %u, shift %u, tagged: %d, anyset: %d\n",
			tag, slot->shift, tagged, anyset);
		for (j = 0; j < RADIX_TREE_MAX_TAGS; j++) {
			printf("tag %d: ", j);
			for (i = 0; i < RADIX_TREE_TAG_LONGS; i++)
				printf("%016lx ", slot->tags[j][i]);
			printf("\n");
		}
		return 1;
	}
	assert(tagged == anyset);

	/* Go for next level */
	if (slot->shift > 0) {
		for (i = 0; i < RADIX_TREE_MAP_SIZE; i++)
			if (slot->slots[i])
				if (verify_node(slot->slots[i], tag,
					    !!test_bit(i, slot->tags[tag]))) {
					printf("Failure at off %d\n", i);
					for (j = 0; j < RADIX_TREE_MAX_TAGS; j++) {
						printf("tag %d: ", j);
						for (i = 0; i < RADIX_TREE_TAG_LONGS; i++)
							printf("%016lx ", slot->tags[j][i]);
						printf("\n");
					}
					return 1;
				}
	}
	return 0;
}

void verify_tag_consistency(struct radix_tree_root *root, unsigned int tag)
{
	struct radix_tree_node *node = root->rnode;
	if (!radix_tree_is_internal_node(node))
		return;
	verify_node(node, tag, !!root_tag_get(root, tag));
}

void item_kill_tree(struct radix_tree_root *root)
{
	struct radix_tree_iter iter;
	void **slot;
	struct item *items[32];
	int nfound;

	radix_tree_for_each_slot(slot, root, &iter, 0) {
		if (xa_is_value(*slot))
			radix_tree_delete(root, iter.index);
	}

	while ((nfound = radix_tree_gang_lookup(root, (void **)items, 0, 32))) {
		int i;

		for (i = 0; i < nfound; i++) {
			void *ret;

			ret = radix_tree_delete(root, items[i]->index);
			assert(ret == items[i]);
			free(items[i]);
		}
	}
	assert(radix_tree_gang_lookup(root, (void **)items, 0, 32) == 0);
	assert(root->rnode == NULL);
}

void tree_verify_min_height(struct radix_tree_root *root, int maxindex)
{
	unsigned shift;
	struct radix_tree_node *node = root->rnode;
	if (!radix_tree_is_internal_node(node)) {
		assert(maxindex == 0);
		return;
	}

	node = entry_to_node(node);
	assert(maxindex <= node_maxindex(node));

	shift = node->shift;
	if (shift > 0)
		assert(maxindex > shift_maxindex(shift - RADIX_TREE_MAP_SHIFT));
	else
		assert(maxindex > 0);
}
