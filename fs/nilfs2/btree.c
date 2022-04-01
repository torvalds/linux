// SPDX-License-Identifier: GPL-2.0+
/*
 * btree.c - NILFS B-tree.
 *
 * Copyright (C) 2005-2008 Nippon Telegraph and Telephone Corporation.
 *
 * Written by Koji Sato.
 */

#include <linux/slab.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/pagevec.h>
#include "nilfs.h"
#include "page.h"
#include "btnode.h"
#include "btree.h"
#include "alloc.h"
#include "dat.h"

static void __nilfs_btree_init(struct nilfs_bmap *bmap);

static struct nilfs_btree_path *nilfs_btree_alloc_path(void)
{
	struct nilfs_btree_path *path;
	int level = NILFS_BTREE_LEVEL_DATA;

	path = kmem_cache_alloc(nilfs_btree_path_cache, GFP_NOFS);
	if (path == NULL)
		goto out;

	for (; level < NILFS_BTREE_LEVEL_MAX; level++) {
		path[level].bp_bh = NULL;
		path[level].bp_sib_bh = NULL;
		path[level].bp_index = 0;
		path[level].bp_oldreq.bpr_ptr = NILFS_BMAP_INVALID_PTR;
		path[level].bp_newreq.bpr_ptr = NILFS_BMAP_INVALID_PTR;
		path[level].bp_op = NULL;
	}

out:
	return path;
}

static void nilfs_btree_free_path(struct nilfs_btree_path *path)
{
	int level = NILFS_BTREE_LEVEL_DATA;

	for (; level < NILFS_BTREE_LEVEL_MAX; level++)
		brelse(path[level].bp_bh);

	kmem_cache_free(nilfs_btree_path_cache, path);
}

/*
 * B-tree node operations
 */
static int nilfs_btree_get_new_block(const struct nilfs_bmap *btree,
				     __u64 ptr, struct buffer_head **bhp)
{
	struct inode *btnc_inode = NILFS_BMAP_I(btree)->i_assoc_inode;
	struct address_space *btnc = btnc_inode->i_mapping;
	struct buffer_head *bh;

	bh = nilfs_btnode_create_block(btnc, ptr);
	if (!bh)
		return -ENOMEM;

	set_buffer_nilfs_volatile(bh);
	*bhp = bh;
	return 0;
}

static int nilfs_btree_node_get_flags(const struct nilfs_btree_node *node)
{
	return node->bn_flags;
}

static void
nilfs_btree_node_set_flags(struct nilfs_btree_node *node, int flags)
{
	node->bn_flags = flags;
}

static int nilfs_btree_node_root(const struct nilfs_btree_node *node)
{
	return nilfs_btree_node_get_flags(node) & NILFS_BTREE_NODE_ROOT;
}

static int nilfs_btree_node_get_level(const struct nilfs_btree_node *node)
{
	return node->bn_level;
}

static void
nilfs_btree_node_set_level(struct nilfs_btree_node *node, int level)
{
	node->bn_level = level;
}

static int nilfs_btree_node_get_nchildren(const struct nilfs_btree_node *node)
{
	return le16_to_cpu(node->bn_nchildren);
}

static void
nilfs_btree_node_set_nchildren(struct nilfs_btree_node *node, int nchildren)
{
	node->bn_nchildren = cpu_to_le16(nchildren);
}

static int nilfs_btree_node_size(const struct nilfs_bmap *btree)
{
	return i_blocksize(btree->b_inode);
}

static int nilfs_btree_nchildren_per_block(const struct nilfs_bmap *btree)
{
	return btree->b_nchildren_per_block;
}

static __le64 *
nilfs_btree_node_dkeys(const struct nilfs_btree_node *node)
{
	return (__le64 *)((char *)(node + 1) +
			  (nilfs_btree_node_root(node) ?
			   0 : NILFS_BTREE_NODE_EXTRA_PAD_SIZE));
}

static __le64 *
nilfs_btree_node_dptrs(const struct nilfs_btree_node *node, int ncmax)
{
	return (__le64 *)(nilfs_btree_node_dkeys(node) + ncmax);
}

static __u64
nilfs_btree_node_get_key(const struct nilfs_btree_node *node, int index)
{
	return le64_to_cpu(*(nilfs_btree_node_dkeys(node) + index));
}

static void
nilfs_btree_node_set_key(struct nilfs_btree_node *node, int index, __u64 key)
{
	*(nilfs_btree_node_dkeys(node) + index) = cpu_to_le64(key);
}

static __u64
nilfs_btree_node_get_ptr(const struct nilfs_btree_node *node, int index,
			 int ncmax)
{
	return le64_to_cpu(*(nilfs_btree_node_dptrs(node, ncmax) + index));
}

static void
nilfs_btree_node_set_ptr(struct nilfs_btree_node *node, int index, __u64 ptr,
			 int ncmax)
{
	*(nilfs_btree_node_dptrs(node, ncmax) + index) = cpu_to_le64(ptr);
}

static void nilfs_btree_node_init(struct nilfs_btree_node *node, int flags,
				  int level, int nchildren, int ncmax,
				  const __u64 *keys, const __u64 *ptrs)
{
	__le64 *dkeys;
	__le64 *dptrs;
	int i;

	nilfs_btree_node_set_flags(node, flags);
	nilfs_btree_node_set_level(node, level);
	nilfs_btree_node_set_nchildren(node, nchildren);

	dkeys = nilfs_btree_node_dkeys(node);
	dptrs = nilfs_btree_node_dptrs(node, ncmax);
	for (i = 0; i < nchildren; i++) {
		dkeys[i] = cpu_to_le64(keys[i]);
		dptrs[i] = cpu_to_le64(ptrs[i]);
	}
}

/* Assume the buffer heads corresponding to left and right are locked. */
static void nilfs_btree_node_move_left(struct nilfs_btree_node *left,
				       struct nilfs_btree_node *right,
				       int n, int lncmax, int rncmax)
{
	__le64 *ldkeys, *rdkeys;
	__le64 *ldptrs, *rdptrs;
	int lnchildren, rnchildren;

	ldkeys = nilfs_btree_node_dkeys(left);
	ldptrs = nilfs_btree_node_dptrs(left, lncmax);
	lnchildren = nilfs_btree_node_get_nchildren(left);

	rdkeys = nilfs_btree_node_dkeys(right);
	rdptrs = nilfs_btree_node_dptrs(right, rncmax);
	rnchildren = nilfs_btree_node_get_nchildren(right);

	memcpy(ldkeys + lnchildren, rdkeys, n * sizeof(*rdkeys));
	memcpy(ldptrs + lnchildren, rdptrs, n * sizeof(*rdptrs));
	memmove(rdkeys, rdkeys + n, (rnchildren - n) * sizeof(*rdkeys));
	memmove(rdptrs, rdptrs + n, (rnchildren - n) * sizeof(*rdptrs));

	lnchildren += n;
	rnchildren -= n;
	nilfs_btree_node_set_nchildren(left, lnchildren);
	nilfs_btree_node_set_nchildren(right, rnchildren);
}

/* Assume that the buffer heads corresponding to left and right are locked. */
static void nilfs_btree_node_move_right(struct nilfs_btree_node *left,
					struct nilfs_btree_node *right,
					int n, int lncmax, int rncmax)
{
	__le64 *ldkeys, *rdkeys;
	__le64 *ldptrs, *rdptrs;
	int lnchildren, rnchildren;

	ldkeys = nilfs_btree_node_dkeys(left);
	ldptrs = nilfs_btree_node_dptrs(left, lncmax);
	lnchildren = nilfs_btree_node_get_nchildren(left);

	rdkeys = nilfs_btree_node_dkeys(right);
	rdptrs = nilfs_btree_node_dptrs(right, rncmax);
	rnchildren = nilfs_btree_node_get_nchildren(right);

	memmove(rdkeys + n, rdkeys, rnchildren * sizeof(*rdkeys));
	memmove(rdptrs + n, rdptrs, rnchildren * sizeof(*rdptrs));
	memcpy(rdkeys, ldkeys + lnchildren - n, n * sizeof(*rdkeys));
	memcpy(rdptrs, ldptrs + lnchildren - n, n * sizeof(*rdptrs));

	lnchildren -= n;
	rnchildren += n;
	nilfs_btree_node_set_nchildren(left, lnchildren);
	nilfs_btree_node_set_nchildren(right, rnchildren);
}

/* Assume that the buffer head corresponding to node is locked. */
static void nilfs_btree_node_insert(struct nilfs_btree_node *node, int index,
				    __u64 key, __u64 ptr, int ncmax)
{
	__le64 *dkeys;
	__le64 *dptrs;
	int nchildren;

	dkeys = nilfs_btree_node_dkeys(node);
	dptrs = nilfs_btree_node_dptrs(node, ncmax);
	nchildren = nilfs_btree_node_get_nchildren(node);
	if (index < nchildren) {
		memmove(dkeys + index + 1, dkeys + index,
			(nchildren - index) * sizeof(*dkeys));
		memmove(dptrs + index + 1, dptrs + index,
			(nchildren - index) * sizeof(*dptrs));
	}
	dkeys[index] = cpu_to_le64(key);
	dptrs[index] = cpu_to_le64(ptr);
	nchildren++;
	nilfs_btree_node_set_nchildren(node, nchildren);
}

/* Assume that the buffer head corresponding to node is locked. */
static void nilfs_btree_node_delete(struct nilfs_btree_node *node, int index,
				    __u64 *keyp, __u64 *ptrp, int ncmax)
{
	__u64 key;
	__u64 ptr;
	__le64 *dkeys;
	__le64 *dptrs;
	int nchildren;

	dkeys = nilfs_btree_node_dkeys(node);
	dptrs = nilfs_btree_node_dptrs(node, ncmax);
	key = le64_to_cpu(dkeys[index]);
	ptr = le64_to_cpu(dptrs[index]);
	nchildren = nilfs_btree_node_get_nchildren(node);
	if (keyp != NULL)
		*keyp = key;
	if (ptrp != NULL)
		*ptrp = ptr;

	if (index < nchildren - 1) {
		memmove(dkeys + index, dkeys + index + 1,
			(nchildren - index - 1) * sizeof(*dkeys));
		memmove(dptrs + index, dptrs + index + 1,
			(nchildren - index - 1) * sizeof(*dptrs));
	}
	nchildren--;
	nilfs_btree_node_set_nchildren(node, nchildren);
}

static int nilfs_btree_node_lookup(const struct nilfs_btree_node *node,
				   __u64 key, int *indexp)
{
	__u64 nkey;
	int index, low, high, s;

	/* binary search */
	low = 0;
	high = nilfs_btree_node_get_nchildren(node) - 1;
	index = 0;
	s = 0;
	while (low <= high) {
		index = (low + high) / 2;
		nkey = nilfs_btree_node_get_key(node, index);
		if (nkey == key) {
			s = 0;
			goto out;
		} else if (nkey < key) {
			low = index + 1;
			s = -1;
		} else {
			high = index - 1;
			s = 1;
		}
	}

	/* adjust index */
	if (nilfs_btree_node_get_level(node) > NILFS_BTREE_LEVEL_NODE_MIN) {
		if (s > 0 && index > 0)
			index--;
	} else if (s < 0)
		index++;

 out:
	*indexp = index;

	return s == 0;
}

/**
 * nilfs_btree_node_broken - verify consistency of btree node
 * @node: btree node block to be examined
 * @size: node size (in bytes)
 * @inode: host inode of btree
 * @blocknr: block number
 *
 * Return Value: If node is broken, 1 is returned. Otherwise, 0 is returned.
 */
static int nilfs_btree_node_broken(const struct nilfs_btree_node *node,
				   size_t size, struct inode *inode,
				   sector_t blocknr)
{
	int level, flags, nchildren;
	int ret = 0;

	level = nilfs_btree_node_get_level(node);
	flags = nilfs_btree_node_get_flags(node);
	nchildren = nilfs_btree_node_get_nchildren(node);

	if (unlikely(level < NILFS_BTREE_LEVEL_NODE_MIN ||
		     level >= NILFS_BTREE_LEVEL_MAX ||
		     (flags & NILFS_BTREE_NODE_ROOT) ||
		     nchildren < 0 ||
		     nchildren > NILFS_BTREE_NODE_NCHILDREN_MAX(size))) {
		nilfs_crit(inode->i_sb,
			   "bad btree node (ino=%lu, blocknr=%llu): level = %d, flags = 0x%x, nchildren = %d",
			   inode->i_ino, (unsigned long long)blocknr, level,
			   flags, nchildren);
		ret = 1;
	}
	return ret;
}

/**
 * nilfs_btree_root_broken - verify consistency of btree root node
 * @node: btree root node to be examined
 * @inode: host inode of btree
 *
 * Return Value: If node is broken, 1 is returned. Otherwise, 0 is returned.
 */
static int nilfs_btree_root_broken(const struct nilfs_btree_node *node,
				   struct inode *inode)
{
	int level, flags, nchildren;
	int ret = 0;

	level = nilfs_btree_node_get_level(node);
	flags = nilfs_btree_node_get_flags(node);
	nchildren = nilfs_btree_node_get_nchildren(node);

	if (unlikely(level < NILFS_BTREE_LEVEL_NODE_MIN ||
		     level >= NILFS_BTREE_LEVEL_MAX ||
		     nchildren < 0 ||
		     nchildren > NILFS_BTREE_ROOT_NCHILDREN_MAX)) {
		nilfs_crit(inode->i_sb,
			   "bad btree root (ino=%lu): level = %d, flags = 0x%x, nchildren = %d",
			   inode->i_ino, level, flags, nchildren);
		ret = 1;
	}
	return ret;
}

int nilfs_btree_broken_node_block(struct buffer_head *bh)
{
	struct inode *inode;
	int ret;

	if (buffer_nilfs_checked(bh))
		return 0;

	inode = bh->b_page->mapping->host;
	ret = nilfs_btree_node_broken((struct nilfs_btree_node *)bh->b_data,
				      bh->b_size, inode, bh->b_blocknr);
	if (likely(!ret))
		set_buffer_nilfs_checked(bh);
	return ret;
}

static struct nilfs_btree_node *
nilfs_btree_get_root(const struct nilfs_bmap *btree)
{
	return (struct nilfs_btree_node *)btree->b_u.u_data;
}

static struct nilfs_btree_node *
nilfs_btree_get_nonroot_node(const struct nilfs_btree_path *path, int level)
{
	return (struct nilfs_btree_node *)path[level].bp_bh->b_data;
}

static struct nilfs_btree_node *
nilfs_btree_get_sib_node(const struct nilfs_btree_path *path, int level)
{
	return (struct nilfs_btree_node *)path[level].bp_sib_bh->b_data;
}

static int nilfs_btree_height(const struct nilfs_bmap *btree)
{
	return nilfs_btree_node_get_level(nilfs_btree_get_root(btree)) + 1;
}

static struct nilfs_btree_node *
nilfs_btree_get_node(const struct nilfs_bmap *btree,
		     const struct nilfs_btree_path *path,
		     int level, int *ncmaxp)
{
	struct nilfs_btree_node *node;

	if (level == nilfs_btree_height(btree) - 1) {
		node = nilfs_btree_get_root(btree);
		*ncmaxp = NILFS_BTREE_ROOT_NCHILDREN_MAX;
	} else {
		node = nilfs_btree_get_nonroot_node(path, level);
		*ncmaxp = nilfs_btree_nchildren_per_block(btree);
	}
	return node;
}

static int nilfs_btree_bad_node(const struct nilfs_bmap *btree,
				struct nilfs_btree_node *node, int level)
{
	if (unlikely(nilfs_btree_node_get_level(node) != level)) {
		dump_stack();
		nilfs_crit(btree->b_inode->i_sb,
			   "btree level mismatch (ino=%lu): %d != %d",
			   btree->b_inode->i_ino,
			   nilfs_btree_node_get_level(node), level);
		return 1;
	}
	return 0;
}

struct nilfs_btree_readahead_info {
	struct nilfs_btree_node *node;	/* parent node */
	int max_ra_blocks;		/* max nof blocks to read ahead */
	int index;			/* current index on the parent node */
	int ncmax;			/* nof children in the parent node */
};

static int __nilfs_btree_get_block(const struct nilfs_bmap *btree, __u64 ptr,
				   struct buffer_head **bhp,
				   const struct nilfs_btree_readahead_info *ra)
{
	struct inode *btnc_inode = NILFS_BMAP_I(btree)->i_assoc_inode;
	struct address_space *btnc = btnc_inode->i_mapping;
	struct buffer_head *bh, *ra_bh;
	sector_t submit_ptr = 0;
	int ret;

	ret = nilfs_btnode_submit_block(btnc, ptr, 0, REQ_OP_READ, 0, &bh,
					&submit_ptr);
	if (ret) {
		if (ret != -EEXIST)
			return ret;
		goto out_check;
	}

	if (ra) {
		int i, n;
		__u64 ptr2;

		/* read ahead sibling nodes */
		for (n = ra->max_ra_blocks, i = ra->index + 1;
		     n > 0 && i < ra->ncmax; n--, i++) {
			ptr2 = nilfs_btree_node_get_ptr(ra->node, i, ra->ncmax);

			ret = nilfs_btnode_submit_block(btnc, ptr2, 0,
							REQ_OP_READ, REQ_RAHEAD,
							&ra_bh, &submit_ptr);
			if (likely(!ret || ret == -EEXIST))
				brelse(ra_bh);
			else if (ret != -EBUSY)
				break;
			if (!buffer_locked(bh))
				goto out_no_wait;
		}
	}

	wait_on_buffer(bh);

 out_no_wait:
	if (!buffer_uptodate(bh)) {
		nilfs_err(btree->b_inode->i_sb,
			  "I/O error reading b-tree node block (ino=%lu, blocknr=%llu)",
			  btree->b_inode->i_ino, (unsigned long long)ptr);
		brelse(bh);
		return -EIO;
	}

 out_check:
	if (nilfs_btree_broken_node_block(bh)) {
		clear_buffer_uptodate(bh);
		brelse(bh);
		return -EINVAL;
	}

	*bhp = bh;
	return 0;
}

static int nilfs_btree_get_block(const struct nilfs_bmap *btree, __u64 ptr,
				   struct buffer_head **bhp)
{
	return __nilfs_btree_get_block(btree, ptr, bhp, NULL);
}

static int nilfs_btree_do_lookup(const struct nilfs_bmap *btree,
				 struct nilfs_btree_path *path,
				 __u64 key, __u64 *ptrp, int minlevel,
				 int readahead)
{
	struct nilfs_btree_node *node;
	struct nilfs_btree_readahead_info p, *ra;
	__u64 ptr;
	int level, index, found, ncmax, ret;

	node = nilfs_btree_get_root(btree);
	level = nilfs_btree_node_get_level(node);
	if (level < minlevel || nilfs_btree_node_get_nchildren(node) <= 0)
		return -ENOENT;

	found = nilfs_btree_node_lookup(node, key, &index);
	ptr = nilfs_btree_node_get_ptr(node, index,
				       NILFS_BTREE_ROOT_NCHILDREN_MAX);
	path[level].bp_bh = NULL;
	path[level].bp_index = index;

	ncmax = nilfs_btree_nchildren_per_block(btree);

	while (--level >= minlevel) {
		ra = NULL;
		if (level == NILFS_BTREE_LEVEL_NODE_MIN && readahead) {
			p.node = nilfs_btree_get_node(btree, path, level + 1,
						      &p.ncmax);
			p.index = index;
			p.max_ra_blocks = 7;
			ra = &p;
		}
		ret = __nilfs_btree_get_block(btree, ptr, &path[level].bp_bh,
					      ra);
		if (ret < 0)
			return ret;

		node = nilfs_btree_get_nonroot_node(path, level);
		if (nilfs_btree_bad_node(btree, node, level))
			return -EINVAL;
		if (!found)
			found = nilfs_btree_node_lookup(node, key, &index);
		else
			index = 0;
		if (index < ncmax) {
			ptr = nilfs_btree_node_get_ptr(node, index, ncmax);
		} else {
			WARN_ON(found || level != NILFS_BTREE_LEVEL_NODE_MIN);
			/* insert */
			ptr = NILFS_BMAP_INVALID_PTR;
		}
		path[level].bp_index = index;
	}
	if (!found)
		return -ENOENT;

	if (ptrp != NULL)
		*ptrp = ptr;

	return 0;
}

static int nilfs_btree_do_lookup_last(const struct nilfs_bmap *btree,
				      struct nilfs_btree_path *path,
				      __u64 *keyp, __u64 *ptrp)
{
	struct nilfs_btree_node *node;
	__u64 ptr;
	int index, level, ncmax, ret;

	node = nilfs_btree_get_root(btree);
	index = nilfs_btree_node_get_nchildren(node) - 1;
	if (index < 0)
		return -ENOENT;
	level = nilfs_btree_node_get_level(node);
	ptr = nilfs_btree_node_get_ptr(node, index,
				       NILFS_BTREE_ROOT_NCHILDREN_MAX);
	path[level].bp_bh = NULL;
	path[level].bp_index = index;
	ncmax = nilfs_btree_nchildren_per_block(btree);

	for (level--; level > 0; level--) {
		ret = nilfs_btree_get_block(btree, ptr, &path[level].bp_bh);
		if (ret < 0)
			return ret;
		node = nilfs_btree_get_nonroot_node(path, level);
		if (nilfs_btree_bad_node(btree, node, level))
			return -EINVAL;
		index = nilfs_btree_node_get_nchildren(node) - 1;
		ptr = nilfs_btree_node_get_ptr(node, index, ncmax);
		path[level].bp_index = index;
	}

	if (keyp != NULL)
		*keyp = nilfs_btree_node_get_key(node, index);
	if (ptrp != NULL)
		*ptrp = ptr;

	return 0;
}

/**
 * nilfs_btree_get_next_key - get next valid key from btree path array
 * @btree: bmap struct of btree
 * @path: array of nilfs_btree_path struct
 * @minlevel: start level
 * @nextkey: place to store the next valid key
 *
 * Return Value: If a next key was found, 0 is returned. Otherwise,
 * -ENOENT is returned.
 */
static int nilfs_btree_get_next_key(const struct nilfs_bmap *btree,
				    const struct nilfs_btree_path *path,
				    int minlevel, __u64 *nextkey)
{
	struct nilfs_btree_node *node;
	int maxlevel = nilfs_btree_height(btree) - 1;
	int index, next_adj, level;

	/* Next index is already set to bp_index for leaf nodes. */
	next_adj = 0;
	for (level = minlevel; level <= maxlevel; level++) {
		if (level == maxlevel)
			node = nilfs_btree_get_root(btree);
		else
			node = nilfs_btree_get_nonroot_node(path, level);

		index = path[level].bp_index + next_adj;
		if (index < nilfs_btree_node_get_nchildren(node)) {
			/* Next key is in this node */
			*nextkey = nilfs_btree_node_get_key(node, index);
			return 0;
		}
		/* For non-leaf nodes, next index is stored at bp_index + 1. */
		next_adj = 1;
	}
	return -ENOENT;
}

static int nilfs_btree_lookup(const struct nilfs_bmap *btree,
			      __u64 key, int level, __u64 *ptrp)
{
	struct nilfs_btree_path *path;
	int ret;

	path = nilfs_btree_alloc_path();
	if (path == NULL)
		return -ENOMEM;

	ret = nilfs_btree_do_lookup(btree, path, key, ptrp, level, 0);

	nilfs_btree_free_path(path);

	return ret;
}

static int nilfs_btree_lookup_contig(const struct nilfs_bmap *btree,
				     __u64 key, __u64 *ptrp,
				     unsigned int maxblocks)
{
	struct nilfs_btree_path *path;
	struct nilfs_btree_node *node;
	struct inode *dat = NULL;
	__u64 ptr, ptr2;
	sector_t blocknr;
	int level = NILFS_BTREE_LEVEL_NODE_MIN;
	int ret, cnt, index, maxlevel, ncmax;
	struct nilfs_btree_readahead_info p;

	path = nilfs_btree_alloc_path();
	if (path == NULL)
		return -ENOMEM;

	ret = nilfs_btree_do_lookup(btree, path, key, &ptr, level, 1);
	if (ret < 0)
		goto out;

	if (NILFS_BMAP_USE_VBN(btree)) {
		dat = nilfs_bmap_get_dat(btree);
		ret = nilfs_dat_translate(dat, ptr, &blocknr);
		if (ret < 0)
			goto out;
		ptr = blocknr;
	}
	cnt = 1;
	if (cnt == maxblocks)
		goto end;

	maxlevel = nilfs_btree_height(btree) - 1;
	node = nilfs_btree_get_node(btree, path, level, &ncmax);
	index = path[level].bp_index + 1;
	for (;;) {
		while (index < nilfs_btree_node_get_nchildren(node)) {
			if (nilfs_btree_node_get_key(node, index) !=
			    key + cnt)
				goto end;
			ptr2 = nilfs_btree_node_get_ptr(node, index, ncmax);
			if (dat) {
				ret = nilfs_dat_translate(dat, ptr2, &blocknr);
				if (ret < 0)
					goto out;
				ptr2 = blocknr;
			}
			if (ptr2 != ptr + cnt || ++cnt == maxblocks)
				goto end;
			index++;
			continue;
		}
		if (level == maxlevel)
			break;

		/* look-up right sibling node */
		p.node = nilfs_btree_get_node(btree, path, level + 1, &p.ncmax);
		p.index = path[level + 1].bp_index + 1;
		p.max_ra_blocks = 7;
		if (p.index >= nilfs_btree_node_get_nchildren(p.node) ||
		    nilfs_btree_node_get_key(p.node, p.index) != key + cnt)
			break;
		ptr2 = nilfs_btree_node_get_ptr(p.node, p.index, p.ncmax);
		path[level + 1].bp_index = p.index;

		brelse(path[level].bp_bh);
		path[level].bp_bh = NULL;

		ret = __nilfs_btree_get_block(btree, ptr2, &path[level].bp_bh,
					      &p);
		if (ret < 0)
			goto out;
		node = nilfs_btree_get_nonroot_node(path, level);
		ncmax = nilfs_btree_nchildren_per_block(btree);
		index = 0;
		path[level].bp_index = index;
	}
 end:
	*ptrp = ptr;
	ret = cnt;
 out:
	nilfs_btree_free_path(path);
	return ret;
}

static void nilfs_btree_promote_key(struct nilfs_bmap *btree,
				    struct nilfs_btree_path *path,
				    int level, __u64 key)
{
	if (level < nilfs_btree_height(btree) - 1) {
		do {
			nilfs_btree_node_set_key(
				nilfs_btree_get_nonroot_node(path, level),
				path[level].bp_index, key);
			if (!buffer_dirty(path[level].bp_bh))
				mark_buffer_dirty(path[level].bp_bh);
		} while ((path[level].bp_index == 0) &&
			 (++level < nilfs_btree_height(btree) - 1));
	}

	/* root */
	if (level == nilfs_btree_height(btree) - 1) {
		nilfs_btree_node_set_key(nilfs_btree_get_root(btree),
					 path[level].bp_index, key);
	}
}

static void nilfs_btree_do_insert(struct nilfs_bmap *btree,
				  struct nilfs_btree_path *path,
				  int level, __u64 *keyp, __u64 *ptrp)
{
	struct nilfs_btree_node *node;
	int ncblk;

	if (level < nilfs_btree_height(btree) - 1) {
		node = nilfs_btree_get_nonroot_node(path, level);
		ncblk = nilfs_btree_nchildren_per_block(btree);
		nilfs_btree_node_insert(node, path[level].bp_index,
					*keyp, *ptrp, ncblk);
		if (!buffer_dirty(path[level].bp_bh))
			mark_buffer_dirty(path[level].bp_bh);

		if (path[level].bp_index == 0)
			nilfs_btree_promote_key(btree, path, level + 1,
						nilfs_btree_node_get_key(node,
									 0));
	} else {
		node = nilfs_btree_get_root(btree);
		nilfs_btree_node_insert(node, path[level].bp_index,
					*keyp, *ptrp,
					NILFS_BTREE_ROOT_NCHILDREN_MAX);
	}
}

static void nilfs_btree_carry_left(struct nilfs_bmap *btree,
				   struct nilfs_btree_path *path,
				   int level, __u64 *keyp, __u64 *ptrp)
{
	struct nilfs_btree_node *node, *left;
	int nchildren, lnchildren, n, move, ncblk;

	node = nilfs_btree_get_nonroot_node(path, level);
	left = nilfs_btree_get_sib_node(path, level);
	nchildren = nilfs_btree_node_get_nchildren(node);
	lnchildren = nilfs_btree_node_get_nchildren(left);
	ncblk = nilfs_btree_nchildren_per_block(btree);
	move = 0;

	n = (nchildren + lnchildren + 1) / 2 - lnchildren;
	if (n > path[level].bp_index) {
		/* move insert point */
		n--;
		move = 1;
	}

	nilfs_btree_node_move_left(left, node, n, ncblk, ncblk);

	if (!buffer_dirty(path[level].bp_bh))
		mark_buffer_dirty(path[level].bp_bh);
	if (!buffer_dirty(path[level].bp_sib_bh))
		mark_buffer_dirty(path[level].bp_sib_bh);

	nilfs_btree_promote_key(btree, path, level + 1,
				nilfs_btree_node_get_key(node, 0));

	if (move) {
		brelse(path[level].bp_bh);
		path[level].bp_bh = path[level].bp_sib_bh;
		path[level].bp_sib_bh = NULL;
		path[level].bp_index += lnchildren;
		path[level + 1].bp_index--;
	} else {
		brelse(path[level].bp_sib_bh);
		path[level].bp_sib_bh = NULL;
		path[level].bp_index -= n;
	}

	nilfs_btree_do_insert(btree, path, level, keyp, ptrp);
}

static void nilfs_btree_carry_right(struct nilfs_bmap *btree,
				    struct nilfs_btree_path *path,
				    int level, __u64 *keyp, __u64 *ptrp)
{
	struct nilfs_btree_node *node, *right;
	int nchildren, rnchildren, n, move, ncblk;

	node = nilfs_btree_get_nonroot_node(path, level);
	right = nilfs_btree_get_sib_node(path, level);
	nchildren = nilfs_btree_node_get_nchildren(node);
	rnchildren = nilfs_btree_node_get_nchildren(right);
	ncblk = nilfs_btree_nchildren_per_block(btree);
	move = 0;

	n = (nchildren + rnchildren + 1) / 2 - rnchildren;
	if (n > nchildren - path[level].bp_index) {
		/* move insert point */
		n--;
		move = 1;
	}

	nilfs_btree_node_move_right(node, right, n, ncblk, ncblk);

	if (!buffer_dirty(path[level].bp_bh))
		mark_buffer_dirty(path[level].bp_bh);
	if (!buffer_dirty(path[level].bp_sib_bh))
		mark_buffer_dirty(path[level].bp_sib_bh);

	path[level + 1].bp_index++;
	nilfs_btree_promote_key(btree, path, level + 1,
				nilfs_btree_node_get_key(right, 0));
	path[level + 1].bp_index--;

	if (move) {
		brelse(path[level].bp_bh);
		path[level].bp_bh = path[level].bp_sib_bh;
		path[level].bp_sib_bh = NULL;
		path[level].bp_index -= nilfs_btree_node_get_nchildren(node);
		path[level + 1].bp_index++;
	} else {
		brelse(path[level].bp_sib_bh);
		path[level].bp_sib_bh = NULL;
	}

	nilfs_btree_do_insert(btree, path, level, keyp, ptrp);
}

static void nilfs_btree_split(struct nilfs_bmap *btree,
			      struct nilfs_btree_path *path,
			      int level, __u64 *keyp, __u64 *ptrp)
{
	struct nilfs_btree_node *node, *right;
	int nchildren, n, move, ncblk;

	node = nilfs_btree_get_nonroot_node(path, level);
	right = nilfs_btree_get_sib_node(path, level);
	nchildren = nilfs_btree_node_get_nchildren(node);
	ncblk = nilfs_btree_nchildren_per_block(btree);
	move = 0;

	n = (nchildren + 1) / 2;
	if (n > nchildren - path[level].bp_index) {
		n--;
		move = 1;
	}

	nilfs_btree_node_move_right(node, right, n, ncblk, ncblk);

	if (!buffer_dirty(path[level].bp_bh))
		mark_buffer_dirty(path[level].bp_bh);
	if (!buffer_dirty(path[level].bp_sib_bh))
		mark_buffer_dirty(path[level].bp_sib_bh);

	if (move) {
		path[level].bp_index -= nilfs_btree_node_get_nchildren(node);
		nilfs_btree_node_insert(right, path[level].bp_index,
					*keyp, *ptrp, ncblk);

		*keyp = nilfs_btree_node_get_key(right, 0);
		*ptrp = path[level].bp_newreq.bpr_ptr;

		brelse(path[level].bp_bh);
		path[level].bp_bh = path[level].bp_sib_bh;
		path[level].bp_sib_bh = NULL;
	} else {
		nilfs_btree_do_insert(btree, path, level, keyp, ptrp);

		*keyp = nilfs_btree_node_get_key(right, 0);
		*ptrp = path[level].bp_newreq.bpr_ptr;

		brelse(path[level].bp_sib_bh);
		path[level].bp_sib_bh = NULL;
	}

	path[level + 1].bp_index++;
}

static void nilfs_btree_grow(struct nilfs_bmap *btree,
			     struct nilfs_btree_path *path,
			     int level, __u64 *keyp, __u64 *ptrp)
{
	struct nilfs_btree_node *root, *child;
	int n, ncblk;

	root = nilfs_btree_get_root(btree);
	child = nilfs_btree_get_sib_node(path, level);
	ncblk = nilfs_btree_nchildren_per_block(btree);

	n = nilfs_btree_node_get_nchildren(root);

	nilfs_btree_node_move_right(root, child, n,
				    NILFS_BTREE_ROOT_NCHILDREN_MAX, ncblk);
	nilfs_btree_node_set_level(root, level + 1);

	if (!buffer_dirty(path[level].bp_sib_bh))
		mark_buffer_dirty(path[level].bp_sib_bh);

	path[level].bp_bh = path[level].bp_sib_bh;
	path[level].bp_sib_bh = NULL;

	nilfs_btree_do_insert(btree, path, level, keyp, ptrp);

	*keyp = nilfs_btree_node_get_key(child, 0);
	*ptrp = path[level].bp_newreq.bpr_ptr;
}

static __u64 nilfs_btree_find_near(const struct nilfs_bmap *btree,
				   const struct nilfs_btree_path *path)
{
	struct nilfs_btree_node *node;
	int level, ncmax;

	if (path == NULL)
		return NILFS_BMAP_INVALID_PTR;

	/* left sibling */
	level = NILFS_BTREE_LEVEL_NODE_MIN;
	if (path[level].bp_index > 0) {
		node = nilfs_btree_get_node(btree, path, level, &ncmax);
		return nilfs_btree_node_get_ptr(node,
						path[level].bp_index - 1,
						ncmax);
	}

	/* parent */
	level = NILFS_BTREE_LEVEL_NODE_MIN + 1;
	if (level <= nilfs_btree_height(btree) - 1) {
		node = nilfs_btree_get_node(btree, path, level, &ncmax);
		return nilfs_btree_node_get_ptr(node, path[level].bp_index,
						ncmax);
	}

	return NILFS_BMAP_INVALID_PTR;
}

static __u64 nilfs_btree_find_target_v(const struct nilfs_bmap *btree,
				       const struct nilfs_btree_path *path,
				       __u64 key)
{
	__u64 ptr;

	ptr = nilfs_bmap_find_target_seq(btree, key);
	if (ptr != NILFS_BMAP_INVALID_PTR)
		/* sequential access */
		return ptr;

	ptr = nilfs_btree_find_near(btree, path);
	if (ptr != NILFS_BMAP_INVALID_PTR)
		/* near */
		return ptr;

	/* block group */
	return nilfs_bmap_find_target_in_group(btree);
}

static int nilfs_btree_prepare_insert(struct nilfs_bmap *btree,
				      struct nilfs_btree_path *path,
				      int *levelp, __u64 key, __u64 ptr,
				      struct nilfs_bmap_stats *stats)
{
	struct buffer_head *bh;
	struct nilfs_btree_node *node, *parent, *sib;
	__u64 sibptr;
	int pindex, level, ncmax, ncblk, ret;
	struct inode *dat = NULL;

	stats->bs_nblocks = 0;
	level = NILFS_BTREE_LEVEL_DATA;

	/* allocate a new ptr for data block */
	if (NILFS_BMAP_USE_VBN(btree)) {
		path[level].bp_newreq.bpr_ptr =
			nilfs_btree_find_target_v(btree, path, key);
		dat = nilfs_bmap_get_dat(btree);
	}

	ret = nilfs_bmap_prepare_alloc_ptr(btree, &path[level].bp_newreq, dat);
	if (ret < 0)
		goto err_out_data;

	ncblk = nilfs_btree_nchildren_per_block(btree);

	for (level = NILFS_BTREE_LEVEL_NODE_MIN;
	     level < nilfs_btree_height(btree) - 1;
	     level++) {
		node = nilfs_btree_get_nonroot_node(path, level);
		if (nilfs_btree_node_get_nchildren(node) < ncblk) {
			path[level].bp_op = nilfs_btree_do_insert;
			stats->bs_nblocks++;
			goto out;
		}

		parent = nilfs_btree_get_node(btree, path, level + 1, &ncmax);
		pindex = path[level + 1].bp_index;

		/* left sibling */
		if (pindex > 0) {
			sibptr = nilfs_btree_node_get_ptr(parent, pindex - 1,
							  ncmax);
			ret = nilfs_btree_get_block(btree, sibptr, &bh);
			if (ret < 0)
				goto err_out_child_node;
			sib = (struct nilfs_btree_node *)bh->b_data;
			if (nilfs_btree_node_get_nchildren(sib) < ncblk) {
				path[level].bp_sib_bh = bh;
				path[level].bp_op = nilfs_btree_carry_left;
				stats->bs_nblocks++;
				goto out;
			} else {
				brelse(bh);
			}
		}

		/* right sibling */
		if (pindex < nilfs_btree_node_get_nchildren(parent) - 1) {
			sibptr = nilfs_btree_node_get_ptr(parent, pindex + 1,
							  ncmax);
			ret = nilfs_btree_get_block(btree, sibptr, &bh);
			if (ret < 0)
				goto err_out_child_node;
			sib = (struct nilfs_btree_node *)bh->b_data;
			if (nilfs_btree_node_get_nchildren(sib) < ncblk) {
				path[level].bp_sib_bh = bh;
				path[level].bp_op = nilfs_btree_carry_right;
				stats->bs_nblocks++;
				goto out;
			} else {
				brelse(bh);
			}
		}

		/* split */
		path[level].bp_newreq.bpr_ptr =
			path[level - 1].bp_newreq.bpr_ptr + 1;
		ret = nilfs_bmap_prepare_alloc_ptr(btree,
						   &path[level].bp_newreq, dat);
		if (ret < 0)
			goto err_out_child_node;
		ret = nilfs_btree_get_new_block(btree,
						path[level].bp_newreq.bpr_ptr,
						&bh);
		if (ret < 0)
			goto err_out_curr_node;

		stats->bs_nblocks++;

		sib = (struct nilfs_btree_node *)bh->b_data;
		nilfs_btree_node_init(sib, 0, level, 0, ncblk, NULL, NULL);
		path[level].bp_sib_bh = bh;
		path[level].bp_op = nilfs_btree_split;
	}

	/* root */
	node = nilfs_btree_get_root(btree);
	if (nilfs_btree_node_get_nchildren(node) <
	    NILFS_BTREE_ROOT_NCHILDREN_MAX) {
		path[level].bp_op = nilfs_btree_do_insert;
		stats->bs_nblocks++;
		goto out;
	}

	/* grow */
	path[level].bp_newreq.bpr_ptr = path[level - 1].bp_newreq.bpr_ptr + 1;
	ret = nilfs_bmap_prepare_alloc_ptr(btree, &path[level].bp_newreq, dat);
	if (ret < 0)
		goto err_out_child_node;
	ret = nilfs_btree_get_new_block(btree, path[level].bp_newreq.bpr_ptr,
					&bh);
	if (ret < 0)
		goto err_out_curr_node;

	nilfs_btree_node_init((struct nilfs_btree_node *)bh->b_data,
			      0, level, 0, ncblk, NULL, NULL);
	path[level].bp_sib_bh = bh;
	path[level].bp_op = nilfs_btree_grow;

	level++;
	path[level].bp_op = nilfs_btree_do_insert;

	/* a newly-created node block and a data block are added */
	stats->bs_nblocks += 2;

	/* success */
 out:
	*levelp = level;
	return ret;

	/* error */
 err_out_curr_node:
	nilfs_bmap_abort_alloc_ptr(btree, &path[level].bp_newreq, dat);
 err_out_child_node:
	for (level--; level > NILFS_BTREE_LEVEL_DATA; level--) {
		nilfs_btnode_delete(path[level].bp_sib_bh);
		nilfs_bmap_abort_alloc_ptr(btree, &path[level].bp_newreq, dat);

	}

	nilfs_bmap_abort_alloc_ptr(btree, &path[level].bp_newreq, dat);
 err_out_data:
	*levelp = level;
	stats->bs_nblocks = 0;
	return ret;
}

static void nilfs_btree_commit_insert(struct nilfs_bmap *btree,
				      struct nilfs_btree_path *path,
				      int maxlevel, __u64 key, __u64 ptr)
{
	struct inode *dat = NULL;
	int level;

	set_buffer_nilfs_volatile((struct buffer_head *)((unsigned long)ptr));
	ptr = path[NILFS_BTREE_LEVEL_DATA].bp_newreq.bpr_ptr;
	if (NILFS_BMAP_USE_VBN(btree)) {
		nilfs_bmap_set_target_v(btree, key, ptr);
		dat = nilfs_bmap_get_dat(btree);
	}

	for (level = NILFS_BTREE_LEVEL_NODE_MIN; level <= maxlevel; level++) {
		nilfs_bmap_commit_alloc_ptr(btree,
					    &path[level - 1].bp_newreq, dat);
		path[level].bp_op(btree, path, level, &key, &ptr);
	}

	if (!nilfs_bmap_dirty(btree))
		nilfs_bmap_set_dirty(btree);
}

static int nilfs_btree_insert(struct nilfs_bmap *btree, __u64 key, __u64 ptr)
{
	struct nilfs_btree_path *path;
	struct nilfs_bmap_stats stats;
	int level, ret;

	path = nilfs_btree_alloc_path();
	if (path == NULL)
		return -ENOMEM;

	ret = nilfs_btree_do_lookup(btree, path, key, NULL,
				    NILFS_BTREE_LEVEL_NODE_MIN, 0);
	if (ret != -ENOENT) {
		if (ret == 0)
			ret = -EEXIST;
		goto out;
	}

	ret = nilfs_btree_prepare_insert(btree, path, &level, key, ptr, &stats);
	if (ret < 0)
		goto out;
	nilfs_btree_commit_insert(btree, path, level, key, ptr);
	nilfs_inode_add_blocks(btree->b_inode, stats.bs_nblocks);

 out:
	nilfs_btree_free_path(path);
	return ret;
}

static void nilfs_btree_do_delete(struct nilfs_bmap *btree,
				  struct nilfs_btree_path *path,
				  int level, __u64 *keyp, __u64 *ptrp)
{
	struct nilfs_btree_node *node;
	int ncblk;

	if (level < nilfs_btree_height(btree) - 1) {
		node = nilfs_btree_get_nonroot_node(path, level);
		ncblk = nilfs_btree_nchildren_per_block(btree);
		nilfs_btree_node_delete(node, path[level].bp_index,
					keyp, ptrp, ncblk);
		if (!buffer_dirty(path[level].bp_bh))
			mark_buffer_dirty(path[level].bp_bh);
		if (path[level].bp_index == 0)
			nilfs_btree_promote_key(btree, path, level + 1,
				nilfs_btree_node_get_key(node, 0));
	} else {
		node = nilfs_btree_get_root(btree);
		nilfs_btree_node_delete(node, path[level].bp_index,
					keyp, ptrp,
					NILFS_BTREE_ROOT_NCHILDREN_MAX);
	}
}

static void nilfs_btree_borrow_left(struct nilfs_bmap *btree,
				    struct nilfs_btree_path *path,
				    int level, __u64 *keyp, __u64 *ptrp)
{
	struct nilfs_btree_node *node, *left;
	int nchildren, lnchildren, n, ncblk;

	nilfs_btree_do_delete(btree, path, level, keyp, ptrp);

	node = nilfs_btree_get_nonroot_node(path, level);
	left = nilfs_btree_get_sib_node(path, level);
	nchildren = nilfs_btree_node_get_nchildren(node);
	lnchildren = nilfs_btree_node_get_nchildren(left);
	ncblk = nilfs_btree_nchildren_per_block(btree);

	n = (nchildren + lnchildren) / 2 - nchildren;

	nilfs_btree_node_move_right(left, node, n, ncblk, ncblk);

	if (!buffer_dirty(path[level].bp_bh))
		mark_buffer_dirty(path[level].bp_bh);
	if (!buffer_dirty(path[level].bp_sib_bh))
		mark_buffer_dirty(path[level].bp_sib_bh);

	nilfs_btree_promote_key(btree, path, level + 1,
				nilfs_btree_node_get_key(node, 0));

	brelse(path[level].bp_sib_bh);
	path[level].bp_sib_bh = NULL;
	path[level].bp_index += n;
}

static void nilfs_btree_borrow_right(struct nilfs_bmap *btree,
				     struct nilfs_btree_path *path,
				     int level, __u64 *keyp, __u64 *ptrp)
{
	struct nilfs_btree_node *node, *right;
	int nchildren, rnchildren, n, ncblk;

	nilfs_btree_do_delete(btree, path, level, keyp, ptrp);

	node = nilfs_btree_get_nonroot_node(path, level);
	right = nilfs_btree_get_sib_node(path, level);
	nchildren = nilfs_btree_node_get_nchildren(node);
	rnchildren = nilfs_btree_node_get_nchildren(right);
	ncblk = nilfs_btree_nchildren_per_block(btree);

	n = (nchildren + rnchildren) / 2 - nchildren;

	nilfs_btree_node_move_left(node, right, n, ncblk, ncblk);

	if (!buffer_dirty(path[level].bp_bh))
		mark_buffer_dirty(path[level].bp_bh);
	if (!buffer_dirty(path[level].bp_sib_bh))
		mark_buffer_dirty(path[level].bp_sib_bh);

	path[level + 1].bp_index++;
	nilfs_btree_promote_key(btree, path, level + 1,
				nilfs_btree_node_get_key(right, 0));
	path[level + 1].bp_index--;

	brelse(path[level].bp_sib_bh);
	path[level].bp_sib_bh = NULL;
}

static void nilfs_btree_concat_left(struct nilfs_bmap *btree,
				    struct nilfs_btree_path *path,
				    int level, __u64 *keyp, __u64 *ptrp)
{
	struct nilfs_btree_node *node, *left;
	int n, ncblk;

	nilfs_btree_do_delete(btree, path, level, keyp, ptrp);

	node = nilfs_btree_get_nonroot_node(path, level);
	left = nilfs_btree_get_sib_node(path, level);
	ncblk = nilfs_btree_nchildren_per_block(btree);

	n = nilfs_btree_node_get_nchildren(node);

	nilfs_btree_node_move_left(left, node, n, ncblk, ncblk);

	if (!buffer_dirty(path[level].bp_sib_bh))
		mark_buffer_dirty(path[level].bp_sib_bh);

	nilfs_btnode_delete(path[level].bp_bh);
	path[level].bp_bh = path[level].bp_sib_bh;
	path[level].bp_sib_bh = NULL;
	path[level].bp_index += nilfs_btree_node_get_nchildren(left);
}

static void nilfs_btree_concat_right(struct nilfs_bmap *btree,
				     struct nilfs_btree_path *path,
				     int level, __u64 *keyp, __u64 *ptrp)
{
	struct nilfs_btree_node *node, *right;
	int n, ncblk;

	nilfs_btree_do_delete(btree, path, level, keyp, ptrp);

	node = nilfs_btree_get_nonroot_node(path, level);
	right = nilfs_btree_get_sib_node(path, level);
	ncblk = nilfs_btree_nchildren_per_block(btree);

	n = nilfs_btree_node_get_nchildren(right);

	nilfs_btree_node_move_left(node, right, n, ncblk, ncblk);

	if (!buffer_dirty(path[level].bp_bh))
		mark_buffer_dirty(path[level].bp_bh);

	nilfs_btnode_delete(path[level].bp_sib_bh);
	path[level].bp_sib_bh = NULL;
	path[level + 1].bp_index++;
}

static void nilfs_btree_shrink(struct nilfs_bmap *btree,
			       struct nilfs_btree_path *path,
			       int level, __u64 *keyp, __u64 *ptrp)
{
	struct nilfs_btree_node *root, *child;
	int n, ncblk;

	nilfs_btree_do_delete(btree, path, level, keyp, ptrp);

	root = nilfs_btree_get_root(btree);
	child = nilfs_btree_get_nonroot_node(path, level);
	ncblk = nilfs_btree_nchildren_per_block(btree);

	nilfs_btree_node_delete(root, 0, NULL, NULL,
				NILFS_BTREE_ROOT_NCHILDREN_MAX);
	nilfs_btree_node_set_level(root, level);
	n = nilfs_btree_node_get_nchildren(child);
	nilfs_btree_node_move_left(root, child, n,
				   NILFS_BTREE_ROOT_NCHILDREN_MAX, ncblk);

	nilfs_btnode_delete(path[level].bp_bh);
	path[level].bp_bh = NULL;
}

static void nilfs_btree_nop(struct nilfs_bmap *btree,
			    struct nilfs_btree_path *path,
			    int level, __u64 *keyp, __u64 *ptrp)
{
}

static int nilfs_btree_prepare_delete(struct nilfs_bmap *btree,
				      struct nilfs_btree_path *path,
				      int *levelp,
				      struct nilfs_bmap_stats *stats,
				      struct inode *dat)
{
	struct buffer_head *bh;
	struct nilfs_btree_node *node, *parent, *sib;
	__u64 sibptr;
	int pindex, dindex, level, ncmin, ncmax, ncblk, ret;

	ret = 0;
	stats->bs_nblocks = 0;
	ncmin = NILFS_BTREE_NODE_NCHILDREN_MIN(nilfs_btree_node_size(btree));
	ncblk = nilfs_btree_nchildren_per_block(btree);

	for (level = NILFS_BTREE_LEVEL_NODE_MIN, dindex = path[level].bp_index;
	     level < nilfs_btree_height(btree) - 1;
	     level++) {
		node = nilfs_btree_get_nonroot_node(path, level);
		path[level].bp_oldreq.bpr_ptr =
			nilfs_btree_node_get_ptr(node, dindex, ncblk);
		ret = nilfs_bmap_prepare_end_ptr(btree,
						 &path[level].bp_oldreq, dat);
		if (ret < 0)
			goto err_out_child_node;

		if (nilfs_btree_node_get_nchildren(node) > ncmin) {
			path[level].bp_op = nilfs_btree_do_delete;
			stats->bs_nblocks++;
			goto out;
		}

		parent = nilfs_btree_get_node(btree, path, level + 1, &ncmax);
		pindex = path[level + 1].bp_index;
		dindex = pindex;

		if (pindex > 0) {
			/* left sibling */
			sibptr = nilfs_btree_node_get_ptr(parent, pindex - 1,
							  ncmax);
			ret = nilfs_btree_get_block(btree, sibptr, &bh);
			if (ret < 0)
				goto err_out_curr_node;
			sib = (struct nilfs_btree_node *)bh->b_data;
			if (nilfs_btree_node_get_nchildren(sib) > ncmin) {
				path[level].bp_sib_bh = bh;
				path[level].bp_op = nilfs_btree_borrow_left;
				stats->bs_nblocks++;
				goto out;
			} else {
				path[level].bp_sib_bh = bh;
				path[level].bp_op = nilfs_btree_concat_left;
				stats->bs_nblocks++;
				/* continue; */
			}
		} else if (pindex <
			   nilfs_btree_node_get_nchildren(parent) - 1) {
			/* right sibling */
			sibptr = nilfs_btree_node_get_ptr(parent, pindex + 1,
							  ncmax);
			ret = nilfs_btree_get_block(btree, sibptr, &bh);
			if (ret < 0)
				goto err_out_curr_node;
			sib = (struct nilfs_btree_node *)bh->b_data;
			if (nilfs_btree_node_get_nchildren(sib) > ncmin) {
				path[level].bp_sib_bh = bh;
				path[level].bp_op = nilfs_btree_borrow_right;
				stats->bs_nblocks++;
				goto out;
			} else {
				path[level].bp_sib_bh = bh;
				path[level].bp_op = nilfs_btree_concat_right;
				stats->bs_nblocks++;
				/*
				 * When merging right sibling node
				 * into the current node, pointer to
				 * the right sibling node must be
				 * terminated instead.  The adjustment
				 * below is required for that.
				 */
				dindex = pindex + 1;
				/* continue; */
			}
		} else {
			/* no siblings */
			/* the only child of the root node */
			WARN_ON(level != nilfs_btree_height(btree) - 2);
			if (nilfs_btree_node_get_nchildren(node) - 1 <=
			    NILFS_BTREE_ROOT_NCHILDREN_MAX) {
				path[level].bp_op = nilfs_btree_shrink;
				stats->bs_nblocks += 2;
				level++;
				path[level].bp_op = nilfs_btree_nop;
				goto shrink_root_child;
			} else {
				path[level].bp_op = nilfs_btree_do_delete;
				stats->bs_nblocks++;
				goto out;
			}
		}
	}

	/* child of the root node is deleted */
	path[level].bp_op = nilfs_btree_do_delete;
	stats->bs_nblocks++;

shrink_root_child:
	node = nilfs_btree_get_root(btree);
	path[level].bp_oldreq.bpr_ptr =
		nilfs_btree_node_get_ptr(node, dindex,
					 NILFS_BTREE_ROOT_NCHILDREN_MAX);

	ret = nilfs_bmap_prepare_end_ptr(btree, &path[level].bp_oldreq, dat);
	if (ret < 0)
		goto err_out_child_node;

	/* success */
 out:
	*levelp = level;
	return ret;

	/* error */
 err_out_curr_node:
	nilfs_bmap_abort_end_ptr(btree, &path[level].bp_oldreq, dat);
 err_out_child_node:
	for (level--; level >= NILFS_BTREE_LEVEL_NODE_MIN; level--) {
		brelse(path[level].bp_sib_bh);
		nilfs_bmap_abort_end_ptr(btree, &path[level].bp_oldreq, dat);
	}
	*levelp = level;
	stats->bs_nblocks = 0;
	return ret;
}

static void nilfs_btree_commit_delete(struct nilfs_bmap *btree,
				      struct nilfs_btree_path *path,
				      int maxlevel, struct inode *dat)
{
	int level;

	for (level = NILFS_BTREE_LEVEL_NODE_MIN; level <= maxlevel; level++) {
		nilfs_bmap_commit_end_ptr(btree, &path[level].bp_oldreq, dat);
		path[level].bp_op(btree, path, level, NULL, NULL);
	}

	if (!nilfs_bmap_dirty(btree))
		nilfs_bmap_set_dirty(btree);
}

static int nilfs_btree_delete(struct nilfs_bmap *btree, __u64 key)

{
	struct nilfs_btree_path *path;
	struct nilfs_bmap_stats stats;
	struct inode *dat;
	int level, ret;

	path = nilfs_btree_alloc_path();
	if (path == NULL)
		return -ENOMEM;

	ret = nilfs_btree_do_lookup(btree, path, key, NULL,
				    NILFS_BTREE_LEVEL_NODE_MIN, 0);
	if (ret < 0)
		goto out;


	dat = NILFS_BMAP_USE_VBN(btree) ? nilfs_bmap_get_dat(btree) : NULL;

	ret = nilfs_btree_prepare_delete(btree, path, &level, &stats, dat);
	if (ret < 0)
		goto out;
	nilfs_btree_commit_delete(btree, path, level, dat);
	nilfs_inode_sub_blocks(btree->b_inode, stats.bs_nblocks);

out:
	nilfs_btree_free_path(path);
	return ret;
}

static int nilfs_btree_seek_key(const struct nilfs_bmap *btree, __u64 start,
				__u64 *keyp)
{
	struct nilfs_btree_path *path;
	const int minlevel = NILFS_BTREE_LEVEL_NODE_MIN;
	int ret;

	path = nilfs_btree_alloc_path();
	if (!path)
		return -ENOMEM;

	ret = nilfs_btree_do_lookup(btree, path, start, NULL, minlevel, 0);
	if (!ret)
		*keyp = start;
	else if (ret == -ENOENT)
		ret = nilfs_btree_get_next_key(btree, path, minlevel, keyp);

	nilfs_btree_free_path(path);
	return ret;
}

static int nilfs_btree_last_key(const struct nilfs_bmap *btree, __u64 *keyp)
{
	struct nilfs_btree_path *path;
	int ret;

	path = nilfs_btree_alloc_path();
	if (path == NULL)
		return -ENOMEM;

	ret = nilfs_btree_do_lookup_last(btree, path, keyp, NULL);

	nilfs_btree_free_path(path);

	return ret;
}

static int nilfs_btree_check_delete(struct nilfs_bmap *btree, __u64 key)
{
	struct buffer_head *bh;
	struct nilfs_btree_node *root, *node;
	__u64 maxkey, nextmaxkey;
	__u64 ptr;
	int nchildren, ret;

	root = nilfs_btree_get_root(btree);
	switch (nilfs_btree_height(btree)) {
	case 2:
		bh = NULL;
		node = root;
		break;
	case 3:
		nchildren = nilfs_btree_node_get_nchildren(root);
		if (nchildren > 1)
			return 0;
		ptr = nilfs_btree_node_get_ptr(root, nchildren - 1,
					       NILFS_BTREE_ROOT_NCHILDREN_MAX);
		ret = nilfs_btree_get_block(btree, ptr, &bh);
		if (ret < 0)
			return ret;
		node = (struct nilfs_btree_node *)bh->b_data;
		break;
	default:
		return 0;
	}

	nchildren = nilfs_btree_node_get_nchildren(node);
	maxkey = nilfs_btree_node_get_key(node, nchildren - 1);
	nextmaxkey = (nchildren > 1) ?
		nilfs_btree_node_get_key(node, nchildren - 2) : 0;
	if (bh != NULL)
		brelse(bh);

	return (maxkey == key) && (nextmaxkey < NILFS_BMAP_LARGE_LOW);
}

static int nilfs_btree_gather_data(struct nilfs_bmap *btree,
				   __u64 *keys, __u64 *ptrs, int nitems)
{
	struct buffer_head *bh;
	struct nilfs_btree_node *node, *root;
	__le64 *dkeys;
	__le64 *dptrs;
	__u64 ptr;
	int nchildren, ncmax, i, ret;

	root = nilfs_btree_get_root(btree);
	switch (nilfs_btree_height(btree)) {
	case 2:
		bh = NULL;
		node = root;
		ncmax = NILFS_BTREE_ROOT_NCHILDREN_MAX;
		break;
	case 3:
		nchildren = nilfs_btree_node_get_nchildren(root);
		WARN_ON(nchildren > 1);
		ptr = nilfs_btree_node_get_ptr(root, nchildren - 1,
					       NILFS_BTREE_ROOT_NCHILDREN_MAX);
		ret = nilfs_btree_get_block(btree, ptr, &bh);
		if (ret < 0)
			return ret;
		node = (struct nilfs_btree_node *)bh->b_data;
		ncmax = nilfs_btree_nchildren_per_block(btree);
		break;
	default:
		node = NULL;
		return -EINVAL;
	}

	nchildren = nilfs_btree_node_get_nchildren(node);
	if (nchildren < nitems)
		nitems = nchildren;
	dkeys = nilfs_btree_node_dkeys(node);
	dptrs = nilfs_btree_node_dptrs(node, ncmax);
	for (i = 0; i < nitems; i++) {
		keys[i] = le64_to_cpu(dkeys[i]);
		ptrs[i] = le64_to_cpu(dptrs[i]);
	}

	if (bh != NULL)
		brelse(bh);

	return nitems;
}

static int
nilfs_btree_prepare_convert_and_insert(struct nilfs_bmap *btree, __u64 key,
				       union nilfs_bmap_ptr_req *dreq,
				       union nilfs_bmap_ptr_req *nreq,
				       struct buffer_head **bhp,
				       struct nilfs_bmap_stats *stats)
{
	struct buffer_head *bh;
	struct inode *dat = NULL;
	int ret;

	stats->bs_nblocks = 0;

	/* for data */
	/* cannot find near ptr */
	if (NILFS_BMAP_USE_VBN(btree)) {
		dreq->bpr_ptr = nilfs_btree_find_target_v(btree, NULL, key);
		dat = nilfs_bmap_get_dat(btree);
	}

	ret = nilfs_attach_btree_node_cache(&NILFS_BMAP_I(btree)->vfs_inode);
	if (ret < 0)
		return ret;

	ret = nilfs_bmap_prepare_alloc_ptr(btree, dreq, dat);
	if (ret < 0)
		return ret;

	*bhp = NULL;
	stats->bs_nblocks++;
	if (nreq != NULL) {
		nreq->bpr_ptr = dreq->bpr_ptr + 1;
		ret = nilfs_bmap_prepare_alloc_ptr(btree, nreq, dat);
		if (ret < 0)
			goto err_out_dreq;

		ret = nilfs_btree_get_new_block(btree, nreq->bpr_ptr, &bh);
		if (ret < 0)
			goto err_out_nreq;

		*bhp = bh;
		stats->bs_nblocks++;
	}

	/* success */
	return 0;

	/* error */
 err_out_nreq:
	nilfs_bmap_abort_alloc_ptr(btree, nreq, dat);
 err_out_dreq:
	nilfs_bmap_abort_alloc_ptr(btree, dreq, dat);
	stats->bs_nblocks = 0;
	return ret;

}

static void
nilfs_btree_commit_convert_and_insert(struct nilfs_bmap *btree,
				      __u64 key, __u64 ptr,
				      const __u64 *keys, const __u64 *ptrs,
				      int n,
				      union nilfs_bmap_ptr_req *dreq,
				      union nilfs_bmap_ptr_req *nreq,
				      struct buffer_head *bh)
{
	struct nilfs_btree_node *node;
	struct inode *dat;
	__u64 tmpptr;
	int ncblk;

	/* free resources */
	if (btree->b_ops->bop_clear != NULL)
		btree->b_ops->bop_clear(btree);

	/* ptr must be a pointer to a buffer head. */
	set_buffer_nilfs_volatile((struct buffer_head *)((unsigned long)ptr));

	/* convert and insert */
	dat = NILFS_BMAP_USE_VBN(btree) ? nilfs_bmap_get_dat(btree) : NULL;
	__nilfs_btree_init(btree);
	if (nreq != NULL) {
		nilfs_bmap_commit_alloc_ptr(btree, dreq, dat);
		nilfs_bmap_commit_alloc_ptr(btree, nreq, dat);

		/* create child node at level 1 */
		node = (struct nilfs_btree_node *)bh->b_data;
		ncblk = nilfs_btree_nchildren_per_block(btree);
		nilfs_btree_node_init(node, 0, 1, n, ncblk, keys, ptrs);
		nilfs_btree_node_insert(node, n, key, dreq->bpr_ptr, ncblk);
		if (!buffer_dirty(bh))
			mark_buffer_dirty(bh);
		if (!nilfs_bmap_dirty(btree))
			nilfs_bmap_set_dirty(btree);

		brelse(bh);

		/* create root node at level 2 */
		node = nilfs_btree_get_root(btree);
		tmpptr = nreq->bpr_ptr;
		nilfs_btree_node_init(node, NILFS_BTREE_NODE_ROOT, 2, 1,
				      NILFS_BTREE_ROOT_NCHILDREN_MAX,
				      &keys[0], &tmpptr);
	} else {
		nilfs_bmap_commit_alloc_ptr(btree, dreq, dat);

		/* create root node at level 1 */
		node = nilfs_btree_get_root(btree);
		nilfs_btree_node_init(node, NILFS_BTREE_NODE_ROOT, 1, n,
				      NILFS_BTREE_ROOT_NCHILDREN_MAX,
				      keys, ptrs);
		nilfs_btree_node_insert(node, n, key, dreq->bpr_ptr,
					NILFS_BTREE_ROOT_NCHILDREN_MAX);
		if (!nilfs_bmap_dirty(btree))
			nilfs_bmap_set_dirty(btree);
	}

	if (NILFS_BMAP_USE_VBN(btree))
		nilfs_bmap_set_target_v(btree, key, dreq->bpr_ptr);
}

/**
 * nilfs_btree_convert_and_insert -
 * @bmap:
 * @key:
 * @ptr:
 * @keys:
 * @ptrs:
 * @n:
 */
int nilfs_btree_convert_and_insert(struct nilfs_bmap *btree,
				   __u64 key, __u64 ptr,
				   const __u64 *keys, const __u64 *ptrs, int n)
{
	struct buffer_head *bh = NULL;
	union nilfs_bmap_ptr_req dreq, nreq, *di, *ni;
	struct nilfs_bmap_stats stats;
	int ret;

	if (n + 1 <= NILFS_BTREE_ROOT_NCHILDREN_MAX) {
		di = &dreq;
		ni = NULL;
	} else if ((n + 1) <= NILFS_BTREE_NODE_NCHILDREN_MAX(
			   nilfs_btree_node_size(btree))) {
		di = &dreq;
		ni = &nreq;
	} else {
		di = NULL;
		ni = NULL;
		BUG();
	}

	ret = nilfs_btree_prepare_convert_and_insert(btree, key, di, ni, &bh,
						     &stats);
	if (ret < 0)
		return ret;
	nilfs_btree_commit_convert_and_insert(btree, key, ptr, keys, ptrs, n,
					      di, ni, bh);
	nilfs_inode_add_blocks(btree->b_inode, stats.bs_nblocks);
	return 0;
}

static int nilfs_btree_propagate_p(struct nilfs_bmap *btree,
				   struct nilfs_btree_path *path,
				   int level,
				   struct buffer_head *bh)
{
	while ((++level < nilfs_btree_height(btree) - 1) &&
	       !buffer_dirty(path[level].bp_bh))
		mark_buffer_dirty(path[level].bp_bh);

	return 0;
}

static int nilfs_btree_prepare_update_v(struct nilfs_bmap *btree,
					struct nilfs_btree_path *path,
					int level, struct inode *dat)
{
	struct nilfs_btree_node *parent;
	int ncmax, ret;

	parent = nilfs_btree_get_node(btree, path, level + 1, &ncmax);
	path[level].bp_oldreq.bpr_ptr =
		nilfs_btree_node_get_ptr(parent, path[level + 1].bp_index,
					 ncmax);
	path[level].bp_newreq.bpr_ptr = path[level].bp_oldreq.bpr_ptr + 1;
	ret = nilfs_dat_prepare_update(dat, &path[level].bp_oldreq.bpr_req,
				       &path[level].bp_newreq.bpr_req);
	if (ret < 0)
		return ret;

	if (buffer_nilfs_node(path[level].bp_bh)) {
		path[level].bp_ctxt.oldkey = path[level].bp_oldreq.bpr_ptr;
		path[level].bp_ctxt.newkey = path[level].bp_newreq.bpr_ptr;
		path[level].bp_ctxt.bh = path[level].bp_bh;
		ret = nilfs_btnode_prepare_change_key(
			NILFS_BMAP_I(btree)->i_assoc_inode->i_mapping,
			&path[level].bp_ctxt);
		if (ret < 0) {
			nilfs_dat_abort_update(dat,
					       &path[level].bp_oldreq.bpr_req,
					       &path[level].bp_newreq.bpr_req);
			return ret;
		}
	}

	return 0;
}

static void nilfs_btree_commit_update_v(struct nilfs_bmap *btree,
					struct nilfs_btree_path *path,
					int level, struct inode *dat)
{
	struct nilfs_btree_node *parent;
	int ncmax;

	nilfs_dat_commit_update(dat, &path[level].bp_oldreq.bpr_req,
				&path[level].bp_newreq.bpr_req,
				btree->b_ptr_type == NILFS_BMAP_PTR_VS);

	if (buffer_nilfs_node(path[level].bp_bh)) {
		nilfs_btnode_commit_change_key(
			NILFS_BMAP_I(btree)->i_assoc_inode->i_mapping,
			&path[level].bp_ctxt);
		path[level].bp_bh = path[level].bp_ctxt.bh;
	}
	set_buffer_nilfs_volatile(path[level].bp_bh);

	parent = nilfs_btree_get_node(btree, path, level + 1, &ncmax);
	nilfs_btree_node_set_ptr(parent, path[level + 1].bp_index,
				 path[level].bp_newreq.bpr_ptr, ncmax);
}

static void nilfs_btree_abort_update_v(struct nilfs_bmap *btree,
				       struct nilfs_btree_path *path,
				       int level, struct inode *dat)
{
	nilfs_dat_abort_update(dat, &path[level].bp_oldreq.bpr_req,
			       &path[level].bp_newreq.bpr_req);
	if (buffer_nilfs_node(path[level].bp_bh))
		nilfs_btnode_abort_change_key(
			NILFS_BMAP_I(btree)->i_assoc_inode->i_mapping,
			&path[level].bp_ctxt);
}

static int nilfs_btree_prepare_propagate_v(struct nilfs_bmap *btree,
					   struct nilfs_btree_path *path,
					   int minlevel, int *maxlevelp,
					   struct inode *dat)
{
	int level, ret;

	level = minlevel;
	if (!buffer_nilfs_volatile(path[level].bp_bh)) {
		ret = nilfs_btree_prepare_update_v(btree, path, level, dat);
		if (ret < 0)
			return ret;
	}
	while ((++level < nilfs_btree_height(btree) - 1) &&
	       !buffer_dirty(path[level].bp_bh)) {

		WARN_ON(buffer_nilfs_volatile(path[level].bp_bh));
		ret = nilfs_btree_prepare_update_v(btree, path, level, dat);
		if (ret < 0)
			goto out;
	}

	/* success */
	*maxlevelp = level - 1;
	return 0;

	/* error */
 out:
	while (--level > minlevel)
		nilfs_btree_abort_update_v(btree, path, level, dat);
	if (!buffer_nilfs_volatile(path[level].bp_bh))
		nilfs_btree_abort_update_v(btree, path, level, dat);
	return ret;
}

static void nilfs_btree_commit_propagate_v(struct nilfs_bmap *btree,
					   struct nilfs_btree_path *path,
					   int minlevel, int maxlevel,
					   struct buffer_head *bh,
					   struct inode *dat)
{
	int level;

	if (!buffer_nilfs_volatile(path[minlevel].bp_bh))
		nilfs_btree_commit_update_v(btree, path, minlevel, dat);

	for (level = minlevel + 1; level <= maxlevel; level++)
		nilfs_btree_commit_update_v(btree, path, level, dat);
}

static int nilfs_btree_propagate_v(struct nilfs_bmap *btree,
				   struct nilfs_btree_path *path,
				   int level, struct buffer_head *bh)
{
	int maxlevel = 0, ret;
	struct nilfs_btree_node *parent;
	struct inode *dat = nilfs_bmap_get_dat(btree);
	__u64 ptr;
	int ncmax;

	get_bh(bh);
	path[level].bp_bh = bh;
	ret = nilfs_btree_prepare_propagate_v(btree, path, level, &maxlevel,
					      dat);
	if (ret < 0)
		goto out;

	if (buffer_nilfs_volatile(path[level].bp_bh)) {
		parent = nilfs_btree_get_node(btree, path, level + 1, &ncmax);
		ptr = nilfs_btree_node_get_ptr(parent,
					       path[level + 1].bp_index,
					       ncmax);
		ret = nilfs_dat_mark_dirty(dat, ptr);
		if (ret < 0)
			goto out;
	}

	nilfs_btree_commit_propagate_v(btree, path, level, maxlevel, bh, dat);

 out:
	brelse(path[level].bp_bh);
	path[level].bp_bh = NULL;
	return ret;
}

static int nilfs_btree_propagate(struct nilfs_bmap *btree,
				 struct buffer_head *bh)
{
	struct nilfs_btree_path *path;
	struct nilfs_btree_node *node;
	__u64 key;
	int level, ret;

	WARN_ON(!buffer_dirty(bh));

	path = nilfs_btree_alloc_path();
	if (path == NULL)
		return -ENOMEM;

	if (buffer_nilfs_node(bh)) {
		node = (struct nilfs_btree_node *)bh->b_data;
		key = nilfs_btree_node_get_key(node, 0);
		level = nilfs_btree_node_get_level(node);
	} else {
		key = nilfs_bmap_data_get_key(btree, bh);
		level = NILFS_BTREE_LEVEL_DATA;
	}

	ret = nilfs_btree_do_lookup(btree, path, key, NULL, level + 1, 0);
	if (ret < 0) {
		if (unlikely(ret == -ENOENT))
			nilfs_crit(btree->b_inode->i_sb,
				   "writing node/leaf block does not appear in b-tree (ino=%lu) at key=%llu, level=%d",
				   btree->b_inode->i_ino,
				   (unsigned long long)key, level);
		goto out;
	}

	ret = NILFS_BMAP_USE_VBN(btree) ?
		nilfs_btree_propagate_v(btree, path, level, bh) :
		nilfs_btree_propagate_p(btree, path, level, bh);

 out:
	nilfs_btree_free_path(path);

	return ret;
}

static int nilfs_btree_propagate_gc(struct nilfs_bmap *btree,
				    struct buffer_head *bh)
{
	return nilfs_dat_mark_dirty(nilfs_bmap_get_dat(btree), bh->b_blocknr);
}

static void nilfs_btree_add_dirty_buffer(struct nilfs_bmap *btree,
					 struct list_head *lists,
					 struct buffer_head *bh)
{
	struct list_head *head;
	struct buffer_head *cbh;
	struct nilfs_btree_node *node, *cnode;
	__u64 key, ckey;
	int level;

	get_bh(bh);
	node = (struct nilfs_btree_node *)bh->b_data;
	key = nilfs_btree_node_get_key(node, 0);
	level = nilfs_btree_node_get_level(node);
	if (level < NILFS_BTREE_LEVEL_NODE_MIN ||
	    level >= NILFS_BTREE_LEVEL_MAX) {
		dump_stack();
		nilfs_warn(btree->b_inode->i_sb,
			   "invalid btree level: %d (key=%llu, ino=%lu, blocknr=%llu)",
			   level, (unsigned long long)key,
			   btree->b_inode->i_ino,
			   (unsigned long long)bh->b_blocknr);
		return;
	}

	list_for_each(head, &lists[level]) {
		cbh = list_entry(head, struct buffer_head, b_assoc_buffers);
		cnode = (struct nilfs_btree_node *)cbh->b_data;
		ckey = nilfs_btree_node_get_key(cnode, 0);
		if (key < ckey)
			break;
	}
	list_add_tail(&bh->b_assoc_buffers, head);
}

static void nilfs_btree_lookup_dirty_buffers(struct nilfs_bmap *btree,
					     struct list_head *listp)
{
	struct inode *btnc_inode = NILFS_BMAP_I(btree)->i_assoc_inode;
	struct address_space *btcache = btnc_inode->i_mapping;
	struct list_head lists[NILFS_BTREE_LEVEL_MAX];
	struct pagevec pvec;
	struct buffer_head *bh, *head;
	pgoff_t index = 0;
	int level, i;

	for (level = NILFS_BTREE_LEVEL_NODE_MIN;
	     level < NILFS_BTREE_LEVEL_MAX;
	     level++)
		INIT_LIST_HEAD(&lists[level]);

	pagevec_init(&pvec);

	while (pagevec_lookup_tag(&pvec, btcache, &index,
					PAGECACHE_TAG_DIRTY)) {
		for (i = 0; i < pagevec_count(&pvec); i++) {
			bh = head = page_buffers(pvec.pages[i]);
			do {
				if (buffer_dirty(bh))
					nilfs_btree_add_dirty_buffer(btree,
								     lists, bh);
			} while ((bh = bh->b_this_page) != head);
		}
		pagevec_release(&pvec);
		cond_resched();
	}

	for (level = NILFS_BTREE_LEVEL_NODE_MIN;
	     level < NILFS_BTREE_LEVEL_MAX;
	     level++)
		list_splice_tail(&lists[level], listp);
}

static int nilfs_btree_assign_p(struct nilfs_bmap *btree,
				struct nilfs_btree_path *path,
				int level,
				struct buffer_head **bh,
				sector_t blocknr,
				union nilfs_binfo *binfo)
{
	struct nilfs_btree_node *parent;
	__u64 key;
	__u64 ptr;
	int ncmax, ret;

	parent = nilfs_btree_get_node(btree, path, level + 1, &ncmax);
	ptr = nilfs_btree_node_get_ptr(parent, path[level + 1].bp_index,
				       ncmax);
	if (buffer_nilfs_node(*bh)) {
		path[level].bp_ctxt.oldkey = ptr;
		path[level].bp_ctxt.newkey = blocknr;
		path[level].bp_ctxt.bh = *bh;
		ret = nilfs_btnode_prepare_change_key(
			NILFS_BMAP_I(btree)->i_assoc_inode->i_mapping,
			&path[level].bp_ctxt);
		if (ret < 0)
			return ret;
		nilfs_btnode_commit_change_key(
			NILFS_BMAP_I(btree)->i_assoc_inode->i_mapping,
			&path[level].bp_ctxt);
		*bh = path[level].bp_ctxt.bh;
	}

	nilfs_btree_node_set_ptr(parent, path[level + 1].bp_index, blocknr,
				 ncmax);

	key = nilfs_btree_node_get_key(parent, path[level + 1].bp_index);
	/* on-disk format */
	binfo->bi_dat.bi_blkoff = cpu_to_le64(key);
	binfo->bi_dat.bi_level = level;

	return 0;
}

static int nilfs_btree_assign_v(struct nilfs_bmap *btree,
				struct nilfs_btree_path *path,
				int level,
				struct buffer_head **bh,
				sector_t blocknr,
				union nilfs_binfo *binfo)
{
	struct nilfs_btree_node *parent;
	struct inode *dat = nilfs_bmap_get_dat(btree);
	__u64 key;
	__u64 ptr;
	union nilfs_bmap_ptr_req req;
	int ncmax, ret;

	parent = nilfs_btree_get_node(btree, path, level + 1, &ncmax);
	ptr = nilfs_btree_node_get_ptr(parent, path[level + 1].bp_index,
				       ncmax);
	req.bpr_ptr = ptr;
	ret = nilfs_dat_prepare_start(dat, &req.bpr_req);
	if (ret < 0)
		return ret;
	nilfs_dat_commit_start(dat, &req.bpr_req, blocknr);

	key = nilfs_btree_node_get_key(parent, path[level + 1].bp_index);
	/* on-disk format */
	binfo->bi_v.bi_vblocknr = cpu_to_le64(ptr);
	binfo->bi_v.bi_blkoff = cpu_to_le64(key);

	return 0;
}

static int nilfs_btree_assign(struct nilfs_bmap *btree,
			      struct buffer_head **bh,
			      sector_t blocknr,
			      union nilfs_binfo *binfo)
{
	struct nilfs_btree_path *path;
	struct nilfs_btree_node *node;
	__u64 key;
	int level, ret;

	path = nilfs_btree_alloc_path();
	if (path == NULL)
		return -ENOMEM;

	if (buffer_nilfs_node(*bh)) {
		node = (struct nilfs_btree_node *)(*bh)->b_data;
		key = nilfs_btree_node_get_key(node, 0);
		level = nilfs_btree_node_get_level(node);
	} else {
		key = nilfs_bmap_data_get_key(btree, *bh);
		level = NILFS_BTREE_LEVEL_DATA;
	}

	ret = nilfs_btree_do_lookup(btree, path, key, NULL, level + 1, 0);
	if (ret < 0) {
		WARN_ON(ret == -ENOENT);
		goto out;
	}

	ret = NILFS_BMAP_USE_VBN(btree) ?
		nilfs_btree_assign_v(btree, path, level, bh, blocknr, binfo) :
		nilfs_btree_assign_p(btree, path, level, bh, blocknr, binfo);

 out:
	nilfs_btree_free_path(path);

	return ret;
}

static int nilfs_btree_assign_gc(struct nilfs_bmap *btree,
				 struct buffer_head **bh,
				 sector_t blocknr,
				 union nilfs_binfo *binfo)
{
	struct nilfs_btree_node *node;
	__u64 key;
	int ret;

	ret = nilfs_dat_move(nilfs_bmap_get_dat(btree), (*bh)->b_blocknr,
			     blocknr);
	if (ret < 0)
		return ret;

	if (buffer_nilfs_node(*bh)) {
		node = (struct nilfs_btree_node *)(*bh)->b_data;
		key = nilfs_btree_node_get_key(node, 0);
	} else
		key = nilfs_bmap_data_get_key(btree, *bh);

	/* on-disk format */
	binfo->bi_v.bi_vblocknr = cpu_to_le64((*bh)->b_blocknr);
	binfo->bi_v.bi_blkoff = cpu_to_le64(key);

	return 0;
}

static int nilfs_btree_mark(struct nilfs_bmap *btree, __u64 key, int level)
{
	struct buffer_head *bh;
	struct nilfs_btree_path *path;
	__u64 ptr;
	int ret;

	path = nilfs_btree_alloc_path();
	if (path == NULL)
		return -ENOMEM;

	ret = nilfs_btree_do_lookup(btree, path, key, &ptr, level + 1, 0);
	if (ret < 0) {
		WARN_ON(ret == -ENOENT);
		goto out;
	}
	ret = nilfs_btree_get_block(btree, ptr, &bh);
	if (ret < 0) {
		WARN_ON(ret == -ENOENT);
		goto out;
	}

	if (!buffer_dirty(bh))
		mark_buffer_dirty(bh);
	brelse(bh);
	if (!nilfs_bmap_dirty(btree))
		nilfs_bmap_set_dirty(btree);

 out:
	nilfs_btree_free_path(path);
	return ret;
}

static const struct nilfs_bmap_operations nilfs_btree_ops = {
	.bop_lookup		=	nilfs_btree_lookup,
	.bop_lookup_contig	=	nilfs_btree_lookup_contig,
	.bop_insert		=	nilfs_btree_insert,
	.bop_delete		=	nilfs_btree_delete,
	.bop_clear		=	NULL,

	.bop_propagate		=	nilfs_btree_propagate,

	.bop_lookup_dirty_buffers =	nilfs_btree_lookup_dirty_buffers,

	.bop_assign		=	nilfs_btree_assign,
	.bop_mark		=	nilfs_btree_mark,

	.bop_seek_key		=	nilfs_btree_seek_key,
	.bop_last_key		=	nilfs_btree_last_key,

	.bop_check_insert	=	NULL,
	.bop_check_delete	=	nilfs_btree_check_delete,
	.bop_gather_data	=	nilfs_btree_gather_data,
};

static const struct nilfs_bmap_operations nilfs_btree_ops_gc = {
	.bop_lookup		=	NULL,
	.bop_lookup_contig	=	NULL,
	.bop_insert		=	NULL,
	.bop_delete		=	NULL,
	.bop_clear		=	NULL,

	.bop_propagate		=	nilfs_btree_propagate_gc,

	.bop_lookup_dirty_buffers =	nilfs_btree_lookup_dirty_buffers,

	.bop_assign		=	nilfs_btree_assign_gc,
	.bop_mark		=	NULL,

	.bop_seek_key		=	NULL,
	.bop_last_key		=	NULL,

	.bop_check_insert	=	NULL,
	.bop_check_delete	=	NULL,
	.bop_gather_data	=	NULL,
};

static void __nilfs_btree_init(struct nilfs_bmap *bmap)
{
	bmap->b_ops = &nilfs_btree_ops;
	bmap->b_nchildren_per_block =
		NILFS_BTREE_NODE_NCHILDREN_MAX(nilfs_btree_node_size(bmap));
}

int nilfs_btree_init(struct nilfs_bmap *bmap)
{
	int ret = 0;

	__nilfs_btree_init(bmap);

	if (nilfs_btree_root_broken(nilfs_btree_get_root(bmap), bmap->b_inode))
		ret = -EIO;
	else
		ret = nilfs_attach_btree_node_cache(
			&NILFS_BMAP_I(bmap)->vfs_inode);

	return ret;
}

void nilfs_btree_init_gc(struct nilfs_bmap *bmap)
{
	bmap->b_ops = &nilfs_btree_ops_gc;
	bmap->b_nchildren_per_block =
		NILFS_BTREE_NODE_NCHILDREN_MAX(nilfs_btree_node_size(bmap));
}
