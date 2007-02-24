#include <stdio.h>
#include <stdlib.h>
#include "kerncompat.h"
#include "radix-tree.h"
#include "ctree.h"
#include "disk-io.h"
#include "print-tree.h"

int split_node(struct ctree_root *root, struct ctree_path *path, int level);
int split_leaf(struct ctree_root *root, struct ctree_path *path, int data_size);
int push_node_left(struct ctree_root *root, struct ctree_path *path, int level);
int push_node_right(struct ctree_root *root,
		    struct ctree_path *path, int level);
int del_ptr(struct ctree_root *root, struct ctree_path *path, int level);

inline void init_path(struct ctree_path *p)
{
	memset(p, 0, sizeof(*p));
}

void release_path(struct ctree_root *root, struct ctree_path *p)
{
	int i;
	for (i = 0; i < MAX_LEVEL; i++) {
		if (!p->nodes[i])
			break;
		tree_block_release(root, p->nodes[i]);
	}
}

/*
 * The leaf data grows from end-to-front in the node.
 * this returns the address of the start of the last item,
 * which is the stop of the leaf data stack
 */
static inline unsigned int leaf_data_end(struct leaf *leaf)
{
	unsigned int nr = leaf->header.nritems;
	if (nr == 0)
		return sizeof(leaf->data);
	return leaf->items[nr-1].offset;
}

/*
 * The space between the end of the leaf items and
 * the start of the leaf data.  IOW, how much room
 * the leaf has left for both items and data
 */
int leaf_free_space(struct leaf *leaf)
{
	int data_end = leaf_data_end(leaf);
	int nritems = leaf->header.nritems;
	char *items_end = (char *)(leaf->items + nritems + 1);
	return (char *)(leaf->data + data_end) - (char *)items_end;
}

/*
 * compare two keys in a memcmp fashion
 */
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

/*
 * search for key in the array p.  items p are item_size apart
 * and there are 'max' items in p
 * the slot in the array is returned via slot, and it points to
 * the place where you would insert key if it is not found in
 * the array.
 *
 * slot may point to max if the key is bigger than all of the keys
 */
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

/*
 * look for key in the tree.  path is filled in with nodes along the way
 * if key is found, we return zero and you can find the item in the leaf
 * level of the path (level 0)
 *
 * If the key isn't found, the path points to the slot where it should
 * be inserted.
 */
int search_slot(struct ctree_root *root, struct key *key,
		struct ctree_path *p, int ins_len)
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
			if (ins_len > 0 &&
			    c->header.nritems == NODEPTRS_PER_BLOCK) {
				int sret = split_node(root, p, level);
				BUG_ON(sret > 0);
				if (sret)
					return sret;
				b = p->nodes[level];
				c = &b->node;
				slot = p->slots[level];
			} else if (ins_len < 0 &&
				   c->header.nritems <= NODEPTRS_PER_BLOCK/4) {
				u64 blocknr = b->blocknr;
				slot = p->slots[level +1];
				b->count++;
				if (push_node_left(root, p, level))
					push_node_right(root, p, level);
				if (c->header.nritems == 0 &&
				    level < MAX_LEVEL - 1 &&
				    p->nodes[level + 1]) {
					int tslot = p->slots[level + 1];

					p->slots[level + 1] = slot;
					del_ptr(root, p, level + 1);
					p->slots[level + 1] = tslot;
					tree_block_release(root, b);
					free_extent(root, blocknr, 1);
				} else {
					tree_block_release(root, b);
				}
				b = p->nodes[level];
				c = &b->node;
				slot = p->slots[level];
			}
			b = read_tree_block(root, c->blockptrs[slot]);
			continue;
		} else {
			struct leaf *l = (struct leaf *)c;
			p->slots[level] = slot;
			if (ins_len > 0 && leaf_free_space(l) <
			    sizeof(struct item) + ins_len) {
				int sret = split_leaf(root, p, ins_len);
				BUG_ON(sret > 0);
				if (sret)
					return sret;
			}
			return ret;
		}
	}
	return -1;
}

/*
 * adjust the pointers going up the tree, starting at level
 * making sure the right key of each node is points to 'key'.
 * This is used after shifting pointers to the left, so it stops
 * fixing up pointers when a given leaf/node is not in slot 0 of the
 * higher levels
 */
static void fixup_low_keys(struct ctree_root *root,
			   struct ctree_path *path, struct key *key,
			   int level)
{
	int i;
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

/*
 * try to push data from one node into the next node left in the
 * tree.  The src node is found at specified level in the path.
 * If some bytes were pushed, return 0, otherwise return 1.
 *
 * Lower nodes/leaves in the path are not touched, higher nodes may
 * be modified to reflect the push.
 *
 * The path is altered to reflect the push.
 */
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

/*
 * try to push data from one node into the next node right in the
 * tree.  The src node is found at specified level in the path.
 * If some bytes were pushed, return 0, otherwise return 1.
 *
 * Lower nodes/leaves in the path are not touched, higher nodes may
 * be modified to reflect the push.
 *
 * The path is altered to reflect the push.
 */
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

	/* can't push from the root */
	if (level == MAX_LEVEL - 1 || path->nodes[level + 1] == 0)
		return 1;

	/* only try to push inside the node higher up */
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

	/* then fixup the pointers in the path */
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

static int insert_new_root(struct ctree_root *root,
			   struct ctree_path *path, int level)
{
	struct tree_buffer *t;
	struct node *lower;
	struct node *c;
	struct key *lower_key;

	BUG_ON(path->nodes[level]);
	BUG_ON(path->nodes[level-1] != root->node);

	t = alloc_free_block(root);
	c = &t->node;
	memset(c, 0, sizeof(c));
	c->header.nritems = 1;
	c->header.flags = node_level(level);
	c->header.blocknr = t->blocknr;
	c->header.parentid = root->node->node.header.parentid;
	lower = &path->nodes[level-1]->node;
	if (is_leaf(lower->header.flags))
		lower_key = &((struct leaf *)lower)->items[0].key;
	else
		lower_key = lower->keys;
	memcpy(c->keys, lower_key, sizeof(struct key));
	c->blockptrs[0] = path->nodes[level-1]->blocknr;
	/* the super has an extra ref to root->node */
	tree_block_release(root, root->node);
	root->node = t;
	t->count++;
	write_tree_block(root, t);
	path->nodes[level] = t;
	path->slots[level] = 0;
	return 0;
}

/*
 * worker function to insert a single pointer in a node.
 * the node should have enough room for the pointer already
 * slot and level indicate where you want the key to go, and
 * blocknr is the block the key points to.
 */
int insert_ptr(struct ctree_root *root,
		struct ctree_path *path, struct key *key,
		u64 blocknr, int slot, int level)
{
	struct node *lower;
	int nritems;

	BUG_ON(!path->nodes[level]);
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

int split_node(struct ctree_root *root, struct ctree_path *path, int level)
{
	struct tree_buffer *t;
	struct node *c;
	struct tree_buffer *split_buffer;
	struct node *split;
	int mid;
	int ret;

	ret = push_node_left(root, path, level);
	if (!ret)
		return 0;
	ret = push_node_right(root, path, level);
	if (!ret)
		return 0;
	t = path->nodes[level];
	c = &t->node;
	if (t == root->node) {
		/* trying to split the root, lets make a new one */
		ret = insert_new_root(root, path, level + 1);
		if (ret)
			return ret;
	}
	split_buffer = alloc_free_block(root);
	split = &split_buffer->node;
	split->header.flags = c->header.flags;
	split->header.blocknr = split_buffer->blocknr;
	split->header.parentid = root->node->node.header.parentid;
	mid = (c->header.nritems + 1) / 2;
	memcpy(split->keys, c->keys + mid,
		(c->header.nritems - mid) * sizeof(struct key));
	memcpy(split->blockptrs, c->blockptrs + mid,
		(c->header.nritems - mid) * sizeof(u64));
	split->header.nritems = c->header.nritems - mid;
	c->header.nritems = mid;
	write_tree_block(root, t);
	write_tree_block(root, split_buffer);
	insert_ptr(root, path, split->keys, split_buffer->blocknr,
		     path->slots[level + 1] + 1, level + 1);
	if (path->slots[level] >= mid) {
		path->slots[level] -= mid;
		tree_block_release(root, t);
		path->nodes[level] = split_buffer;
		path->slots[level + 1] += 1;
	} else {
		tree_block_release(root, split_buffer);
	}
	return 0;
}

/*
 * how many bytes are required to store the items in a leaf.  start
 * and nr indicate which items in the leaf to check.  This totals up the
 * space used both by the item structs and the item data
 */
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

/*
 * push some data in the path leaf to the right, trying to free up at
 * least data_size bytes.  returns zero if the push worked, nonzero otherwise
 */
int push_leaf_right(struct ctree_root *root, struct ctree_path *path,
		   int data_size)
{
	struct tree_buffer *left_buf = path->nodes[0];
	struct leaf *left = &left_buf->leaf;
	struct leaf *right;
	struct tree_buffer *right_buf;
	struct tree_buffer *upper;
	int slot;
	int i;
	int free_space;
	int push_space = 0;
	int push_items = 0;
	struct item *item;

	slot = path->slots[1];
	if (!path->nodes[1]) {
		return 1;
	}
	upper = path->nodes[1];
	if (slot >= upper->node.header.nritems - 1) {
		return 1;
	}
	right_buf = read_tree_block(root, upper->node.blockptrs[slot + 1]);
	right = &right_buf->leaf;
	free_space = leaf_free_space(right);
	if (free_space < data_size + sizeof(struct item)) {
		tree_block_release(root, right_buf);
		return 1;
	}
	for (i = left->header.nritems - 1; i >= 0; i--) {
		item = left->items + i;
		if (path->slots[0] == i)
			push_space += data_size + sizeof(*item);
		if (item->size + sizeof(*item) + push_space > free_space)
			break;
		push_items++;
		push_space += item->size + sizeof(*item);
	}
	if (push_items == 0) {
		tree_block_release(root, right_buf);
		return 1;
	}
	/* push left to right */
	push_space = left->items[left->header.nritems - push_items].offset +
		     left->items[left->header.nritems - push_items].size;
	push_space -= leaf_data_end(left);
	/* make room in the right data area */
	memmove(right->data + leaf_data_end(right) - push_space,
		right->data + leaf_data_end(right),
		LEAF_DATA_SIZE - leaf_data_end(right));
	/* copy from the left data area */
	memcpy(right->data + LEAF_DATA_SIZE - push_space,
		left->data + leaf_data_end(left),
		push_space);
	memmove(right->items + push_items, right->items,
		right->header.nritems * sizeof(struct item));
	/* copy the items from left to right */
	memcpy(right->items, left->items + left->header.nritems - push_items,
		push_items * sizeof(struct item));

	/* update the item pointers */
	right->header.nritems += push_items;
	push_space = LEAF_DATA_SIZE;
	for (i = 0; i < right->header.nritems; i++) {
		right->items[i].offset = push_space - right->items[i].size;
		push_space = right->items[i].offset;
	}
	left->header.nritems -= push_items;

	write_tree_block(root, left_buf);
	write_tree_block(root, right_buf);
	memcpy(upper->node.keys + slot + 1,
		&right->items[0].key, sizeof(struct key));
	write_tree_block(root, upper);
	/* then fixup the leaf pointer in the path */
	// FIXME use nritems in here somehow
	if (path->slots[0] >= left->header.nritems) {
		path->slots[0] -= left->header.nritems;
		tree_block_release(root, path->nodes[0]);
		path->nodes[0] = right_buf;
		path->slots[1] += 1;
	} else {
		tree_block_release(root, right_buf);
	}
	return 0;
}
/*
 * push some data in the path leaf to the left, trying to free up at
 * least data_size bytes.  returns zero if the push worked, nonzero otherwise
 */
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

/*
 * split the path's leaf in two, making sure there is at least data_size
 * available for the resulting leaf level of the path.
 */
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

	if (push_leaf_left(root, path, data_size) == 0 ||
	    push_leaf_right(root, path, data_size) == 0) {
		l_buf = path->nodes[0];
		l = &l_buf->leaf;
		if (leaf_free_space(l) >= sizeof(struct item) + data_size)
			return 0;
	}
	if (!path->nodes[1]) {
		ret = insert_new_root(root, path, 1);
		if (ret)
			return ret;
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
	right->header.parentid = root->node->node.header.parentid;
	data_copy_size = l->items[mid].offset + l->items[mid].size -
			 leaf_data_end(l);
	memcpy(right->items, l->items + mid,
	       (nritems - mid) * sizeof(struct item));
	memcpy(right->data + LEAF_DATA_SIZE - data_copy_size,
	       l->data + leaf_data_end(l), data_copy_size);
	rt_data_off = LEAF_DATA_SIZE -
		     (l->items[mid].offset + l->items[mid].size);

	for (i = 0; i < right->header.nritems; i++)
		right->items[i].offset += rt_data_off;

	l->header.nritems = mid;
	ret = insert_ptr(root, path, &right->items[0].key,
			  right_buffer->blocknr, path->slots[1] + 1, 1);
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

/*
 * Given a key and some data, insert an item into the tree.
 * This does all the path init required, making room in the tree if needed.
 */
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

	/* create a root if there isn't one */
	if (!root->node)
		BUG();
	init_path(&path);
	ret = search_slot(root, key, &path, data_size);
	if (ret == 0) {
		release_path(root, &path);
		return -EEXIST;
	}

	slot_orig = path.slots[0];
	leaf_buf = path.nodes[0];
	leaf = &leaf_buf->leaf;

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
	/* copy the new data in */
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

/*
 * delete the pointer from a given node.
 *
 * If the delete empties a node, the node is removed from the tree,
 * continuing all the way the root if required.  The root is converted into
 * a leaf if all the nodes are emptied.
 */
int del_ptr(struct ctree_root *root, struct ctree_path *path, int level)
{
	int slot;
	struct tree_buffer *t;
	struct node *node;
	int nritems;
	u64 blocknr;

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
		blocknr = t->blocknr;
		if (node->header.nritems != 0) {
			if (slot == 0)
				fixup_low_keys(root, path, node->keys,
					       level + 1);
			break;
		}
		if (t == root->node) {
			/* just turn the root into a leaf and break */
			root->node->node.header.flags = node_level(0);
			write_tree_block(root, t);
			break;
		}
		level++;
		free_extent(root, blocknr, 1);
		if (!path->nodes[level])
			BUG();
	}
	return 0;
}

/*
 * delete the item at the leaf level in path.  If that empties
 * the leaf, remove it from the tree
 */
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
	/* delete the leaf if we've emptied it */
	if (leaf->header.nritems == 0) {
		if (leaf_buf == root->node) {
			leaf->header.flags = node_level(0);
			write_tree_block(root, leaf_buf);
		} else {
			del_ptr(root, path, 1);
			free_extent(root, leaf_buf->blocknr, 1);
		}
	} else {
		int used = leaf_space_used(leaf, 0, leaf->header.nritems);
		if (slot == 0)
			fixup_low_keys(root, path, &leaf->items[0].key, 1);
		write_tree_block(root, leaf_buf);
		/* delete the leaf if it is mostly empty */
		if (used < LEAF_DATA_SIZE / 3) {
			/* push_leaf_left fixes the path.
			 * make sure the path still points to our leaf
			 * for possible call to del_ptr below
			 */
			slot = path->slots[1];
			leaf_buf->count++;
			push_leaf_left(root, path, 1);
			if (leaf->header.nritems)
				push_leaf_right(root, path, 1);
			if (leaf->header.nritems == 0) {
				u64 blocknr = leaf_buf->blocknr;
				path->slots[1] = slot;
				del_ptr(root, path, 1);
				tree_block_release(root, leaf_buf);
				free_extent(root, blocknr, 1);
			} else {
				tree_block_release(root, leaf_buf);
			}
		}
	}
	return 0;
}

int next_leaf(struct ctree_root *root, struct ctree_path *path)
{
	int slot;
	int level = 1;
	u64 blocknr;
	struct tree_buffer *c;
	struct tree_buffer *next = NULL;

	while(level < MAX_LEVEL) {
		if (!path->nodes[level])
			return -1;
		slot = path->slots[level] + 1;
		c = path->nodes[level];
		if (slot >= c->node.header.nritems) {
			level++;
			continue;
		}
		blocknr = c->node.blockptrs[slot];
		if (next)
			tree_block_release(root, next);
		next = read_tree_block(root, blocknr);
		break;
	}
	path->slots[level] = slot;
	while(1) {
		level--;
		c = path->nodes[level];
		tree_block_release(root, c);
		path->nodes[level] = next;
		path->slots[level] = 0;
		if (!level)
			break;
		next = read_tree_block(root, next->node.blockptrs[0]);
	}
	return 0;
}

/* for testing only */
int next_key(int i, int max_key) {
	return rand() % max_key;
	//return i;
}

int main() {
	struct ctree_root *root;
	struct key ins;
	struct key last = { (u64)-1, 0, 0};
	char *buf;
	int i;
	int num;
	int ret;
	int run_size = 20000000;
	int max_key =  100000000;
	int tree_size = 0;
	struct ctree_path path;
	struct ctree_super_block super;

	radix_tree_init();


	root = open_ctree("dbfile", &super);

	srand(55);
	for (i = 0; i < run_size; i++) {
		buf = malloc(64);
		num = next_key(i, max_key);
		// num = i;
		sprintf(buf, "string-%d", num);
		if (i % 10000 == 0)
			fprintf(stderr, "insert %d:%d\n", num, i);
		ins.objectid = num;
		ins.offset = 0;
		ins.flags = 0;
		ret = insert_item(root, &ins, buf, strlen(buf));
		if (!ret)
			tree_size++;
		free(buf);
	}
	write_ctree_super(root, &super);
	close_ctree(root);

	root = open_ctree("dbfile", &super);
	printf("starting search\n");
	srand(55);
	for (i = 0; i < run_size; i++) {
		num = next_key(i, max_key);
		ins.objectid = num;
		init_path(&path);
		if (i % 10000 == 0)
			fprintf(stderr, "search %d:%d\n", num, i);
		ret = search_slot(root, &ins, &path, 0);
		if (ret) {
			print_tree(root, root->node);
			printf("unable to find %d\n", num);
			exit(1);
		}
		release_path(root, &path);
	}
	write_ctree_super(root, &super);
	close_ctree(root);
	root = open_ctree("dbfile", &super);
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
		ret = search_slot(root, &ins, &path, -1);
		if (!ret) {
			if (i % 10000 == 0)
				fprintf(stderr, "del %d:%d\n", num, i);
			ret = del_item(root, &path);
			if (ret != 0)
				BUG();
			tree_size--;
		}
		release_path(root, &path);
	}
	write_ctree_super(root, &super);
	close_ctree(root);
	root = open_ctree("dbfile", &super);
	srand(128);
	for (i = 0; i < run_size; i++) {
		buf = malloc(64);
		num = next_key(i, max_key);
		sprintf(buf, "string-%d", num);
		ins.objectid = num;
		if (i % 10000 == 0)
			fprintf(stderr, "insert %d:%d\n", num, i);
		ret = insert_item(root, &ins, buf, strlen(buf));
		if (!ret)
			tree_size++;
		free(buf);
	}
	write_ctree_super(root, &super);
	close_ctree(root);
	root = open_ctree("dbfile", &super);
	srand(128);
	printf("starting search2\n");
	for (i = 0; i < run_size; i++) {
		num = next_key(i, max_key);
		ins.objectid = num;
		init_path(&path);
		if (i % 10000 == 0)
			fprintf(stderr, "search %d:%d\n", num, i);
		ret = search_slot(root, &ins, &path, 0);
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
		ret = search_slot(root, &ins, &path, -1);
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
			if (tree_size % 10000 == 0)
				printf("big del %d:%d\n", tree_size, i);
			ret = del_item(root, &path);
			if (ret != 0) {
				printf("del_item returned %d\n", ret);
				BUG();
			}
			tree_size--;
		}
		release_path(root, &path);
	}
	printf("tree size is now %d\n", tree_size);
	printf("map tree\n");
	print_tree(root->extent_root, root->extent_root->node);
	write_ctree_super(root, &super);
	close_ctree(root);
	return 0;
}
