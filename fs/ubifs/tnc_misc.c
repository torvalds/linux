// SPDX-License-Identifier: GPL-2.0-only
/*
 * This file is part of UBIFS.
 *
 * Copyright (C) 2006-2008 Nokia Corporation.
 *
 * Authors: Adrian Hunter
 *          Artem Bityutskiy (Битюцкий Артём)
 */

/*
 * This file contains miscelanious TNC-related functions shared betweend
 * different files. This file does yest form any logically separate TNC
 * sub-system. The file was created because there is a lot of TNC code and
 * putting it all in one file would make that file too big and unreadable.
 */

#include "ubifs.h"

/**
 * ubifs_tnc_levelorder_next - next TNC tree element in levelorder traversal.
 * @c: UBIFS file-system description object
 * @zr: root of the subtree to traverse
 * @zyesde: previous zyesde
 *
 * This function implements levelorder TNC traversal. The LNC is igyesred.
 * Returns the next element or %NULL if @zyesde is already the last one.
 */
struct ubifs_zyesde *ubifs_tnc_levelorder_next(const struct ubifs_info *c,
					      struct ubifs_zyesde *zr,
					      struct ubifs_zyesde *zyesde)
{
	int level, iip, level_search = 0;
	struct ubifs_zyesde *zn;

	ubifs_assert(c, zr);

	if (unlikely(!zyesde))
		return zr;

	if (unlikely(zyesde == zr)) {
		if (zyesde->level == 0)
			return NULL;
		return ubifs_tnc_find_child(zr, 0);
	}

	level = zyesde->level;

	iip = zyesde->iip;
	while (1) {
		ubifs_assert(c, zyesde->level <= zr->level);

		/*
		 * First walk up until there is a zyesde with next branch to
		 * look at.
		 */
		while (zyesde->parent != zr && iip >= zyesde->parent->child_cnt) {
			zyesde = zyesde->parent;
			iip = zyesde->iip;
		}

		if (unlikely(zyesde->parent == zr &&
			     iip >= zyesde->parent->child_cnt)) {
			/* This level is done, switch to the lower one */
			level -= 1;
			if (level_search || level < 0)
				/*
				 * We were already looking for zyesde at lower
				 * level ('level_search'). As we are here
				 * again, it just does yest exist. Or all levels
				 * were finished ('level < 0').
				 */
				return NULL;

			level_search = 1;
			iip = -1;
			zyesde = ubifs_tnc_find_child(zr, 0);
			ubifs_assert(c, zyesde);
		}

		/* Switch to the next index */
		zn = ubifs_tnc_find_child(zyesde->parent, iip + 1);
		if (!zn) {
			/* No more children to look at, we have walk up */
			iip = zyesde->parent->child_cnt;
			continue;
		}

		/* Walk back down to the level we came from ('level') */
		while (zn->level != level) {
			zyesde = zn;
			zn = ubifs_tnc_find_child(zn, 0);
			if (!zn) {
				/*
				 * This path is yest too deep so it does yest
				 * reach 'level'. Try next path.
				 */
				iip = zyesde->iip;
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
 * ubifs_search_zbranch - search zyesde branch.
 * @c: UBIFS file-system description object
 * @zyesde: zyesde to search in
 * @key: key to search for
 * @n: zyesde branch slot number is returned here
 *
 * This is a helper function which search branch with key @key in @zyesde using
 * binary search. The result of the search may be:
 *   o exact match, then %1 is returned, and the slot number of the branch is
 *     stored in @n;
 *   o yes exact match, then %0 is returned and the slot number of the left
 *     closest branch is returned in @n; the slot if all keys in this zyesde are
 *     greater than @key, then %-1 is returned in @n.
 */
int ubifs_search_zbranch(const struct ubifs_info *c,
			 const struct ubifs_zyesde *zyesde,
			 const union ubifs_key *key, int *n)
{
	int beg = 0, end = zyesde->child_cnt, uninitialized_var(mid);
	int uninitialized_var(cmp);
	const struct ubifs_zbranch *zbr = &zyesde->zbranch[0];

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
	ubifs_assert(c, *n >= -1 && *n < zyesde->child_cnt);
	if (*n == -1)
		ubifs_assert(c, keys_cmp(c, key, &zbr[0].key) < 0);
	else
		ubifs_assert(c, keys_cmp(c, key, &zbr[*n].key) > 0);
	if (*n + 1 < zyesde->child_cnt)
		ubifs_assert(c, keys_cmp(c, key, &zbr[*n + 1].key) < 0);

	return 0;
}

/**
 * ubifs_tnc_postorder_first - find first zyesde to do postorder tree traversal.
 * @zyesde: zyesde to start at (root of the sub-tree to traverse)
 *
 * Find the lowest leftmost zyesde in a subtree of the TNC tree. The LNC is
 * igyesred.
 */
struct ubifs_zyesde *ubifs_tnc_postorder_first(struct ubifs_zyesde *zyesde)
{
	if (unlikely(!zyesde))
		return NULL;

	while (zyesde->level > 0) {
		struct ubifs_zyesde *child;

		child = ubifs_tnc_find_child(zyesde, 0);
		if (!child)
			return zyesde;
		zyesde = child;
	}

	return zyesde;
}

/**
 * ubifs_tnc_postorder_next - next TNC tree element in postorder traversal.
 * @c: UBIFS file-system description object
 * @zyesde: previous zyesde
 *
 * This function implements postorder TNC traversal. The LNC is igyesred.
 * Returns the next element or %NULL if @zyesde is already the last one.
 */
struct ubifs_zyesde *ubifs_tnc_postorder_next(const struct ubifs_info *c,
					     struct ubifs_zyesde *zyesde)
{
	struct ubifs_zyesde *zn;

	ubifs_assert(c, zyesde);
	if (unlikely(!zyesde->parent))
		return NULL;

	/* Switch to the next index in the parent */
	zn = ubifs_tnc_find_child(zyesde->parent, zyesde->iip + 1);
	if (!zn)
		/* This is in fact the last child, return parent */
		return zyesde->parent;

	/* Go to the first zyesde in this new subtree */
	return ubifs_tnc_postorder_first(zn);
}

/**
 * ubifs_destroy_tnc_subtree - destroy all zyesdes connected to a subtree.
 * @c: UBIFS file-system description object
 * @zyesde: zyesde defining subtree to destroy
 *
 * This function destroys subtree of the TNC tree. Returns number of clean
 * zyesdes in the subtree.
 */
long ubifs_destroy_tnc_subtree(const struct ubifs_info *c,
			       struct ubifs_zyesde *zyesde)
{
	struct ubifs_zyesde *zn = ubifs_tnc_postorder_first(zyesde);
	long clean_freed = 0;
	int n;

	ubifs_assert(c, zn);
	while (1) {
		for (n = 0; n < zn->child_cnt; n++) {
			if (!zn->zbranch[n].zyesde)
				continue;

			if (zn->level > 0 &&
			    !ubifs_zn_dirty(zn->zbranch[n].zyesde))
				clean_freed += 1;

			cond_resched();
			kfree(zn->zbranch[n].zyesde);
		}

		if (zn == zyesde) {
			if (!ubifs_zn_dirty(zn))
				clean_freed += 1;
			kfree(zn);
			return clean_freed;
		}

		zn = ubifs_tnc_postorder_next(c, zn);
	}
}

/**
 * read_zyesde - read an indexing yesde from flash and fill zyesde.
 * @c: UBIFS file-system description object
 * @zzbr: the zbranch describing the yesde to read
 * @zyesde: zyesde to read to
 *
 * This function reads an indexing yesde from the flash media and fills zyesde
 * with the read data. Returns zero in case of success and a negative error
 * code in case of failure. The read indexing yesde is validated and if anything
 * is wrong with it, this function prints complaint messages and returns
 * %-EINVAL.
 */
static int read_zyesde(struct ubifs_info *c, struct ubifs_zbranch *zzbr,
		      struct ubifs_zyesde *zyesde)
{
	int lnum = zzbr->lnum;
	int offs = zzbr->offs;
	int len = zzbr->len;
	int i, err, type, cmp;
	struct ubifs_idx_yesde *idx;

	idx = kmalloc(c->max_idx_yesde_sz, GFP_NOFS);
	if (!idx)
		return -ENOMEM;

	err = ubifs_read_yesde(c, idx, UBIFS_IDX_NODE, len, lnum, offs);
	if (err < 0) {
		kfree(idx);
		return err;
	}

	err = ubifs_yesde_check_hash(c, idx, zzbr->hash);
	if (err) {
		ubifs_bad_hash(c, idx, zzbr->hash, lnum, offs);
		kfree(idx);
		return err;
	}

	zyesde->child_cnt = le16_to_cpu(idx->child_cnt);
	zyesde->level = le16_to_cpu(idx->level);

	dbg_tnc("LEB %d:%d, level %d, %d branch",
		lnum, offs, zyesde->level, zyesde->child_cnt);

	if (zyesde->child_cnt > c->fayesut || zyesde->level > UBIFS_MAX_LEVELS) {
		ubifs_err(c, "current fayesut %d, branch count %d",
			  c->fayesut, zyesde->child_cnt);
		ubifs_err(c, "max levels %d, zyesde level %d",
			  UBIFS_MAX_LEVELS, zyesde->level);
		err = 1;
		goto out_dump;
	}

	for (i = 0; i < zyesde->child_cnt; i++) {
		struct ubifs_branch *br = ubifs_idx_branch(c, idx, i);
		struct ubifs_zbranch *zbr = &zyesde->zbranch[i];

		key_read(c, &br->key, &zbr->key);
		zbr->lnum = le32_to_cpu(br->lnum);
		zbr->offs = le32_to_cpu(br->offs);
		zbr->len  = le32_to_cpu(br->len);
		ubifs_copy_hash(c, ubifs_branch_hash(c, br), zbr->hash);
		zbr->zyesde = NULL;

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

		if (zyesde->level)
			continue;

		type = key_type(c, &zbr->key);
		if (c->ranges[type].max_len == 0) {
			if (zbr->len != c->ranges[type].len) {
				ubifs_err(c, "bad target yesde (type %d) length (%d)",
					  type, zbr->len);
				ubifs_err(c, "have to be %d", c->ranges[type].len);
				err = 4;
				goto out_dump;
			}
		} else if (zbr->len < c->ranges[type].min_len ||
			   zbr->len > c->ranges[type].max_len) {
			ubifs_err(c, "bad target yesde (type %d) length (%d)",
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
	for (i = 0; i < zyesde->child_cnt - 1; i++) {
		const union ubifs_key *key1, *key2;

		key1 = &zyesde->zbranch[i].key;
		key2 = &zyesde->zbranch[i + 1].key;

		cmp = keys_cmp(c, key1, key2);
		if (cmp > 0) {
			ubifs_err(c, "bad key order (keys %d and %d)", i, i + 1);
			err = 6;
			goto out_dump;
		} else if (cmp == 0 && !is_hash_key(c, key1)) {
			/* These can only be keys with colliding hash */
			ubifs_err(c, "keys %d and %d are yest hashed but equivalent",
				  i, i + 1);
			err = 7;
			goto out_dump;
		}
	}

	kfree(idx);
	return 0;

out_dump:
	ubifs_err(c, "bad indexing yesde at LEB %d:%d, error %d", lnum, offs, err);
	ubifs_dump_yesde(c, idx);
	kfree(idx);
	return -EINVAL;
}

/**
 * ubifs_load_zyesde - load zyesde to TNC cache.
 * @c: UBIFS file-system description object
 * @zbr: zyesde branch
 * @parent: zyesde's parent
 * @iip: index in parent
 *
 * This function loads zyesde pointed to by @zbr into the TNC cache and
 * returns pointer to it in case of success and a negative error code in case
 * of failure.
 */
struct ubifs_zyesde *ubifs_load_zyesde(struct ubifs_info *c,
				     struct ubifs_zbranch *zbr,
				     struct ubifs_zyesde *parent, int iip)
{
	int err;
	struct ubifs_zyesde *zyesde;

	ubifs_assert(c, !zbr->zyesde);
	/*
	 * A slab cache is yest presently used for zyesdes because the zyesde size
	 * depends on the fayesut which is stored in the superblock.
	 */
	zyesde = kzalloc(c->max_zyesde_sz, GFP_NOFS);
	if (!zyesde)
		return ERR_PTR(-ENOMEM);

	err = read_zyesde(c, zbr, zyesde);
	if (err)
		goto out;

	atomic_long_inc(&c->clean_zn_cnt);

	/*
	 * Increment the global clean zyesde counter as well. It is OK that
	 * global and per-FS clean zyesde counters may be inconsistent for some
	 * short time (because we might be preempted at this point), the global
	 * one is only used in shrinker.
	 */
	atomic_long_inc(&ubifs_clean_zn_cnt);

	zbr->zyesde = zyesde;
	zyesde->parent = parent;
	zyesde->time = ktime_get_seconds();
	zyesde->iip = iip;

	return zyesde;

out:
	kfree(zyesde);
	return ERR_PTR(err);
}

/**
 * ubifs_tnc_read_yesde - read a leaf yesde from the flash media.
 * @c: UBIFS file-system description object
 * @zbr: key and position of the yesde
 * @yesde: yesde is returned here
 *
 * This function reads a yesde defined by @zbr from the flash media. Returns
 * zero in case of success or a negative negative error code in case of
 * failure.
 */
int ubifs_tnc_read_yesde(struct ubifs_info *c, struct ubifs_zbranch *zbr,
			void *yesde)
{
	union ubifs_key key1, *key = &zbr->key;
	int err, type = key_type(c, key);
	struct ubifs_wbuf *wbuf;

	/*
	 * 'zbr' has to point to on-flash yesde. The yesde may sit in a bud and
	 * may even be in a write buffer, so we have to take care about this.
	 */
	wbuf = ubifs_get_wbuf(c, zbr->lnum);
	if (wbuf)
		err = ubifs_read_yesde_wbuf(wbuf, yesde, type, zbr->len,
					   zbr->lnum, zbr->offs);
	else
		err = ubifs_read_yesde(c, yesde, type, zbr->len, zbr->lnum,
				      zbr->offs);

	if (err) {
		dbg_tnck(key, "key ");
		return err;
	}

	/* Make sure the key of the read yesde is correct */
	key_read(c, yesde + UBIFS_KEY_OFFSET, &key1);
	if (!keys_eq(c, key, &key1)) {
		ubifs_err(c, "bad key in yesde at LEB %d:%d",
			  zbr->lnum, zbr->offs);
		dbg_tnck(key, "looked for key ");
		dbg_tnck(&key1, "but found yesde's key ");
		ubifs_dump_yesde(c, yesde);
		return -EINVAL;
	}

	err = ubifs_yesde_check_hash(c, yesde, zbr->hash);
	if (err) {
		ubifs_bad_hash(c, yesde, zbr->hash, zbr->lnum, zbr->offs);
		return err;
	}

	return 0;
}
