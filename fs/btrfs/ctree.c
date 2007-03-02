#include <stdio.h>
#include <stdlib.h>
#include "kerncompat.h"
#include "radix-tree.h"
#include "ctree.h"
#include "disk-io.h"
#include "print-tree.h"

static int split_node(struct ctree_root *root, struct ctree_path *path,
		      int level);
static int split_leaf(struct ctree_root *root, struct ctree_path *path,
		      int data_size);
static int push_node_left(struct ctree_root *root, struct tree_buffer *dst,
			  struct tree_buffer *src);
static int balance_node_right(struct ctree_root *root,
			      struct tree_buffer *dst_buf,
			      struct tree_buffer *src_buf);
static int del_ptr(struct ctree_root *root, struct ctree_path *path, int level,
		   int slot);

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
	memset(p, 0, sizeof(*p));
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

int check_node(struct ctree_path *path, int level)
{
	int i;
	struct node *parent = NULL;
	struct node *node = &path->nodes[level]->node;
	int parent_slot;

	if (path->nodes[level + 1])
		parent = &path->nodes[level + 1]->node;
	parent_slot = path->slots[level + 1];
	if (parent && node->header.nritems > 0) {
		struct key *parent_key;
		parent_key = &parent->keys[parent_slot];
		BUG_ON(memcmp(parent_key, node->keys, sizeof(struct key)));
		BUG_ON(parent->blockptrs[parent_slot] != node->header.blocknr);
	}
	BUG_ON(node->header.nritems > NODEPTRS_PER_BLOCK);
	for (i = 0; i < node->header.nritems - 2; i++) {
		BUG_ON(comp_keys(&node->keys[i], &node->keys[i+1]) >= 0);
	}
	return 0;
}

int check_leaf(struct ctree_path *path, int level)
{
	int i;
	struct leaf *leaf = &path->nodes[level]->leaf;
	struct node *parent = NULL;
	int parent_slot;

	if (path->nodes[level + 1])
		parent = &path->nodes[level + 1]->node;
	parent_slot = path->slots[level + 1];
	if (parent && leaf->header.nritems > 0) {
		struct key *parent_key;
		parent_key = &parent->keys[parent_slot];
		BUG_ON(memcmp(parent_key, &leaf->items[0].key,
		       sizeof(struct key)));
		BUG_ON(parent->blockptrs[parent_slot] != leaf->header.blocknr);
	}
	for (i = 0; i < leaf->header.nritems - 2; i++) {
		BUG_ON(comp_keys(&leaf->items[i].key,
		                 &leaf->items[i+1].key) >= 0);
		BUG_ON(leaf->items[i].offset != leaf->items[i + 1].offset +
		    leaf->items[i + 1].size);
		if (i == 0) {
			BUG_ON(leaf->items[i].offset + leaf->items[i].size !=
				LEAF_DATA_SIZE);
		}
	}
	BUG_ON(leaf_free_space(leaf) < 0);
	return 0;
}

int check_block(struct ctree_path *path, int level)
{
	if (level == 0)
		return check_leaf(path, level);
	return check_node(path, level);
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

/*
 * simple bin_search frontend that does the right thing for
 * leaves vs nodes
 */
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

struct tree_buffer *read_node_slot(struct ctree_root *root,
				   struct tree_buffer *parent_buf,
				   int slot)
{
	struct node *node = &parent_buf->node;
	if (slot < 0)
		return NULL;
	if (slot >= node->header.nritems)
		return NULL;
	return read_tree_block(root, node->blockptrs[slot]);
}

static int balance_level(struct ctree_root *root, struct ctree_path *path,
			int level)
{
	struct tree_buffer *right_buf;
	struct tree_buffer *mid_buf;
	struct tree_buffer *left_buf;
	struct tree_buffer *parent_buf = NULL;
	struct node *right = NULL;
	struct node *mid;
	struct node *left = NULL;
	struct node *parent = NULL;
	int ret = 0;
	int wret;
	int pslot;
	int orig_slot = path->slots[level];
	u64 orig_ptr;

	if (level == 0)
		return 0;

	mid_buf = path->nodes[level];
	mid = &mid_buf->node;
	orig_ptr = mid->blockptrs[orig_slot];

	if (level < MAX_LEVEL - 1)
		parent_buf = path->nodes[level + 1];
	pslot = path->slots[level + 1];

	if (!parent_buf) {
		struct tree_buffer *child;
		u64 blocknr = mid_buf->blocknr;

		if (mid->header.nritems != 1)
			return 0;

		/* promote the child to a root */
		child = read_node_slot(root, mid_buf, 0);
		BUG_ON(!child);
		root->node = child;
		path->nodes[level] = NULL;
		/* once for the path */
		tree_block_release(root, mid_buf);
		/* once for the root ptr */
		tree_block_release(root, mid_buf);
		clean_tree_block(root, mid_buf);
		return free_extent(root, blocknr, 1);
	}
	parent = &parent_buf->node;

	if (mid->header.nritems > NODEPTRS_PER_BLOCK / 4)
		return 0;

	left_buf = read_node_slot(root, parent_buf, pslot - 1);
	right_buf = read_node_slot(root, parent_buf, pslot + 1);

	/* first, try to make some room in the middle buffer */
	if (left_buf) {
		left = &left_buf->node;
		orig_slot += left->header.nritems;
		wret = push_node_left(root, left_buf, mid_buf);
		if (wret < 0)
			ret = wret;
	}

	/*
	 * then try to empty the right most buffer into the middle
	 */
	if (right_buf) {
		right = &right_buf->node;
		wret = push_node_left(root, mid_buf, right_buf);
		if (wret < 0)
			ret = wret;
		if (right->header.nritems == 0) {
			u64 blocknr = right_buf->blocknr;
			tree_block_release(root, right_buf);
			clean_tree_block(root, right_buf);
			right_buf = NULL;
			right = NULL;
			wret = del_ptr(root, path, level + 1, pslot + 1);
			if (wret)
				ret = wret;
			wret = free_extent(root, blocknr, 1);
			if (wret)
				ret = wret;
		} else {
			memcpy(parent->keys + pslot + 1, right->keys,
				sizeof(struct key));
			wret = dirty_tree_block(root, parent_buf);
			if (wret)
				ret = wret;
		}
	}
	if (mid->header.nritems == 1) {
		/*
		 * we're not allowed to leave a node with one item in the
		 * tree during a delete.  A deletion from lower in the tree
		 * could try to delete the only pointer in this node.
		 * So, pull some keys from the left.
		 * There has to be a left pointer at this point because
		 * otherwise we would have pulled some pointers from the
		 * right
		 */
		BUG_ON(!left_buf);
		wret = balance_node_right(root, mid_buf, left_buf);
		if (wret < 0)
			ret = wret;
		BUG_ON(wret == 1);
	}
	if (mid->header.nritems == 0) {
		/* we've managed to empty the middle node, drop it */
		u64 blocknr = mid_buf->blocknr;
		tree_block_release(root, mid_buf);
		clean_tree_block(root, mid_buf);
		mid_buf = NULL;
		mid = NULL;
		wret = del_ptr(root, path, level + 1, pslot);
		if (wret)
			ret = wret;
		wret = free_extent(root, blocknr, 1);
		if (wret)
			ret = wret;
	} else {
		/* update the parent key to reflect our changes */
		memcpy(parent->keys + pslot, mid->keys, sizeof(struct key));
		wret = dirty_tree_block(root, parent_buf);
		if (wret)
			ret = wret;
	}

	/* update the path */
	if (left_buf) {
		if (left->header.nritems > orig_slot) {
			left_buf->count++; // released below
			path->nodes[level] = left_buf;
			path->slots[level + 1] -= 1;
			path->slots[level] = orig_slot;
			if (mid_buf)
				tree_block_release(root, mid_buf);
		} else {
			orig_slot -= left->header.nritems;
			path->slots[level] = orig_slot;
		}
	}
	/* double check we haven't messed things up */
	check_block(path, level);
	if (orig_ptr != path->nodes[level]->node.blockptrs[path->slots[level]])
		BUG();

	if (right_buf)
		tree_block_release(root, right_buf);
	if (left_buf)
		tree_block_release(root, left_buf);
	return ret;
}

/*
 * look for key in the tree.  path is filled in with nodes along the way
 * if key is found, we return zero and you can find the item in the leaf
 * level of the path (level 0)
 *
 * If the key isn't found, the path points to the slot where it should
 * be inserted, and 1 is returned.  If there are other errors during the
 * search a negative error number is returned.
 *
 * if ins_len > 0, nodes and leaves will be split as we walk down the
 * tree.  if ins_len < 0, nodes will be merged as we walk down the tree (if
 * possible)
 */
int search_slot(struct ctree_root *root, struct key *key,
		struct ctree_path *p, int ins_len)
{
	struct tree_buffer *b;
	struct node *c;
	int slot;
	int ret;
	int level;

again:
	b = root->node;
	b->count++;
	while (b) {
		c = &b->node;
		level = node_level(c->header.flags);
		p->nodes[level] = b;
		ret = check_block(p, level);
		if (ret)
			return -1;
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
			} else if (ins_len < 0) {
				int sret = balance_level(root, p, level);
				if (sret)
					return sret;
				b = p->nodes[level];
				if (!b)
					goto again;
				c = &b->node;
				slot = p->slots[level];
				BUG_ON(c->header.nritems == 1);
			}
			b = read_tree_block(root, c->blockptrs[slot]);
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
			BUG_ON(root->node->count == 1);
			return ret;
		}
	}
	BUG_ON(root->node->count == 1);
	return 1;
}

/*
 * adjust the pointers going up the tree, starting at level
 * making sure the right key of each node is points to 'key'.
 * This is used after shifting pointers to the left, so it stops
 * fixing up pointers when a given leaf/node is not in slot 0 of the
 * higher levels
 *
 * If this fails to write a tree block, it returns -1, but continues
 * fixing up the blocks in ram so the tree is consistent.
 */
static int fixup_low_keys(struct ctree_root *root,
			   struct ctree_path *path, struct key *key,
			   int level)
{
	int i;
	int ret = 0;
	int wret;
	for (i = level; i < MAX_LEVEL; i++) {
		struct node *t;
		int tslot = path->slots[i];
		if (!path->nodes[i])
			break;
		t = &path->nodes[i]->node;
		memcpy(t->keys + tslot, key, sizeof(*key));
		wret = dirty_tree_block(root, path->nodes[i]);
		if (wret)
			ret = wret;
		if (tslot != 0)
			break;
	}
	return ret;
}

/*
 * try to push data from one node into the next node left in the
 * tree.
 *
 * returns 0 if some ptrs were pushed left, < 0 if there was some horrible
 * error, and > 0 if there was no room in the left hand block.
 */
static int push_node_left(struct ctree_root *root, struct tree_buffer *dst_buf,
			  struct tree_buffer *src_buf)
{
	struct node *src = &src_buf->node;
	struct node *dst = &dst_buf->node;
	int push_items = 0;
	int src_nritems;
	int dst_nritems;
	int ret = 0;
	int wret;

	src_nritems = src->header.nritems;
	dst_nritems = dst->header.nritems;
	push_items = NODEPTRS_PER_BLOCK - dst_nritems;
	if (push_items <= 0) {
		return 1;
	}

	if (src_nritems < push_items)
		push_items = src_nritems;

	memcpy(dst->keys + dst_nritems, src->keys,
		push_items * sizeof(struct key));
	memcpy(dst->blockptrs + dst_nritems, src->blockptrs,
		push_items * sizeof(u64));
	if (push_items < src_nritems) {
		memmove(src->keys, src->keys + push_items,
			(src_nritems - push_items) * sizeof(struct key));
		memmove(src->blockptrs, src->blockptrs + push_items,
			(src_nritems - push_items) * sizeof(u64));
	}
	src->header.nritems -= push_items;
	dst->header.nritems += push_items;

	wret = dirty_tree_block(root, src_buf);
	if (wret < 0)
		ret = wret;

	wret = dirty_tree_block(root, dst_buf);
	if (wret < 0)
		ret = wret;
	return ret;
}

/*
 * try to push data from one node into the next node right in the
 * tree.
 *
 * returns 0 if some ptrs were pushed, < 0 if there was some horrible
 * error, and > 0 if there was no room in the right hand block.
 *
 * this will  only push up to 1/2 the contents of the left node over
 */
static int balance_node_right(struct ctree_root *root,
			      struct tree_buffer *dst_buf,
			      struct tree_buffer *src_buf)
{
	struct node *src = &src_buf->node;
	struct node *dst = &dst_buf->node;
	int push_items = 0;
	int max_push;
	int src_nritems;
	int dst_nritems;
	int ret = 0;
	int wret;

	src_nritems = src->header.nritems;
	dst_nritems = dst->header.nritems;
	push_items = NODEPTRS_PER_BLOCK - dst_nritems;
	if (push_items <= 0) {
		return 1;
	}

	max_push = src_nritems / 2 + 1;
	/* don't try to empty the node */
	if (max_push > src_nritems)
		return 1;
	if (max_push < push_items)
		push_items = max_push;

	memmove(dst->keys + push_items, dst->keys,
		dst_nritems * sizeof(struct key));
	memmove(dst->blockptrs + push_items, dst->blockptrs,
		dst_nritems * sizeof(u64));
	memcpy(dst->keys, src->keys + src_nritems - push_items,
		push_items * sizeof(struct key));
	memcpy(dst->blockptrs, src->blockptrs + src_nritems - push_items,
		push_items * sizeof(u64));

	src->header.nritems -= push_items;
	dst->header.nritems += push_items;

	wret = dirty_tree_block(root, src_buf);
	if (wret < 0)
		ret = wret;

	wret = dirty_tree_block(root, dst_buf);
	if (wret < 0)
		ret = wret;
	return ret;
}

/*
 * helper function to insert a new root level in the tree.
 * A new node is allocated, and a single item is inserted to
 * point to the existing root
 *
 * returns zero on success or < 0 on failure.
 */
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
	dirty_tree_block(root, t);
	path->nodes[level] = t;
	path->slots[level] = 0;
	return 0;
}

/*
 * worker function to insert a single pointer in a node.
 * the node should have enough room for the pointer already
 *
 * slot and level indicate where you want the key to go, and
 * blocknr is the block the key points to.
 *
 * returns zero on success and < 0 on any error
 */
static int insert_ptr(struct ctree_root *root,
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
	dirty_tree_block(root, path->nodes[level]);
	return 0;
}

/*
 * split the node at the specified level in path in two.
 * The path is corrected to point to the appropriate node after the split
 *
 * Before splitting this tries to make some room in the node by pushing
 * left and right, if either one works, it returns right away.
 *
 * returns 0 on success and < 0 on failure
 */
static int split_node(struct ctree_root *root, struct ctree_path *path,
		      int level)
{
	struct tree_buffer *t;
	struct node *c;
	struct tree_buffer *split_buffer;
	struct node *split;
	int mid;
	int ret;
	int wret;

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
	ret = 0;

	wret = dirty_tree_block(root, t);
	if (wret)
		ret = wret;
	wret = dirty_tree_block(root, split_buffer);
	if (wret)
		ret = wret;
	wret = insert_ptr(root, path, split->keys, split_buffer->blocknr,
			  path->slots[level + 1] + 1, level + 1);
	if (wret)
		ret = wret;

	if (path->slots[level] >= mid) {
		path->slots[level] -= mid;
		tree_block_release(root, t);
		path->nodes[level] = split_buffer;
		path->slots[level + 1] += 1;
	} else {
		tree_block_release(root, split_buffer);
	}
	return ret;
}

/*
 * how many bytes are required to store the items in a leaf.  start
 * and nr indicate which items in the leaf to check.  This totals up the
 * space used both by the item structs and the item data
 */
static int leaf_space_used(struct leaf *l, int start, int nr)
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
 *
 * returns 1 if the push failed because the other node didn't have enough
 * room, 0 if everything worked out and < 0 if there were major errors.
 */
static int push_leaf_right(struct ctree_root *root, struct ctree_path *path,
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

	dirty_tree_block(root, left_buf);
	dirty_tree_block(root, right_buf);
	memcpy(upper->node.keys + slot + 1,
		&right->items[0].key, sizeof(struct key));
	dirty_tree_block(root, upper);
	/* then fixup the leaf pointer in the path */
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
static int push_leaf_left(struct ctree_root *root, struct ctree_path *path,
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
	int ret = 0;
	int wret;

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

	wret = dirty_tree_block(root, t);
	if (wret)
		ret = wret;
	wret = dirty_tree_block(root, right_buf);
	if (wret)
		ret = wret;

	wret = fixup_low_keys(root, path, &right->items[0].key, 1);
	if (wret)
		ret = wret;

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
	return ret;
}

/*
 * split the path's leaf in two, making sure there is at least data_size
 * available for the resulting leaf level of the path.
 *
 * returns 0 if all went well and < 0 on failure.
 */
static int split_leaf(struct ctree_root *root, struct ctree_path *path,
		      int data_size)
{
	struct tree_buffer *l_buf;
	struct leaf *l;
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
	int wret;

	wret = push_leaf_left(root, path, data_size);
	if (wret < 0)
		return wret;
	if (wret) {
		wret = push_leaf_right(root, path, data_size);
		if (wret < 0)
			return wret;
	}
	l_buf = path->nodes[0];
	l = &l_buf->leaf;

	/* did the pushes work? */
	if (leaf_free_space(l) >= sizeof(struct item) + data_size)
		return 0;

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
		/* FIXME, just alloc a new leaf here */
		if (leaf_space_used(l, mid, nritems - mid) + space_needed >
			LEAF_DATA_SIZE)
			BUG();
	} else {
		/* FIXME, just alloc a new leaf here */
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
	ret = 0;
	wret = insert_ptr(root, path, &right->items[0].key,
			  right_buffer->blocknr, path->slots[1] + 1, 1);
	if (wret)
		ret = wret;
	wret = dirty_tree_block(root, right_buffer);
	if (wret)
		ret = wret;
	wret = dirty_tree_block(root, l_buf);
	if (wret)
		ret = wret;

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
	int ret = 0;
	int wret;
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
	if (ret < 0)
		goto out;

	slot_orig = path.slots[0];
	leaf_buf = path.nodes[0];
	leaf = &leaf_buf->leaf;

	nritems = leaf->header.nritems;
	data_end = leaf_data_end(leaf);

	if (leaf_free_space(leaf) <  sizeof(struct item) + data_size)
		BUG();

	slot = path.slots[0];
	BUG_ON(slot < 0);
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

	ret = 0;
	if (slot == 0)
		ret = fixup_low_keys(root, &path, key, 1);

	wret = dirty_tree_block(root, leaf_buf);
	if (wret)
		ret = wret;

	if (leaf_free_space(leaf) < 0)
		BUG();
	check_leaf(&path, 0);
out:
	release_path(root, &path);
	return ret;
}

/*
 * delete the pointer from a given node.
 *
 * If the delete empties a node, the node is removed from the tree,
 * continuing all the way the root if required.  The root is converted into
 * a leaf if all the nodes are emptied.
 */
static int del_ptr(struct ctree_root *root, struct ctree_path *path, int level,
		   int slot)
{
	struct node *node;
	struct tree_buffer *parent = path->nodes[level];
	int nritems;
	int ret = 0;
	int wret;

	node = &parent->node;
	nritems = node->header.nritems;

	if (slot != nritems -1) {
		memmove(node->keys + slot, node->keys + slot + 1,
			sizeof(struct key) * (nritems - slot - 1));
		memmove(node->blockptrs + slot,
			node->blockptrs + slot + 1,
			sizeof(u64) * (nritems - slot - 1));
	}
	node->header.nritems--;
	if (node->header.nritems == 0 && parent == root->node) {
		BUG_ON(node_level(root->node->node.header.flags) != 1);
		/* just turn the root into a leaf and break */
		root->node->node.header.flags = node_level(0);
	} else if (slot == 0) {
		wret = fixup_low_keys(root, path, node->keys, level + 1);
		if (wret)
			ret = wret;
	}
	wret = dirty_tree_block(root, parent);
	if (wret)
		ret = wret;
	return ret;
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
	int ret = 0;
	int wret;

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
			dirty_tree_block(root, leaf_buf);
		} else {
			clean_tree_block(root, leaf_buf);
			wret = del_ptr(root, path, 1, path->slots[1]);
			if (wret)
				ret = wret;
			wret = free_extent(root, leaf_buf->blocknr, 1);
			if (wret)
				ret = wret;
		}
	} else {
		int used = leaf_space_used(leaf, 0, leaf->header.nritems);
		if (slot == 0) {
			wret = fixup_low_keys(root, path,
						   &leaf->items[0].key, 1);
			if (wret)
				ret = wret;
		}
		wret = dirty_tree_block(root, leaf_buf);
		if (wret)
			ret = wret;

		/* delete the leaf if it is mostly empty */
		if (used < LEAF_DATA_SIZE / 3) {
			/* push_leaf_left fixes the path.
			 * make sure the path still points to our leaf
			 * for possible call to del_ptr below
			 */
			slot = path->slots[1];
			leaf_buf->count++;
			wret = push_leaf_left(root, path, 1);
			if (wret < 0)
				ret = wret;
			if (path->nodes[0] == leaf_buf &&
			    leaf->header.nritems) {
				wret = push_leaf_right(root, path, 1);
				if (wret < 0)
					ret = wret;
			}
			if (leaf->header.nritems == 0) {
				u64 blocknr = leaf_buf->blocknr;
				clean_tree_block(root, leaf_buf);
				wret = del_ptr(root, path, 1, slot);
				if (wret)
					ret = wret;
				tree_block_release(root, leaf_buf);
				wret = free_extent(root, blocknr, 1);
				if (wret)
					ret = wret;
			} else {
				tree_block_release(root, leaf_buf);
			}
		}
	}
	return ret;
}

/*
 * walk up the tree as far as required to find the next leaf.
 * returns 0 if it found something or 1 if there are no greater leaves.
 * returns < 0 on io errors.
 */
int next_leaf(struct ctree_root *root, struct ctree_path *path)
{
	int slot;
	int level = 1;
	u64 blocknr;
	struct tree_buffer *c;
	struct tree_buffer *next = NULL;

	while(level < MAX_LEVEL) {
		if (!path->nodes[level])
			return 1;
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

