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
	return radix_tree_insert(root, item->index, item);
}

int item_insert(struct radix_tree_root *root, unsigned long index)
{
	return __item_insert(root, item_create(index));
}

int item_delete(struct radix_tree_root *root, unsigned long index)
{
	struct item *item = radix_tree_delete(root, index);

	if (item) {
		assert(item->index == index);
		free(item);
		return 1;
	}
	return 0;
}

struct item *item_create(unsigned long index)
{
	struct item *ret = malloc(sizeof(*ret));

	ret->index = index;
	return ret;
}

void item_check_present(struct radix_tree_root *root, unsigned long index)
{
	struct item *item;

	item = radix_tree_lookup(root, index);
	assert(item != 0);
	assert(item->index == index);
}

struct item *item_lookup(struct radix_tree_root *root, unsigned long index)
{
	return radix_tree_lookup(root, index);
}

void item_check_absent(struct radix_tree_root *root, unsigned long index)
{
	struct item *item;

	item = radix_tree_lookup(root, index);
	assert(item == 0);
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

static int verify_node(struct radix_tree_node *slot, unsigned int tag,
			unsigned int height, int tagged)
{
	int anyset = 0;
	int i;
	int j;

	slot = indirect_to_ptr(slot);

	/* Verify consistency at this level */
	for (i = 0; i < RADIX_TREE_TAG_LONGS; i++) {
		if (slot->tags[tag][i]) {
			anyset = 1;
			break;
		}
	}
	if (tagged != anyset) {
		printf("tag: %u, height %u, tagged: %d, anyset: %d\n", tag, height, tagged, anyset);
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
	if (height > 1) {
		for (i = 0; i < RADIX_TREE_MAP_SIZE; i++)
			if (slot->slots[i])
				if (verify_node(slot->slots[i], tag, height - 1,
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
	if (!root->height)
		return;
	verify_node(root->rnode, tag, root->height, !!root_tag_get(root, tag));
}

void item_kill_tree(struct radix_tree_root *root)
{
	struct item *items[32];
	int nfound;

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
	assert(radix_tree_maxindex(root->height) >= maxindex);
	if (root->height > 1)
		assert(radix_tree_maxindex(root->height-1) < maxindex);
	else if (root->height == 1)
		assert(radix_tree_maxindex(root->height-1) <= maxindex);
}
