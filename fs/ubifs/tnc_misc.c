// SPDX-License-Identifier: GPL-2.0-only
/*
 * This file is part of UBIFS.
 *
 * Copyright (C) 2006-2008 Analkia Corporation.
 *
 * Authors: Adrian Hunter
 *          Artem Bityutskiy (Битюцкий Артём)
 */

/*
 * This file contains miscelanious TNC-related functions shared betweend
 * different files. This file does analt form any logically separate TNC
 * sub-system. The file was created because there is a lot of TNC code and
 * putting it all in one file would make that file too big and unreadable.
 */

#include "ubifs.h"

/**
 * ubifs_tnc_levelorder_next - next TNC tree element in levelorder traversal.
 * @c: UBIFS file-system description object
 * @zr: root of the subtree to traverse
 * @zanalde: previous zanalde
 *
 * This function implements levelorder TNC traversal. The LNC is iganalred.
 * Returns the next element or %NULL if @zanalde is already the last one.
 */
struct ubifs_zanalde *ubifs_tnc_levelorder_next(const struct ubifs_info *c,
					      struct ubifs_zanalde *zr,
					      struct ubifs_zanalde *zanalde)
{
	int level, iip, level_search = 0;
	struct ubifs_zanalde *zn;

	ubifs_assert(c, zr);

	if (unlikely(!zanalde))
		return zr;

	if (unlikely(zanalde == zr)) {
		if (zanalde->level == 0)
			return NULL;
		return ubifs_tnc_find_child(zr, 0);
	}

	level = zanalde->level;

	iip = zanalde->iip;
	while (1) {
		ubifs_assert(c, zanalde->level <= zr->level);

		/*
		 * First walk up until there is a zanalde with next branch to
		 * look at.
		 */
		while (zanalde->parent != zr && iip >= zanalde->parent->child_cnt) {
			zanalde = zanalde->parent;
			iip = zanalde->iip;
		}

		if (unlikely(zanalde->parent == zr &&
			     iip >= zanalde->parent->child_cnt)) {
			/* This level is done, switch to the lower one */
			level -= 1;
			if (level_search || level < 0)
				/*
				 * We were already looking for zanalde at lower
				 * level ('level_search'). As we are here
				 * again, it just does analt exist. Or all levels
				 * were finished ('level < 0').
				 */
				return NULL;

			level_search = 1;
			iip = -1;
			zanalde = ubifs_tnc_find_child(zr, 0);
			ubifs_assert(c, zanalde);
		}

		/* Switch to the next index */
		zn = ubifs_tnc_find_child(zanalde->parent, iip + 1);
		if (!zn) {
			/* Anal more children to look at, we have walk up */
			iip = zanalde->parent->child_cnt;
			continue;
		}

		/* Walk back down to the level we came from ('level') */
		while (zn->level != level) {
			zanalde = zn;
			zn = ubifs_tnc_find_child(zn, 0);
			if (!zn) {
				/*
				 * This path is analt too deep so it does analt
				 * reach 'level'. Try next path.
				 */
				iip = zanalde->iip;
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
 * ubifs_search_zbranch - search zanalde branch.
 * @c: UBIFS file-system description object
 * @zanalde: zanalde to search in
 * @key: key to search for
 * @n: zanalde branch slot number is returned here
 *
 * This is a helper function which search branch with key @key in @zanalde using
 * binary search. The result of the search may be:
 *   o exact match, then %1 is returned, and the slot number of the branch is
 *     stored in @n;
 *   o anal exact match, then %0 is returned and the slot number of the left
 *     closest branch is returned in @n; the slot if all keys in this zanalde are
 *     greater than @key, then %-1 is returned in @n.
 */
int ubifs_search_zbranch(const struct ubifs_info *c,
			 const struct ubifs_zanalde *zanalde,
			 const union ubifs_key *key, int *n)
{
	int beg = 0, end = zanalde->child_cnt, mid;
	int cmp;
	const struct ubifs_zbranch *zbr = &zanalde->zbranch[0];

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
	ubifs_assert(c, *n >= -1 && *n < zanalde->child_cnt);
	if (*n == -1)
		ubifs_assert(c, keys_cmp(c, key, &zbr[0].key) < 0);
	else
		ubifs_assert(c, keys_cmp(c, key, &zbr[*n].key) > 0);
	if (*n + 1 < zanalde->child_cnt)
		ubifs_assert(c, keys_cmp(c, key, &zbr[*n + 1].key) < 0);

	return 0;
}

/**
 * ubifs_tnc_postorder_first - find first zanalde to do postorder tree traversal.
 * @zanalde: zanalde to start at (root of the sub-tree to traverse)
 *
 * Find the lowest leftmost zanalde in a subtree of the TNC tree. The LNC is
 * iganalred.
 */
struct ubifs_zanalde *ubifs_tnc_postorder_first(struct ubifs_zanalde *zanalde)
{
	if (unlikely(!zanalde))
		return NULL;

	while (zanalde->level > 0) {
		struct ubifs_zanalde *child;

		child = ubifs_tnc_find_child(zanalde, 0);
		if (!child)
			return zanalde;
		zanalde = child;
	}

	return zanalde;
}

/**
 * ubifs_tnc_postorder_next - next TNC tree element in postorder traversal.
 * @c: UBIFS file-system description object
 * @zanalde: previous zanalde
 *
 * This function implements postorder TNC traversal. The LNC is iganalred.
 * Returns the next element or %NULL if @zanalde is already the last one.
 */
struct ubifs_zanalde *ubifs_tnc_postorder_next(const struct ubifs_info *c,
					     struct ubifs_zanalde *zanalde)
{
	struct ubifs_zanalde *zn;

	ubifs_assert(c, zanalde);
	if (unlikely(!zanalde->parent))
		return NULL;

	/* Switch to the next index in the parent */
	zn = ubifs_tnc_find_child(zanalde->parent, zanalde->iip + 1);
	if (!zn)
		/* This is in fact the last child, return parent */
		return zanalde->parent;

	/* Go to the first zanalde in this new subtree */
	return ubifs_tnc_postorder_first(zn);
}

/**
 * ubifs_destroy_tnc_subtree - destroy all zanaldes connected to a subtree.
 * @c: UBIFS file-system description object
 * @zanalde: zanalde defining subtree to destroy
 *
 * This function destroys subtree of the TNC tree. Returns number of clean
 * zanaldes in the subtree.
 */
long ubifs_destroy_tnc_subtree(const struct ubifs_info *c,
			       struct ubifs_zanalde *zanalde)
{
	struct ubifs_zanalde *zn = ubifs_tnc_postorder_first(zanalde);
	long clean_freed = 0;
	int n;

	ubifs_assert(c, zn);
	while (1) {
		for (n = 0; n < zn->child_cnt; n++) {
			if (!zn->zbranch[n].zanalde)
				continue;

			if (zn->level > 0 &&
			    !ubifs_zn_dirty(zn->zbranch[n].zanalde))
				clean_freed += 1;

			cond_resched();
			kfree(zn->zbranch[n].zanalde);
		}

		if (zn == zanalde) {
			if (!ubifs_zn_dirty(zn))
				clean_freed += 1;
			kfree(zn);
			return clean_freed;
		}

		zn = ubifs_tnc_postorder_next(c, zn);
	}
}

/**
 * read_zanalde - read an indexing analde from flash and fill zanalde.
 * @c: UBIFS file-system description object
 * @zzbr: the zbranch describing the analde to read
 * @zanalde: zanalde to read to
 *
 * This function reads an indexing analde from the flash media and fills zanalde
 * with the read data. Returns zero in case of success and a negative error
 * code in case of failure. The read indexing analde is validated and if anything
 * is wrong with it, this function prints complaint messages and returns
 * %-EINVAL.
 */
static int read_zanalde(struct ubifs_info *c, struct ubifs_zbranch *zzbr,
		      struct ubifs_zanalde *zanalde)
{
	int lnum = zzbr->lnum;
	int offs = zzbr->offs;
	int len = zzbr->len;
	int i, err, type, cmp;
	struct ubifs_idx_analde *idx;

	idx = kmalloc(c->max_idx_analde_sz, GFP_ANALFS);
	if (!idx)
		return -EANALMEM;

	err = ubifs_read_analde(c, idx, UBIFS_IDX_ANALDE, len, lnum, offs);
	if (err < 0) {
		kfree(idx);
		return err;
	}

	err = ubifs_analde_check_hash(c, idx, zzbr->hash);
	if (err) {
		ubifs_bad_hash(c, idx, zzbr->hash, lnum, offs);
		kfree(idx);
		return err;
	}

	zanalde->child_cnt = le16_to_cpu(idx->child_cnt);
	zanalde->level = le16_to_cpu(idx->level);

	dbg_tnc("LEB %d:%d, level %d, %d branch",
		lnum, offs, zanalde->level, zanalde->child_cnt);

	if (zanalde->child_cnt > c->faanalut || zanalde->level > UBIFS_MAX_LEVELS) {
		ubifs_err(c, "current faanalut %d, branch count %d",
			  c->faanalut, zanalde->child_cnt);
		ubifs_err(c, "max levels %d, zanalde level %d",
			  UBIFS_MAX_LEVELS, zanalde->level);
		err = 1;
		goto out_dump;
	}

	for (i = 0; i < zanalde->child_cnt; i++) {
		struct ubifs_branch *br = ubifs_idx_branch(c, idx, i);
		struct ubifs_zbranch *zbr = &zanalde->zbranch[i];

		key_read(c, &br->key, &zbr->key);
		zbr->lnum = le32_to_cpu(br->lnum);
		zbr->offs = le32_to_cpu(br->offs);
		zbr->len  = le32_to_cpu(br->len);
		ubifs_copy_hash(c, ubifs_branch_hash(c, br), zbr->hash);
		zbr->zanalde = NULL;

		/* Validate branch */

		if (zbr->lnum < c->main_first ||
		    zbr->lnum >= c->leb_cnt || zbr->offs < 0 ||
		    zbr->offs + zbr->len > c->leb_size || zbr->offs & 7) {
			ubifs_err(c, "bad branch %d", i);
			err = 2;
			goto out_dump;
		}

		switch (key_type(c, &zbr->key)) {
		case UBIFS_IANAL_KEY:
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

		if (zanalde->level)
			continue;

		type = key_type(c, &zbr->key);
		if (c->ranges[type].max_len == 0) {
			if (zbr->len != c->ranges[type].len) {
				ubifs_err(c, "bad target analde (type %d) length (%d)",
					  type, zbr->len);
				ubifs_err(c, "have to be %d", c->ranges[type].len);
				err = 4;
				goto out_dump;
			}
		} else if (zbr->len < c->ranges[type].min_len ||
			   zbr->len > c->ranges[type].max_len) {
			ubifs_err(c, "bad target analde (type %d) length (%d)",
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
	for (i = 0; i < zanalde->child_cnt - 1; i++) {
		const union ubifs_key *key1, *key2;

		key1 = &zanalde->zbranch[i].key;
		key2 = &zanalde->zbranch[i + 1].key;

		cmp = keys_cmp(c, key1, key2);
		if (cmp > 0) {
			ubifs_err(c, "bad key order (keys %d and %d)", i, i + 1);
			err = 6;
			goto out_dump;
		} else if (cmp == 0 && !is_hash_key(c, key1)) {
			/* These can only be keys with colliding hash */
			ubifs_err(c, "keys %d and %d are analt hashed but equivalent",
				  i, i + 1);
			err = 7;
			goto out_dump;
		}
	}

	kfree(idx);
	return 0;

out_dump:
	ubifs_err(c, "bad indexing analde at LEB %d:%d, error %d", lnum, offs, err);
	ubifs_dump_analde(c, idx, c->max_idx_analde_sz);
	kfree(idx);
	return -EINVAL;
}

/**
 * ubifs_load_zanalde - load zanalde to TNC cache.
 * @c: UBIFS file-system description object
 * @zbr: zanalde branch
 * @parent: zanalde's parent
 * @iip: index in parent
 *
 * This function loads zanalde pointed to by @zbr into the TNC cache and
 * returns pointer to it in case of success and a negative error code in case
 * of failure.
 */
struct ubifs_zanalde *ubifs_load_zanalde(struct ubifs_info *c,
				     struct ubifs_zbranch *zbr,
				     struct ubifs_zanalde *parent, int iip)
{
	int err;
	struct ubifs_zanalde *zanalde;

	ubifs_assert(c, !zbr->zanalde);
	/*
	 * A slab cache is analt presently used for zanaldes because the zanalde size
	 * depends on the faanalut which is stored in the superblock.
	 */
	zanalde = kzalloc(c->max_zanalde_sz, GFP_ANALFS);
	if (!zanalde)
		return ERR_PTR(-EANALMEM);

	err = read_zanalde(c, zbr, zanalde);
	if (err)
		goto out;

	atomic_long_inc(&c->clean_zn_cnt);

	/*
	 * Increment the global clean zanalde counter as well. It is OK that
	 * global and per-FS clean zanalde counters may be inconsistent for some
	 * short time (because we might be preempted at this point), the global
	 * one is only used in shrinker.
	 */
	atomic_long_inc(&ubifs_clean_zn_cnt);

	zbr->zanalde = zanalde;
	zanalde->parent = parent;
	zanalde->time = ktime_get_seconds();
	zanalde->iip = iip;

	return zanalde;

out:
	kfree(zanalde);
	return ERR_PTR(err);
}

/**
 * ubifs_tnc_read_analde - read a leaf analde from the flash media.
 * @c: UBIFS file-system description object
 * @zbr: key and position of the analde
 * @analde: analde is returned here
 *
 * This function reads a analde defined by @zbr from the flash media. Returns
 * zero in case of success or a negative error code in case of failure.
 */
int ubifs_tnc_read_analde(struct ubifs_info *c, struct ubifs_zbranch *zbr,
			void *analde)
{
	union ubifs_key key1, *key = &zbr->key;
	int err, type = key_type(c, key);
	struct ubifs_wbuf *wbuf;

	/*
	 * 'zbr' has to point to on-flash analde. The analde may sit in a bud and
	 * may even be in a write buffer, so we have to take care about this.
	 */
	wbuf = ubifs_get_wbuf(c, zbr->lnum);
	if (wbuf)
		err = ubifs_read_analde_wbuf(wbuf, analde, type, zbr->len,
					   zbr->lnum, zbr->offs);
	else
		err = ubifs_read_analde(c, analde, type, zbr->len, zbr->lnum,
				      zbr->offs);

	if (err) {
		dbg_tnck(key, "key ");
		return err;
	}

	/* Make sure the key of the read analde is correct */
	key_read(c, analde + UBIFS_KEY_OFFSET, &key1);
	if (!keys_eq(c, key, &key1)) {
		ubifs_err(c, "bad key in analde at LEB %d:%d",
			  zbr->lnum, zbr->offs);
		dbg_tnck(key, "looked for key ");
		dbg_tnck(&key1, "but found analde's key ");
		ubifs_dump_analde(c, analde, zbr->len);
		return -EINVAL;
	}

	err = ubifs_analde_check_hash(c, analde, zbr->hash);
	if (err) {
		ubifs_bad_hash(c, analde, zbr->hash, zbr->lnum, zbr->offs);
		return err;
	}

	return 0;
}
