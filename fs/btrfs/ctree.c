#include <stdio.h>
#include <stdlib.h>
#include "kerncompat.h"
#include "radix-tree.h"
#include "ctree.h"
#include "disk-io.h"

static inline void init_path(struct ctree_path *p)
{
	memset(p, 0, sizeof(*p));
}

static void release_path(struct ctree_root *root, struct ctree_path *p)
{
	int i;
	for (i = 0; i < MAX_LEVEL; i++) {
		if (!p->nodes[i])
			break;
		tree_block_release(root, p->nodes[i]);
	}
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

int search_slot(struct ctree_root *root, struct key *key, struct ctree_path *p)
{
	struct tree_buffer *b = root->node;
	struct node *c;

	int slot;
	int ret;
	int level;
	b->count++;
	while (b) {
		c = &b->node;
		level = node_level(c->header.flags);
		p->nodes[level] = b;
		ret = bin_search(c, key, &slot);
		if (!is_leaf(c->header.flags)) {
			if (ret && slot > 0)
				slot -= 1;
			p->slots[level] = slot;
			b = read_tree_block(root, c->blockptrs[slot]);
			continue;
		} else {
			p->slots[level] = slot;
			return ret;
		}
	}
	return -1;
}

static void fixup_low_keys(struct ctree_root *root,
			   struct ctree_path *path, struct key *key,
			   int level)
{
	int i;
	/* adjust the pointers going up the tree */
	for (i = level; i < MAX_LEVEL; i++) {
		struct node *t;
		int tslot = path->slots[i];
		if (!path->nodes[i])
			break;
		t = &path->nodes[i]->node;
		memcpy(t->keys + tslot, key, sizeof(*key));
		write_tree_block(root, path->nodes[i]);
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
		struct tree_buffer *t;
		t = alloc_free_block(root);
		c = &t->node;
		memset(c, 0, sizeof(c));
		c->header.nritems = 2;
		c->header.flags = node_level(level);
		c->header.blocknr = t->blocknr;
		lower = &path->nodes[level-1]->node;
		if (is_leaf(lower->header.flags))
			lower_key = &((struct leaf *)lower)->items[0].key;
		else
			lower_key = lower->keys;
		memcpy(c->keys, lower_key, sizeof(struct key));
		memcpy(c->keys + 1, key, sizeof(struct key));
		c->blockptrs[0] = path->nodes[level-1]->blocknr;
		c->blockptrs[1] = blocknr;
		/* the path has an extra ref to root->node */
		tree_block_release(root, root->node);
		root->node = t;
		t->count++;
		write_tree_block(root, t);
		path->nodes[level] = t;
		path->slots[level] = 0;
		if (c->keys[1].objectid == 0)
			BUG();
		return 0;
	}
	lower = &path->nodes[level]->node;
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
	write_tree_block(root, path->nodes[level]);
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
	struct tree_buffer *t;
	struct tree_buffer *right_buf;

	if (level == MAX_LEVEL - 1 || path->nodes[level + 1] == 0)
		return 1;
	slot = path->slots[level + 1];
	if (slot == 0)
		return 1;

	t = read_tree_block(root,
		            path->nodes[level + 1]->node.blockptrs[slot - 1]);
	left = &t->node;
	right_buf = path->nodes[level];
	right = &right_buf->node;
	left_nritems = left->header.nritems;
	right_nritems = right->header.nritems;
	push_items = NODEPTRS_PER_BLOCK - (left_nritems + 1);
	if (push_items <= 0) {
		tree_block_release(root, t);
		return 1;
	}

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
	fixup_low_keys(root, path, right->keys, level + 1);

	write_tree_block(root, t);
	write_tree_block(root, right_buf);

	/* then fixup the leaf pointer in the path */
	if (path->slots[level] < push_items) {
		path->slots[level] += left_nritems;
		tree_block_release(root, path->nodes[level]);
		path->nodes[level] = t;
		path->slots[level + 1] -= 1;
	} else {
		path->slots[level] -= push_items;
		tree_block_release(root, t);
	}
	return 0;
}

int push_node_right(struct ctree_root *root, struct ctree_path *path, int level)
{
	int slot;
	struct tree_buffer *t;
	struct tree_buffer *src_buffer;
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

	if (slot >= path->nodes[level + 1]->node.header.nritems -1)
		return 1;

	t = read_tree_block(root,
			    path->nodes[level + 1]->node.blockptrs[slot + 1]);
	dst = &t->node;
	src_buffer = path->nodes[level];
	src = &src_buffer->node;
	dst_nritems = dst->header.nritems;
	src_nritems = src->header.nritems;
	push_items = NODEPTRS_PER_BLOCK - (dst_nritems + 1);
	if (push_items <= 0) {
		tree_block_release(root, t);
		return 1;
	}

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
	memcpy(path->nodes[level + 1]->node.keys + path->slots[level + 1] + 1,
		dst->keys, sizeof(struct key));

	write_tree_block(root, path->nodes[level + 1]);
	write_tree_block(root, t);
	write_tree_block(root, src_buffer);

	/* then fixup the leaf pointer in the path */
	if (path->slots[level] >= src->header.nritems) {
		path->slots[level] -= src->header.nritems;
		tree_block_release(root, path->nodes[level]);
		path->nodes[level] = t;
		path->slots[level + 1] += 1;
	} else {
		tree_block_release(root, t);
	}
	return 0;
}

int insert_ptr(struct ctree_root *root,
		struct ctree_path *path, struct key *key,
		u64 blocknr, int level)
{
	struct tree_buffer *t = path->nodes[level];
	struct node *c = &path->nodes[level]->node;
	struct node *b;
	struct tree_buffer *b_buffer;
	struct tree_buffer *bal[MAX_LEVEL];
	int bal_level = level;
	int mid;
	int bal_start = -1;

	memset(bal, 0, ARRAY_SIZE(bal));
	while(t && t->node.header.nritems == NODEPTRS_PER_BLOCK) {
		c = &t->node;
		if (push_node_left(root, path,
		   node_level(c->header.flags)) == 0)
			break;
		if (push_node_right(root, path,
		   node_level(c->header.flags)) == 0)
			break;
		bal_start = bal_level;
		if (bal_level == MAX_LEVEL - 1)
			BUG();
		b_buffer = alloc_free_block(root);
		b = &b_buffer->node;
		b->header.flags = c->header.flags;
		b->header.blocknr = b_buffer->blocknr;
		mid = (c->header.nritems + 1) / 2;
		memcpy(b->keys, c->keys + mid,
			(c->header.nritems - mid) * sizeof(struct key));
		memcpy(b->blockptrs, c->blockptrs + mid,
			(c->header.nritems - mid) * sizeof(u64));
		b->header.nritems = c->header.nritems - mid;
		c->header.nritems = mid;

		write_tree_block(root, t);
		write_tree_block(root, b_buffer);

		bal[bal_level] = b_buffer;
		if (bal_level == MAX_LEVEL - 1)
			break;
		bal_level += 1;
		t = path->nodes[bal_level];
	}
	while(bal_start > 0) {
		b_buffer = bal[bal_start];
		c = &path->nodes[bal_start]->node;
		__insert_ptr(root, path, b_buffer->node.keys, b_buffer->blocknr,
				path->slots[bal_start + 1] + 1, bal_start + 1);
		if (path->slots[bal_start] >= c->header.nritems) {
			path->slots[bal_start] -= c->header.nritems;
			tree_block_release(root, path->nodes[bal_start]);
			path->nodes[bal_start] = b_buffer;
			path->slots[bal_start + 1] += 1;
		} else {
			tree_block_release(root, b_buffer);
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
	struct tree_buffer *right_buf = path->nodes[0];
	struct leaf *right = &right_buf->leaf;
	struct tree_buffer *t;
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
	t = read_tree_block(root, path->nodes[1]->node.blockptrs[slot - 1]);
	left = &t->leaf;
	free_space = leaf_free_space(left);
	if (free_space < data_size + sizeof(struct item)) {
		tree_block_release(root, t);
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
		tree_block_release(root, t);
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
	BUG_ON(old_left_nritems < 0);

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

	write_tree_block(root, t);
	write_tree_block(root, right_buf);

	fixup_low_keys(root, path, &right->items[0].key, 1);

	/* then fixup the leaf pointer in the path */
	if (path->slots[0] < push_items) {
		path->slots[0] += old_left_nritems;
		tree_block_release(root, path->nodes[0]);
		path->nodes[0] = t;
		path->slots[1] -= 1;
	} else {
		tree_block_release(root, t);
		path->slots[0] -= push_items;
	}
	BUG_ON(path->slots[0] < 0);
	return 0;
}

int split_leaf(struct ctree_root *root, struct ctree_path *path, int data_size)
{
	struct tree_buffer *l_buf = path->nodes[0];
	struct leaf *l = &l_buf->leaf;
	int nritems;
	int mid;
	int slot;
	struct leaf *right;
	struct tree_buffer *right_buffer;
	int space_needed = data_size + sizeof(struct item);
	int data_copy_size;
	int rt_data_off;
	int i;
	int ret;

	if (push_leaf_left(root, path, data_size) == 0) {
		l_buf = path->nodes[0];
		l = &l_buf->leaf;
		if (leaf_free_space(l) >= sizeof(struct item) + data_size)
			return 0;
	}
	slot = path->slots[0];
	nritems = l->header.nritems;
	mid = (nritems + 1)/ 2;

	right_buffer = alloc_free_block(root);
	BUG_ON(!right_buffer);
	BUG_ON(mid == nritems);
	right = &right_buffer->leaf;
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
	right->header.blocknr = right_buffer->blocknr;
	right->header.flags = node_level(0);
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
			  right_buffer->blocknr, 1);

	write_tree_block(root, right_buffer);
	write_tree_block(root, l_buf);

	BUG_ON(path->slots[0] != slot);
	if (mid <= slot) {
		tree_block_release(root, path->nodes[0]);
		path->nodes[0] = right_buffer;
		path->slots[0] -= mid;
		path->slots[1] += 1;
	} else
		tree_block_release(root, right_buffer);
	BUG_ON(path->slots[0] < 0);
	return ret;
}

int insert_item(struct ctree_root *root, struct key *key,
			  void *data, int data_size)
{
	int ret;
	int slot;
	int slot_orig;
	struct leaf *leaf;
	struct tree_buffer *leaf_buf;
	unsigned int nritems;
	unsigned int data_end;
	struct ctree_path path;

	if (!root->node) {
		struct tree_buffer *t;
		t = alloc_free_block(root);
		BUG_ON(!t);
		t->node.header.nritems = 0;
		t->node.header.flags = node_level(0);
		t->node.header.blocknr = t->blocknr;
		root->node = t;
		write_tree_block(root, t);
	}
	init_path(&path);
	ret = search_slot(root, key, &path);
	if (ret == 0) {
		release_path(root, &path);
		return -EEXIST;
	}

	slot_orig = path.slots[0];
	leaf_buf = path.nodes[0];
	leaf = &leaf_buf->leaf;
	if (leaf_free_space(leaf) <  sizeof(struct item) + data_size) {
		split_leaf(root, &path, data_size);
		leaf_buf = path.nodes[0];
		leaf = &path.nodes[0]->leaf;
	}
	nritems = leaf->header.nritems;
	data_end = leaf_data_end(leaf);

	if (leaf_free_space(leaf) <  sizeof(struct item) + data_size)
		BUG();

	slot = path.slots[0];
	BUG_ON(slot < 0);
	if (slot == 0)
		fixup_low_keys(root, &path, key, 1);
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
	write_tree_block(root, leaf_buf);
	if (leaf_free_space(leaf) < 0)
		BUG();
	release_path(root, &path);
	return 0;
}

int del_ptr(struct ctree_root *root, struct ctree_path *path, int level)
{
	int slot;
	struct tree_buffer *t;
	struct node *node;
	int nritems;

	while(1) {
		t = path->nodes[level];
		if (!t)
			break;
		node = &t->node;
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
		write_tree_block(root, t);
		if (node->header.nritems != 0) {
			int tslot;
			if (slot == 0)
				fixup_low_keys(root, path, node->keys,
					       level + 1);
			tslot = path->slots[level+1];
			t->count++;
			push_node_left(root, path, level);
			if (node->header.nritems) {
				push_node_right(root, path, level);
			}
			if (node->header.nritems) {
				tree_block_release(root, t);
				break;
			}
			tree_block_release(root, t);
			path->slots[level+1] = tslot;
		}
		if (t == root->node) {
			/* just turn the root into a leaf and break */
			root->node->node.header.flags = node_level(0);
			write_tree_block(root, t);
			break;
		}
		level++;
		if (!path->nodes[level])
			BUG();
	}
	return 0;
}

int del_item(struct ctree_root *root, struct ctree_path *path)
{
	int slot;
	struct leaf *leaf;
	struct tree_buffer *leaf_buf;
	int doff;
	int dsize;

	leaf_buf = path->nodes[0];
	leaf = &leaf_buf->leaf;
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
		if (leaf_buf == root->node) {
			leaf->header.flags = node_level(0);
			write_tree_block(root, leaf_buf);
		} else
			del_ptr(root, path, 1);
	} else {
		if (slot == 0)
			fixup_low_keys(root, path, &leaf->items[0].key, 1);
		write_tree_block(root, leaf_buf);
		if (leaf_space_used(leaf, 0, leaf->header.nritems) <
		    LEAF_DATA_SIZE / 4) {
			/* push_leaf_left fixes the path.
			 * make sure the path still points to our leaf
			 * for possible call to del_ptr below
			 */
			slot = path->slots[1];
			leaf_buf->count++;
			push_leaf_left(root, path, 1);
			if (leaf->header.nritems == 0) {
				path->slots[1] = slot;
				del_ptr(root, path, 1);
			}
			tree_block_release(root, leaf_buf);
		}
	}
	return 0;
}

void print_leaf(struct leaf *l)
{
	int i;
	int nr = l->header.nritems;
	struct item *item;
	printf("leaf %lu total ptrs %d free space %d\n", l->header.blocknr, nr,
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
void print_tree(struct ctree_root *root, struct tree_buffer *t)
{
	int i;
	int nr;
	struct node *c;

	if (!t)
		return;
	c = &t->node;
	nr = c->header.nritems;
	if (c->header.blocknr != t->blocknr)
		BUG();
	if (is_leaf(c->header.flags)) {
		print_leaf((struct leaf *)c);
		return;
	}
	printf("node %lu level %d total ptrs %d free spc %lu\n", t->blocknr,
	        node_level(c->header.flags), c->header.nritems,
		NODEPTRS_PER_BLOCK - c->header.nritems);
	fflush(stdout);
	for (i = 0; i < nr; i++) {
		printf("\tkey %d (%lu %u %lu) block %lu\n",
		       i,
		       c->keys[i].objectid, c->keys[i].flags, c->keys[i].offset,
		       c->blockptrs[i]);
		fflush(stdout);
	}
	for (i = 0; i < nr; i++) {
		struct tree_buffer *next_buf = read_tree_block(root,
							    c->blockptrs[i]);
		struct node *next = &next_buf->node;
		if (is_leaf(next->header.flags) &&
		    node_level(c->header.flags) != 1)
			BUG();
		if (node_level(next->header.flags) !=
			node_level(c->header.flags) - 1)
			BUG();
		print_tree(root, next_buf);
		tree_block_release(root, next_buf);
	}

}

/* for testing only */
int next_key(int i, int max_key) {
	return rand() % max_key;
	// return i;
}

int main() {
	struct ctree_root *root;
	struct key ins;
	struct key last = { (u64)-1, 0, 0};
	char *buf;
	int i;
	int num;
	int ret;
	int run_size = 1000000;
	int max_key = 100000000;
	int tree_size = 0;
	struct ctree_path path;

	radix_tree_init();


	root = open_ctree("dbfile");

	srand(55);
	for (i = 0; i < run_size; i++) {
		buf = malloc(64);
		num = next_key(i, max_key);
		// num = i;
		sprintf(buf, "string-%d", num);
		// printf("insert %d\n", num);
		ins.objectid = num;
		ins.offset = 0;
		ins.flags = 0;
		ret = insert_item(root, &ins, buf, strlen(buf));
		if (!ret)
			tree_size++;
	}
	close_ctree(root);
	root = open_ctree("dbfile");
	printf("starting search\n");
	srand(55);
	for (i = 0; i < run_size; i++) {
		num = next_key(i, max_key);
		ins.objectid = num;
		init_path(&path);
		ret = search_slot(root, &ins, &path);
		if (ret) {
			print_tree(root, root->node);
			printf("unable to find %d\n", num);
			exit(1);
		}
		release_path(root, &path);
	}
	close_ctree(root);
	root = open_ctree("dbfile");
	printf("node %p level %d total ptrs %d free spc %lu\n", root->node,
	        node_level(root->node->node.header.flags),
		root->node->node.header.nritems,
		NODEPTRS_PER_BLOCK - root->node->node.header.nritems);
	printf("all searches good, deleting some items\n");
	i = 0;
	srand(55);
	for (i = 0 ; i < run_size/4; i++) {
		num = next_key(i, max_key);
		ins.objectid = num;
		init_path(&path);
		ret = search_slot(root, &ins, &path);
		if (ret)
			continue;
		ret = del_item(root, &path);
		if (ret != 0)
			BUG();
		release_path(root, &path);
		tree_size--;
	}
	srand(128);
	for (i = 0; i < run_size; i++) {
		buf = malloc(64);
		num = next_key(i, max_key);
		sprintf(buf, "string-%d", num);
		ins.objectid = num;
		ret = insert_item(root, &ins, buf, strlen(buf));
		if (!ret)
			tree_size++;
	}
	close_ctree(root);
	root = open_ctree("dbfile");
	printf("starting search2\n");
	srand(128);
	for (i = 0; i < run_size; i++) {
		num = next_key(i, max_key);
		ins.objectid = num;
		init_path(&path);
		ret = search_slot(root, &ins, &path);
		if (ret) {
			print_tree(root, root->node);
			printf("unable to find %d\n", num);
			exit(1);
		}
		release_path(root, &path);
	}
	printf("starting big long delete run\n");
	while(root->node && root->node->node.header.nritems > 0) {
		struct leaf *leaf;
		int slot;
		ins.objectid = (u64)-1;
		init_path(&path);
		ret = search_slot(root, &ins, &path);
		if (ret == 0)
			BUG();

		leaf = &path.nodes[0]->leaf;
		slot = path.slots[0];
		if (slot != leaf->header.nritems)
			BUG();
		while(path.slots[0] > 0) {
			path.slots[0] -= 1;
			slot = path.slots[0];
			leaf = &path.nodes[0]->leaf;

			if (comp_keys(&last, &leaf->items[slot].key) <= 0)
				BUG();
			memcpy(&last, &leaf->items[slot].key, sizeof(last));
			ret = del_item(root, &path);
			if (ret != 0) {
				printf("del_item returned %d\n", ret);
				BUG();
			}
			tree_size--;
		}
		release_path(root, &path);
	}
	close_ctree(root);
	printf("tree size is now %d\n", tree_size);
	return 0;
}
