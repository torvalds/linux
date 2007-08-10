/*
 * Copyright (C) 2007 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License v2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 */

#include "ctree.h"
#include "disk-io.h"
#include "transaction.h"

static int split_node(struct btrfs_trans_handle *trans, struct btrfs_root
		      *root, struct btrfs_path *path, int level);
static int split_leaf(struct btrfs_trans_handle *trans, struct btrfs_root
		      *root, struct btrfs_key *ins_key,
		      struct btrfs_path *path, int data_size);
static int push_node_left(struct btrfs_trans_handle *trans, struct btrfs_root
			  *root, struct buffer_head *dst, struct buffer_head
			  *src);
static int balance_node_right(struct btrfs_trans_handle *trans, struct
			      btrfs_root *root, struct buffer_head *dst_buf,
			      struct buffer_head *src_buf);
static int del_ptr(struct btrfs_trans_handle *trans, struct btrfs_root *root,
		   struct btrfs_path *path, int level, int slot);

inline void btrfs_init_path(struct btrfs_path *p)
{
	memset(p, 0, sizeof(*p));
}

struct btrfs_path *btrfs_alloc_path(void)
{
	struct btrfs_path *path;
	path = kmem_cache_alloc(btrfs_path_cachep, GFP_NOFS);
	if (path)
		btrfs_init_path(path);
	return path;
}

void btrfs_free_path(struct btrfs_path *p)
{
	btrfs_release_path(NULL, p);
	kmem_cache_free(btrfs_path_cachep, p);
}

void btrfs_release_path(struct btrfs_root *root, struct btrfs_path *p)
{
	int i;
	for (i = 0; i < BTRFS_MAX_LEVEL; i++) {
		if (!p->nodes[i])
			break;
		btrfs_block_release(root, p->nodes[i]);
	}
	memset(p, 0, sizeof(*p));
}

static int __btrfs_cow_block(struct btrfs_trans_handle *trans, struct btrfs_root
			   *root, struct buffer_head *buf, struct buffer_head
			   *parent, int parent_slot, struct buffer_head
			   **cow_ret, u64 search_start, u64 empty_size)
{
	struct buffer_head *cow;
	struct btrfs_node *cow_node;
	int ret = 0;
	int different_trans = 0;

	WARN_ON(root->ref_cows && trans->transid != root->last_trans);
	WARN_ON(!buffer_uptodate(buf));
	cow = btrfs_alloc_free_block(trans, root, search_start, empty_size);
	if (IS_ERR(cow))
		return PTR_ERR(cow);

	cow_node = btrfs_buffer_node(cow);
	if (buf->b_size != root->blocksize || cow->b_size != root->blocksize)
		WARN_ON(1);

	memcpy(cow_node, btrfs_buffer_node(buf), root->blocksize);
	btrfs_set_header_blocknr(&cow_node->header, bh_blocknr(cow));
	btrfs_set_header_generation(&cow_node->header, trans->transid);
	btrfs_set_header_owner(&cow_node->header, root->root_key.objectid);

	WARN_ON(btrfs_header_generation(btrfs_buffer_header(buf)) >
		trans->transid);
	if (btrfs_header_generation(btrfs_buffer_header(buf)) !=
				    trans->transid) {
		different_trans = 1;
		ret = btrfs_inc_ref(trans, root, buf);
		if (ret)
			return ret;
	} else {
		clean_tree_block(trans, root, buf);
	}

	if (buf == root->node) {
		root->node = cow;
		get_bh(cow);
		if (buf != root->commit_root) {
			btrfs_free_extent(trans, root, bh_blocknr(buf), 1, 1);
		}
		btrfs_block_release(root, buf);
	} else {
		btrfs_set_node_blockptr(btrfs_buffer_node(parent), parent_slot,
					bh_blocknr(cow));
		btrfs_mark_buffer_dirty(parent);
		WARN_ON(btrfs_header_generation(btrfs_buffer_header(parent)) !=
				    trans->transid);
		btrfs_free_extent(trans, root, bh_blocknr(buf), 1, 1);
	}
	btrfs_block_release(root, buf);
	btrfs_mark_buffer_dirty(cow);
	*cow_ret = cow;
	return 0;
}

int btrfs_cow_block(struct btrfs_trans_handle *trans, struct btrfs_root
			   *root, struct buffer_head *buf, struct buffer_head
			   *parent, int parent_slot, struct buffer_head
			   **cow_ret)
{
	u64 search_start;
	if (trans->transaction != root->fs_info->running_transaction) {
		printk(KERN_CRIT "trans %Lu running %Lu\n", trans->transid,
		       root->fs_info->running_transaction->transid);
		WARN_ON(1);
	}
	if (trans->transid != root->fs_info->generation) {
		printk(KERN_CRIT "trans %Lu running %Lu\n", trans->transid,
		       root->fs_info->generation);
		WARN_ON(1);
	}
	if (btrfs_header_generation(btrfs_buffer_header(buf)) ==
				    trans->transid) {
		*cow_ret = buf;
		return 0;
	}

	search_start = bh_blocknr(buf) & ~((u64)65535);
	return __btrfs_cow_block(trans, root, buf, parent,
				 parent_slot, cow_ret, search_start, 0);
}

static int close_blocks(u64 blocknr, u64 other)
{
	if (blocknr < other && other - blocknr < 8)
		return 1;
	if (blocknr > other && blocknr - other < 8)
		return 1;
	return 0;
}

int btrfs_realloc_node(struct btrfs_trans_handle *trans,
		       struct btrfs_root *root, struct buffer_head *parent,
		       int cache_only, u64 *last_ret)
{
	struct btrfs_node *parent_node;
	struct buffer_head *cur_bh;
	struct buffer_head *tmp_bh;
	u64 blocknr;
	u64 search_start = *last_ret;
	u64 last_block = 0;
	u64 other;
	u32 parent_nritems;
	int start_slot;
	int end_slot;
	int i;
	int err = 0;

	if (trans->transaction != root->fs_info->running_transaction) {
		printk(KERN_CRIT "trans %Lu running %Lu\n", trans->transid,
		       root->fs_info->running_transaction->transid);
		WARN_ON(1);
	}
	if (trans->transid != root->fs_info->generation) {
		printk(KERN_CRIT "trans %Lu running %Lu\n", trans->transid,
		       root->fs_info->generation);
		WARN_ON(1);
	}
	parent_node = btrfs_buffer_node(parent);
	parent_nritems = btrfs_header_nritems(&parent_node->header);

	start_slot = 0;
	end_slot = parent_nritems;

	if (parent_nritems == 1)
		return 0;

	for (i = start_slot; i < end_slot; i++) {
		int close = 1;
		blocknr = btrfs_node_blockptr(parent_node, i);
		if (last_block == 0)
			last_block = blocknr;
		if (i > 0) {
			other = btrfs_node_blockptr(parent_node, i - 1);
			close = close_blocks(blocknr, other);
		}
		if (close && i < end_slot - 1) {
			other = btrfs_node_blockptr(parent_node, i + 1);
			close = close_blocks(blocknr, other);
		}
		if (close) {
			last_block = blocknr;
			continue;
		}

		cur_bh = btrfs_find_tree_block(root, blocknr);
		if (!cur_bh || !buffer_uptodate(cur_bh) ||
		    buffer_locked(cur_bh)) {
			if (cache_only) {
				brelse(cur_bh);
				continue;
			}
			brelse(cur_bh);
			cur_bh = read_tree_block(root, blocknr);
		}
		if (search_start == 0)
			search_start = last_block & ~((u64)65535);

		err = __btrfs_cow_block(trans, root, cur_bh, parent, i,
					&tmp_bh, search_start,
					min(8, end_slot - i));
		if (err)
			break;
		search_start = bh_blocknr(tmp_bh);
		brelse(tmp_bh);
	}
	return err;
}

/*
 * The leaf data grows from end-to-front in the node.
 * this returns the address of the start of the last item,
 * which is the stop of the leaf data stack
 */
static inline unsigned int leaf_data_end(struct btrfs_root *root,
					 struct btrfs_leaf *leaf)
{
	u32 nr = btrfs_header_nritems(&leaf->header);
	if (nr == 0)
		return BTRFS_LEAF_DATA_SIZE(root);
	return btrfs_item_offset(leaf->items + nr - 1);
}

/*
 * compare two keys in a memcmp fashion
 */
static int comp_keys(struct btrfs_disk_key *disk, struct btrfs_key *k2)
{
	struct btrfs_key k1;

	btrfs_disk_key_to_cpu(&k1, disk);

	if (k1.objectid > k2->objectid)
		return 1;
	if (k1.objectid < k2->objectid)
		return -1;
	if (k1.flags > k2->flags)
		return 1;
	if (k1.flags < k2->flags)
		return -1;
	if (k1.offset > k2->offset)
		return 1;
	if (k1.offset < k2->offset)
		return -1;
	return 0;
}

static int check_node(struct btrfs_root *root, struct btrfs_path *path,
		      int level)
{
	struct btrfs_node *parent = NULL;
	struct btrfs_node *node = btrfs_buffer_node(path->nodes[level]);
	int parent_slot;
	int slot;
	struct btrfs_key cpukey;
	u32 nritems = btrfs_header_nritems(&node->header);

	if (path->nodes[level + 1])
		parent = btrfs_buffer_node(path->nodes[level + 1]);

	slot = path->slots[level];
	BUG_ON(nritems == 0);
	if (parent) {
		struct btrfs_disk_key *parent_key;

		parent_slot = path->slots[level + 1];
		parent_key = &parent->ptrs[parent_slot].key;
		BUG_ON(memcmp(parent_key, &node->ptrs[0].key,
			      sizeof(struct btrfs_disk_key)));
		BUG_ON(btrfs_node_blockptr(parent, parent_slot) !=
		       btrfs_header_blocknr(&node->header));
	}
	BUG_ON(nritems > BTRFS_NODEPTRS_PER_BLOCK(root));
	if (slot != 0) {
		btrfs_disk_key_to_cpu(&cpukey, &node->ptrs[slot - 1].key);
		BUG_ON(comp_keys(&node->ptrs[slot].key, &cpukey) <= 0);
	}
	if (slot < nritems - 1) {
		btrfs_disk_key_to_cpu(&cpukey, &node->ptrs[slot + 1].key);
		BUG_ON(comp_keys(&node->ptrs[slot].key, &cpukey) >= 0);
	}
	return 0;
}

static int check_leaf(struct btrfs_root *root, struct btrfs_path *path,
		      int level)
{
	struct btrfs_leaf *leaf = btrfs_buffer_leaf(path->nodes[level]);
	struct btrfs_node *parent = NULL;
	int parent_slot;
	int slot = path->slots[0];
	struct btrfs_key cpukey;

	u32 nritems = btrfs_header_nritems(&leaf->header);

	if (path->nodes[level + 1])
		parent = btrfs_buffer_node(path->nodes[level + 1]);

	BUG_ON(btrfs_leaf_free_space(root, leaf) < 0);

	if (nritems == 0)
		return 0;

	if (parent) {
		struct btrfs_disk_key *parent_key;

		parent_slot = path->slots[level + 1];
		parent_key = &parent->ptrs[parent_slot].key;

		BUG_ON(memcmp(parent_key, &leaf->items[0].key,
		       sizeof(struct btrfs_disk_key)));
		BUG_ON(btrfs_node_blockptr(parent, parent_slot) !=
		       btrfs_header_blocknr(&leaf->header));
	}
	if (slot != 0) {
		btrfs_disk_key_to_cpu(&cpukey, &leaf->items[slot - 1].key);
		BUG_ON(comp_keys(&leaf->items[slot].key, &cpukey) <= 0);
		BUG_ON(btrfs_item_offset(leaf->items + slot - 1) !=
			btrfs_item_end(leaf->items + slot));
	}
	if (slot < nritems - 1) {
		btrfs_disk_key_to_cpu(&cpukey, &leaf->items[slot + 1].key);
		BUG_ON(comp_keys(&leaf->items[slot].key, &cpukey) >= 0);
		BUG_ON(btrfs_item_offset(leaf->items + slot) !=
			btrfs_item_end(leaf->items + slot + 1));
	}
	BUG_ON(btrfs_item_offset(leaf->items) +
	       btrfs_item_size(leaf->items) != BTRFS_LEAF_DATA_SIZE(root));
	return 0;
}

static int check_block(struct btrfs_root *root, struct btrfs_path *path,
			int level)
{
	struct btrfs_node *node = btrfs_buffer_node(path->nodes[level]);
	if (memcmp(node->header.fsid, root->fs_info->disk_super->fsid,
		   sizeof(node->header.fsid)))
		BUG();
	if (level == 0)
		return check_leaf(root, path, level);
	return check_node(root, path, level);
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
static int generic_bin_search(char *p, int item_size, struct btrfs_key *key,
		       int max, int *slot)
{
	int low = 0;
	int high = max;
	int mid;
	int ret;
	struct btrfs_disk_key *tmp;

	while(low < high) {
		mid = (low + high) / 2;
		tmp = (struct btrfs_disk_key *)(p + mid * item_size);
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
static int bin_search(struct btrfs_node *c, struct btrfs_key *key, int *slot)
{
	if (btrfs_is_leaf(c)) {
		struct btrfs_leaf *l = (struct btrfs_leaf *)c;
		return generic_bin_search((void *)l->items,
					  sizeof(struct btrfs_item),
					  key, btrfs_header_nritems(&c->header),
					  slot);
	} else {
		return generic_bin_search((void *)c->ptrs,
					  sizeof(struct btrfs_key_ptr),
					  key, btrfs_header_nritems(&c->header),
					  slot);
	}
	return -1;
}

static struct buffer_head *read_node_slot(struct btrfs_root *root,
				   struct buffer_head *parent_buf,
				   int slot)
{
	struct btrfs_node *node = btrfs_buffer_node(parent_buf);
	if (slot < 0)
		return NULL;
	if (slot >= btrfs_header_nritems(&node->header))
		return NULL;
	return read_tree_block(root, btrfs_node_blockptr(node, slot));
}

static int balance_level(struct btrfs_trans_handle *trans, struct btrfs_root
			 *root, struct btrfs_path *path, int level)
{
	struct buffer_head *right_buf;
	struct buffer_head *mid_buf;
	struct buffer_head *left_buf;
	struct buffer_head *parent_buf = NULL;
	struct btrfs_node *right = NULL;
	struct btrfs_node *mid;
	struct btrfs_node *left = NULL;
	struct btrfs_node *parent = NULL;
	int ret = 0;
	int wret;
	int pslot;
	int orig_slot = path->slots[level];
	int err_on_enospc = 0;
	u64 orig_ptr;

	if (level == 0)
		return 0;

	mid_buf = path->nodes[level];
	mid = btrfs_buffer_node(mid_buf);
	orig_ptr = btrfs_node_blockptr(mid, orig_slot);

	if (level < BTRFS_MAX_LEVEL - 1)
		parent_buf = path->nodes[level + 1];
	pslot = path->slots[level + 1];

	/*
	 * deal with the case where there is only one pointer in the root
	 * by promoting the node below to a root
	 */
	if (!parent_buf) {
		struct buffer_head *child;
		u64 blocknr = bh_blocknr(mid_buf);

		if (btrfs_header_nritems(&mid->header) != 1)
			return 0;

		/* promote the child to a root */
		child = read_node_slot(root, mid_buf, 0);
		BUG_ON(!child);
		root->node = child;
		path->nodes[level] = NULL;
		clean_tree_block(trans, root, mid_buf);
		wait_on_buffer(mid_buf);
		/* once for the path */
		btrfs_block_release(root, mid_buf);
		/* once for the root ptr */
		btrfs_block_release(root, mid_buf);
		return btrfs_free_extent(trans, root, blocknr, 1, 1);
	}
	parent = btrfs_buffer_node(parent_buf);

	if (btrfs_header_nritems(&mid->header) >
	    BTRFS_NODEPTRS_PER_BLOCK(root) / 4)
		return 0;

	if (btrfs_header_nritems(&mid->header) < 2)
		err_on_enospc = 1;

	left_buf = read_node_slot(root, parent_buf, pslot - 1);
	right_buf = read_node_slot(root, parent_buf, pslot + 1);

	/* first, try to make some room in the middle buffer */
	if (left_buf) {
		wret = btrfs_cow_block(trans, root, left_buf,
				       parent_buf, pslot - 1, &left_buf);
		if (wret) {
			ret = wret;
			goto enospc;
		}
		left = btrfs_buffer_node(left_buf);
		orig_slot += btrfs_header_nritems(&left->header);
		wret = push_node_left(trans, root, left_buf, mid_buf);
		if (wret < 0)
			ret = wret;
		if (btrfs_header_nritems(&mid->header) < 2)
			err_on_enospc = 1;
	}

	/*
	 * then try to empty the right most buffer into the middle
	 */
	if (right_buf) {
		wret = btrfs_cow_block(trans, root, right_buf,
				       parent_buf, pslot + 1, &right_buf);
		if (wret) {
			ret = wret;
			goto enospc;
		}

		right = btrfs_buffer_node(right_buf);
		wret = push_node_left(trans, root, mid_buf, right_buf);
		if (wret < 0 && wret != -ENOSPC)
			ret = wret;
		if (btrfs_header_nritems(&right->header) == 0) {
			u64 blocknr = bh_blocknr(right_buf);
			clean_tree_block(trans, root, right_buf);
			wait_on_buffer(right_buf);
			btrfs_block_release(root, right_buf);
			right_buf = NULL;
			right = NULL;
			wret = del_ptr(trans, root, path, level + 1, pslot +
				       1);
			if (wret)
				ret = wret;
			wret = btrfs_free_extent(trans, root, blocknr, 1, 1);
			if (wret)
				ret = wret;
		} else {
			btrfs_memcpy(root, parent,
				     &parent->ptrs[pslot + 1].key,
				     &right->ptrs[0].key,
				     sizeof(struct btrfs_disk_key));
			btrfs_mark_buffer_dirty(parent_buf);
		}
	}
	if (btrfs_header_nritems(&mid->header) == 1) {
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
		wret = balance_node_right(trans, root, mid_buf, left_buf);
		if (wret < 0) {
			ret = wret;
			goto enospc;
		}
		BUG_ON(wret == 1);
	}
	if (btrfs_header_nritems(&mid->header) == 0) {
		/* we've managed to empty the middle node, drop it */
		u64 blocknr = bh_blocknr(mid_buf);
		clean_tree_block(trans, root, mid_buf);
		wait_on_buffer(mid_buf);
		btrfs_block_release(root, mid_buf);
		mid_buf = NULL;
		mid = NULL;
		wret = del_ptr(trans, root, path, level + 1, pslot);
		if (wret)
			ret = wret;
		wret = btrfs_free_extent(trans, root, blocknr, 1, 1);
		if (wret)
			ret = wret;
	} else {
		/* update the parent key to reflect our changes */
		btrfs_memcpy(root, parent,
			     &parent->ptrs[pslot].key, &mid->ptrs[0].key,
			     sizeof(struct btrfs_disk_key));
		btrfs_mark_buffer_dirty(parent_buf);
	}

	/* update the path */
	if (left_buf) {
		if (btrfs_header_nritems(&left->header) > orig_slot) {
			get_bh(left_buf);
			path->nodes[level] = left_buf;
			path->slots[level + 1] -= 1;
			path->slots[level] = orig_slot;
			if (mid_buf)
				btrfs_block_release(root, mid_buf);
		} else {
			orig_slot -= btrfs_header_nritems(&left->header);
			path->slots[level] = orig_slot;
		}
	}
	/* double check we haven't messed things up */
	check_block(root, path, level);
	if (orig_ptr !=
	    btrfs_node_blockptr(btrfs_buffer_node(path->nodes[level]),
				path->slots[level]))
		BUG();
enospc:
	if (right_buf)
		btrfs_block_release(root, right_buf);
	if (left_buf)
		btrfs_block_release(root, left_buf);
	return ret;
}

/* returns zero if the push worked, non-zero otherwise */
static int push_nodes_for_insert(struct btrfs_trans_handle *trans,
				struct btrfs_root *root,
				struct btrfs_path *path, int level)
{
	struct buffer_head *right_buf;
	struct buffer_head *mid_buf;
	struct buffer_head *left_buf;
	struct buffer_head *parent_buf = NULL;
	struct btrfs_node *right = NULL;
	struct btrfs_node *mid;
	struct btrfs_node *left = NULL;
	struct btrfs_node *parent = NULL;
	int ret = 0;
	int wret;
	int pslot;
	int orig_slot = path->slots[level];
	u64 orig_ptr;

	if (level == 0)
		return 1;

	mid_buf = path->nodes[level];
	mid = btrfs_buffer_node(mid_buf);
	orig_ptr = btrfs_node_blockptr(mid, orig_slot);

	if (level < BTRFS_MAX_LEVEL - 1)
		parent_buf = path->nodes[level + 1];
	pslot = path->slots[level + 1];

	if (!parent_buf)
		return 1;
	parent = btrfs_buffer_node(parent_buf);

	left_buf = read_node_slot(root, parent_buf, pslot - 1);

	/* first, try to make some room in the middle buffer */
	if (left_buf) {
		u32 left_nr;
		left = btrfs_buffer_node(left_buf);
		left_nr = btrfs_header_nritems(&left->header);
		if (left_nr >= BTRFS_NODEPTRS_PER_BLOCK(root) - 1) {
			wret = 1;
		} else {
			ret = btrfs_cow_block(trans, root, left_buf, parent_buf,
					      pslot - 1, &left_buf);
			if (ret)
				wret = 1;
			else {
				left = btrfs_buffer_node(left_buf);
				wret = push_node_left(trans, root,
						      left_buf, mid_buf);
			}
		}
		if (wret < 0)
			ret = wret;
		if (wret == 0) {
			orig_slot += left_nr;
			btrfs_memcpy(root, parent,
				     &parent->ptrs[pslot].key,
				     &mid->ptrs[0].key,
				     sizeof(struct btrfs_disk_key));
			btrfs_mark_buffer_dirty(parent_buf);
			if (btrfs_header_nritems(&left->header) > orig_slot) {
				path->nodes[level] = left_buf;
				path->slots[level + 1] -= 1;
				path->slots[level] = orig_slot;
				btrfs_block_release(root, mid_buf);
			} else {
				orig_slot -=
					btrfs_header_nritems(&left->header);
				path->slots[level] = orig_slot;
				btrfs_block_release(root, left_buf);
			}
			check_node(root, path, level);
			return 0;
		}
		btrfs_block_release(root, left_buf);
	}
	right_buf = read_node_slot(root, parent_buf, pslot + 1);

	/*
	 * then try to empty the right most buffer into the middle
	 */
	if (right_buf) {
		u32 right_nr;
		right = btrfs_buffer_node(right_buf);
		right_nr = btrfs_header_nritems(&right->header);
		if (right_nr >= BTRFS_NODEPTRS_PER_BLOCK(root) - 1) {
			wret = 1;
		} else {
			ret = btrfs_cow_block(trans, root, right_buf,
					      parent_buf, pslot + 1,
					      &right_buf);
			if (ret)
				wret = 1;
			else {
				right = btrfs_buffer_node(right_buf);
				wret = balance_node_right(trans, root,
							  right_buf, mid_buf);
			}
		}
		if (wret < 0)
			ret = wret;
		if (wret == 0) {
			btrfs_memcpy(root, parent,
				     &parent->ptrs[pslot + 1].key,
				     &right->ptrs[0].key,
				     sizeof(struct btrfs_disk_key));
			btrfs_mark_buffer_dirty(parent_buf);
			if (btrfs_header_nritems(&mid->header) <= orig_slot) {
				path->nodes[level] = right_buf;
				path->slots[level + 1] += 1;
				path->slots[level] = orig_slot -
					btrfs_header_nritems(&mid->header);
				btrfs_block_release(root, mid_buf);
			} else {
				btrfs_block_release(root, right_buf);
			}
			check_node(root, path, level);
			return 0;
		}
		btrfs_block_release(root, right_buf);
	}
	check_node(root, path, level);
	return 1;
}

/*
 * readahead one full node of leaves
 */
static void reada_for_search(struct btrfs_root *root, struct btrfs_path *path,
			     int level, int slot)
{
	struct btrfs_node *node;
	int i;
	u32 nritems;
	u64 item_objectid;
	u64 blocknr;
	u64 search;
	u64 cluster_start;
	int ret;
	int nread = 0;
	int direction = path->reada;
	struct radix_tree_root found;
	unsigned long gang[8];
	struct buffer_head *bh;

	if (level == 0)
		return;

	if (!path->nodes[level])
		return;

	node = btrfs_buffer_node(path->nodes[level]);
	search = btrfs_node_blockptr(node, slot);
	bh = btrfs_find_tree_block(root, search);
	if (bh) {
		brelse(bh);
		return;
	}

	init_bit_radix(&found);
	nritems = btrfs_header_nritems(&node->header);
	for (i = slot; i < nritems; i++) {
		item_objectid = btrfs_disk_key_objectid(&node->ptrs[i].key);
		blocknr = btrfs_node_blockptr(node, i);
		set_radix_bit(&found, blocknr);
	}
	if (direction > 0) {
		cluster_start = search - 4;
		if (cluster_start > search)
			cluster_start = 0;
	} else
		cluster_start = search + 4;
	while(1) {
		ret = find_first_radix_bit(&found, gang, 0, ARRAY_SIZE(gang));
		if (!ret)
			break;
		for (i = 0; i < ret; i++) {
			blocknr = gang[i];
			clear_radix_bit(&found, blocknr);
			if (nread > 32)
				continue;
			if (direction > 0 && cluster_start <= blocknr &&
			    cluster_start + 8 > blocknr) {
				cluster_start = blocknr;
				readahead_tree_block(root, blocknr);
				nread++;
			} else if (direction < 0 && cluster_start >= blocknr &&
				   blocknr + 8 > cluster_start) {
				cluster_start = blocknr;
				readahead_tree_block(root, blocknr);
				nread++;
			}
		}
	}
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
int btrfs_search_slot(struct btrfs_trans_handle *trans, struct btrfs_root
		      *root, struct btrfs_key *key, struct btrfs_path *p, int
		      ins_len, int cow)
{
	struct buffer_head *b;
	struct buffer_head *cow_buf;
	struct btrfs_node *c;
	u64 blocknr;
	int slot;
	int ret;
	int level;
	int should_reada = p->reada;
	u8 lowest_level = 0;

	lowest_level = p->lowest_level;
	WARN_ON(lowest_level && ins_len);
	WARN_ON(p->nodes[0] != NULL);
	WARN_ON(!mutex_is_locked(&root->fs_info->fs_mutex));
again:
	b = root->node;
	get_bh(b);
	while (b) {
		c = btrfs_buffer_node(b);
		level = btrfs_header_level(&c->header);
		if (cow) {
			int wret;
			wret = btrfs_cow_block(trans, root, b,
					       p->nodes[level + 1],
					       p->slots[level + 1],
					       &cow_buf);
			if (wret) {
				btrfs_block_release(root, cow_buf);
				return wret;
			}
			b = cow_buf;
			c = btrfs_buffer_node(b);
		}
		BUG_ON(!cow && ins_len);
		if (level != btrfs_header_level(&c->header))
			WARN_ON(1);
		level = btrfs_header_level(&c->header);
		p->nodes[level] = b;
		ret = check_block(root, p, level);
		if (ret)
			return -1;
		ret = bin_search(c, key, &slot);
		if (!btrfs_is_leaf(c)) {
			if (ret && slot > 0)
				slot -= 1;
			p->slots[level] = slot;
			if (ins_len > 0 && btrfs_header_nritems(&c->header) >=
			    BTRFS_NODEPTRS_PER_BLOCK(root) - 1) {
				int sret = split_node(trans, root, p, level);
				BUG_ON(sret > 0);
				if (sret)
					return sret;
				b = p->nodes[level];
				c = btrfs_buffer_node(b);
				slot = p->slots[level];
			} else if (ins_len < 0) {
				int sret = balance_level(trans, root, p,
							 level);
				if (sret)
					return sret;
				b = p->nodes[level];
				if (!b)
					goto again;
				c = btrfs_buffer_node(b);
				slot = p->slots[level];
				BUG_ON(btrfs_header_nritems(&c->header) == 1);
			}
			/* this is only true while dropping a snapshot */
			if (level == lowest_level)
				break;
			blocknr = btrfs_node_blockptr(c, slot);
			if (should_reada)
				reada_for_search(root, p, level, slot);
			b = read_tree_block(root, btrfs_node_blockptr(c, slot));

		} else {
			struct btrfs_leaf *l = (struct btrfs_leaf *)c;
			p->slots[level] = slot;
			if (ins_len > 0 && btrfs_leaf_free_space(root, l) <
			    sizeof(struct btrfs_item) + ins_len) {
				int sret = split_leaf(trans, root, key,
						      p, ins_len);
				BUG_ON(sret > 0);
				if (sret)
					return sret;
			}
			return ret;
		}
	}
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
static int fixup_low_keys(struct btrfs_trans_handle *trans, struct btrfs_root
			  *root, struct btrfs_path *path, struct btrfs_disk_key
			  *key, int level)
{
	int i;
	int ret = 0;
	for (i = level; i < BTRFS_MAX_LEVEL; i++) {
		struct btrfs_node *t;
		int tslot = path->slots[i];
		if (!path->nodes[i])
			break;
		t = btrfs_buffer_node(path->nodes[i]);
		btrfs_memcpy(root, t, &t->ptrs[tslot].key, key, sizeof(*key));
		btrfs_mark_buffer_dirty(path->nodes[i]);
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
static int push_node_left(struct btrfs_trans_handle *trans, struct btrfs_root
			  *root, struct buffer_head *dst_buf, struct
			  buffer_head *src_buf)
{
	struct btrfs_node *src = btrfs_buffer_node(src_buf);
	struct btrfs_node *dst = btrfs_buffer_node(dst_buf);
	int push_items = 0;
	int src_nritems;
	int dst_nritems;
	int ret = 0;

	src_nritems = btrfs_header_nritems(&src->header);
	dst_nritems = btrfs_header_nritems(&dst->header);
	push_items = BTRFS_NODEPTRS_PER_BLOCK(root) - dst_nritems;

	if (push_items <= 0) {
		return 1;
	}

	if (src_nritems < push_items)
		push_items = src_nritems;

	btrfs_memcpy(root, dst, dst->ptrs + dst_nritems, src->ptrs,
		     push_items * sizeof(struct btrfs_key_ptr));
	if (push_items < src_nritems) {
		btrfs_memmove(root, src, src->ptrs, src->ptrs + push_items,
			(src_nritems - push_items) *
			sizeof(struct btrfs_key_ptr));
	}
	btrfs_set_header_nritems(&src->header, src_nritems - push_items);
	btrfs_set_header_nritems(&dst->header, dst_nritems + push_items);
	btrfs_mark_buffer_dirty(src_buf);
	btrfs_mark_buffer_dirty(dst_buf);
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
static int balance_node_right(struct btrfs_trans_handle *trans, struct
			      btrfs_root *root, struct buffer_head *dst_buf,
			      struct buffer_head *src_buf)
{
	struct btrfs_node *src = btrfs_buffer_node(src_buf);
	struct btrfs_node *dst = btrfs_buffer_node(dst_buf);
	int push_items = 0;
	int max_push;
	int src_nritems;
	int dst_nritems;
	int ret = 0;

	src_nritems = btrfs_header_nritems(&src->header);
	dst_nritems = btrfs_header_nritems(&dst->header);
	push_items = BTRFS_NODEPTRS_PER_BLOCK(root) - dst_nritems;
	if (push_items <= 0) {
		return 1;
	}

	max_push = src_nritems / 2 + 1;
	/* don't try to empty the node */
	if (max_push > src_nritems)
		return 1;
	if (max_push < push_items)
		push_items = max_push;

	btrfs_memmove(root, dst, dst->ptrs + push_items, dst->ptrs,
		      dst_nritems * sizeof(struct btrfs_key_ptr));

	btrfs_memcpy(root, dst, dst->ptrs,
		     src->ptrs + src_nritems - push_items,
		     push_items * sizeof(struct btrfs_key_ptr));

	btrfs_set_header_nritems(&src->header, src_nritems - push_items);
	btrfs_set_header_nritems(&dst->header, dst_nritems + push_items);

	btrfs_mark_buffer_dirty(src_buf);
	btrfs_mark_buffer_dirty(dst_buf);
	return ret;
}

/*
 * helper function to insert a new root level in the tree.
 * A new node is allocated, and a single item is inserted to
 * point to the existing root
 *
 * returns zero on success or < 0 on failure.
 */
static int insert_new_root(struct btrfs_trans_handle *trans, struct btrfs_root
			   *root, struct btrfs_path *path, int level)
{
	struct buffer_head *t;
	struct btrfs_node *lower;
	struct btrfs_node *c;
	struct btrfs_disk_key *lower_key;

	BUG_ON(path->nodes[level]);
	BUG_ON(path->nodes[level-1] != root->node);

	t = btrfs_alloc_free_block(trans, root, root->node->b_blocknr, 0);
	if (IS_ERR(t))
		return PTR_ERR(t);
	c = btrfs_buffer_node(t);
	memset(c, 0, root->blocksize);
	btrfs_set_header_nritems(&c->header, 1);
	btrfs_set_header_level(&c->header, level);
	btrfs_set_header_blocknr(&c->header, bh_blocknr(t));
	btrfs_set_header_generation(&c->header, trans->transid);
	btrfs_set_header_owner(&c->header, root->root_key.objectid);
	lower = btrfs_buffer_node(path->nodes[level-1]);
	memcpy(c->header.fsid, root->fs_info->disk_super->fsid,
	       sizeof(c->header.fsid));
	if (btrfs_is_leaf(lower))
		lower_key = &((struct btrfs_leaf *)lower)->items[0].key;
	else
		lower_key = &lower->ptrs[0].key;
	btrfs_memcpy(root, c, &c->ptrs[0].key, lower_key,
		     sizeof(struct btrfs_disk_key));
	btrfs_set_node_blockptr(c, 0, bh_blocknr(path->nodes[level - 1]));

	btrfs_mark_buffer_dirty(t);

	/* the super has an extra ref to root->node */
	btrfs_block_release(root, root->node);
	root->node = t;
	get_bh(t);
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
static int insert_ptr(struct btrfs_trans_handle *trans, struct btrfs_root
		      *root, struct btrfs_path *path, struct btrfs_disk_key
		      *key, u64 blocknr, int slot, int level)
{
	struct btrfs_node *lower;
	int nritems;

	BUG_ON(!path->nodes[level]);
	lower = btrfs_buffer_node(path->nodes[level]);
	nritems = btrfs_header_nritems(&lower->header);
	if (slot > nritems)
		BUG();
	if (nritems == BTRFS_NODEPTRS_PER_BLOCK(root))
		BUG();
	if (slot != nritems) {
		btrfs_memmove(root, lower, lower->ptrs + slot + 1,
			      lower->ptrs + slot,
			      (nritems - slot) * sizeof(struct btrfs_key_ptr));
	}
	btrfs_memcpy(root, lower, &lower->ptrs[slot].key,
		     key, sizeof(struct btrfs_disk_key));
	btrfs_set_node_blockptr(lower, slot, blocknr);
	btrfs_set_header_nritems(&lower->header, nritems + 1);
	btrfs_mark_buffer_dirty(path->nodes[level]);
	check_node(root, path, level);
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
static int split_node(struct btrfs_trans_handle *trans, struct btrfs_root
		      *root, struct btrfs_path *path, int level)
{
	struct buffer_head *t;
	struct btrfs_node *c;
	struct buffer_head *split_buffer;
	struct btrfs_node *split;
	int mid;
	int ret;
	int wret;
	u32 c_nritems;

	t = path->nodes[level];
	c = btrfs_buffer_node(t);
	if (t == root->node) {
		/* trying to split the root, lets make a new one */
		ret = insert_new_root(trans, root, path, level + 1);
		if (ret)
			return ret;
	} else {
		ret = push_nodes_for_insert(trans, root, path, level);
		t = path->nodes[level];
		c = btrfs_buffer_node(t);
		if (!ret &&
		    btrfs_header_nritems(&c->header) <
		    BTRFS_NODEPTRS_PER_BLOCK(root) - 1)
			return 0;
		if (ret < 0)
			return ret;
	}

	c_nritems = btrfs_header_nritems(&c->header);
	split_buffer = btrfs_alloc_free_block(trans, root, t->b_blocknr, 0);
	if (IS_ERR(split_buffer))
		return PTR_ERR(split_buffer);

	split = btrfs_buffer_node(split_buffer);
	btrfs_set_header_flags(&split->header, btrfs_header_flags(&c->header));
	btrfs_set_header_level(&split->header, btrfs_header_level(&c->header));
	btrfs_set_header_blocknr(&split->header, bh_blocknr(split_buffer));
	btrfs_set_header_generation(&split->header, trans->transid);
	btrfs_set_header_owner(&split->header, root->root_key.objectid);
	memcpy(split->header.fsid, root->fs_info->disk_super->fsid,
	       sizeof(split->header.fsid));
	mid = (c_nritems + 1) / 2;
	btrfs_memcpy(root, split, split->ptrs, c->ptrs + mid,
		     (c_nritems - mid) * sizeof(struct btrfs_key_ptr));
	btrfs_set_header_nritems(&split->header, c_nritems - mid);
	btrfs_set_header_nritems(&c->header, mid);
	ret = 0;

	btrfs_mark_buffer_dirty(t);
	btrfs_mark_buffer_dirty(split_buffer);
	wret = insert_ptr(trans, root, path, &split->ptrs[0].key,
			  bh_blocknr(split_buffer), path->slots[level + 1] + 1,
			  level + 1);
	if (wret)
		ret = wret;

	if (path->slots[level] >= mid) {
		path->slots[level] -= mid;
		btrfs_block_release(root, t);
		path->nodes[level] = split_buffer;
		path->slots[level + 1] += 1;
	} else {
		btrfs_block_release(root, split_buffer);
	}
	return ret;
}

/*
 * how many bytes are required to store the items in a leaf.  start
 * and nr indicate which items in the leaf to check.  This totals up the
 * space used both by the item structs and the item data
 */
static int leaf_space_used(struct btrfs_leaf *l, int start, int nr)
{
	int data_len;
	int nritems = btrfs_header_nritems(&l->header);
	int end = min(nritems, start + nr) - 1;

	if (!nr)
		return 0;
	data_len = btrfs_item_end(l->items + start);
	data_len = data_len - btrfs_item_offset(l->items + end);
	data_len += sizeof(struct btrfs_item) * nr;
	WARN_ON(data_len < 0);
	return data_len;
}

/*
 * The space between the end of the leaf items and
 * the start of the leaf data.  IOW, how much room
 * the leaf has left for both items and data
 */
int btrfs_leaf_free_space(struct btrfs_root *root, struct btrfs_leaf *leaf)
{
	int nritems = btrfs_header_nritems(&leaf->header);
	return BTRFS_LEAF_DATA_SIZE(root) - leaf_space_used(leaf, 0, nritems);
}

/*
 * push some data in the path leaf to the right, trying to free up at
 * least data_size bytes.  returns zero if the push worked, nonzero otherwise
 *
 * returns 1 if the push failed because the other node didn't have enough
 * room, 0 if everything worked out and < 0 if there were major errors.
 */
static int push_leaf_right(struct btrfs_trans_handle *trans, struct btrfs_root
			   *root, struct btrfs_path *path, int data_size)
{
	struct buffer_head *left_buf = path->nodes[0];
	struct btrfs_leaf *left = btrfs_buffer_leaf(left_buf);
	struct btrfs_leaf *right;
	struct buffer_head *right_buf;
	struct buffer_head *upper;
	struct btrfs_node *upper_node;
	int slot;
	int i;
	int free_space;
	int push_space = 0;
	int push_items = 0;
	struct btrfs_item *item;
	u32 left_nritems;
	u32 right_nritems;
	int ret;

	slot = path->slots[1];
	if (!path->nodes[1]) {
		return 1;
	}
	upper = path->nodes[1];
	upper_node = btrfs_buffer_node(upper);
	if (slot >= btrfs_header_nritems(&upper_node->header) - 1) {
		return 1;
	}
	right_buf = read_tree_block(root,
		    btrfs_node_blockptr(btrfs_buffer_node(upper), slot + 1));
	right = btrfs_buffer_leaf(right_buf);
	free_space = btrfs_leaf_free_space(root, right);
	if (free_space < data_size + sizeof(struct btrfs_item)) {
		btrfs_block_release(root, right_buf);
		return 1;
	}
	/* cow and double check */
	ret = btrfs_cow_block(trans, root, right_buf, upper,
			      slot + 1, &right_buf);
	if (ret) {
		btrfs_block_release(root, right_buf);
		return 1;
	}
	right = btrfs_buffer_leaf(right_buf);
	free_space = btrfs_leaf_free_space(root, right);
	if (free_space < data_size + sizeof(struct btrfs_item)) {
		btrfs_block_release(root, right_buf);
		return 1;
	}

	left_nritems = btrfs_header_nritems(&left->header);
	if (left_nritems == 0) {
		btrfs_block_release(root, right_buf);
		return 1;
	}
	for (i = left_nritems - 1; i >= 1; i--) {
		item = left->items + i;
		if (path->slots[0] == i)
			push_space += data_size + sizeof(*item);
		if (btrfs_item_size(item) + sizeof(*item) + push_space >
		    free_space)
			break;
		push_items++;
		push_space += btrfs_item_size(item) + sizeof(*item);
	}
	if (push_items == 0) {
		btrfs_block_release(root, right_buf);
		return 1;
	}
	if (push_items == left_nritems)
		WARN_ON(1);
	right_nritems = btrfs_header_nritems(&right->header);
	/* push left to right */
	push_space = btrfs_item_end(left->items + left_nritems - push_items);
	push_space -= leaf_data_end(root, left);
	/* make room in the right data area */
	btrfs_memmove(root, right, btrfs_leaf_data(right) +
		      leaf_data_end(root, right) - push_space,
		      btrfs_leaf_data(right) +
		      leaf_data_end(root, right), BTRFS_LEAF_DATA_SIZE(root) -
		      leaf_data_end(root, right));
	/* copy from the left data area */
	btrfs_memcpy(root, right, btrfs_leaf_data(right) +
		     BTRFS_LEAF_DATA_SIZE(root) - push_space,
		     btrfs_leaf_data(left) + leaf_data_end(root, left),
		     push_space);
	btrfs_memmove(root, right, right->items + push_items, right->items,
		right_nritems * sizeof(struct btrfs_item));
	/* copy the items from left to right */
	btrfs_memcpy(root, right, right->items, left->items +
		     left_nritems - push_items,
		     push_items * sizeof(struct btrfs_item));

	/* update the item pointers */
	right_nritems += push_items;
	btrfs_set_header_nritems(&right->header, right_nritems);
	push_space = BTRFS_LEAF_DATA_SIZE(root);
	for (i = 0; i < right_nritems; i++) {
		btrfs_set_item_offset(right->items + i, push_space -
				      btrfs_item_size(right->items + i));
		push_space = btrfs_item_offset(right->items + i);
	}
	left_nritems -= push_items;
	btrfs_set_header_nritems(&left->header, left_nritems);

	btrfs_mark_buffer_dirty(left_buf);
	btrfs_mark_buffer_dirty(right_buf);

	btrfs_memcpy(root, upper_node, &upper_node->ptrs[slot + 1].key,
		&right->items[0].key, sizeof(struct btrfs_disk_key));
	btrfs_mark_buffer_dirty(upper);

	/* then fixup the leaf pointer in the path */
	if (path->slots[0] >= left_nritems) {
		path->slots[0] -= left_nritems;
		btrfs_block_release(root, path->nodes[0]);
		path->nodes[0] = right_buf;
		path->slots[1] += 1;
	} else {
		btrfs_block_release(root, right_buf);
	}
	if (path->nodes[1])
		check_node(root, path, 1);
	return 0;
}
/*
 * push some data in the path leaf to the left, trying to free up at
 * least data_size bytes.  returns zero if the push worked, nonzero otherwise
 */
static int push_leaf_left(struct btrfs_trans_handle *trans, struct btrfs_root
			  *root, struct btrfs_path *path, int data_size)
{
	struct buffer_head *right_buf = path->nodes[0];
	struct btrfs_leaf *right = btrfs_buffer_leaf(right_buf);
	struct buffer_head *t;
	struct btrfs_leaf *left;
	int slot;
	int i;
	int free_space;
	int push_space = 0;
	int push_items = 0;
	struct btrfs_item *item;
	u32 old_left_nritems;
	int ret = 0;
	int wret;

	slot = path->slots[1];
	if (slot == 0) {
		return 1;
	}
	if (!path->nodes[1]) {
		return 1;
	}
	t = read_tree_block(root,
	    btrfs_node_blockptr(btrfs_buffer_node(path->nodes[1]), slot - 1));
	left = btrfs_buffer_leaf(t);
	free_space = btrfs_leaf_free_space(root, left);
	if (free_space < data_size + sizeof(struct btrfs_item)) {
		btrfs_block_release(root, t);
		return 1;
	}

	/* cow and double check */
	ret = btrfs_cow_block(trans, root, t, path->nodes[1], slot - 1, &t);
	if (ret) {
		/* we hit -ENOSPC, but it isn't fatal here */
		return 1;
	}
	left = btrfs_buffer_leaf(t);
	free_space = btrfs_leaf_free_space(root, left);
	if (free_space < data_size + sizeof(struct btrfs_item)) {
		btrfs_block_release(root, t);
		return 1;
	}

	if (btrfs_header_nritems(&right->header) == 0) {
		btrfs_block_release(root, t);
		return 1;
	}

	for (i = 0; i < btrfs_header_nritems(&right->header) - 1; i++) {
		item = right->items + i;
		if (path->slots[0] == i)
			push_space += data_size + sizeof(*item);
		if (btrfs_item_size(item) + sizeof(*item) + push_space >
		    free_space)
			break;
		push_items++;
		push_space += btrfs_item_size(item) + sizeof(*item);
	}
	if (push_items == 0) {
		btrfs_block_release(root, t);
		return 1;
	}
	if (push_items == btrfs_header_nritems(&right->header))
		WARN_ON(1);
	/* push data from right to left */
	btrfs_memcpy(root, left, left->items +
		     btrfs_header_nritems(&left->header),
		     right->items, push_items * sizeof(struct btrfs_item));
	push_space = BTRFS_LEAF_DATA_SIZE(root) -
		     btrfs_item_offset(right->items + push_items -1);
	btrfs_memcpy(root, left, btrfs_leaf_data(left) +
		     leaf_data_end(root, left) - push_space,
		     btrfs_leaf_data(right) +
		     btrfs_item_offset(right->items + push_items - 1),
		     push_space);
	old_left_nritems = btrfs_header_nritems(&left->header);
	BUG_ON(old_left_nritems < 0);

	for (i = old_left_nritems; i < old_left_nritems + push_items; i++) {
		u32 ioff = btrfs_item_offset(left->items + i);
		btrfs_set_item_offset(left->items + i, ioff -
				     (BTRFS_LEAF_DATA_SIZE(root) -
				      btrfs_item_offset(left->items +
						        old_left_nritems - 1)));
	}
	btrfs_set_header_nritems(&left->header, old_left_nritems + push_items);

	/* fixup right node */
	push_space = btrfs_item_offset(right->items + push_items - 1) -
		     leaf_data_end(root, right);
	btrfs_memmove(root, right, btrfs_leaf_data(right) +
		      BTRFS_LEAF_DATA_SIZE(root) - push_space,
		      btrfs_leaf_data(right) +
		      leaf_data_end(root, right), push_space);
	btrfs_memmove(root, right, right->items, right->items + push_items,
		(btrfs_header_nritems(&right->header) - push_items) *
		sizeof(struct btrfs_item));
	btrfs_set_header_nritems(&right->header,
				 btrfs_header_nritems(&right->header) -
				 push_items);
	push_space = BTRFS_LEAF_DATA_SIZE(root);

	for (i = 0; i < btrfs_header_nritems(&right->header); i++) {
		btrfs_set_item_offset(right->items + i, push_space -
				      btrfs_item_size(right->items + i));
		push_space = btrfs_item_offset(right->items + i);
	}

	btrfs_mark_buffer_dirty(t);
	btrfs_mark_buffer_dirty(right_buf);

	wret = fixup_low_keys(trans, root, path, &right->items[0].key, 1);
	if (wret)
		ret = wret;

	/* then fixup the leaf pointer in the path */
	if (path->slots[0] < push_items) {
		path->slots[0] += old_left_nritems;
		btrfs_block_release(root, path->nodes[0]);
		path->nodes[0] = t;
		path->slots[1] -= 1;
	} else {
		btrfs_block_release(root, t);
		path->slots[0] -= push_items;
	}
	BUG_ON(path->slots[0] < 0);
	if (path->nodes[1])
		check_node(root, path, 1);
	return ret;
}

/*
 * split the path's leaf in two, making sure there is at least data_size
 * available for the resulting leaf level of the path.
 *
 * returns 0 if all went well and < 0 on failure.
 */
static int split_leaf(struct btrfs_trans_handle *trans, struct btrfs_root
		      *root, struct btrfs_key *ins_key,
		      struct btrfs_path *path, int data_size)
{
	struct buffer_head *l_buf;
	struct btrfs_leaf *l;
	u32 nritems;
	int mid;
	int slot;
	struct btrfs_leaf *right;
	struct buffer_head *right_buffer;
	int space_needed = data_size + sizeof(struct btrfs_item);
	int data_copy_size;
	int rt_data_off;
	int i;
	int ret = 0;
	int wret;
	int double_split = 0;
	struct btrfs_disk_key disk_key;

	/* first try to make some room by pushing left and right */
	wret = push_leaf_left(trans, root, path, data_size);
	if (wret < 0)
		return wret;
	if (wret) {
		wret = push_leaf_right(trans, root, path, data_size);
		if (wret < 0)
			return wret;
	}
	l_buf = path->nodes[0];
	l = btrfs_buffer_leaf(l_buf);

	/* did the pushes work? */
	if (btrfs_leaf_free_space(root, l) >=
	    sizeof(struct btrfs_item) + data_size)
		return 0;

	if (!path->nodes[1]) {
		ret = insert_new_root(trans, root, path, 1);
		if (ret)
			return ret;
	}
	slot = path->slots[0];
	nritems = btrfs_header_nritems(&l->header);
	mid = (nritems + 1)/ 2;

	right_buffer = btrfs_alloc_free_block(trans, root, l_buf->b_blocknr, 0);
	if (IS_ERR(right_buffer))
		return PTR_ERR(right_buffer);

	right = btrfs_buffer_leaf(right_buffer);
	memset(&right->header, 0, sizeof(right->header));
	btrfs_set_header_blocknr(&right->header, bh_blocknr(right_buffer));
	btrfs_set_header_generation(&right->header, trans->transid);
	btrfs_set_header_owner(&right->header, root->root_key.objectid);
	btrfs_set_header_level(&right->header, 0);
	memcpy(right->header.fsid, root->fs_info->disk_super->fsid,
	       sizeof(right->header.fsid));
	if (mid <= slot) {
		if (nritems == 1 ||
		    leaf_space_used(l, mid, nritems - mid) + space_needed >
			BTRFS_LEAF_DATA_SIZE(root)) {
			if (slot >= nritems) {
				btrfs_cpu_key_to_disk(&disk_key, ins_key);
				btrfs_set_header_nritems(&right->header, 0);
				wret = insert_ptr(trans, root, path,
						  &disk_key,
						  bh_blocknr(right_buffer),
						  path->slots[1] + 1, 1);
				if (wret)
					ret = wret;
				btrfs_block_release(root, path->nodes[0]);
				path->nodes[0] = right_buffer;
				path->slots[0] = 0;
				path->slots[1] += 1;
				return ret;
			}
			mid = slot;
			double_split = 1;
		}
	} else {
		if (leaf_space_used(l, 0, mid + 1) + space_needed >
			BTRFS_LEAF_DATA_SIZE(root)) {
			if (slot == 0) {
				btrfs_cpu_key_to_disk(&disk_key, ins_key);
				btrfs_set_header_nritems(&right->header, 0);
				wret = insert_ptr(trans, root, path,
						  &disk_key,
						  bh_blocknr(right_buffer),
						  path->slots[1], 1);
				if (wret)
					ret = wret;
				btrfs_block_release(root, path->nodes[0]);
				path->nodes[0] = right_buffer;
				path->slots[0] = 0;
				if (path->slots[1] == 0) {
					wret = fixup_low_keys(trans, root,
					           path, &disk_key, 1);
					if (wret)
						ret = wret;
				}
				return ret;
			}
			mid = slot;
			double_split = 1;
		}
	}
	btrfs_set_header_nritems(&right->header, nritems - mid);
	data_copy_size = btrfs_item_end(l->items + mid) -
			 leaf_data_end(root, l);
	btrfs_memcpy(root, right, right->items, l->items + mid,
		     (nritems - mid) * sizeof(struct btrfs_item));
	btrfs_memcpy(root, right,
		     btrfs_leaf_data(right) + BTRFS_LEAF_DATA_SIZE(root) -
		     data_copy_size, btrfs_leaf_data(l) +
		     leaf_data_end(root, l), data_copy_size);
	rt_data_off = BTRFS_LEAF_DATA_SIZE(root) -
		      btrfs_item_end(l->items + mid);

	for (i = 0; i < btrfs_header_nritems(&right->header); i++) {
		u32 ioff = btrfs_item_offset(right->items + i);
		btrfs_set_item_offset(right->items + i, ioff + rt_data_off);
	}

	btrfs_set_header_nritems(&l->header, mid);
	ret = 0;
	wret = insert_ptr(trans, root, path, &right->items[0].key,
			  bh_blocknr(right_buffer), path->slots[1] + 1, 1);
	if (wret)
		ret = wret;
	btrfs_mark_buffer_dirty(right_buffer);
	btrfs_mark_buffer_dirty(l_buf);
	BUG_ON(path->slots[0] != slot);
	if (mid <= slot) {
		btrfs_block_release(root, path->nodes[0]);
		path->nodes[0] = right_buffer;
		path->slots[0] -= mid;
		path->slots[1] += 1;
	} else
		btrfs_block_release(root, right_buffer);
	BUG_ON(path->slots[0] < 0);
	check_node(root, path, 1);

	if (!double_split)
		return ret;
	right_buffer = btrfs_alloc_free_block(trans, root, l_buf->b_blocknr, 0);
	if (IS_ERR(right_buffer))
		return PTR_ERR(right_buffer);

	right = btrfs_buffer_leaf(right_buffer);
	memset(&right->header, 0, sizeof(right->header));
	btrfs_set_header_blocknr(&right->header, bh_blocknr(right_buffer));
	btrfs_set_header_generation(&right->header, trans->transid);
	btrfs_set_header_owner(&right->header, root->root_key.objectid);
	btrfs_set_header_level(&right->header, 0);
	memcpy(right->header.fsid, root->fs_info->disk_super->fsid,
	       sizeof(right->header.fsid));
	btrfs_cpu_key_to_disk(&disk_key, ins_key);
	btrfs_set_header_nritems(&right->header, 0);
	wret = insert_ptr(trans, root, path,
			  &disk_key,
			  bh_blocknr(right_buffer),
			  path->slots[1], 1);
	if (wret)
		ret = wret;
	if (path->slots[1] == 0) {
		wret = fixup_low_keys(trans, root, path, &disk_key, 1);
		if (wret)
			ret = wret;
	}
	btrfs_block_release(root, path->nodes[0]);
	path->nodes[0] = right_buffer;
	path->slots[0] = 0;
	check_node(root, path, 1);
	check_leaf(root, path, 0);
	return ret;
}

int btrfs_truncate_item(struct btrfs_trans_handle *trans,
			struct btrfs_root *root,
			struct btrfs_path *path,
			u32 new_size)
{
	int ret = 0;
	int slot;
	int slot_orig;
	struct btrfs_leaf *leaf;
	struct buffer_head *leaf_buf;
	u32 nritems;
	unsigned int data_end;
	unsigned int old_data_start;
	unsigned int old_size;
	unsigned int size_diff;
	int i;

	slot_orig = path->slots[0];
	leaf_buf = path->nodes[0];
	leaf = btrfs_buffer_leaf(leaf_buf);

	nritems = btrfs_header_nritems(&leaf->header);
	data_end = leaf_data_end(root, leaf);

	slot = path->slots[0];
	old_data_start = btrfs_item_offset(leaf->items + slot);
	old_size = btrfs_item_size(leaf->items + slot);
	BUG_ON(old_size <= new_size);
	size_diff = old_size - new_size;

	BUG_ON(slot < 0);
	BUG_ON(slot >= nritems);

	/*
	 * item0..itemN ... dataN.offset..dataN.size .. data0.size
	 */
	/* first correct the data pointers */
	for (i = slot; i < nritems; i++) {
		u32 ioff = btrfs_item_offset(leaf->items + i);
		btrfs_set_item_offset(leaf->items + i,
				      ioff + size_diff);
	}
	/* shift the data */
	btrfs_memmove(root, leaf, btrfs_leaf_data(leaf) +
		      data_end + size_diff, btrfs_leaf_data(leaf) +
		      data_end, old_data_start + new_size - data_end);
	btrfs_set_item_size(leaf->items + slot, new_size);
	btrfs_mark_buffer_dirty(leaf_buf);

	ret = 0;
	if (btrfs_leaf_free_space(root, leaf) < 0)
		BUG();
	check_leaf(root, path, 0);
	return ret;
}

int btrfs_extend_item(struct btrfs_trans_handle *trans, struct btrfs_root
		      *root, struct btrfs_path *path, u32 data_size)
{
	int ret = 0;
	int slot;
	int slot_orig;
	struct btrfs_leaf *leaf;
	struct buffer_head *leaf_buf;
	u32 nritems;
	unsigned int data_end;
	unsigned int old_data;
	unsigned int old_size;
	int i;

	slot_orig = path->slots[0];
	leaf_buf = path->nodes[0];
	leaf = btrfs_buffer_leaf(leaf_buf);

	nritems = btrfs_header_nritems(&leaf->header);
	data_end = leaf_data_end(root, leaf);

	if (btrfs_leaf_free_space(root, leaf) < data_size)
		BUG();
	slot = path->slots[0];
	old_data = btrfs_item_end(leaf->items + slot);

	BUG_ON(slot < 0);
	BUG_ON(slot >= nritems);

	/*
	 * item0..itemN ... dataN.offset..dataN.size .. data0.size
	 */
	/* first correct the data pointers */
	for (i = slot; i < nritems; i++) {
		u32 ioff = btrfs_item_offset(leaf->items + i);
		btrfs_set_item_offset(leaf->items + i,
				      ioff - data_size);
	}
	/* shift the data */
	btrfs_memmove(root, leaf, btrfs_leaf_data(leaf) +
		      data_end - data_size, btrfs_leaf_data(leaf) +
		      data_end, old_data - data_end);
	data_end = old_data;
	old_size = btrfs_item_size(leaf->items + slot);
	btrfs_set_item_size(leaf->items + slot, old_size + data_size);
	btrfs_mark_buffer_dirty(leaf_buf);

	ret = 0;
	if (btrfs_leaf_free_space(root, leaf) < 0)
		BUG();
	check_leaf(root, path, 0);
	return ret;
}

/*
 * Given a key and some data, insert an item into the tree.
 * This does all the path init required, making room in the tree if needed.
 */
int btrfs_insert_empty_item(struct btrfs_trans_handle *trans, struct btrfs_root
			    *root, struct btrfs_path *path, struct btrfs_key
			    *cpu_key, u32 data_size)
{
	int ret = 0;
	int slot;
	int slot_orig;
	struct btrfs_leaf *leaf;
	struct buffer_head *leaf_buf;
	u32 nritems;
	unsigned int data_end;
	struct btrfs_disk_key disk_key;

	btrfs_cpu_key_to_disk(&disk_key, cpu_key);

	/* create a root if there isn't one */
	if (!root->node)
		BUG();
	ret = btrfs_search_slot(trans, root, cpu_key, path, data_size, 1);
	if (ret == 0) {
		return -EEXIST;
	}
	if (ret < 0)
		goto out;

	slot_orig = path->slots[0];
	leaf_buf = path->nodes[0];
	leaf = btrfs_buffer_leaf(leaf_buf);

	nritems = btrfs_header_nritems(&leaf->header);
	data_end = leaf_data_end(root, leaf);

	if (btrfs_leaf_free_space(root, leaf) <
	    sizeof(struct btrfs_item) + data_size) {
		BUG();
	}
	slot = path->slots[0];
	BUG_ON(slot < 0);
	if (slot != nritems) {
		int i;
		unsigned int old_data = btrfs_item_end(leaf->items + slot);

		/*
		 * item0..itemN ... dataN.offset..dataN.size .. data0.size
		 */
		/* first correct the data pointers */
		for (i = slot; i < nritems; i++) {
			u32 ioff = btrfs_item_offset(leaf->items + i);
			btrfs_set_item_offset(leaf->items + i,
					      ioff - data_size);
		}

		/* shift the items */
		btrfs_memmove(root, leaf, leaf->items + slot + 1,
			      leaf->items + slot,
			      (nritems - slot) * sizeof(struct btrfs_item));

		/* shift the data */
		btrfs_memmove(root, leaf, btrfs_leaf_data(leaf) +
			      data_end - data_size, btrfs_leaf_data(leaf) +
			      data_end, old_data - data_end);
		data_end = old_data;
	}
	/* setup the item for the new data */
	btrfs_memcpy(root, leaf, &leaf->items[slot].key, &disk_key,
		     sizeof(struct btrfs_disk_key));
	btrfs_set_item_offset(leaf->items + slot, data_end - data_size);
	btrfs_set_item_size(leaf->items + slot, data_size);
	btrfs_set_header_nritems(&leaf->header, nritems + 1);
	btrfs_mark_buffer_dirty(leaf_buf);

	ret = 0;
	if (slot == 0)
		ret = fixup_low_keys(trans, root, path, &disk_key, 1);

	if (btrfs_leaf_free_space(root, leaf) < 0)
		BUG();
	check_leaf(root, path, 0);
out:
	return ret;
}

/*
 * Given a key and some data, insert an item into the tree.
 * This does all the path init required, making room in the tree if needed.
 */
int btrfs_insert_item(struct btrfs_trans_handle *trans, struct btrfs_root
		      *root, struct btrfs_key *cpu_key, void *data, u32
		      data_size)
{
	int ret = 0;
	struct btrfs_path *path;
	u8 *ptr;

	path = btrfs_alloc_path();
	BUG_ON(!path);
	ret = btrfs_insert_empty_item(trans, root, path, cpu_key, data_size);
	if (!ret) {
		ptr = btrfs_item_ptr(btrfs_buffer_leaf(path->nodes[0]),
				     path->slots[0], u8);
		btrfs_memcpy(root, path->nodes[0]->b_data,
			     ptr, data, data_size);
		btrfs_mark_buffer_dirty(path->nodes[0]);
	}
	btrfs_free_path(path);
	return ret;
}

/*
 * delete the pointer from a given node.
 *
 * If the delete empties a node, the node is removed from the tree,
 * continuing all the way the root if required.  The root is converted into
 * a leaf if all the nodes are emptied.
 */
static int del_ptr(struct btrfs_trans_handle *trans, struct btrfs_root *root,
		   struct btrfs_path *path, int level, int slot)
{
	struct btrfs_node *node;
	struct buffer_head *parent = path->nodes[level];
	u32 nritems;
	int ret = 0;
	int wret;

	node = btrfs_buffer_node(parent);
	nritems = btrfs_header_nritems(&node->header);
	if (slot != nritems -1) {
		btrfs_memmove(root, node, node->ptrs + slot,
			      node->ptrs + slot + 1,
			      sizeof(struct btrfs_key_ptr) *
			      (nritems - slot - 1));
	}
	nritems--;
	btrfs_set_header_nritems(&node->header, nritems);
	if (nritems == 0 && parent == root->node) {
		struct btrfs_header *header = btrfs_buffer_header(root->node);
		BUG_ON(btrfs_header_level(header) != 1);
		/* just turn the root into a leaf and break */
		btrfs_set_header_level(header, 0);
	} else if (slot == 0) {
		wret = fixup_low_keys(trans, root, path, &node->ptrs[0].key,
				      level + 1);
		if (wret)
			ret = wret;
	}
	btrfs_mark_buffer_dirty(parent);
	return ret;
}

/*
 * delete the item at the leaf level in path.  If that empties
 * the leaf, remove it from the tree
 */
int btrfs_del_item(struct btrfs_trans_handle *trans, struct btrfs_root *root,
		   struct btrfs_path *path)
{
	int slot;
	struct btrfs_leaf *leaf;
	struct buffer_head *leaf_buf;
	int doff;
	int dsize;
	int ret = 0;
	int wret;
	u32 nritems;

	leaf_buf = path->nodes[0];
	leaf = btrfs_buffer_leaf(leaf_buf);
	slot = path->slots[0];
	doff = btrfs_item_offset(leaf->items + slot);
	dsize = btrfs_item_size(leaf->items + slot);
	nritems = btrfs_header_nritems(&leaf->header);

	if (slot != nritems - 1) {
		int i;
		int data_end = leaf_data_end(root, leaf);
		btrfs_memmove(root, leaf, btrfs_leaf_data(leaf) +
			      data_end + dsize,
			      btrfs_leaf_data(leaf) + data_end,
			      doff - data_end);
		for (i = slot + 1; i < nritems; i++) {
			u32 ioff = btrfs_item_offset(leaf->items + i);
			btrfs_set_item_offset(leaf->items + i, ioff + dsize);
		}
		btrfs_memmove(root, leaf, leaf->items + slot,
			      leaf->items + slot + 1,
			      sizeof(struct btrfs_item) *
			      (nritems - slot - 1));
	}
	btrfs_set_header_nritems(&leaf->header, nritems - 1);
	nritems--;
	/* delete the leaf if we've emptied it */
	if (nritems == 0) {
		if (leaf_buf == root->node) {
			btrfs_set_header_level(&leaf->header, 0);
		} else {
			clean_tree_block(trans, root, leaf_buf);
			wait_on_buffer(leaf_buf);
			wret = del_ptr(trans, root, path, 1, path->slots[1]);
			if (wret)
				ret = wret;
			wret = btrfs_free_extent(trans, root,
						 bh_blocknr(leaf_buf), 1, 1);
			if (wret)
				ret = wret;
		}
	} else {
		int used = leaf_space_used(leaf, 0, nritems);
		if (slot == 0) {
			wret = fixup_low_keys(trans, root, path,
					      &leaf->items[0].key, 1);
			if (wret)
				ret = wret;
		}

		/* delete the leaf if it is mostly empty */
		if (used < BTRFS_LEAF_DATA_SIZE(root) / 3) {
			/* push_leaf_left fixes the path.
			 * make sure the path still points to our leaf
			 * for possible call to del_ptr below
			 */
			slot = path->slots[1];
			get_bh(leaf_buf);
			wret = push_leaf_left(trans, root, path, 1);
			if (wret < 0 && wret != -ENOSPC)
				ret = wret;
			if (path->nodes[0] == leaf_buf &&
			    btrfs_header_nritems(&leaf->header)) {
				wret = push_leaf_right(trans, root, path, 1);
				if (wret < 0 && wret != -ENOSPC)
					ret = wret;
			}
			if (btrfs_header_nritems(&leaf->header) == 0) {
				u64 blocknr = bh_blocknr(leaf_buf);
				clean_tree_block(trans, root, leaf_buf);
				wait_on_buffer(leaf_buf);
				wret = del_ptr(trans, root, path, 1, slot);
				if (wret)
					ret = wret;
				btrfs_block_release(root, leaf_buf);
				wret = btrfs_free_extent(trans, root, blocknr,
							 1, 1);
				if (wret)
					ret = wret;
			} else {
				btrfs_mark_buffer_dirty(leaf_buf);
				btrfs_block_release(root, leaf_buf);
			}
		} else {
			btrfs_mark_buffer_dirty(leaf_buf);
		}
	}
	return ret;
}

/*
 * walk up the tree as far as required to find the next leaf.
 * returns 0 if it found something or 1 if there are no greater leaves.
 * returns < 0 on io errors.
 */
int btrfs_next_leaf(struct btrfs_root *root, struct btrfs_path *path)
{
	int slot;
	int level = 1;
	u64 blocknr;
	struct buffer_head *c;
	struct btrfs_node *c_node;
	struct buffer_head *next = NULL;

	while(level < BTRFS_MAX_LEVEL) {
		if (!path->nodes[level])
			return 1;
		slot = path->slots[level] + 1;
		c = path->nodes[level];
		c_node = btrfs_buffer_node(c);
		if (slot >= btrfs_header_nritems(&c_node->header)) {
			level++;
			continue;
		}
		blocknr = btrfs_node_blockptr(c_node, slot);
		if (next)
			btrfs_block_release(root, next);
		if (path->reada)
			reada_for_search(root, path, level, slot);
		next = read_tree_block(root, blocknr);
		break;
	}
	path->slots[level] = slot;
	while(1) {
		level--;
		c = path->nodes[level];
		btrfs_block_release(root, c);
		path->nodes[level] = next;
		path->slots[level] = 0;
		if (!level)
			break;
		if (path->reada)
			reada_for_search(root, path, level, slot);
		next = read_tree_block(root,
		       btrfs_node_blockptr(btrfs_buffer_node(next), 0));
	}
	return 0;
}
