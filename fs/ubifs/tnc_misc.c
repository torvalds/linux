/*
 * This file is part of UBIFS.
 *
 * Copyright (C) 2006-2008 Nokia Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * Authors: Adrian Hunter
 *          Artem Bityutskiy (Битюцкий Артём)
 */

/*
 * This file contains miscelanious TNC-related functions shared betweend
 * different files. This file does not form any logically separate TNC
 * sub-system. The file was created because there is a lot of TNC code and
 * putting it all in one file would make that file too big and unreadable.
 */

#include "ubifs.h"

/**
 * ubifs_tnc_levelorder_next - next TNC tree element in levelorder traversal.
 * @c: UBIFS file-system description object
 * @zr: root of the subtree to traverse
 * @znode: previous znode
 *
 * This function implements levelorder TNC traversal. The LNC is ignored.
 * Returns the next element or %NULL if @znode is already the last one.
 */
struct ubifs_znode *ubifs_tnc_levelorder_next(const struct ubifs_info *c,
					      struct ubifs_znode *zr,
					      struct ubifs_znode *znode)
{
	int level, iip, level_search = 0;
	struct ubifs_znode *zn;

	ubifs_assert(c, zr);

	if (unlikely(!znode))
		return zr;

	if (unlikely(znode == zr)) {
		if (znode->level == 0)
			return NULL;
		return ubifs_tnc_find_child(zr, 0);
	}

	level = znode->level;

	iip = znode->iip;
	while (1) {
		ubifs_assert(c, znode->level <= zr->level);

		/*
		 * First walk up until there is a znode with next branch to
		 * look at.
		 */
		while (znode->parent != zr && iip >= znode->parent->child_cnt) {
			znode = znode->parent;
			iip = znode->iip;
		}

		if (unlikely(znode->parent == zr &&
			     iip >= znode->parent->child_cnt)) {
			/* This level is done, switch to the lower one */
			level -= 1;
			if (level_search || level < 0)
				/*
				 * We were already looking for znode at lower
				 * level ('level_search'). As we are here
				 * again, it just does not exist. Or all levels
				 * were finished ('level < 0').
				 */
				return NULL;

			level_search = 1;
			iip = -1;
			znode = ubifs_tnc_find_child(zr, 0);
			ubifs_assert(c, znode);
		}

		/* Switch to the next index */
		zn = ubifs_tnc_find_child(znode->parent, iip + 1);
		if (!zn) {
			/* No more children to look at, we have walk up */
			iip = znode->parent->child_cnt;
			continue;
		}

		/* Walk back down to the level we came from ('level') */
		while (zn->level != level) {
			znode = zn;
			zn = ubifs_tnc_find_child(zn, 0);
			if (!zn) {
				/*
				 * This path is not too deep so it does not
				 * reach 'level'. Try next path.
				 */
				iip = znode->iip;
				break;
			}
		}

		if (zn) {
			ubifs_assert(c, zn->level >= 0);
			return zn;
		}
	}
}

/**
 * ubifs_search_zbranch - search znode branch.
 * @c: UBIFS file-system description object
 * @znode: znode to search in
 * @key: key to search for
 * @n: znode branch slot number is returned here
 *
 * This is a helper function which search branch with key @key in @znode using
 * binary search. The result of the search may be:
 *   o exact match, then %1 is returned, and the slot number of the branch is
 *     stored in @n;
 *   o no exact match, then %0 is returned and the slot number of the left
 *     closest branch is returned in @n; the slot if all keys in this znode are
 *     greater than @key, then %-1 is returned in @n.
 */
int ubifs_search_zbranch(const struct ubifs_info *c,
			 const struct ubifs_znode *znode,
			 const union ubifs_key *key, int *n)
{
	int beg = 0, end = znode->child_cnt, uninitialized_var(mid);
	int uninitialized_var(cmp);
	const struct ubifs_zbranch *zbr = &znode->zbranch[0];

	ubifs_assert(c, end > beg);

	while (end > beg) {
		mid = (beg + end) >> 1;
		cmp = keys_cmp(c, key, &zbr[mid].key);
		if (cmp > 0)
			beg = mid + 1;
		else if (cmp < 0)
			end = mid;
		else {
			*n = mid;
			return 1;
		}
	}

	*n = end - 1;

	/* The insert point is after *n */
	ubifs_assert(c, *n >= -1 && *n < znode->child_cnt);
	if (*n == -1)
		ubifs_assert(c, keys_cmp(c, key, &zbr[0].key) < 0);
	else
		ubifs_assert(c, keys_cmp(c, key, &zbr[*n].key) > 0);
	if (*n + 1 < znode->child_cnt)
		ubifs_assert(c, keys_cmp(c, key, &zbr[*n + 1].key) < 0);

	return 0;
}

/**
 * ubifs_tnc_postorder_first - find first znode to do postorder tree traversal.
 * @znode: znode to start at (root of the sub-tree to traverse)
 *
 * Find the lowest leftmost znode in a subtree of the TNC tree. The LNC is
 * ignored.
 */
struct ubifs_znode *ubifs_tnc_postorder_first(struct ubifs_znode *znode)
{
	if (unlikely(!znode))
		return NULL;

	while (znode->level > 0) {
		struct ubifs_znode *child;

		child = ubifs_tnc_find_child(znode, 0);
		if (!child)
			return znode;
		znode = child;
	}

	return znode;
}

/**
 * ubifs_tnc_postorder_next - next TNC tree element in postorder traversal.
 * @c: UBIFS file-system description object
 * @znode: previous znode
 *
 * This function implements postorder TNC traversal. The LNC is ignored.
 * Returns the next element or %NULL if @znode is already the last one.
 */
struct ubifs_znode *ubifs_tnc_postorder_next(const struct ubifs_info *c,
					     struct ubifs_znode *znode)
{
	struct ubifs_znode *zn;

	ubifs_assert(c, znode);
	if (unlikely(!znode->parent))
		return NULL;

	/* Switch to the next index in the parent */
	zn = ubifs_tnc_find_child(znode->parent, znode->iip + 1);
	if (!zn)
		/* This is in fact the last child, return parent */
		return znode->parent;

	/* Go to the first znode in this new subtree */
	return ubifs_tnc_postorder_first(zn);
}

/**
 * ubifs_destroy_tnc_subtree - destroy all znodes connected to a subtree.
 * @c: UBIFS file-system description object
 * @znode: znode defining subtree to destroy
 *
 * This function destroys subtree of the TNC tree. Returns number of clean
 * znodes in the subtree.
 */
long ubifs_destroy_tnc_subtree(const struct ubifs_info *c,
			       struct ubifs_znode *znode)
{
	struct ubifs_znode *zn = ubifs_tnc_postorder_first(znode);
	long clean_freed = 0;
	int n;

	ubifs_assert(c, zn);
	while (1) {
		for (n = 0; n < zn->child_cnt; n++) {
			if (!zn->zbranch[n].znode)
				continue;

			if (zn->level > 0 &&
			    !ubifs_zn_dirty(zn->zbranch[n].znode))
				clean_freed += 1;

			cond_resched();
			kfree(zn->zbranch[n].znode);
		}

		if (zn == znode) {
			if (!ubifs_zn_dirty(zn))
				clean_freed += 1;
			kfree(zn);
			return clean_freed;
		}

		zn = ubifs_tnc_postorder_next(c, zn);
	}
}

/**
 * read_znode - read an indexing node from flash and fill znode.
 * @c: UBIFS file-system description object
 * @zzbr: the zbranch describing the node to read
 * @znode: znode to read to
 *
 * This function reads an indexing node from the flash media and fills znode
 * with the read data. Returns zero in case of success and a negative error
 * code in case of failure. The read indexing node is validated and if anything
 * is wrong with it, this function prints complaint messages and returns
 * %-EINVAL.
 */
static int read_znode(struct ubifs_info *c, struct ubifs_zbranch *zzbr,
		      struct ubifs_znode *znode)
{
	int lnum = zzbr->lnum;
	int offs = zzbr->offs;
	int len = zzbr->len;
	int i, err, type, cmp;
	struct ubifs_idx_node *idx;

	idx = kmalloc(c->max_idx_node_sz, GFP_NOFS);
	if (!idx)
		return -ENOMEM;

	err = ubifs_read_node(c, idx, UBIFS_IDX_NODE, len, lnum, offs);
	if (err < 0) {
		kfree(idx);
		return err;
	}

	err = ubifs_node_check_hash(c, idx, zzbr->hash);
	if (err) {
		ubifs_bad_hash(c, idx, zzbr->hash, lnum, offs);
		return err;
	}

	znode->child_cnt = le16_to_cpu(idx->child_cnt);
	znode->level = le16_to_cpu(idx->level);

	dbg_tnc("LEB %d:%d, level %d, %d branch",
		lnum, offs, znode->level, znode->child_cnt);

	if (znode->child_cnt > c->fanout || znode->level > UBIFS_MAX_LEVELS) {
		ubifs_err(c, "current fanout %d, branch count %d",
			  c->fanout, znode->child_cnt);
		ubifs_err(c, "max levels %d, znode level %d",
			  UBIFS_MAX_LEVELS, znode->level);
		err = 1;
		goto out_dump;
	}

	for (i = 0; i < znode->child_cnt; i++) {
		struct ubifs_branch *br = ubifs_idx_branch(c, idx, i);
		struct ubifs_zbranch *zbr = &znode->zbranch[i];

		key_read(c, &br->key, &zbr->key);
		zbr->lnum = le32_to_cpu(br->lnum);
		zbr->offs = le32_to_cpu(br->offs);
		zbr->len  = le32_to_cpu(br->len);
		ubifs_copy_hash(c, ubifs_branch_hash(c, br), zbr->hash);
		zbr->znode = NULL;

		/* Validate branch */

		if (zbr->lnum < c->main_first ||
		    zbr->lnum >= c->leb_cnt || zbr->offs < 0 ||
		    zbr->offs + zbr->len > c->leb_size || zbr->offs & 7) {
			ubifs_err(c, "bad branch %d", i);
			err = 2;
			goto out_dump;
		}

		switch (key_type(c, &zbr->key)) {
		case UBIFS_INO_KEY:
		case UBIFS_DATA_KEY:
		case UBIFS_DENT_KEY:
		case UBIFS_XENT_KEY:
			break;
		default:
			ubifs_err(c, "bad key type at slot %d: %d",
				  i, key_type(c, &zbr->key));
			err = 3;
			goto out_dump;
		}

		if (znode->level)
			continue;

		type = key_type(c, &zbr->key);
		if (c->ranges[type].max_len == 0) {
			if (zbr->len != c->ranges[type].len) {
				ubifs_err(c, "bad target node (type %d) length (%d)",
					  type, zbr->len);
				ubifs_err(c, "have to be %d", c->ranges[type].len);
				err = 4;
				goto out_dump;
			}
		} else if (zbr->len < c->ranges[type].min_len ||
			   zbr->len > c->ranges[type].max_len) {
			ubifs_err(c, "bad target node (type %d) length (%d)",
				  type, zbr->len);
			ubifs_err(c, "have to be in range of %d-%d",
				  c->ranges[type].min_len,
				  c->ranges[type].max_len);
			err = 5;
			goto out_dump;
		}
	}

	/*
	 * Ensure that the next key is greater or equivalent to the
	 * previous one.
	 */
	for (i = 0; i < znode->child_cnt - 1; i++) {
		const union ubifs_key *key1, *key2;

		key1 = &znode->zbranch[i].key;
		key2 = &znode->zbranch[i + 1].key;

		cmp = keys_cmp(c, key1, key2);
		if (cmp > 0) {
			ubifs_err(c, "bad key order (keys %d and %d)", i, i + 1);
			err = 6;
			goto out_dump;
		} else if (cmp == 0 && !is_hash_key(c, key1)) {
			/* These can only be keys with colliding hash */
			ubifs_err(c, "keys %d and %d are not hashed but equivalent",
				  i, i + 1);
			err = 7;
			goto out_dump;
		}
	}

	kfree(idx);
	return 0;

out_dump:
	ubifs_err(c, "bad indexing node at LEB %d:%d, error %d", lnum, offs, err);
	ubifs_dump_node(c, idx);
	kfree(idx);
	return -EINVAL;
}

/**
 * ubifs_load_znode - load znode to TNC cache.
 * @c: UBIFS file-system description object
 * @zbr: znode branch
 * @parent: znode's parent
 * @iip: index in parent
 *
 * This function loads znode pointed to by @zbr into the TNC cache and
 * returns pointer to it in case of success and a negative error code in case
 * of failure.
 */
struct ubifs_znode *ubifs_load_znode(struct ubifs_info *c,
				     struct ubifs_zbranch *zbr,
				     struct ubifs_znode *parent, int iip)
{
	int err;
	struct ubifs_znode *znode;

	ubifs_assert(c, !zbr->znode);
	/*
	 * A slab cache is not presently used for znodes because the znode size
	 * depends on the fanout which is stored in the superblock.
	 */
	znode = kzalloc(c->max_znode_sz, GFP_NOFS);
	if (!znode)
		return ERR_PTR(-ENOMEM);

	err = read_znode(c, zbr, znode);
	if (err)
		goto out;

	atomic_long_inc(&c->clean_zn_cnt);

	/*
	 * Increment the global clean znode counter as well. It is OK that
	 * global and per-FS clean znode counters may be inconsistent for some
	 * short time (because we might be preempted at this point), the global
	 * one is only used in shrinker.
	 */
	atomic_long_inc(&ubifs_clean_zn_cnt);

	zbr->znode = znode;
	znode->parent = parent;
	znode->time = ktime_get_seconds();
	znode->iip = iip;

	return znode;

out:
	kfree(znode);
	return ERR_PTR(err);
}

/**
 * ubifs_tnc_read_node - read a leaf node from the flash media.
 * @c: UBIFS file-system description object
 * @zbr: key and position of the node
 * @node: node is returned here
 *
 * This function reads a node defined by @zbr from the flash media. Returns
 * zero in case of success or a negative negative error code in case of
 * failure.
 */
int ubifs_tnc_read_node(struct ubifs_info *c, struct ubifs_zbranch *zbr,
			void *node)
{
	union ubifs_key key1, *key = &zbr->key;
	int err, type = key_type(c, key);
	struct ubifs_wbuf *wbuf;

	/*
	 * 'zbr' has to point to on-flash node. The node may sit in a bud and
	 * may even be in a write buffer, so we have to take care about this.
	 */
	wbuf = ubifs_get_wbuf(c, zbr->lnum);
	if (wbuf)
		err = ubifs_read_node_wbuf(wbuf, node, type, zbr->len,
					   zbr->lnum, zbr->offs);
	else
		err = ubifs_read_node(c, node, type, zbr->len, zbr->lnum,
				      zbr->offs);

	if (err) {
		dbg_tnck(key, "key ");
		return err;
	}

	/* Make sure the key of the read node is correct */
	key_read(c, node + UBIFS_KEY_OFFSET, &key1);
	if (!keys_eq(c, key, &key1)) {
		ubifs_err(c, "bad key in node at LEB %d:%d",
			  zbr->lnum, zbr->offs);
		dbg_tnck(key, "looked for key ");
		dbg_tnck(&key1, "but found node's key ");
		ubifs_dump_node(c, node);
		return -EINVAL;
	}

	err = ubifs_node_check_hash(c, node, zbr->hash);
	if (err) {
		ubifs_bad_hash(c, node, zbr->hash, zbr->lnum, zbr->offs);
		return err;
	}

	return 0;
}
