#include <stdio.h>
#include <stdlib.h>
#include "kerncompat.h"

#define BLOCKSIZE 4096

struct key {
	u64 objectid;
	u32 flags;
	u64 offset;
} __attribute__ ((__packed__));

struct header {
	u64 fsid[2]; /* FS specific uuid */
	u64 blocknum;
	u64 parentid;
	u32 csum;
	u32 ham;
	u16 nritems;
	u16 flags;
} __attribute__ ((__packed__));

#define NODEPTRS_PER_BLOCK ((BLOCKSIZE - sizeof(struct header)) / \
			    (sizeof(struct key) + sizeof(u64)))

#define LEVEL_BITS 3
#define MAX_LEVEL (1 << LEVEL_BITS)
#define node_level(f) ((f) & (MAX_LEVEL-1))
#define is_leaf(f) (node_level(f) == 0)

struct ctree_root {
	struct node *node;
};

struct item {
	struct key key;
	u16 offset;
	u16 size;
} __attribute__ ((__packed__));

#define LEAF_DATA_SIZE (BLOCKSIZE - sizeof(struct header))
struct leaf {
	struct header header;
	union {
		struct item items[LEAF_DATA_SIZE/sizeof(struct item)];
		u8 data[BLOCKSIZE-sizeof(struct header)];
	};
} __attribute__ ((__packed__));

struct node {
	struct header header;
	struct key keys[NODEPTRS_PER_BLOCK];
	u64 blockptrs[NODEPTRS_PER_BLOCK];
} __attribute__ ((__packed__));

struct ctree_path {
	struct node *nodes[MAX_LEVEL];
	int slots[MAX_LEVEL];
};

static inline void init_path(struct ctree_path *p)
{
	memset(p, 0, sizeof(*p));
}

static inline unsigned int leaf_data_end(struct leaf *leaf)
{
	unsigned int nr = leaf->header.nritems;
	if (nr == 0)
		return ARRAY_SIZE(leaf->data);
	return leaf->items[nr-1].offset;
}

static inline int leaf_free_space(struct leaf *leaf)
{
	int data_end = leaf_data_end(leaf);
	int nritems = leaf->header.nritems;
	char *items_end = (char *)(leaf->items + nritems + 1);
	return (char *)(leaf->data + data_end) - (char *)items_end;
}

int comp_keys(struct key *k1, struct key *k2)
{
	if (k1->objectid > k2->objectid)
		return 1;
	if (k1->objectid < k2->objectid)
		return -1;
	if (k1->flags > k2->flags)
		return 1;
	if (k1->flags < k2->flags)
		return -1;
	if (k1->offset > k2->offset)
		return 1;
	if (k1->offset < k2->offset)
		return -1;
	return 0;
}
int generic_bin_search(char *p, int item_size, struct key *key,
		       int max, int *slot)
{
	int low = 0;
	int high = max;
	int mid;
	int ret;
	struct key *tmp;

	while(low < high) {
		mid = (low + high) / 2;
		tmp = (struct key *)(p + mid * item_size);
		ret = comp_keys(tmp, key);

		if (ret < 0)
			low = mid + 1;
		else if (ret > 0)
			high = mid;
		else {
			*slot = mid;
			return 0;
		}
	}
	*slot = low;
	return 1;
}

int bin_search(struct node *c, struct key *key, int *slot)
{
	if (is_leaf(c->header.flags)) {
		struct leaf *l = (struct leaf *)c;
		return generic_bin_search((void *)l->items, sizeof(struct item),
					  key, c->header.nritems, slot);
	} else {
		return generic_bin_search((void *)c->keys, sizeof(struct key),
					  key, c->header.nritems, slot);
	}
	return -1;
}

void *read_block(u64 blocknum)
{
	return (void *)blocknum;
}

int search_slot(struct ctree_root *root, struct key *key, struct ctree_path *p)
{
	struct node *c = root->node;
	int slot;
	int ret;
	int level;
	while (c) {
		level = node_level(c->header.flags);
		p->nodes[level] = c;
		ret = bin_search(c, key, &slot);
		if (!is_leaf(c->header.flags)) {
			if (ret && slot > 0)
				slot -= 1;
			p->slots[level] = slot;
			c = read_block(c->blockptrs[slot]);
			continue;
		} else {
			p->slots[level] = slot;
			return ret;
		}
	}
	return -1;
}

static void fixup_low_keys(struct ctree_path *path, struct key *key,
			     int level)
{
	int i;
	/* adjust the pointers going up the tree */
	for (i = level; i < MAX_LEVEL; i++) {
		struct node *t = path->nodes[i];
		int tslot = path->slots[i];
		if (!t)
			break;
		memcpy(t->keys + tslot, key, sizeof(*key));
		if (tslot != 0)
			break;
	}
}

int __insert_ptr(struct ctree_root *root,
		struct ctree_path *path, struct key *key,
		u64 blocknr, int slot, int level)
{
	struct node *c;
	struct node *lower;
	struct key *lower_key;
	int nritems;
	/* need a new root */
	if (!path->nodes[level]) {
		c = malloc(sizeof(struct node));
		memset(c, 0, sizeof(c));
		c->header.nritems = 2;
		c->header.flags = node_level(level);
		lower = path->nodes[level-1];
		if (is_leaf(lower->header.flags))
			lower_key = &((struct leaf *)lower)->items[0].key;
		else
			lower_key = lower->keys;
		memcpy(c->keys, lower_key, sizeof(struct key));
		memcpy(c->keys + 1, key, sizeof(struct key));
		c->blockptrs[0] = (u64)lower;
		c->blockptrs[1] = blocknr;
		root->node = c;
		path->nodes[level] = c;
		path->slots[level] = 0;
		if (c->keys[1].objectid == 0)
			BUG();
		return 0;
	}
	lower = path->nodes[level];
	nritems = lower->header.nritems;
	if (slot > nritems)
		BUG();
	if (nritems == NODEPTRS_PER_BLOCK)
		BUG();
	if (slot != nritems) {
		memmove(lower->keys + slot + 1, lower->keys + slot,
			(nritems - slot) * sizeof(struct key));
		memmove(lower->blockptrs + slot + 1, lower->blockptrs + slot,
			(nritems - slot) * sizeof(u64));
	}
	memcpy(lower->keys + slot, key, sizeof(struct key));
	lower->blockptrs[slot] = blocknr;
	lower->header.nritems++;
	if (lower->keys[1].objectid == 0)
			BUG();
	return 0;
}

int push_node_left(struct ctree_root *root, struct ctree_path *path, int level)
{
	int slot;
	struct node *left;
	struct node *right;
	int push_items = 0;
	int left_nritems;
	int right_nritems;

	if (level == MAX_LEVEL - 1 || path->nodes[level + 1] == 0)
		return 1;
	slot = path->slots[level + 1];
	if (slot == 0)
		return 1;

	left = read_block(path->nodes[level + 1]->blockptrs[slot - 1]);
	right = path->nodes[level];
	left_nritems = left->header.nritems;
	right_nritems = right->header.nritems;
	push_items = NODEPTRS_PER_BLOCK - (left_nritems + 1);
	if (push_items <= 0)
		return 1;

	if (right_nritems < push_items)
		push_items = right_nritems;
	memcpy(left->keys + left_nritems, right->keys,
		push_items * sizeof(struct key));
	memcpy(left->blockptrs + left_nritems, right->blockptrs,
		push_items * sizeof(u64));
	memmove(right->keys, right->keys + push_items,
		(right_nritems - push_items) * sizeof(struct key));
	memmove(right->blockptrs, right->blockptrs + push_items,
		(right_nritems - push_items) * sizeof(u64));
	right->header.nritems -= push_items;
	left->header.nritems += push_items;

	/* adjust the pointers going up the tree */
	fixup_low_keys(path, right->keys, level + 1);

	/* then fixup the leaf pointer in the path */
	if (path->slots[level] < push_items) {
		path->slots[level] += left_nritems;
		path->nodes[level] = (struct node*)left;
		path->slots[level + 1] -= 1;
	} else {
		path->slots[level] -= push_items;
	}
	return 0;
}

int push_node_right(struct ctree_root *root, struct ctree_path *path, int level)
{
	int slot;
	struct node *dst;
	struct node *src;
	int push_items = 0;
	int dst_nritems;
	int src_nritems;

	if (level == MAX_LEVEL - 1 || path->nodes[level + 1] == 0)
		return 1;
	slot = path->slots[level + 1];
	if (slot == NODEPTRS_PER_BLOCK - 1)
		return 1;

	if (slot >= path->nodes[level + 1]->header.nritems -1)
		return 1;

	dst = read_block(path->nodes[level + 1]->blockptrs[slot + 1]);
	src = path->nodes[level];
	dst_nritems = dst->header.nritems;
	src_nritems = src->header.nritems;
	push_items = NODEPTRS_PER_BLOCK - (dst_nritems + 1);
	if (push_items <= 0)
		return 1;

	if (src_nritems < push_items)
		push_items = src_nritems;
	memmove(dst->keys + push_items, dst->keys,
		dst_nritems * sizeof(struct key));
	memcpy(dst->keys, src->keys + src_nritems - push_items,
		push_items * sizeof(struct key));

	memmove(dst->blockptrs + push_items, dst->blockptrs,
		dst_nritems * sizeof(u64));
	memcpy(dst->blockptrs, src->blockptrs + src_nritems - push_items,
		push_items * sizeof(u64));

	src->header.nritems -= push_items;
	dst->header.nritems += push_items;

	/* adjust the pointers going up the tree */
	memcpy(path->nodes[level + 1]->keys + path->slots[level + 1] + 1,
		dst->keys, sizeof(struct key));
	/* then fixup the leaf pointer in the path */
	if (path->slots[level] >= src->header.nritems) {
		path->slots[level] -= src->header.nritems;
		path->nodes[level] = (struct node*)dst;
		path->slots[level + 1] += 1;
	}
	return 0;
}

int insert_ptr(struct ctree_root *root,
		struct ctree_path *path, struct key *key,
		u64 blocknr, int level)
{
	struct node *c = path->nodes[level];
	struct node *b;
	struct node *bal[MAX_LEVEL];
	int bal_level = level;
	int mid;
	int bal_start = -1;

	memset(bal, 0, ARRAY_SIZE(bal));
	while(c && c->header.nritems == NODEPTRS_PER_BLOCK) {
		if (push_node_left(root, path,
		   node_level(c->header.flags)) == 0)
			break;
		if (push_node_right(root, path,
		   node_level(c->header.flags)) == 0)
			break;
		bal_start = bal_level;
		if (bal_level == MAX_LEVEL - 1)
			BUG();
		b = malloc(sizeof(struct node));
		b->header.flags = c->header.flags;
		mid = (c->header.nritems + 1) / 2;
		memcpy(b->keys, c->keys + mid,
			(c->header.nritems - mid) * sizeof(struct key));
		memcpy(b->blockptrs, c->blockptrs + mid,
			(c->header.nritems - mid) * sizeof(u64));
		b->header.nritems = c->header.nritems - mid;
		c->header.nritems = mid;
		bal[bal_level] = b;
		if (bal_level == MAX_LEVEL - 1)
			break;
		bal_level += 1;
		c = path->nodes[bal_level];
	}
	while(bal_start > 0) {
		b = bal[bal_start];
		c = path->nodes[bal_start];
		__insert_ptr(root, path, b->keys, (u64)b,
				path->slots[bal_start + 1] + 1, bal_start + 1);
		if (path->slots[bal_start] >= c->header.nritems) {
			path->slots[bal_start] -= c->header.nritems;
			path->nodes[bal_start] = b;
			path->slots[bal_start + 1] += 1;
		}
		bal_start--;
		if (!bal[bal_start])
			break;
	}
	return __insert_ptr(root, path, key, blocknr, path->slots[level] + 1,
			    level);
}

int leaf_space_used(struct leaf *l, int start, int nr)
{
	int data_len;
	int end = start + nr - 1;

	if (!nr)
		return 0;
	data_len = l->items[start].offset + l->items[start].size;
	data_len = data_len - l->items[end].offset;
	data_len += sizeof(struct item) * nr;
	return data_len;
}

int push_leaf_left(struct ctree_root *root, struct ctree_path *path,
		   int data_size)
{
	struct leaf *right = (struct leaf *)path->nodes[0];
	struct leaf *left;
	int slot;
	int i;
	int free_space;
	int push_space = 0;
	int push_items = 0;
	struct item *item;
	int old_left_nritems;

	slot = path->slots[1];
	if (slot == 0) {
		return 1;
	}
	if (!path->nodes[1]) {
		return 1;
	}
	left = read_block(path->nodes[1]->blockptrs[slot - 1]);
	free_space = leaf_free_space(left);
	if (free_space < data_size + sizeof(struct item)) {
		return 1;
	}
	for (i = 0; i < right->header.nritems; i++) {
		item = right->items + i;
		if (path->slots[0] == i)
			push_space += data_size + sizeof(*item);
		if (item->size + sizeof(*item) + push_space > free_space)
			break;
		push_items++;
		push_space += item->size + sizeof(*item);
	}
	if (push_items == 0) {
		return 1;
	}
	/* push data from right to left */
	memcpy(left->items + left->header.nritems,
		right->items, push_items * sizeof(struct item));
	push_space = LEAF_DATA_SIZE - right->items[push_items -1].offset;
	memcpy(left->data + leaf_data_end(left) - push_space,
		right->data + right->items[push_items - 1].offset,
		push_space);
	old_left_nritems = left->header.nritems;
	for(i = old_left_nritems; i < old_left_nritems + push_items; i++) {
		left->items[i].offset -= LEAF_DATA_SIZE -
			left->items[old_left_nritems -1].offset;
	}
	left->header.nritems += push_items;

	/* fixup right node */
	push_space = right->items[push_items-1].offset - leaf_data_end(right);
	memmove(right->data + LEAF_DATA_SIZE - push_space, right->data +
		leaf_data_end(right), push_space);
	memmove(right->items, right->items + push_items,
		(right->header.nritems - push_items) * sizeof(struct item));
	right->header.nritems -= push_items;
	push_space = LEAF_DATA_SIZE;
	for (i = 0; i < right->header.nritems; i++) {
		right->items[i].offset = push_space - right->items[i].size;
		push_space = right->items[i].offset;
	}
	fixup_low_keys(path, &right->items[0].key, 1);

	/* then fixup the leaf pointer in the path */
	if (path->slots[0] < push_items) {
		path->slots[0] += old_left_nritems;
		path->nodes[0] = (struct node*)left;
		path->slots[1] -= 1;
	} else {
		path->slots[0] -= push_items;
	}
	return 0;
}

int split_leaf(struct ctree_root *root, struct ctree_path *path, int data_size)
{
	struct leaf *l = (struct leaf *)path->nodes[0];
	int nritems = l->header.nritems;
	int mid = (nritems + 1)/ 2;
	int slot = path->slots[0];
	struct leaf *right;
	int space_needed = data_size + sizeof(struct item);
	int data_copy_size;
	int rt_data_off;
	int i;
	int ret;

	if (push_leaf_left(root, path, data_size) == 0) {
		return 0;
	}
	right = malloc(sizeof(struct leaf));
	memset(right, 0, sizeof(*right));
	if (mid <= slot) {
		if (leaf_space_used(l, mid, nritems - mid) + space_needed >
			LEAF_DATA_SIZE)
			BUG();
	} else {
		if (leaf_space_used(l, 0, mid + 1) + space_needed >
			LEAF_DATA_SIZE)
			BUG();
	}
	right->header.nritems = nritems - mid;
	data_copy_size = l->items[mid].offset + l->items[mid].size -
			 leaf_data_end(l);
	memcpy(right->items, l->items + mid,
	       (nritems - mid) * sizeof(struct item));
	memcpy(right->data + LEAF_DATA_SIZE - data_copy_size,
	       l->data + leaf_data_end(l), data_copy_size);
	rt_data_off = LEAF_DATA_SIZE -
		     (l->items[mid].offset + l->items[mid].size);
	for (i = 0; i < right->header.nritems; i++) {
		right->items[i].offset += rt_data_off;
	}
	l->header.nritems = mid;
	ret = insert_ptr(root, path, &right->items[0].key,
			  (u64)right, 1);
	if (mid <= slot) {
		path->nodes[0] = (struct node *)right;
		path->slots[0] -= mid;
		path->slots[1] += 1;
	}
	return ret;
}

int insert_item(struct ctree_root *root, struct key *key,
			  void *data, int data_size)
{
	int ret;
	int slot;
	struct leaf *leaf;
	unsigned int nritems;
	unsigned int data_end;
	struct ctree_path path;

	init_path(&path);
	ret = search_slot(root, key, &path);
	if (ret == 0)
		return -EEXIST;

	leaf = (struct leaf *)path.nodes[0];
	if (leaf_free_space(leaf) <  sizeof(struct item) + data_size)
		split_leaf(root, &path, data_size);
	leaf = (struct leaf *)path.nodes[0];
	nritems = leaf->header.nritems;
	data_end = leaf_data_end(leaf);
	if (leaf_free_space(leaf) <  sizeof(struct item) + data_size)
		BUG();

	slot = path.slots[0];
	if (slot == 0)
		fixup_low_keys(&path, key, 1);
	if (slot != nritems) {
		int i;
		unsigned int old_data = leaf->items[slot].offset +
					leaf->items[slot].size;

		/*
		 * item0..itemN ... dataN.offset..dataN.size .. data0.size
		 */
		/* first correct the data pointers */
		for (i = slot; i < nritems; i++)
			leaf->items[i].offset -= data_size;

		/* shift the items */
		memmove(leaf->items + slot + 1, leaf->items + slot,
		        (nritems - slot) * sizeof(struct item));

		/* shift the data */
		memmove(leaf->data + data_end - data_size, leaf->data +
		        data_end, old_data - data_end);
		data_end = old_data;
	}
	memcpy(&leaf->items[slot].key, key, sizeof(struct key));
	leaf->items[slot].offset = data_end - data_size;
	leaf->items[slot].size = data_size;
	memcpy(leaf->data + data_end - data_size, data, data_size);
	leaf->header.nritems += 1;
	if (leaf_free_space(leaf) < 0)
		BUG();
	return 0;
}

int del_ptr(struct ctree_root *root, struct ctree_path *path, int level)
{
	int slot;
	struct node *node;
	int nritems;

	while(1) {
		node = path->nodes[level];
		if (!node)
			break;
		slot = path->slots[level];
		nritems = node->header.nritems;

		if (slot != nritems -1) {
			memmove(node->keys + slot, node->keys + slot + 1,
				sizeof(struct key) * (nritems - slot - 1));
			memmove(node->blockptrs + slot,
				node->blockptrs + slot + 1,
				sizeof(u64) * (nritems - slot - 1));
		}
		node->header.nritems--;
		if (node->header.nritems != 0) {
			int tslot;
			if (slot == 0)
				fixup_low_keys(path, node->keys, level + 1);
			tslot = path->slots[level+1];
			push_node_left(root, path, level);
			if (node->header.nritems) {
				push_node_right(root, path, level);
			}
			if (node->header.nritems)
				break;
			path->slots[level+1] = tslot;
		}
		if (node == root->node) {
			printf("root is now null!\n");
			root->node = NULL;
			break;
		}
		level++;
		if (!path->nodes[level])
			BUG();
		free(node);
	}
	return 0;
}

int del_item(struct ctree_root *root, struct ctree_path *path)
{
	int slot;
	struct leaf *leaf;
	int doff;
	int dsize;

	leaf = (struct leaf *)path->nodes[0];
	slot = path->slots[0];
	doff = leaf->items[slot].offset;
	dsize = leaf->items[slot].size;

	if (slot != leaf->header.nritems - 1) {
		int i;
		int data_end = leaf_data_end(leaf);
		memmove(leaf->data + data_end + dsize,
			leaf->data + data_end,
			doff - data_end);
		for (i = slot + 1; i < leaf->header.nritems; i++)
			leaf->items[i].offset += dsize;
		memmove(leaf->items + slot, leaf->items + slot + 1,
			sizeof(struct item) *
			(leaf->header.nritems - slot - 1));
	}
	leaf->header.nritems -= 1;
	if (leaf->header.nritems == 0) {
		if (leaf == (struct leaf *)root->node)
			root->node = NULL;
		else
			del_ptr(root, path, 1);
		free(leaf);
	} else {
		if (slot == 0)
			fixup_low_keys(path, &leaf->items[0].key, 1);
		if (leaf_space_used(leaf, 0, leaf->header.nritems) <
		    LEAF_DATA_SIZE / 4) {
			/* push_leaf_left fixes the path.
			 * make sure the path still points to our leaf
			 * for possible call to del_ptr below
			 */
			slot = path->slots[1];
			push_leaf_left(root, path, 1);
			if (leaf->header.nritems == 0) {
				free(leaf);
				path->slots[1] = slot;
				del_ptr(root, path, 1);
			}
		}
	}
	return 0;
}

void print_leaf(struct leaf *l)
{
	int i;
	int nr = l->header.nritems;
	struct item *item;
	printf("leaf %p total ptrs %d free space %d\n", l, nr,
	       leaf_free_space(l));
	fflush(stdout);
	for (i = 0 ; i < nr ; i++) {
		item = l->items + i;
		printf("\titem %d key (%lu %u %lu) itemoff %d itemsize %d\n",
			i,
			item->key.objectid, item->key.flags, item->key.offset,
			item->offset, item->size);
		fflush(stdout);
		printf("\t\titem data %.*s\n", item->size, l->data+item->offset);
		fflush(stdout);
	}
}
void print_tree(struct node *c)
{
	int i;
	int nr;

	if (!c)
		return;
	nr = c->header.nritems;
	if (is_leaf(c->header.flags)) {
		print_leaf((struct leaf *)c);
		return;
	}
	printf("node %p level %d total ptrs %d free spc %lu\n", c,
	        node_level(c->header.flags), c->header.nritems,
		NODEPTRS_PER_BLOCK - c->header.nritems);
	fflush(stdout);
	for (i = 0; i < nr; i++) {
		printf("\tkey %d (%lu %u %lu) block %lx\n",
		       i,
		       c->keys[i].objectid, c->keys[i].flags, c->keys[i].offset,
		       c->blockptrs[i]);
		fflush(stdout);
	}
	for (i = 0; i < nr; i++) {
		struct node *next = read_block(c->blockptrs[i]);
		if (is_leaf(next->header.flags) &&
		    node_level(c->header.flags) != 1)
			BUG();
		if (node_level(next->header.flags) !=
			node_level(c->header.flags) - 1)
			BUG();
		print_tree(next);
	}

}

/* for testing only */
int next_key(int i, int max_key) {
	return rand() % max_key;
	// return i;
}

int main() {
	struct leaf *first_node = malloc(sizeof(struct leaf));
	struct ctree_root root;
	struct key ins;
	struct key last = { (u64)-1, 0, 0};
	char *buf;
	int i;
	int num;
	int ret;
	int run_size = 100000;
	int max_key = 100000000;
	int tree_size = 0;
	struct ctree_path path;


	srand(55);
	root.node = (struct node *)first_node;
	memset(first_node, 0, sizeof(*first_node));
	for (i = 0; i < run_size; i++) {
		buf = malloc(64);
		num = next_key(i, max_key);
		// num = i;
		sprintf(buf, "string-%d", num);
		// printf("insert %d\n", num);
		ins.objectid = num;
		ins.offset = 0;
		ins.flags = 0;
		ret = insert_item(&root, &ins, buf, strlen(buf));
		if (!ret)
			tree_size++;
	}
	srand(55);
	for (i = 0; i < run_size; i++) {
		num = next_key(i, max_key);
		ins.objectid = num;
		init_path(&path);
		ret = search_slot(&root, &ins, &path);
		if (ret) {
			print_tree(root.node);
			printf("unable to find %d\n", num);
			exit(1);
		}
	}
	printf("node %p level %d total ptrs %d free spc %lu\n", root.node,
	        node_level(root.node->header.flags), root.node->header.nritems,
		NODEPTRS_PER_BLOCK - root.node->header.nritems);
	// print_tree(root.node);
	printf("all searches good\n");
	i = 0;
	srand(55);
	for (i = 0 ; i < run_size/4; i++) {
		num = next_key(i, max_key);
		ins.objectid = num;
		init_path(&path);
		ret = search_slot(&root, &ins, &path);
		if (ret)
			continue;
		ret = del_item(&root, &path);
		if (ret != 0)
			BUG();
		tree_size--;
	}
	srand(128);
	for (i = 0; i < run_size; i++) {
		buf = malloc(64);
		num = next_key(i, max_key);
		sprintf(buf, "string-%d", num);
		ins.objectid = num;
		ret = insert_item(&root, &ins, buf, strlen(buf));
		if (!ret)
			tree_size++;
	}
	while(root.node) {
		struct leaf *leaf;
		int slot;
		ins.objectid = (u64)-1;
		init_path(&path);
		ret = search_slot(&root, &ins, &path);
		if (ret == 0)
			BUG();

		leaf = (struct leaf *)(path.nodes[0]);
		slot = path.slots[0];
		if (slot != leaf->header.nritems)
			BUG();
		while(path.slots[0] > 0) {
			path.slots[0] -= 1;
			slot = path.slots[0];
			leaf = (struct leaf *)(path.nodes[0]);

			if (comp_keys(&last, &leaf->items[slot].key) <= 0)
				BUG();
			memcpy(&last, &leaf->items[slot].key, sizeof(last));
			ret = del_item(&root, &path);
			if (ret != 0)
				BUG();
			tree_size--;
		}
	}
	print_tree(root.node);
	printf("tree size is now %d\n", tree_size);
	return 0;
}
