/*
 * btree.c - NILFS B-tree.
 *
 * Copyright (C) 2005-2008 Nippon Telegraph and Telephone Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Written by Koji Sato <koji@osrg.net>.
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

/**
 * struct nilfs_btree_path - A path on which B-tree operations are executed
 * @bp_bh: buffer head of node block
 * @bp_sib_bh: buffer head of sibling node block
 * @bp_index: index of child node
 * @bp_oldreq: ptr end request for old ptr
 * @bp_newreq: ptr alloc request for new ptr
 * @bp_op: rebalance operation
 */
struct nilfs_btree_path {
	struct buffer_head *bp_bh;
	struct buffer_head *bp_sib_bh;
	int bp_index;
	union nilfs_bmap_ptr_req bp_oldreq;
	union nilfs_bmap_ptr_req bp_newreq;
	struct nilfs_btnode_chkey_ctxt bp_ctxt;
	void (*bp_op)(struct nilfs_btree *, struct nilfs_btree_path *,
		      int, __u64 *, __u64 *);
};

/*
 * B-tree path operations
 */

static struct kmem_cache *nilfs_btree_path_cache;

int __init nilfs_btree_path_cache_init(void)
{
	nilfs_btree_path_cache =
		kmem_cache_create("nilfs2_btree_path_cache",
				  sizeof(struct nilfs_btree_path) *
				  NILFS_BTREE_LEVEL_MAX, 0, 0, NULL);
	return (nilfs_btree_path_cache != NULL) ? 0 : -ENOMEM;
}

void nilfs_btree_path_cache_destroy(void)
{
	kmem_cache_destroy(nilfs_btree_path_cache);
}

static inline struct nilfs_btree_path *
nilfs_btree_alloc_path(const struct nilfs_btree *btree)
{
	return (struct nilfs_btree_path *)
		kmem_cache_alloc(nilfs_btree_path_cache, GFP_NOFS);
}

static inline void nilfs_btree_free_path(const struct nilfs_btree *btree,
					 struct nilfs_btree_path *path)
{
	kmem_cache_free(nilfs_btree_path_cache, path);
}

static void nilfs_btree_init_path(const struct nilfs_btree *btree,
				  struct nilfs_btree_path *path)
{
	int level;

	for (level = NILFS_BTREE_LEVEL_DATA;
	     level < NILFS_BTREE_LEVEL_MAX;
	     level++) {
		path[level].bp_bh = NULL;
		path[level].bp_sib_bh = NULL;
		path[level].bp_index = 0;
		path[level].bp_oldreq.bpr_ptr = NILFS_BMAP_INVALID_PTR;
		path[level].bp_newreq.bpr_ptr = NILFS_BMAP_INVALID_PTR;
		path[level].bp_op = NULL;
	}
}

static void nilfs_btree_clear_path(const struct nilfs_btree *btree,
				   struct nilfs_btree_path *path)
{
	int level;

	for (level = NILFS_BTREE_LEVEL_DATA;
	     level < NILFS_BTREE_LEVEL_MAX;
	     level++) {
		if (path[level].bp_bh != NULL) {
			brelse(path[level].bp_bh);
			path[level].bp_bh = NULL;
		}
		/* sib_bh is released or deleted by prepare or commit
		 * operations. */
		path[level].bp_sib_bh = NULL;
		path[level].bp_index = 0;
		path[level].bp_oldreq.bpr_ptr = NILFS_BMAP_INVALID_PTR;
		path[level].bp_newreq.bpr_ptr = NILFS_BMAP_INVALID_PTR;
		path[level].bp_op = NULL;
	}
}

/*
 * B-tree node operations
 */
static int nilfs_btree_get_block(const struct nilfs_btree *btree, __u64 ptr,
				 struct buffer_head **bhp)
{
	struct address_space *btnc =
		&NILFS_BMAP_I((struct nilfs_bmap *)btree)->i_btnode_cache;
	return nilfs_btnode_get(btnc, ptr, 0, bhp, 0);
}

static int nilfs_btree_get_new_block(const struct nilfs_btree *btree,
				     __u64 ptr, struct buffer_head **bhp)
{
	struct address_space *btnc =
		&NILFS_BMAP_I((struct nilfs_bmap *)btree)->i_btnode_cache;
	int ret;

	ret = nilfs_btnode_get(btnc, ptr, 0, bhp, 1);
	if (!ret)
		set_buffer_nilfs_volatile(*bhp);
	return ret;
}

static inline int
nilfs_btree_node_get_flags(const struct nilfs_btree *btree,
			   const struct nilfs_btree_node *node)
{
	return node->bn_flags;
}

static inline void
nilfs_btree_node_set_flags(struct nilfs_btree *btree,
			   struct nilfs_btree_node *node,
			   int flags)
{
	node->bn_flags = flags;
}

static inline int nilfs_btree_node_root(const struct nilfs_btree *btree,
					const struct nilfs_btree_node *node)
{
	return nilfs_btree_node_get_flags(btree, node) & NILFS_BTREE_NODE_ROOT;
}

static inline int
nilfs_btree_node_get_level(const struct nilfs_btree *btree,
			   const struct nilfs_btree_node *node)
{
	return node->bn_level;
}

static inline void
nilfs_btree_node_set_level(struct nilfs_btree *btree,
			   struct nilfs_btree_node *node,
			   int level)
{
	node->bn_level = level;
}

static inline int
nilfs_btree_node_get_nchildren(const struct nilfs_btree *btree,
			       const struct nilfs_btree_node *node)
{
	return le16_to_cpu(node->bn_nchildren);
}

static inline void
nilfs_btree_node_set_nchildren(struct nilfs_btree *btree,
			       struct nilfs_btree_node *node,
			       int nchildren)
{
	node->bn_nchildren = cpu_to_le16(nchildren);
}

static inline int
nilfs_btree_node_size(const struct nilfs_btree *btree)
{
	return 1 << btree->bt_bmap.b_inode->i_blkbits;
}

static inline int
nilfs_btree_node_nchildren_min(const struct nilfs_btree *btree,
			       const struct nilfs_btree_node *node)
{
	return nilfs_btree_node_root(btree, node) ?
		NILFS_BTREE_ROOT_NCHILDREN_MIN :
		NILFS_BTREE_NODE_NCHILDREN_MIN(nilfs_btree_node_size(btree));
}

static inline int
nilfs_btree_node_nchildren_max(const struct nilfs_btree *btree,
			       const struct nilfs_btree_node *node)
{
	return nilfs_btree_node_root(btree, node) ?
		NILFS_BTREE_ROOT_NCHILDREN_MAX :
		NILFS_BTREE_NODE_NCHILDREN_MAX(nilfs_btree_node_size(btree));
}

static inline __le64 *
nilfs_btree_node_dkeys(const struct nilfs_btree *btree,
		       const struct nilfs_btree_node *node)
{
	return (__le64 *)((char *)(node + 1) +
			  (nilfs_btree_node_root(btree, node) ?
			   0 : NILFS_BTREE_NODE_EXTRA_PAD_SIZE));
}

static inline __le64 *
nilfs_btree_node_dptrs(const struct nilfs_btree *btree,
		       const struct nilfs_btree_node *node)
{
	return (__le64 *)(nilfs_btree_node_dkeys(btree, node) +
			  nilfs_btree_node_nchildren_max(btree, node));
}

static inline __u64
nilfs_btree_node_get_key(const struct nilfs_btree *btree,
			 const struct nilfs_btree_node *node, int index)
{
	return nilfs_bmap_dkey_to_key(*(nilfs_btree_node_dkeys(btree, node) +
					index));
}

static inline void
nilfs_btree_node_set_key(struct nilfs_btree *btree,
			 struct nilfs_btree_node *node, int index, __u64 key)
{
	*(nilfs_btree_node_dkeys(btree, node) + index) =
		nilfs_bmap_key_to_dkey(key);
}

static inline __u64
nilfs_btree_node_get_ptr(const struct nilfs_btree *btree,
			 const struct nilfs_btree_node *node,
			 int index)
{
	return nilfs_bmap_dptr_to_ptr(*(nilfs_btree_node_dptrs(btree, node) +
					index));
}

static inline void
nilfs_btree_node_set_ptr(struct nilfs_btree *btree,
			 struct nilfs_btree_node *node,
			 int index,
			 __u64 ptr)
{
	*(nilfs_btree_node_dptrs(btree, node) + index) =
		nilfs_bmap_ptr_to_dptr(ptr);
}

static void nilfs_btree_node_init(struct nilfs_btree *btree,
				  struct nilfs_btree_node *node,
				  int flags, int level, int nchildren,
				  const __u64 *keys, const __u64 *ptrs)
{
	__le64 *dkeys;
	__le64 *dptrs;
	int i;

	nilfs_btree_node_set_flags(btree, node, flags);
	nilfs_btree_node_set_level(btree, node, level);
	nilfs_btree_node_set_nchildren(btree, node, nchildren);

	dkeys = nilfs_btree_node_dkeys(btree, node);
	dptrs = nilfs_btree_node_dptrs(btree, node);
	for (i = 0; i < nchildren; i++) {
		dkeys[i] = nilfs_bmap_key_to_dkey(keys[i]);
		dptrs[i] = nilfs_bmap_ptr_to_dptr(ptrs[i]);
	}
}

/* Assume the buffer heads corresponding to left and right are locked. */
static void nilfs_btree_node_move_left(struct nilfs_btree *btree,
				       struct nilfs_btree_node *left,
				       struct nilfs_btree_node *right,
				       int n)
{
	__le64 *ldkeys, *rdkeys;
	__le64 *ldptrs, *rdptrs;
	int lnchildren, rnchildren;

	ldkeys = nilfs_btree_node_dkeys(btree, left);
	ldptrs = nilfs_btree_node_dptrs(btree, left);
	lnchildren = nilfs_btree_node_get_nchildren(btree, left);

	rdkeys = nilfs_btree_node_dkeys(btree, right);
	rdptrs = nilfs_btree_node_dptrs(btree, right);
	rnchildren = nilfs_btree_node_get_nchildren(btree, right);

	memcpy(ldkeys + lnchildren, rdkeys, n * sizeof(*rdkeys));
	memcpy(ldptrs + lnchildren, rdptrs, n * sizeof(*rdptrs));
	memmove(rdkeys, rdkeys + n, (rnchildren - n) * sizeof(*rdkeys));
	memmove(rdptrs, rdptrs + n, (rnchildren - n) * sizeof(*rdptrs));

	lnchildren += n;
	rnchildren -= n;
	nilfs_btree_node_set_nchildren(btree, left, lnchildren);
	nilfs_btree_node_set_nchildren(btree, right, rnchildren);
}

/* Assume that the buffer heads corresponding to left and right are locked. */
static void nilfs_btree_node_move_right(struct nilfs_btree *btree,
					struct nilfs_btree_node *left,
					struct nilfs_btree_node *right,
					int n)
{
	__le64 *ldkeys, *rdkeys;
	__le64 *ldptrs, *rdptrs;
	int lnchildren, rnchildren;

	ldkeys = nilfs_btree_node_dkeys(btree, left);
	ldptrs = nilfs_btree_node_dptrs(btree, left);
	lnchildren = nilfs_btree_node_get_nchildren(btree, left);

	rdkeys = nilfs_btree_node_dkeys(btree, right);
	rdptrs = nilfs_btree_node_dptrs(btree, right);
	rnchildren = nilfs_btree_node_get_nchildren(btree, right);

	memmove(rdkeys + n, rdkeys, rnchildren * sizeof(*rdkeys));
	memmove(rdptrs + n, rdptrs, rnchildren * sizeof(*rdptrs));
	memcpy(rdkeys, ldkeys + lnchildren - n, n * sizeof(*rdkeys));
	memcpy(rdptrs, ldptrs + lnchildren - n, n * sizeof(*rdptrs));

	lnchildren -= n;
	rnchildren += n;
	nilfs_btree_node_set_nchildren(btree, left, lnchildren);
	nilfs_btree_node_set_nchildren(btree, right, rnchildren);
}

/* Assume that the buffer head corresponding to node is locked. */
static void nilfs_btree_node_insert(struct nilfs_btree *btree,
				    struct nilfs_btree_node *node,
				    __u64 key, __u64 ptr, int index)
{
	__le64 *dkeys;
	__le64 *dptrs;
	int nchildren;

	dkeys = nilfs_btree_node_dkeys(btree, node);
	dptrs = nilfs_btree_node_dptrs(btree, node);
	nchildren = nilfs_btree_node_get_nchildren(btree, node);
	if (index < nchildren) {
		memmove(dkeys + index + 1, dkeys + index,
			(nchildren - index) * sizeof(*dkeys));
		memmove(dptrs + index + 1, dptrs + index,
			(nchildren - index) * sizeof(*dptrs));
	}
	dkeys[index] = nilfs_bmap_key_to_dkey(key);
	dptrs[index] = nilfs_bmap_ptr_to_dptr(ptr);
	nchildren++;
	nilfs_btree_node_set_nchildren(btree, node, nchildren);
}

/* Assume that the buffer head corresponding to node is locked. */
static void nilfs_btree_node_delete(struct nilfs_btree *btree,
				    struct nilfs_btree_node *node,
				    __u64 *keyp, __u64 *ptrp, int index)
{
	__u64 key;
	__u64 ptr;
	__le64 *dkeys;
	__le64 *dptrs;
	int nchildren;

	dkeys = nilfs_btree_node_dkeys(btree, node);
	dptrs = nilfs_btree_node_dptrs(btree, node);
	key = nilfs_bmap_dkey_to_key(dkeys[index]);
	ptr = nilfs_bmap_dptr_to_ptr(dptrs[index]);
	nchildren = nilfs_btree_node_get_nchildren(btree, node);
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
	nilfs_btree_node_set_nchildren(btree, node, nchildren);
}

static int nilfs_btree_node_lookup(const struct nilfs_btree *btree,
				   const struct nilfs_btree_node *node,
				   __u64 key, int *indexp)
{
	__u64 nkey;
	int index, low, high, s;

	/* binary search */
	low = 0;
	high = nilfs_btree_node_get_nchildren(btree, node) - 1;
	index = 0;
	s = 0;
	while (low <= high) {
		index = (low + high) / 2;
		nkey = nilfs_btree_node_get_key(btree, node, index);
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
	if (nilfs_btree_node_get_level(btree, node) >
	    NILFS_BTREE_LEVEL_NODE_MIN) {
		if ((s > 0) && (index > 0))
			index--;
	} else if (s < 0)
		index++;

 out:
	*indexp = index;

	return s == 0;
}

static inline struct nilfs_btree_node *
nilfs_btree_get_root(const struct nilfs_btree *btree)
{
	return (struct nilfs_btree_node *)btree->bt_bmap.b_u.u_data;
}

static inline struct nilfs_btree_node *
nilfs_btree_get_nonroot_node(const struct nilfs_btree *btree,
			     const struct nilfs_btree_path *path,
			     int level)
{
	return (struct nilfs_btree_node *)path[level].bp_bh->b_data;
}

static inline struct nilfs_btree_node *
nilfs_btree_get_sib_node(const struct nilfs_btree *btree,
			 const struct nilfs_btree_path *path,
			 int level)
{
	return (struct nilfs_btree_node *)path[level].bp_sib_bh->b_data;
}

static inline int nilfs_btree_height(const struct nilfs_btree *btree)
{
	return nilfs_btree_node_get_level(btree, nilfs_btree_get_root(btree))
		+ 1;
}

static inline struct nilfs_btree_node *
nilfs_btree_get_node(const struct nilfs_btree *btree,
		     const struct nilfs_btree_path *path,
		     int level)
{
	return (level == nilfs_btree_height(btree) - 1) ?
		nilfs_btree_get_root(btree) :
		nilfs_btree_get_nonroot_node(btree, path, level);
}

static int nilfs_btree_do_lookup(const struct nilfs_btree *btree,
				 struct nilfs_btree_path *path,
				 __u64 key, __u64 *ptrp, int minlevel)
{
	struct nilfs_btree_node *node;
	__u64 ptr;
	int level, index, found, ret;

	node = nilfs_btree_get_root(btree);
	level = nilfs_btree_node_get_level(btree, node);
	if ((level < minlevel) ||
	    (nilfs_btree_node_get_nchildren(btree, node) <= 0))
		return -ENOENT;

	found = nilfs_btree_node_lookup(btree, node, key, &index);
	ptr = nilfs_btree_node_get_ptr(btree, node, index);
	path[level].bp_bh = NULL;
	path[level].bp_index = index;

	for (level--; level >= minlevel; level--) {
		ret = nilfs_btree_get_block(btree, ptr, &path[level].bp_bh);
		if (ret < 0)
			return ret;
		node = nilfs_btree_get_nonroot_node(btree, path, level);
		BUG_ON(level != nilfs_btree_node_get_level(btree, node));
		if (!found)
			found = nilfs_btree_node_lookup(btree, node, key,
							&index);
		else
			index = 0;
		if (index < nilfs_btree_node_nchildren_max(btree, node))
			ptr = nilfs_btree_node_get_ptr(btree, node, index);
		else {
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

static int nilfs_btree_do_lookup_last(const struct nilfs_btree *btree,
				      struct nilfs_btree_path *path,
				      __u64 *keyp, __u64 *ptrp)
{
	struct nilfs_btree_node *node;
	__u64 ptr;
	int index, level, ret;

	node = nilfs_btree_get_root(btree);
	index = nilfs_btree_node_get_nchildren(btree, node) - 1;
	if (index < 0)
		return -ENOENT;
	level = nilfs_btree_node_get_level(btree, node);
	ptr = nilfs_btree_node_get_ptr(btree, node, index);
	path[level].bp_bh = NULL;
	path[level].bp_index = index;

	for (level--; level > 0; level--) {
		ret = nilfs_btree_get_block(btree, ptr, &path[level].bp_bh);
		if (ret < 0)
			return ret;
		node = nilfs_btree_get_nonroot_node(btree, path, level);
		BUG_ON(level != nilfs_btree_node_get_level(btree, node));
		index = nilfs_btree_node_get_nchildren(btree, node) - 1;
		ptr = nilfs_btree_node_get_ptr(btree, node, index);
		path[level].bp_index = index;
	}

	if (keyp != NULL)
		*keyp = nilfs_btree_node_get_key(btree, node, index);
	if (ptrp != NULL)
		*ptrp = ptr;

	return 0;
}

static int nilfs_btree_lookup(const struct nilfs_bmap *bmap,
			      __u64 key, int level, __u64 *ptrp)
{
	struct nilfs_btree *btree;
	struct nilfs_btree_path *path;
	__u64 ptr;
	int ret;

	btree = (struct nilfs_btree *)bmap;
	path = nilfs_btree_alloc_path(btree);
	if (path == NULL)
		return -ENOMEM;
	nilfs_btree_init_path(btree, path);

	ret = nilfs_btree_do_lookup(btree, path, key, &ptr, level);

	if (ptrp != NULL)
		*ptrp = ptr;

	nilfs_btree_clear_path(btree, path);
	nilfs_btree_free_path(btree, path);

	return ret;
}

static int nilfs_btree_lookup_contig(const struct nilfs_bmap *bmap,
				     __u64 key, __u64 *ptrp, unsigned maxblocks)
{
	struct nilfs_btree *btree = (struct nilfs_btree *)bmap;
	struct nilfs_btree_path *path;
	struct nilfs_btree_node *node;
	struct inode *dat = NULL;
	__u64 ptr, ptr2;
	sector_t blocknr;
	int level = NILFS_BTREE_LEVEL_NODE_MIN;
	int ret, cnt, index, maxlevel;

	path = nilfs_btree_alloc_path(btree);
	if (path == NULL)
		return -ENOMEM;
	nilfs_btree_init_path(btree, path);
	ret = nilfs_btree_do_lookup(btree, path, key, &ptr, level);
	if (ret < 0)
		goto out;

	if (NILFS_BMAP_USE_VBN(bmap)) {
		dat = nilfs_bmap_get_dat(bmap);
		ret = nilfs_dat_translate(dat, ptr, &blocknr);
		if (ret < 0)
			goto out;
		ptr = blocknr;
	}
	cnt = 1;
	if (cnt == maxblocks)
		goto end;

	maxlevel = nilfs_btree_height(btree) - 1;
	node = nilfs_btree_get_node(btree, path, level);
	index = path[level].bp_index + 1;
	for (;;) {
		while (index < nilfs_btree_node_get_nchildren(btree, node)) {
			if (nilfs_btree_node_get_key(btree, node, index) !=
			    key + cnt)
				goto end;
			ptr2 = nilfs_btree_node_get_ptr(btree, node, index);
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
		node = nilfs_btree_get_node(btree, path, level + 1);
		index = path[level + 1].bp_index + 1;
		if (index >= nilfs_btree_node_get_nchildren(btree, node) ||
		    nilfs_btree_node_get_key(btree, node, index) != key + cnt)
			break;
		ptr2 = nilfs_btree_node_get_ptr(btree, node, index);
		path[level + 1].bp_index = index;

		brelse(path[level].bp_bh);
		path[level].bp_bh = NULL;
		ret = nilfs_btree_get_block(btree, ptr2, &path[level].bp_bh);
		if (ret < 0)
			goto out;
		node = nilfs_btree_get_nonroot_node(btree, path, level);
		index = 0;
		path[level].bp_index = index;
	}
 end:
	*ptrp = ptr;
	ret = cnt;
 out:
	nilfs_btree_clear_path(btree, path);
	nilfs_btree_free_path(btree, path);
	return ret;
}

static void nilfs_btree_promote_key(struct nilfs_btree *btree,
				    struct nilfs_btree_path *path,
				    int level, __u64 key)
{
	if (level < nilfs_btree_height(btree) - 1) {
		do {
			lock_buffer(path[level].bp_bh);
			nilfs_btree_node_set_key(
				btree,
				nilfs_btree_get_nonroot_node(
					btree, path, level),
				path[level].bp_index, key);
			if (!buffer_dirty(path[level].bp_bh))
				nilfs_btnode_mark_dirty(path[level].bp_bh);
			unlock_buffer(path[level].bp_bh);
		} while ((path[level].bp_index == 0) &&
			 (++level < nilfs_btree_height(btree) - 1));
	}

	/* root */
	if (level == nilfs_btree_height(btree) - 1) {
		nilfs_btree_node_set_key(btree,
					 nilfs_btree_get_root(btree),
					 path[level].bp_index, key);
	}
}

static void nilfs_btree_do_insert(struct nilfs_btree *btree,
				  struct nilfs_btree_path *path,
				  int level, __u64 *keyp, __u64 *ptrp)
{
	struct nilfs_btree_node *node;

	if (level < nilfs_btree_height(btree) - 1) {
		lock_buffer(path[level].bp_bh);
		node = nilfs_btree_get_nonroot_node(btree, path, level);
		nilfs_btree_node_insert(btree, node, *keyp, *ptrp,
					path[level].bp_index);
		if (!buffer_dirty(path[level].bp_bh))
			nilfs_btnode_mark_dirty(path[level].bp_bh);
		unlock_buffer(path[level].bp_bh);

		if (path[level].bp_index == 0)
			nilfs_btree_promote_key(btree, path, level + 1,
						nilfs_btree_node_get_key(
							btree, node, 0));
	} else {
		node = nilfs_btree_get_root(btree);
		nilfs_btree_node_insert(btree, node, *keyp, *ptrp,
					path[level].bp_index);
	}
}

static void nilfs_btree_carry_left(struct nilfs_btree *btree,
				   struct nilfs_btree_path *path,
				   int level, __u64 *keyp, __u64 *ptrp)
{
	struct nilfs_btree_node *node, *left;
	int nchildren, lnchildren, n, move;

	lock_buffer(path[level].bp_bh);
	lock_buffer(path[level].bp_sib_bh);

	node = nilfs_btree_get_nonroot_node(btree, path, level);
	left = nilfs_btree_get_sib_node(btree, path, level);
	nchildren = nilfs_btree_node_get_nchildren(btree, node);
	lnchildren = nilfs_btree_node_get_nchildren(btree, left);
	move = 0;

	n = (nchildren + lnchildren + 1) / 2 - lnchildren;
	if (n > path[level].bp_index) {
		/* move insert point */
		n--;
		move = 1;
	}

	nilfs_btree_node_move_left(btree, left, node, n);

	if (!buffer_dirty(path[level].bp_bh))
		nilfs_btnode_mark_dirty(path[level].bp_bh);
	if (!buffer_dirty(path[level].bp_sib_bh))
		nilfs_btnode_mark_dirty(path[level].bp_sib_bh);

	unlock_buffer(path[level].bp_bh);
	unlock_buffer(path[level].bp_sib_bh);

	nilfs_btree_promote_key(btree, path, level + 1,
				nilfs_btree_node_get_key(btree, node, 0));

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

static void nilfs_btree_carry_right(struct nilfs_btree *btree,
				    struct nilfs_btree_path *path,
				    int level, __u64 *keyp, __u64 *ptrp)
{
	struct nilfs_btree_node *node, *right;
	int nchildren, rnchildren, n, move;

	lock_buffer(path[level].bp_bh);
	lock_buffer(path[level].bp_sib_bh);

	node = nilfs_btree_get_nonroot_node(btree, path, level);
	right = nilfs_btree_get_sib_node(btree, path, level);
	nchildren = nilfs_btree_node_get_nchildren(btree, node);
	rnchildren = nilfs_btree_node_get_nchildren(btree, right);
	move = 0;

	n = (nchildren + rnchildren + 1) / 2 - rnchildren;
	if (n > nchildren - path[level].bp_index) {
		/* move insert point */
		n--;
		move = 1;
	}

	nilfs_btree_node_move_right(btree, node, right, n);

	if (!buffer_dirty(path[level].bp_bh))
		nilfs_btnode_mark_dirty(path[level].bp_bh);
	if (!buffer_dirty(path[level].bp_sib_bh))
		nilfs_btnode_mark_dirty(path[level].bp_sib_bh);

	unlock_buffer(path[level].bp_bh);
	unlock_buffer(path[level].bp_sib_bh);

	path[level + 1].bp_index++;
	nilfs_btree_promote_key(btree, path, level + 1,
				nilfs_btree_node_get_key(btree, right, 0));
	path[level + 1].bp_index--;

	if (move) {
		brelse(path[level].bp_bh);
		path[level].bp_bh = path[level].bp_sib_bh;
		path[level].bp_sib_bh = NULL;
		path[level].bp_index -=
			nilfs_btree_node_get_nchildren(btree, node);
		path[level + 1].bp_index++;
	} else {
		brelse(path[level].bp_sib_bh);
		path[level].bp_sib_bh = NULL;
	}

	nilfs_btree_do_insert(btree, path, level, keyp, ptrp);
}

static void nilfs_btree_split(struct nilfs_btree *btree,
			      struct nilfs_btree_path *path,
			      int level, __u64 *keyp, __u64 *ptrp)
{
	struct nilfs_btree_node *node, *right;
	__u64 newkey;
	__u64 newptr;
	int nchildren, n, move;

	lock_buffer(path[level].bp_bh);
	lock_buffer(path[level].bp_sib_bh);

	node = nilfs_btree_get_nonroot_node(btree, path, level);
	right = nilfs_btree_get_sib_node(btree, path, level);
	nchildren = nilfs_btree_node_get_nchildren(btree, node);
	move = 0;

	n = (nchildren + 1) / 2;
	if (n > nchildren - path[level].bp_index) {
		n--;
		move = 1;
	}

	nilfs_btree_node_move_right(btree, node, right, n);

	if (!buffer_dirty(path[level].bp_bh))
		nilfs_btnode_mark_dirty(path[level].bp_bh);
	if (!buffer_dirty(path[level].bp_sib_bh))
		nilfs_btnode_mark_dirty(path[level].bp_sib_bh);

	unlock_buffer(path[level].bp_bh);
	unlock_buffer(path[level].bp_sib_bh);

	newkey = nilfs_btree_node_get_key(btree, right, 0);
	newptr = path[level].bp_newreq.bpr_ptr;

	if (move) {
		path[level].bp_index -=
			nilfs_btree_node_get_nchildren(btree, node);
		nilfs_btree_node_insert(btree, right, *keyp, *ptrp,
					path[level].bp_index);

		*keyp = nilfs_btree_node_get_key(btree, right, 0);
		*ptrp = path[level].bp_newreq.bpr_ptr;

		brelse(path[level].bp_bh);
		path[level].bp_bh = path[level].bp_sib_bh;
		path[level].bp_sib_bh = NULL;
	} else {
		nilfs_btree_do_insert(btree, path, level, keyp, ptrp);

		*keyp = nilfs_btree_node_get_key(btree, right, 0);
		*ptrp = path[level].bp_newreq.bpr_ptr;

		brelse(path[level].bp_sib_bh);
		path[level].bp_sib_bh = NULL;
	}

	path[level + 1].bp_index++;
}

static void nilfs_btree_grow(struct nilfs_btree *btree,
			     struct nilfs_btree_path *path,
			     int level, __u64 *keyp, __u64 *ptrp)
{
	struct nilfs_btree_node *root, *child;
	int n;

	lock_buffer(path[level].bp_sib_bh);

	root = nilfs_btree_get_root(btree);
	child = nilfs_btree_get_sib_node(btree, path, level);

	n = nilfs_btree_node_get_nchildren(btree, root);

	nilfs_btree_node_move_right(btree, root, child, n);
	nilfs_btree_node_set_level(btree, root, level + 1);

	if (!buffer_dirty(path[level].bp_sib_bh))
		nilfs_btnode_mark_dirty(path[level].bp_sib_bh);

	unlock_buffer(path[level].bp_sib_bh);

	path[level].bp_bh = path[level].bp_sib_bh;
	path[level].bp_sib_bh = NULL;

	nilfs_btree_do_insert(btree, path, level, keyp, ptrp);

	*keyp = nilfs_btree_node_get_key(btree, child, 0);
	*ptrp = path[level].bp_newreq.bpr_ptr;
}

static __u64 nilfs_btree_find_near(const struct nilfs_btree *btree,
				   const struct nilfs_btree_path *path)
{
	struct nilfs_btree_node *node;
	int level;

	if (path == NULL)
		return NILFS_BMAP_INVALID_PTR;

	/* left sibling */
	level = NILFS_BTREE_LEVEL_NODE_MIN;
	if (path[level].bp_index > 0) {
		node = nilfs_btree_get_node(btree, path, level);
		return nilfs_btree_node_get_ptr(btree, node,
						path[level].bp_index - 1);
	}

	/* parent */
	level = NILFS_BTREE_LEVEL_NODE_MIN + 1;
	if (level <= nilfs_btree_height(btree) - 1) {
		node = nilfs_btree_get_node(btree, path, level);
		return nilfs_btree_node_get_ptr(btree, node,
						path[level].bp_index);
	}

	return NILFS_BMAP_INVALID_PTR;
}

static __u64 nilfs_btree_find_target_v(const struct nilfs_btree *btree,
				       const struct nilfs_btree_path *path,
				       __u64 key)
{
	__u64 ptr;

	ptr = nilfs_bmap_find_target_seq(&btree->bt_bmap, key);
	if (ptr != NILFS_BMAP_INVALID_PTR)
		/* sequential access */
		return ptr;
	else {
		ptr = nilfs_btree_find_near(btree, path);
		if (ptr != NILFS_BMAP_INVALID_PTR)
			/* near */
			return ptr;
	}
	/* block group */
	return nilfs_bmap_find_target_in_group(&btree->bt_bmap);
}

static void nilfs_btree_set_target_v(struct nilfs_btree *btree, __u64 key,
				     __u64 ptr)
{
	btree->bt_bmap.b_last_allocated_key = key;
	btree->bt_bmap.b_last_allocated_ptr = ptr;
}

static int nilfs_btree_prepare_insert(struct nilfs_btree *btree,
				      struct nilfs_btree_path *path,
				      int *levelp, __u64 key, __u64 ptr,
				      struct nilfs_bmap_stats *stats)
{
	struct buffer_head *bh;
	struct nilfs_btree_node *node, *parent, *sib;
	__u64 sibptr;
	int pindex, level, ret;

	stats->bs_nblocks = 0;
	level = NILFS_BTREE_LEVEL_DATA;

	/* allocate a new ptr for data block */
	if (NILFS_BMAP_USE_VBN(&btree->bt_bmap))
		path[level].bp_newreq.bpr_ptr =
			nilfs_btree_find_target_v(btree, path, key);

	ret = nilfs_bmap_prepare_alloc_ptr(&btree->bt_bmap,
					   &path[level].bp_newreq);
	if (ret < 0)
		goto err_out_data;

	for (level = NILFS_BTREE_LEVEL_NODE_MIN;
	     level < nilfs_btree_height(btree) - 1;
	     level++) {
		node = nilfs_btree_get_nonroot_node(btree, path, level);
		if (nilfs_btree_node_get_nchildren(btree, node) <
		    nilfs_btree_node_nchildren_max(btree, node)) {
			path[level].bp_op = nilfs_btree_do_insert;
			stats->bs_nblocks++;
			goto out;
		}

		parent = nilfs_btree_get_node(btree, path, level + 1);
		pindex = path[level + 1].bp_index;

		/* left sibling */
		if (pindex > 0) {
			sibptr = nilfs_btree_node_get_ptr(btree, parent,
							  pindex - 1);
			ret = nilfs_btree_get_block(btree, sibptr, &bh);
			if (ret < 0)
				goto err_out_child_node;
			sib = (struct nilfs_btree_node *)bh->b_data;
			if (nilfs_btree_node_get_nchildren(btree, sib) <
			    nilfs_btree_node_nchildren_max(btree, sib)) {
				path[level].bp_sib_bh = bh;
				path[level].bp_op = nilfs_btree_carry_left;
				stats->bs_nblocks++;
				goto out;
			} else
				brelse(bh);
		}

		/* right sibling */
		if (pindex <
		    nilfs_btree_node_get_nchildren(btree, parent) - 1) {
			sibptr = nilfs_btree_node_get_ptr(btree, parent,
							  pindex + 1);
			ret = nilfs_btree_get_block(btree, sibptr, &bh);
			if (ret < 0)
				goto err_out_child_node;
			sib = (struct nilfs_btree_node *)bh->b_data;
			if (nilfs_btree_node_get_nchildren(btree, sib) <
			    nilfs_btree_node_nchildren_max(btree, sib)) {
				path[level].bp_sib_bh = bh;
				path[level].bp_op = nilfs_btree_carry_right;
				stats->bs_nblocks++;
				goto out;
			} else
				brelse(bh);
		}

		/* split */
		path[level].bp_newreq.bpr_ptr =
			path[level - 1].bp_newreq.bpr_ptr + 1;
		ret = nilfs_bmap_prepare_alloc_ptr(&btree->bt_bmap,
						   &path[level].bp_newreq);
		if (ret < 0)
			goto err_out_child_node;
		ret = nilfs_btree_get_new_block(btree,
						path[level].bp_newreq.bpr_ptr,
						&bh);
		if (ret < 0)
			goto err_out_curr_node;

		stats->bs_nblocks++;

		lock_buffer(bh);
		nilfs_btree_node_init(btree,
				      (struct nilfs_btree_node *)bh->b_data,
				      0, level, 0, NULL, NULL);
		unlock_buffer(bh);
		path[level].bp_sib_bh = bh;
		path[level].bp_op = nilfs_btree_split;
	}

	/* root */
	node = nilfs_btree_get_root(btree);
	if (nilfs_btree_node_get_nchildren(btree, node) <
	    nilfs_btree_node_nchildren_max(btree, node)) {
		path[level].bp_op = nilfs_btree_do_insert;
		stats->bs_nblocks++;
		goto out;
	}

	/* grow */
	path[level].bp_newreq.bpr_ptr = path[level - 1].bp_newreq.bpr_ptr + 1;
	ret = nilfs_bmap_prepare_alloc_ptr(&btree->bt_bmap,
					   &path[level].bp_newreq);
	if (ret < 0)
		goto err_out_child_node;
	ret = nilfs_btree_get_new_block(btree, path[level].bp_newreq.bpr_ptr,
					&bh);
	if (ret < 0)
		goto err_out_curr_node;

	lock_buffer(bh);
	nilfs_btree_node_init(btree, (struct nilfs_btree_node *)bh->b_data,
			      0, level, 0, NULL, NULL);
	unlock_buffer(bh);
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
	nilfs_bmap_abort_alloc_ptr(&btree->bt_bmap, &path[level].bp_newreq);
 err_out_child_node:
	for (level--; level > NILFS_BTREE_LEVEL_DATA; level--) {
		nilfs_btnode_delete(path[level].bp_sib_bh);
		nilfs_bmap_abort_alloc_ptr(&btree->bt_bmap,
					   &path[level].bp_newreq);

	}

	nilfs_bmap_abort_alloc_ptr(&btree->bt_bmap, &path[level].bp_newreq);
 err_out_data:
	*levelp = level;
	stats->bs_nblocks = 0;
	return ret;
}

static void nilfs_btree_commit_insert(struct nilfs_btree *btree,
				      struct nilfs_btree_path *path,
				      int maxlevel, __u64 key, __u64 ptr)
{
	int level;

	set_buffer_nilfs_volatile((struct buffer_head *)((unsigned long)ptr));
	ptr = path[NILFS_BTREE_LEVEL_DATA].bp_newreq.bpr_ptr;
	if (NILFS_BMAP_USE_VBN(&btree->bt_bmap))
		nilfs_btree_set_target_v(btree, key, ptr);

	for (level = NILFS_BTREE_LEVEL_NODE_MIN; level <= maxlevel; level++) {
		nilfs_bmap_commit_alloc_ptr(&btree->bt_bmap,
					    &path[level - 1].bp_newreq);
		path[level].bp_op(btree, path, level, &key, &ptr);
	}

	if (!nilfs_bmap_dirty(&btree->bt_bmap))
		nilfs_bmap_set_dirty(&btree->bt_bmap);
}

static int nilfs_btree_insert(struct nilfs_bmap *bmap, __u64 key, __u64 ptr)
{
	struct nilfs_btree *btree;
	struct nilfs_btree_path *path;
	struct nilfs_bmap_stats stats;
	int level, ret;

	btree = (struct nilfs_btree *)bmap;
	path = nilfs_btree_alloc_path(btree);
	if (path == NULL)
		return -ENOMEM;
	nilfs_btree_init_path(btree, path);

	ret = nilfs_btree_do_lookup(btree, path, key, NULL,
				    NILFS_BTREE_LEVEL_NODE_MIN);
	if (ret != -ENOENT) {
		if (ret == 0)
			ret = -EEXIST;
		goto out;
	}

	ret = nilfs_btree_prepare_insert(btree, path, &level, key, ptr, &stats);
	if (ret < 0)
		goto out;
	nilfs_btree_commit_insert(btree, path, level, key, ptr);
	nilfs_bmap_add_blocks(bmap, stats.bs_nblocks);

 out:
	nilfs_btree_clear_path(btree, path);
	nilfs_btree_free_path(btree, path);
	return ret;
}

static void nilfs_btree_do_delete(struct nilfs_btree *btree,
				  struct nilfs_btree_path *path,
				  int level, __u64 *keyp, __u64 *ptrp)
{
	struct nilfs_btree_node *node;

	if (level < nilfs_btree_height(btree) - 1) {
		lock_buffer(path[level].bp_bh);
		node = nilfs_btree_get_nonroot_node(btree, path, level);
		nilfs_btree_node_delete(btree, node, keyp, ptrp,
					path[level].bp_index);
		if (!buffer_dirty(path[level].bp_bh))
			nilfs_btnode_mark_dirty(path[level].bp_bh);
		unlock_buffer(path[level].bp_bh);
		if (path[level].bp_index == 0)
			nilfs_btree_promote_key(btree, path, level + 1,
				nilfs_btree_node_get_key(btree, node, 0));
	} else {
		node = nilfs_btree_get_root(btree);
		nilfs_btree_node_delete(btree, node, keyp, ptrp,
					path[level].bp_index);
	}
}

static void nilfs_btree_borrow_left(struct nilfs_btree *btree,
				    struct nilfs_btree_path *path,
				    int level, __u64 *keyp, __u64 *ptrp)
{
	struct nilfs_btree_node *node, *left;
	int nchildren, lnchildren, n;

	nilfs_btree_do_delete(btree, path, level, keyp, ptrp);

	lock_buffer(path[level].bp_bh);
	lock_buffer(path[level].bp_sib_bh);

	node = nilfs_btree_get_nonroot_node(btree, path, level);
	left = nilfs_btree_get_sib_node(btree, path, level);
	nchildren = nilfs_btree_node_get_nchildren(btree, node);
	lnchildren = nilfs_btree_node_get_nchildren(btree, left);

	n = (nchildren + lnchildren) / 2 - nchildren;

	nilfs_btree_node_move_right(btree, left, node, n);

	if (!buffer_dirty(path[level].bp_bh))
		nilfs_btnode_mark_dirty(path[level].bp_bh);
	if (!buffer_dirty(path[level].bp_sib_bh))
		nilfs_btnode_mark_dirty(path[level].bp_sib_bh);

	unlock_buffer(path[level].bp_bh);
	unlock_buffer(path[level].bp_sib_bh);

	nilfs_btree_promote_key(btree, path, level + 1,
				nilfs_btree_node_get_key(btree, node, 0));

	brelse(path[level].bp_sib_bh);
	path[level].bp_sib_bh = NULL;
	path[level].bp_index += n;
}

static void nilfs_btree_borrow_right(struct nilfs_btree *btree,
				     struct nilfs_btree_path *path,
				     int level, __u64 *keyp, __u64 *ptrp)
{
	struct nilfs_btree_node *node, *right;
	int nchildren, rnchildren, n;

	nilfs_btree_do_delete(btree, path, level, keyp, ptrp);

	lock_buffer(path[level].bp_bh);
	lock_buffer(path[level].bp_sib_bh);

	node = nilfs_btree_get_nonroot_node(btree, path, level);
	right = nilfs_btree_get_sib_node(btree, path, level);
	nchildren = nilfs_btree_node_get_nchildren(btree, node);
	rnchildren = nilfs_btree_node_get_nchildren(btree, right);

	n = (nchildren + rnchildren) / 2 - nchildren;

	nilfs_btree_node_move_left(btree, node, right, n);

	if (!buffer_dirty(path[level].bp_bh))
		nilfs_btnode_mark_dirty(path[level].bp_bh);
	if (!buffer_dirty(path[level].bp_sib_bh))
		nilfs_btnode_mark_dirty(path[level].bp_sib_bh);

	unlock_buffer(path[level].bp_bh);
	unlock_buffer(path[level].bp_sib_bh);

	path[level + 1].bp_index++;
	nilfs_btree_promote_key(btree, path, level + 1,
				nilfs_btree_node_get_key(btree, right, 0));
	path[level + 1].bp_index--;

	brelse(path[level].bp_sib_bh);
	path[level].bp_sib_bh = NULL;
}

static void nilfs_btree_concat_left(struct nilfs_btree *btree,
				    struct nilfs_btree_path *path,
				    int level, __u64 *keyp, __u64 *ptrp)
{
	struct nilfs_btree_node *node, *left;
	int n;

	nilfs_btree_do_delete(btree, path, level, keyp, ptrp);

	lock_buffer(path[level].bp_bh);
	lock_buffer(path[level].bp_sib_bh);

	node = nilfs_btree_get_nonroot_node(btree, path, level);
	left = nilfs_btree_get_sib_node(btree, path, level);

	n = nilfs_btree_node_get_nchildren(btree, node);

	nilfs_btree_node_move_left(btree, left, node, n);

	if (!buffer_dirty(path[level].bp_sib_bh))
		nilfs_btnode_mark_dirty(path[level].bp_sib_bh);

	unlock_buffer(path[level].bp_bh);
	unlock_buffer(path[level].bp_sib_bh);

	nilfs_btnode_delete(path[level].bp_bh);
	path[level].bp_bh = path[level].bp_sib_bh;
	path[level].bp_sib_bh = NULL;
	path[level].bp_index += nilfs_btree_node_get_nchildren(btree, left);
}

static void nilfs_btree_concat_right(struct nilfs_btree *btree,
				     struct nilfs_btree_path *path,
				     int level, __u64 *keyp, __u64 *ptrp)
{
	struct nilfs_btree_node *node, *right;
	int n;

	nilfs_btree_do_delete(btree, path, level, keyp, ptrp);

	lock_buffer(path[level].bp_bh);
	lock_buffer(path[level].bp_sib_bh);

	node = nilfs_btree_get_nonroot_node(btree, path, level);
	right = nilfs_btree_get_sib_node(btree, path, level);

	n = nilfs_btree_node_get_nchildren(btree, right);

	nilfs_btree_node_move_left(btree, node, right, n);

	if (!buffer_dirty(path[level].bp_bh))
		nilfs_btnode_mark_dirty(path[level].bp_bh);

	unlock_buffer(path[level].bp_bh);
	unlock_buffer(path[level].bp_sib_bh);

	nilfs_btnode_delete(path[level].bp_sib_bh);
	path[level].bp_sib_bh = NULL;
	path[level + 1].bp_index++;
}

static void nilfs_btree_shrink(struct nilfs_btree *btree,
			       struct nilfs_btree_path *path,
			       int level, __u64 *keyp, __u64 *ptrp)
{
	struct nilfs_btree_node *root, *child;
	int n;

	nilfs_btree_do_delete(btree, path, level, keyp, ptrp);

	lock_buffer(path[level].bp_bh);
	root = nilfs_btree_get_root(btree);
	child = nilfs_btree_get_nonroot_node(btree, path, level);

	nilfs_btree_node_delete(btree, root, NULL, NULL, 0);
	nilfs_btree_node_set_level(btree, root, level);
	n = nilfs_btree_node_get_nchildren(btree, child);
	nilfs_btree_node_move_left(btree, root, child, n);
	unlock_buffer(path[level].bp_bh);

	nilfs_btnode_delete(path[level].bp_bh);
	path[level].bp_bh = NULL;
}


static int nilfs_btree_prepare_delete(struct nilfs_btree *btree,
				      struct nilfs_btree_path *path,
				      int *levelp,
				      struct nilfs_bmap_stats *stats)
{
	struct buffer_head *bh;
	struct nilfs_btree_node *node, *parent, *sib;
	__u64 sibptr;
	int pindex, level, ret;

	ret = 0;
	stats->bs_nblocks = 0;
	for (level = NILFS_BTREE_LEVEL_NODE_MIN;
	     level < nilfs_btree_height(btree) - 1;
	     level++) {
		node = nilfs_btree_get_nonroot_node(btree, path, level);
		path[level].bp_oldreq.bpr_ptr =
			nilfs_btree_node_get_ptr(btree, node,
						 path[level].bp_index);
		ret = nilfs_bmap_prepare_end_ptr(&btree->bt_bmap,
						 &path[level].bp_oldreq);
		if (ret < 0)
			goto err_out_child_node;

		if (nilfs_btree_node_get_nchildren(btree, node) >
		    nilfs_btree_node_nchildren_min(btree, node)) {
			path[level].bp_op = nilfs_btree_do_delete;
			stats->bs_nblocks++;
			goto out;
		}

		parent = nilfs_btree_get_node(btree, path, level + 1);
		pindex = path[level + 1].bp_index;

		if (pindex > 0) {
			/* left sibling */
			sibptr = nilfs_btree_node_get_ptr(btree, parent,
							  pindex - 1);
			ret = nilfs_btree_get_block(btree, sibptr, &bh);
			if (ret < 0)
				goto err_out_curr_node;
			sib = (struct nilfs_btree_node *)bh->b_data;
			if (nilfs_btree_node_get_nchildren(btree, sib) >
			    nilfs_btree_node_nchildren_min(btree, sib)) {
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
			   nilfs_btree_node_get_nchildren(btree, parent) - 1) {
			/* right sibling */
			sibptr = nilfs_btree_node_get_ptr(btree, parent,
							  pindex + 1);
			ret = nilfs_btree_get_block(btree, sibptr, &bh);
			if (ret < 0)
				goto err_out_curr_node;
			sib = (struct nilfs_btree_node *)bh->b_data;
			if (nilfs_btree_node_get_nchildren(btree, sib) >
			    nilfs_btree_node_nchildren_min(btree, sib)) {
				path[level].bp_sib_bh = bh;
				path[level].bp_op = nilfs_btree_borrow_right;
				stats->bs_nblocks++;
				goto out;
			} else {
				path[level].bp_sib_bh = bh;
				path[level].bp_op = nilfs_btree_concat_right;
				stats->bs_nblocks++;
				/* continue; */
			}
		} else {
			/* no siblings */
			/* the only child of the root node */
			WARN_ON(level != nilfs_btree_height(btree) - 2);
			if (nilfs_btree_node_get_nchildren(btree, node) - 1 <=
			    NILFS_BTREE_ROOT_NCHILDREN_MAX) {
				path[level].bp_op = nilfs_btree_shrink;
				stats->bs_nblocks += 2;
			} else {
				path[level].bp_op = nilfs_btree_do_delete;
				stats->bs_nblocks++;
			}

			goto out;

		}
	}

	node = nilfs_btree_get_root(btree);
	path[level].bp_oldreq.bpr_ptr =
		nilfs_btree_node_get_ptr(btree, node, path[level].bp_index);

	ret = nilfs_bmap_prepare_end_ptr(&btree->bt_bmap,
					 &path[level].bp_oldreq);
	if (ret < 0)
		goto err_out_child_node;

	/* child of the root node is deleted */
	path[level].bp_op = nilfs_btree_do_delete;
	stats->bs_nblocks++;

	/* success */
 out:
	*levelp = level;
	return ret;

	/* error */
 err_out_curr_node:
	nilfs_bmap_abort_end_ptr(&btree->bt_bmap, &path[level].bp_oldreq);
 err_out_child_node:
	for (level--; level >= NILFS_BTREE_LEVEL_NODE_MIN; level--) {
		brelse(path[level].bp_sib_bh);
		nilfs_bmap_abort_end_ptr(&btree->bt_bmap,
					 &path[level].bp_oldreq);
	}
	*levelp = level;
	stats->bs_nblocks = 0;
	return ret;
}

static void nilfs_btree_commit_delete(struct nilfs_btree *btree,
				      struct nilfs_btree_path *path,
				      int maxlevel)
{
	int level;

	for (level = NILFS_BTREE_LEVEL_NODE_MIN; level <= maxlevel; level++) {
		nilfs_bmap_commit_end_ptr(&btree->bt_bmap,
					  &path[level].bp_oldreq);
		path[level].bp_op(btree, path, level, NULL, NULL);
	}

	if (!nilfs_bmap_dirty(&btree->bt_bmap))
		nilfs_bmap_set_dirty(&btree->bt_bmap);
}

static int nilfs_btree_delete(struct nilfs_bmap *bmap, __u64 key)

{
	struct nilfs_btree *btree;
	struct nilfs_btree_path *path;
	struct nilfs_bmap_stats stats;
	int level, ret;

	btree = (struct nilfs_btree *)bmap;
	path = nilfs_btree_alloc_path(btree);
	if (path == NULL)
		return -ENOMEM;
	nilfs_btree_init_path(btree, path);
	ret = nilfs_btree_do_lookup(btree, path, key, NULL,
				    NILFS_BTREE_LEVEL_NODE_MIN);
	if (ret < 0)
		goto out;

	ret = nilfs_btree_prepare_delete(btree, path, &level, &stats);
	if (ret < 0)
		goto out;
	nilfs_btree_commit_delete(btree, path, level);
	nilfs_bmap_sub_blocks(bmap, stats.bs_nblocks);

out:
	nilfs_btree_clear_path(btree, path);
	nilfs_btree_free_path(btree, path);
	return ret;
}

static int nilfs_btree_last_key(const struct nilfs_bmap *bmap, __u64 *keyp)
{
	struct nilfs_btree *btree;
	struct nilfs_btree_path *path;
	int ret;

	btree = (struct nilfs_btree *)bmap;
	path = nilfs_btree_alloc_path(btree);
	if (path == NULL)
		return -ENOMEM;
	nilfs_btree_init_path(btree, path);

	ret = nilfs_btree_do_lookup_last(btree, path, keyp, NULL);

	nilfs_btree_clear_path(btree, path);
	nilfs_btree_free_path(btree, path);

	return ret;
}

static int nilfs_btree_check_delete(struct nilfs_bmap *bmap, __u64 key)
{
	struct buffer_head *bh;
	struct nilfs_btree *btree;
	struct nilfs_btree_node *root, *node;
	__u64 maxkey, nextmaxkey;
	__u64 ptr;
	int nchildren, ret;

	btree = (struct nilfs_btree *)bmap;
	root = nilfs_btree_get_root(btree);
	switch (nilfs_btree_height(btree)) {
	case 2:
		bh = NULL;
		node = root;
		break;
	case 3:
		nchildren = nilfs_btree_node_get_nchildren(btree, root);
		if (nchildren > 1)
			return 0;
		ptr = nilfs_btree_node_get_ptr(btree, root, nchildren - 1);
		ret = nilfs_btree_get_block(btree, ptr, &bh);
		if (ret < 0)
			return ret;
		node = (struct nilfs_btree_node *)bh->b_data;
		break;
	default:
		return 0;
	}

	nchildren = nilfs_btree_node_get_nchildren(btree, node);
	maxkey = nilfs_btree_node_get_key(btree, node, nchildren - 1);
	nextmaxkey = (nchildren > 1) ?
		nilfs_btree_node_get_key(btree, node, nchildren - 2) : 0;
	if (bh != NULL)
		brelse(bh);

	return (maxkey == key) && (nextmaxkey < NILFS_BMAP_LARGE_LOW);
}

static int nilfs_btree_gather_data(struct nilfs_bmap *bmap,
				   __u64 *keys, __u64 *ptrs, int nitems)
{
	struct buffer_head *bh;
	struct nilfs_btree *btree;
	struct nilfs_btree_node *node, *root;
	__le64 *dkeys;
	__le64 *dptrs;
	__u64 ptr;
	int nchildren, i, ret;

	btree = (struct nilfs_btree *)bmap;
	root = nilfs_btree_get_root(btree);
	switch (nilfs_btree_height(btree)) {
	case 2:
		bh = NULL;
		node = root;
		break;
	case 3:
		nchildren = nilfs_btree_node_get_nchildren(btree, root);
		WARN_ON(nchildren > 1);
		ptr = nilfs_btree_node_get_ptr(btree, root, nchildren - 1);
		ret = nilfs_btree_get_block(btree, ptr, &bh);
		if (ret < 0)
			return ret;
		node = (struct nilfs_btree_node *)bh->b_data;
		break;
	default:
		node = NULL;
		return -EINVAL;
	}

	nchildren = nilfs_btree_node_get_nchildren(btree, node);
	if (nchildren < nitems)
		nitems = nchildren;
	dkeys = nilfs_btree_node_dkeys(btree, node);
	dptrs = nilfs_btree_node_dptrs(btree, node);
	for (i = 0; i < nitems; i++) {
		keys[i] = nilfs_bmap_dkey_to_key(dkeys[i]);
		ptrs[i] = nilfs_bmap_dptr_to_ptr(dptrs[i]);
	}

	if (bh != NULL)
		brelse(bh);

	return nitems;
}

static int
nilfs_btree_prepare_convert_and_insert(struct nilfs_bmap *bmap, __u64 key,
				       union nilfs_bmap_ptr_req *dreq,
				       union nilfs_bmap_ptr_req *nreq,
				       struct buffer_head **bhp,
				       struct nilfs_bmap_stats *stats)
{
	struct buffer_head *bh;
	struct nilfs_btree *btree;
	int ret;

	btree = (struct nilfs_btree *)bmap;
	stats->bs_nblocks = 0;

	/* for data */
	/* cannot find near ptr */
	if (NILFS_BMAP_USE_VBN(bmap))
		dreq->bpr_ptr = nilfs_btree_find_target_v(btree, NULL, key);

	ret = nilfs_bmap_prepare_alloc_ptr(bmap, dreq);
	if (ret < 0)
		return ret;

	*bhp = NULL;
	stats->bs_nblocks++;
	if (nreq != NULL) {
		nreq->bpr_ptr = dreq->bpr_ptr + 1;
		ret = nilfs_bmap_prepare_alloc_ptr(bmap, nreq);
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
	nilfs_bmap_abort_alloc_ptr(bmap, nreq);
 err_out_dreq:
	nilfs_bmap_abort_alloc_ptr(bmap, dreq);
	stats->bs_nblocks = 0;
	return ret;

}

static void
nilfs_btree_commit_convert_and_insert(struct nilfs_bmap *bmap,
				      __u64 key, __u64 ptr,
				      const __u64 *keys, const __u64 *ptrs,
				      int n,
				      union nilfs_bmap_ptr_req *dreq,
				      union nilfs_bmap_ptr_req *nreq,
				      struct buffer_head *bh)
{
	struct nilfs_btree *btree;
	struct nilfs_btree_node *node;
	__u64 tmpptr;

	/* free resources */
	if (bmap->b_ops->bop_clear != NULL)
		bmap->b_ops->bop_clear(bmap);

	/* ptr must be a pointer to a buffer head. */
	set_buffer_nilfs_volatile((struct buffer_head *)((unsigned long)ptr));

	/* convert and insert */
	btree = (struct nilfs_btree *)bmap;
	nilfs_btree_init(bmap);
	if (nreq != NULL) {
		nilfs_bmap_commit_alloc_ptr(bmap, dreq);
		nilfs_bmap_commit_alloc_ptr(bmap, nreq);

		/* create child node at level 1 */
		lock_buffer(bh);
		node = (struct nilfs_btree_node *)bh->b_data;
		nilfs_btree_node_init(btree, node, 0, 1, n, keys, ptrs);
		nilfs_btree_node_insert(btree, node,
					key, dreq->bpr_ptr, n);
		if (!buffer_dirty(bh))
			nilfs_btnode_mark_dirty(bh);
		if (!nilfs_bmap_dirty(bmap))
			nilfs_bmap_set_dirty(bmap);

		unlock_buffer(bh);
		brelse(bh);

		/* create root node at level 2 */
		node = nilfs_btree_get_root(btree);
		tmpptr = nreq->bpr_ptr;
		nilfs_btree_node_init(btree, node, NILFS_BTREE_NODE_ROOT,
				      2, 1, &keys[0], &tmpptr);
	} else {
		nilfs_bmap_commit_alloc_ptr(bmap, dreq);

		/* create root node at level 1 */
		node = nilfs_btree_get_root(btree);
		nilfs_btree_node_init(btree, node, NILFS_BTREE_NODE_ROOT,
				      1, n, keys, ptrs);
		nilfs_btree_node_insert(btree, node,
					key, dreq->bpr_ptr, n);
		if (!nilfs_bmap_dirty(bmap))
			nilfs_bmap_set_dirty(bmap);
	}

	if (NILFS_BMAP_USE_VBN(bmap))
		nilfs_btree_set_target_v(btree, key, dreq->bpr_ptr);
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
int nilfs_btree_convert_and_insert(struct nilfs_bmap *bmap,
				   __u64 key, __u64 ptr,
				   const __u64 *keys, const __u64 *ptrs, int n)
{
	struct buffer_head *bh;
	union nilfs_bmap_ptr_req dreq, nreq, *di, *ni;
	struct nilfs_bmap_stats stats;
	int ret;

	if (n + 1 <= NILFS_BTREE_ROOT_NCHILDREN_MAX) {
		di = &dreq;
		ni = NULL;
	} else if ((n + 1) <= NILFS_BTREE_NODE_NCHILDREN_MAX(
			   1 << bmap->b_inode->i_blkbits)) {
		di = &dreq;
		ni = &nreq;
	} else {
		di = NULL;
		ni = NULL;
		BUG();
	}

	ret = nilfs_btree_prepare_convert_and_insert(bmap, key, di, ni, &bh,
						     &stats);
	if (ret < 0)
		return ret;
	nilfs_btree_commit_convert_and_insert(bmap, key, ptr, keys, ptrs, n,
					      di, ni, bh);
	nilfs_bmap_add_blocks(bmap, stats.bs_nblocks);
	return 0;
}

static int nilfs_btree_propagate_p(struct nilfs_btree *btree,
				   struct nilfs_btree_path *path,
				   int level,
				   struct buffer_head *bh)
{
	while ((++level < nilfs_btree_height(btree) - 1) &&
	       !buffer_dirty(path[level].bp_bh))
		nilfs_btnode_mark_dirty(path[level].bp_bh);

	return 0;
}

static int nilfs_btree_prepare_update_v(struct nilfs_btree *btree,
					struct nilfs_btree_path *path,
					int level)
{
	struct nilfs_btree_node *parent;
	int ret;

	parent = nilfs_btree_get_node(btree, path, level + 1);
	path[level].bp_oldreq.bpr_ptr =
		nilfs_btree_node_get_ptr(btree, parent,
					 path[level + 1].bp_index);
	path[level].bp_newreq.bpr_ptr = path[level].bp_oldreq.bpr_ptr + 1;
	ret = nilfs_bmap_prepare_update_v(&btree->bt_bmap,
					  &path[level].bp_oldreq,
					  &path[level].bp_newreq);
	if (ret < 0)
		return ret;

	if (buffer_nilfs_node(path[level].bp_bh)) {
		path[level].bp_ctxt.oldkey = path[level].bp_oldreq.bpr_ptr;
		path[level].bp_ctxt.newkey = path[level].bp_newreq.bpr_ptr;
		path[level].bp_ctxt.bh = path[level].bp_bh;
		ret = nilfs_btnode_prepare_change_key(
			&NILFS_BMAP_I(&btree->bt_bmap)->i_btnode_cache,
			&path[level].bp_ctxt);
		if (ret < 0) {
			nilfs_bmap_abort_update_v(&btree->bt_bmap,
						  &path[level].bp_oldreq,
						  &path[level].bp_newreq);
			return ret;
		}
	}

	return 0;
}

static void nilfs_btree_commit_update_v(struct nilfs_btree *btree,
					struct nilfs_btree_path *path,
					int level)
{
	struct nilfs_btree_node *parent;

	nilfs_bmap_commit_update_v(&btree->bt_bmap,
				   &path[level].bp_oldreq,
				   &path[level].bp_newreq);

	if (buffer_nilfs_node(path[level].bp_bh)) {
		nilfs_btnode_commit_change_key(
			&NILFS_BMAP_I(&btree->bt_bmap)->i_btnode_cache,
			&path[level].bp_ctxt);
		path[level].bp_bh = path[level].bp_ctxt.bh;
	}
	set_buffer_nilfs_volatile(path[level].bp_bh);

	parent = nilfs_btree_get_node(btree, path, level + 1);
	nilfs_btree_node_set_ptr(btree, parent, path[level + 1].bp_index,
				 path[level].bp_newreq.bpr_ptr);
}

static void nilfs_btree_abort_update_v(struct nilfs_btree *btree,
				       struct nilfs_btree_path *path,
				       int level)
{
	nilfs_bmap_abort_update_v(&btree->bt_bmap,
				  &path[level].bp_oldreq,
				  &path[level].bp_newreq);
	if (buffer_nilfs_node(path[level].bp_bh))
		nilfs_btnode_abort_change_key(
			&NILFS_BMAP_I(&btree->bt_bmap)->i_btnode_cache,
			&path[level].bp_ctxt);
}

static int nilfs_btree_prepare_propagate_v(struct nilfs_btree *btree,
					   struct nilfs_btree_path *path,
					   int minlevel,
					   int *maxlevelp)
{
	int level, ret;

	level = minlevel;
	if (!buffer_nilfs_volatile(path[level].bp_bh)) {
		ret = nilfs_btree_prepare_update_v(btree, path, level);
		if (ret < 0)
			return ret;
	}
	while ((++level < nilfs_btree_height(btree) - 1) &&
	       !buffer_dirty(path[level].bp_bh)) {

		WARN_ON(buffer_nilfs_volatile(path[level].bp_bh));
		ret = nilfs_btree_prepare_update_v(btree, path, level);
		if (ret < 0)
			goto out;
	}

	/* success */
	*maxlevelp = level - 1;
	return 0;

	/* error */
 out:
	while (--level > minlevel)
		nilfs_btree_abort_update_v(btree, path, level);
	if (!buffer_nilfs_volatile(path[level].bp_bh))
		nilfs_btree_abort_update_v(btree, path, level);
	return ret;
}

static void nilfs_btree_commit_propagate_v(struct nilfs_btree *btree,
					   struct nilfs_btree_path *path,
					   int minlevel,
					   int maxlevel,
					   struct buffer_head *bh)
{
	int level;

	if (!buffer_nilfs_volatile(path[minlevel].bp_bh))
		nilfs_btree_commit_update_v(btree, path, minlevel);

	for (level = minlevel + 1; level <= maxlevel; level++)
		nilfs_btree_commit_update_v(btree, path, level);
}

static int nilfs_btree_propagate_v(struct nilfs_btree *btree,
				   struct nilfs_btree_path *path,
				   int level,
				   struct buffer_head *bh)
{
	int maxlevel, ret;
	struct nilfs_btree_node *parent;
	__u64 ptr;

	get_bh(bh);
	path[level].bp_bh = bh;
	ret = nilfs_btree_prepare_propagate_v(btree, path, level, &maxlevel);
	if (ret < 0)
		goto out;

	if (buffer_nilfs_volatile(path[level].bp_bh)) {
		parent = nilfs_btree_get_node(btree, path, level + 1);
		ptr = nilfs_btree_node_get_ptr(btree, parent,
					       path[level + 1].bp_index);
		ret = nilfs_bmap_mark_dirty(&btree->bt_bmap, ptr);
		if (ret < 0)
			goto out;
	}

	nilfs_btree_commit_propagate_v(btree, path, level, maxlevel, bh);

 out:
	brelse(path[level].bp_bh);
	path[level].bp_bh = NULL;
	return ret;
}

static int nilfs_btree_propagate(const struct nilfs_bmap *bmap,
				 struct buffer_head *bh)
{
	struct nilfs_btree *btree;
	struct nilfs_btree_path *path;
	struct nilfs_btree_node *node;
	__u64 key;
	int level, ret;

	WARN_ON(!buffer_dirty(bh));

	btree = (struct nilfs_btree *)bmap;
	path = nilfs_btree_alloc_path(btree);
	if (path == NULL)
		return -ENOMEM;
	nilfs_btree_init_path(btree, path);

	if (buffer_nilfs_node(bh)) {
		node = (struct nilfs_btree_node *)bh->b_data;
		key = nilfs_btree_node_get_key(btree, node, 0);
		level = nilfs_btree_node_get_level(btree, node);
	} else {
		key = nilfs_bmap_data_get_key(bmap, bh);
		level = NILFS_BTREE_LEVEL_DATA;
	}

	ret = nilfs_btree_do_lookup(btree, path, key, NULL, level + 1);
	if (ret < 0) {
		if (unlikely(ret == -ENOENT))
			printk(KERN_CRIT "%s: key = %llu, level == %d\n",
			       __func__, (unsigned long long)key, level);
		goto out;
	}

	ret = NILFS_BMAP_USE_VBN(bmap) ?
		nilfs_btree_propagate_v(btree, path, level, bh) :
		nilfs_btree_propagate_p(btree, path, level, bh);

 out:
	nilfs_btree_clear_path(btree, path);
	nilfs_btree_free_path(btree, path);

	return ret;
}

static int nilfs_btree_propagate_gc(const struct nilfs_bmap *bmap,
				    struct buffer_head *bh)
{
	return nilfs_bmap_mark_dirty(bmap, bh->b_blocknr);
}

static void nilfs_btree_add_dirty_buffer(struct nilfs_btree *btree,
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
	key = nilfs_btree_node_get_key(btree, node, 0);
	level = nilfs_btree_node_get_level(btree, node);
	list_for_each(head, &lists[level]) {
		cbh = list_entry(head, struct buffer_head, b_assoc_buffers);
		cnode = (struct nilfs_btree_node *)cbh->b_data;
		ckey = nilfs_btree_node_get_key(btree, cnode, 0);
		if (key < ckey)
			break;
	}
	list_add_tail(&bh->b_assoc_buffers, head);
}

static void nilfs_btree_lookup_dirty_buffers(struct nilfs_bmap *bmap,
					     struct list_head *listp)
{
	struct nilfs_btree *btree = (struct nilfs_btree *)bmap;
	struct address_space *btcache = &NILFS_BMAP_I(bmap)->i_btnode_cache;
	struct list_head lists[NILFS_BTREE_LEVEL_MAX];
	struct pagevec pvec;
	struct buffer_head *bh, *head;
	pgoff_t index = 0;
	int level, i;

	for (level = NILFS_BTREE_LEVEL_NODE_MIN;
	     level < NILFS_BTREE_LEVEL_MAX;
	     level++)
		INIT_LIST_HEAD(&lists[level]);

	pagevec_init(&pvec, 0);

	while (pagevec_lookup_tag(&pvec, btcache, &index, PAGECACHE_TAG_DIRTY,
				  PAGEVEC_SIZE)) {
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
		list_splice(&lists[level], listp->prev);
}

static int nilfs_btree_assign_p(struct nilfs_btree *btree,
				struct nilfs_btree_path *path,
				int level,
				struct buffer_head **bh,
				sector_t blocknr,
				union nilfs_binfo *binfo)
{
	struct nilfs_btree_node *parent;
	__u64 key;
	__u64 ptr;
	int ret;

	parent = nilfs_btree_get_node(btree, path, level + 1);
	ptr = nilfs_btree_node_get_ptr(btree, parent,
				       path[level + 1].bp_index);
	if (buffer_nilfs_node(*bh)) {
		path[level].bp_ctxt.oldkey = ptr;
		path[level].bp_ctxt.newkey = blocknr;
		path[level].bp_ctxt.bh = *bh;
		ret = nilfs_btnode_prepare_change_key(
			&NILFS_BMAP_I(&btree->bt_bmap)->i_btnode_cache,
			&path[level].bp_ctxt);
		if (ret < 0)
			return ret;
		nilfs_btnode_commit_change_key(
			&NILFS_BMAP_I(&btree->bt_bmap)->i_btnode_cache,
			&path[level].bp_ctxt);
		*bh = path[level].bp_ctxt.bh;
	}

	nilfs_btree_node_set_ptr(btree, parent,
				 path[level + 1].bp_index, blocknr);

	key = nilfs_btree_node_get_key(btree, parent,
				       path[level + 1].bp_index);
	/* on-disk format */
	binfo->bi_dat.bi_blkoff = nilfs_bmap_key_to_dkey(key);
	binfo->bi_dat.bi_level = level;

	return 0;
}

static int nilfs_btree_assign_v(struct nilfs_btree *btree,
				struct nilfs_btree_path *path,
				int level,
				struct buffer_head **bh,
				sector_t blocknr,
				union nilfs_binfo *binfo)
{
	struct nilfs_btree_node *parent;
	__u64 key;
	__u64 ptr;
	union nilfs_bmap_ptr_req req;
	int ret;

	parent = nilfs_btree_get_node(btree, path, level + 1);
	ptr = nilfs_btree_node_get_ptr(btree, parent,
				       path[level + 1].bp_index);
	req.bpr_ptr = ptr;
	ret = nilfs_bmap_start_v(&btree->bt_bmap, &req, blocknr);
	if (unlikely(ret < 0))
		return ret;

	key = nilfs_btree_node_get_key(btree, parent,
				       path[level + 1].bp_index);
	/* on-disk format */
	binfo->bi_v.bi_vblocknr = nilfs_bmap_ptr_to_dptr(ptr);
	binfo->bi_v.bi_blkoff = nilfs_bmap_key_to_dkey(key);

	return 0;
}

static int nilfs_btree_assign(struct nilfs_bmap *bmap,
			      struct buffer_head **bh,
			      sector_t blocknr,
			      union nilfs_binfo *binfo)
{
	struct nilfs_btree *btree;
	struct nilfs_btree_path *path;
	struct nilfs_btree_node *node;
	__u64 key;
	int level, ret;

	btree = (struct nilfs_btree *)bmap;
	path = nilfs_btree_alloc_path(btree);
	if (path == NULL)
		return -ENOMEM;
	nilfs_btree_init_path(btree, path);

	if (buffer_nilfs_node(*bh)) {
		node = (struct nilfs_btree_node *)(*bh)->b_data;
		key = nilfs_btree_node_get_key(btree, node, 0);
		level = nilfs_btree_node_get_level(btree, node);
	} else {
		key = nilfs_bmap_data_get_key(bmap, *bh);
		level = NILFS_BTREE_LEVEL_DATA;
	}

	ret = nilfs_btree_do_lookup(btree, path, key, NULL, level + 1);
	if (ret < 0) {
		WARN_ON(ret == -ENOENT);
		goto out;
	}

	ret = NILFS_BMAP_USE_VBN(bmap) ?
		nilfs_btree_assign_v(btree, path, level, bh, blocknr, binfo) :
		nilfs_btree_assign_p(btree, path, level, bh, blocknr, binfo);

 out:
	nilfs_btree_clear_path(btree, path);
	nilfs_btree_free_path(btree, path);

	return ret;
}

static int nilfs_btree_assign_gc(struct nilfs_bmap *bmap,
				 struct buffer_head **bh,
				 sector_t blocknr,
				 union nilfs_binfo *binfo)
{
	struct nilfs_btree *btree;
	struct nilfs_btree_node *node;
	__u64 key;
	int ret;

	btree = (struct nilfs_btree *)bmap;
	ret = nilfs_bmap_move_v(bmap, (*bh)->b_blocknr, blocknr);
	if (ret < 0)
		return ret;

	if (buffer_nilfs_node(*bh)) {
		node = (struct nilfs_btree_node *)(*bh)->b_data;
		key = nilfs_btree_node_get_key(btree, node, 0);
	} else
		key = nilfs_bmap_data_get_key(bmap, *bh);

	/* on-disk format */
	binfo->bi_v.bi_vblocknr = cpu_to_le64((*bh)->b_blocknr);
	binfo->bi_v.bi_blkoff = nilfs_bmap_key_to_dkey(key);

	return 0;
}

static int nilfs_btree_mark(struct nilfs_bmap *bmap, __u64 key, int level)
{
	struct buffer_head *bh;
	struct nilfs_btree *btree;
	struct nilfs_btree_path *path;
	__u64 ptr;
	int ret;

	btree = (struct nilfs_btree *)bmap;
	path = nilfs_btree_alloc_path(btree);
	if (path == NULL)
		return -ENOMEM;
	nilfs_btree_init_path(btree, path);

	ret = nilfs_btree_do_lookup(btree, path, key, &ptr, level + 1);
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
		nilfs_btnode_mark_dirty(bh);
	brelse(bh);
	if (!nilfs_bmap_dirty(&btree->bt_bmap))
		nilfs_bmap_set_dirty(&btree->bt_bmap);

 out:
	nilfs_btree_clear_path(btree, path);
	nilfs_btree_free_path(btree, path);
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

	.bop_last_key		=	NULL,
	.bop_check_insert	=	NULL,
	.bop_check_delete	=	NULL,
	.bop_gather_data	=	NULL,
};

int nilfs_btree_init(struct nilfs_bmap *bmap)
{
	bmap->b_ops = &nilfs_btree_ops;
	return 0;
}

void nilfs_btree_init_gc(struct nilfs_bmap *bmap)
{
	bmap->b_ops = &nilfs_btree_ops_gc;
}
