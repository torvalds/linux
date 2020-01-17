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
 * This file implements TNC (Tree Node Cache) which caches indexing yesdes of
 * the UBIFS B-tree.
 *
 * At the moment the locking rules of the TNC tree are quite simple and
 * straightforward. We just have a mutex and lock it when we traverse the
 * tree. If a zyesde is yest in memory, we read it from flash while still having
 * the mutex locked.
 */

#include <linux/crc32.h>
#include <linux/slab.h>
#include "ubifs.h"

static int try_read_yesde(const struct ubifs_info *c, void *buf, int type,
			 struct ubifs_zbranch *zbr);
static int fallible_read_yesde(struct ubifs_info *c, const union ubifs_key *key,
			      struct ubifs_zbranch *zbr, void *yesde);

/*
 * Returned codes of 'matches_name()' and 'fallible_matches_name()' functions.
 * @NAME_LESS: name corresponding to the first argument is less than second
 * @NAME_MATCHES: names match
 * @NAME_GREATER: name corresponding to the second argument is greater than
 *                first
 * @NOT_ON_MEDIA: yesde referred by zbranch does yest exist on the media
 *
 * These constants were introduce to improve readability.
 */
enum {
	NAME_LESS    = 0,
	NAME_MATCHES = 1,
	NAME_GREATER = 2,
	NOT_ON_MEDIA = 3,
};

/**
 * insert_old_idx - record an index yesde obsoleted since the last commit start.
 * @c: UBIFS file-system description object
 * @lnum: LEB number of obsoleted index yesde
 * @offs: offset of obsoleted index yesde
 *
 * Returns %0 on success, and a negative error code on failure.
 *
 * For recovery, there must always be a complete intact version of the index on
 * flash at all times. That is called the "old index". It is the index as at the
 * time of the last successful commit. Many of the index yesdes in the old index
 * may be dirty, but they must yest be erased until the next successful commit
 * (at which point that index becomes the old index).
 *
 * That means that the garbage collection and the in-the-gaps method of
 * committing must be able to determine if an index yesde is in the old index.
 * Most of the old index yesdes can be found by looking up the TNC using the
 * 'lookup_zyesde()' function. However, some of the old index yesdes may have
 * been deleted from the current index or may have been changed so much that
 * they canyest be easily found. In those cases, an entry is added to an RB-tree.
 * That is what this function does. The RB-tree is ordered by LEB number and
 * offset because they uniquely identify the old index yesde.
 */
static int insert_old_idx(struct ubifs_info *c, int lnum, int offs)
{
	struct ubifs_old_idx *old_idx, *o;
	struct rb_yesde **p, *parent = NULL;

	old_idx = kmalloc(sizeof(struct ubifs_old_idx), GFP_NOFS);
	if (unlikely(!old_idx))
		return -ENOMEM;
	old_idx->lnum = lnum;
	old_idx->offs = offs;

	p = &c->old_idx.rb_yesde;
	while (*p) {
		parent = *p;
		o = rb_entry(parent, struct ubifs_old_idx, rb);
		if (lnum < o->lnum)
			p = &(*p)->rb_left;
		else if (lnum > o->lnum)
			p = &(*p)->rb_right;
		else if (offs < o->offs)
			p = &(*p)->rb_left;
		else if (offs > o->offs)
			p = &(*p)->rb_right;
		else {
			ubifs_err(c, "old idx added twice!");
			kfree(old_idx);
			return 0;
		}
	}
	rb_link_yesde(&old_idx->rb, parent, p);
	rb_insert_color(&old_idx->rb, &c->old_idx);
	return 0;
}

/**
 * insert_old_idx_zyesde - record a zyesde obsoleted since last commit start.
 * @c: UBIFS file-system description object
 * @zyesde: zyesde of obsoleted index yesde
 *
 * Returns %0 on success, and a negative error code on failure.
 */
int insert_old_idx_zyesde(struct ubifs_info *c, struct ubifs_zyesde *zyesde)
{
	if (zyesde->parent) {
		struct ubifs_zbranch *zbr;

		zbr = &zyesde->parent->zbranch[zyesde->iip];
		if (zbr->len)
			return insert_old_idx(c, zbr->lnum, zbr->offs);
	} else
		if (c->zroot.len)
			return insert_old_idx(c, c->zroot.lnum,
					      c->zroot.offs);
	return 0;
}

/**
 * ins_clr_old_idx_zyesde - record a zyesde obsoleted since last commit start.
 * @c: UBIFS file-system description object
 * @zyesde: zyesde of obsoleted index yesde
 *
 * Returns %0 on success, and a negative error code on failure.
 */
static int ins_clr_old_idx_zyesde(struct ubifs_info *c,
				 struct ubifs_zyesde *zyesde)
{
	int err;

	if (zyesde->parent) {
		struct ubifs_zbranch *zbr;

		zbr = &zyesde->parent->zbranch[zyesde->iip];
		if (zbr->len) {
			err = insert_old_idx(c, zbr->lnum, zbr->offs);
			if (err)
				return err;
			zbr->lnum = 0;
			zbr->offs = 0;
			zbr->len = 0;
		}
	} else
		if (c->zroot.len) {
			err = insert_old_idx(c, c->zroot.lnum, c->zroot.offs);
			if (err)
				return err;
			c->zroot.lnum = 0;
			c->zroot.offs = 0;
			c->zroot.len = 0;
		}
	return 0;
}

/**
 * destroy_old_idx - destroy the old_idx RB-tree.
 * @c: UBIFS file-system description object
 *
 * During start commit, the old_idx RB-tree is used to avoid overwriting index
 * yesdes that were in the index last commit but have since been deleted.  This
 * is necessary for recovery i.e. the old index must be kept intact until the
 * new index is successfully written.  The old-idx RB-tree is used for the
 * in-the-gaps method of writing index yesdes and is destroyed every commit.
 */
void destroy_old_idx(struct ubifs_info *c)
{
	struct ubifs_old_idx *old_idx, *n;

	rbtree_postorder_for_each_entry_safe(old_idx, n, &c->old_idx, rb)
		kfree(old_idx);

	c->old_idx = RB_ROOT;
}

/**
 * copy_zyesde - copy a dirty zyesde.
 * @c: UBIFS file-system description object
 * @zyesde: zyesde to copy
 *
 * A dirty zyesde being committed may yest be changed, so it is copied.
 */
static struct ubifs_zyesde *copy_zyesde(struct ubifs_info *c,
				      struct ubifs_zyesde *zyesde)
{
	struct ubifs_zyesde *zn;

	zn = kmemdup(zyesde, c->max_zyesde_sz, GFP_NOFS);
	if (unlikely(!zn))
		return ERR_PTR(-ENOMEM);

	zn->cnext = NULL;
	__set_bit(DIRTY_ZNODE, &zn->flags);
	__clear_bit(COW_ZNODE, &zn->flags);

	ubifs_assert(c, !ubifs_zn_obsolete(zyesde));
	__set_bit(OBSOLETE_ZNODE, &zyesde->flags);

	if (zyesde->level != 0) {
		int i;
		const int n = zn->child_cnt;

		/* The children yesw have new parent */
		for (i = 0; i < n; i++) {
			struct ubifs_zbranch *zbr = &zn->zbranch[i];

			if (zbr->zyesde)
				zbr->zyesde->parent = zn;
		}
	}

	atomic_long_inc(&c->dirty_zn_cnt);
	return zn;
}

/**
 * add_idx_dirt - add dirt due to a dirty zyesde.
 * @c: UBIFS file-system description object
 * @lnum: LEB number of index yesde
 * @dirt: size of index yesde
 *
 * This function updates lprops dirty space and the new size of the index.
 */
static int add_idx_dirt(struct ubifs_info *c, int lnum, int dirt)
{
	c->calc_idx_sz -= ALIGN(dirt, 8);
	return ubifs_add_dirt(c, lnum, dirt);
}

/**
 * dirty_cow_zyesde - ensure a zyesde is yest being committed.
 * @c: UBIFS file-system description object
 * @zbr: branch of zyesde to check
 *
 * Returns dirtied zyesde on success or negative error code on failure.
 */
static struct ubifs_zyesde *dirty_cow_zyesde(struct ubifs_info *c,
					   struct ubifs_zbranch *zbr)
{
	struct ubifs_zyesde *zyesde = zbr->zyesde;
	struct ubifs_zyesde *zn;
	int err;

	if (!ubifs_zn_cow(zyesde)) {
		/* zyesde is yest being committed */
		if (!test_and_set_bit(DIRTY_ZNODE, &zyesde->flags)) {
			atomic_long_inc(&c->dirty_zn_cnt);
			atomic_long_dec(&c->clean_zn_cnt);
			atomic_long_dec(&ubifs_clean_zn_cnt);
			err = add_idx_dirt(c, zbr->lnum, zbr->len);
			if (unlikely(err))
				return ERR_PTR(err);
		}
		return zyesde;
	}

	zn = copy_zyesde(c, zyesde);
	if (IS_ERR(zn))
		return zn;

	if (zbr->len) {
		err = insert_old_idx(c, zbr->lnum, zbr->offs);
		if (unlikely(err))
			return ERR_PTR(err);
		err = add_idx_dirt(c, zbr->lnum, zbr->len);
	} else
		err = 0;

	zbr->zyesde = zn;
	zbr->lnum = 0;
	zbr->offs = 0;
	zbr->len = 0;

	if (unlikely(err))
		return ERR_PTR(err);
	return zn;
}

/**
 * lnc_add - add a leaf yesde to the leaf yesde cache.
 * @c: UBIFS file-system description object
 * @zbr: zbranch of leaf yesde
 * @yesde: leaf yesde
 *
 * Leaf yesdes are yesn-index yesdes directory entry yesdes or data yesdes. The
 * purpose of the leaf yesde cache is to save re-reading the same leaf yesde over
 * and over again. Most things are cached by VFS, however the file system must
 * cache directory entries for readdir and for resolving hash collisions. The
 * present implementation of the leaf yesde cache is extremely simple, and
 * allows for error returns that are yest used but that may be needed if a more
 * complex implementation is created.
 *
 * Note, this function does yest add the @yesde object to LNC directly, but
 * allocates a copy of the object and adds the copy to LNC. The reason for this
 * is that @yesde has been allocated outside of the TNC subsystem and will be
 * used with @c->tnc_mutex unlock upon return from the TNC subsystem. But LNC
 * may be changed at any time, e.g. freed by the shrinker.
 */
static int lnc_add(struct ubifs_info *c, struct ubifs_zbranch *zbr,
		   const void *yesde)
{
	int err;
	void *lnc_yesde;
	const struct ubifs_dent_yesde *dent = yesde;

	ubifs_assert(c, !zbr->leaf);
	ubifs_assert(c, zbr->len != 0);
	ubifs_assert(c, is_hash_key(c, &zbr->key));

	err = ubifs_validate_entry(c, dent);
	if (err) {
		dump_stack();
		ubifs_dump_yesde(c, dent);
		return err;
	}

	lnc_yesde = kmemdup(yesde, zbr->len, GFP_NOFS);
	if (!lnc_yesde)
		/* We don't have to have the cache, so yes error */
		return 0;

	zbr->leaf = lnc_yesde;
	return 0;
}

 /**
 * lnc_add_directly - add a leaf yesde to the leaf-yesde-cache.
 * @c: UBIFS file-system description object
 * @zbr: zbranch of leaf yesde
 * @yesde: leaf yesde
 *
 * This function is similar to 'lnc_add()', but it does yest create a copy of
 * @yesde but inserts @yesde to TNC directly.
 */
static int lnc_add_directly(struct ubifs_info *c, struct ubifs_zbranch *zbr,
			    void *yesde)
{
	int err;

	ubifs_assert(c, !zbr->leaf);
	ubifs_assert(c, zbr->len != 0);

	err = ubifs_validate_entry(c, yesde);
	if (err) {
		dump_stack();
		ubifs_dump_yesde(c, yesde);
		return err;
	}

	zbr->leaf = yesde;
	return 0;
}

/**
 * lnc_free - remove a leaf yesde from the leaf yesde cache.
 * @zbr: zbranch of leaf yesde
 * @yesde: leaf yesde
 */
static void lnc_free(struct ubifs_zbranch *zbr)
{
	if (!zbr->leaf)
		return;
	kfree(zbr->leaf);
	zbr->leaf = NULL;
}

/**
 * tnc_read_hashed_yesde - read a "hashed" leaf yesde.
 * @c: UBIFS file-system description object
 * @zbr: key and position of the yesde
 * @yesde: yesde is returned here
 *
 * This function reads a "hashed" yesde defined by @zbr from the leaf yesde cache
 * (in it is there) or from the hash media, in which case the yesde is also
 * added to LNC. Returns zero in case of success or a negative negative error
 * code in case of failure.
 */
static int tnc_read_hashed_yesde(struct ubifs_info *c, struct ubifs_zbranch *zbr,
				void *yesde)
{
	int err;

	ubifs_assert(c, is_hash_key(c, &zbr->key));

	if (zbr->leaf) {
		/* Read from the leaf yesde cache */
		ubifs_assert(c, zbr->len != 0);
		memcpy(yesde, zbr->leaf, zbr->len);
		return 0;
	}

	if (c->replaying) {
		err = fallible_read_yesde(c, &zbr->key, zbr, yesde);
		/*
		 * When the yesde was yest found, return -ENOENT, 0 otherwise.
		 * Negative return codes stay as-is.
		 */
		if (err == 0)
			err = -ENOENT;
		else if (err == 1)
			err = 0;
	} else {
		err = ubifs_tnc_read_yesde(c, zbr, yesde);
	}
	if (err)
		return err;

	/* Add the yesde to the leaf yesde cache */
	err = lnc_add(c, zbr, yesde);
	return err;
}

/**
 * try_read_yesde - read a yesde if it is a yesde.
 * @c: UBIFS file-system description object
 * @buf: buffer to read to
 * @type: yesde type
 * @zbr: the zbranch describing the yesde to read
 *
 * This function tries to read a yesde of kyeswn type and length, checks it and
 * stores it in @buf. This function returns %1 if a yesde is present and %0 if
 * a yesde is yest present. A negative error code is returned for I/O errors.
 * This function performs that same function as ubifs_read_yesde except that
 * it does yest require that there is actually a yesde present and instead
 * the return code indicates if a yesde was read.
 *
 * Note, this function does yest check CRC of data yesdes if @c->yes_chk_data_crc
 * is true (it is controlled by corresponding mount option). However, if
 * @c->mounting or @c->remounting_rw is true (we are mounting or re-mounting to
 * R/W mode), @c->yes_chk_data_crc is igyesred and CRC is checked. This is
 * because during mounting or re-mounting from R/O mode to R/W mode we may read
 * journal yesdes (when replying the journal or doing the recovery) and the
 * journal yesdes may potentially be corrupted, so checking is required.
 */
static int try_read_yesde(const struct ubifs_info *c, void *buf, int type,
			 struct ubifs_zbranch *zbr)
{
	int len = zbr->len;
	int lnum = zbr->lnum;
	int offs = zbr->offs;
	int err, yesde_len;
	struct ubifs_ch *ch = buf;
	uint32_t crc, yesde_crc;

	dbg_io("LEB %d:%d, %s, length %d", lnum, offs, dbg_ntype(type), len);

	err = ubifs_leb_read(c, lnum, buf, offs, len, 1);
	if (err) {
		ubifs_err(c, "canyest read yesde type %d from LEB %d:%d, error %d",
			  type, lnum, offs, err);
		return err;
	}

	if (le32_to_cpu(ch->magic) != UBIFS_NODE_MAGIC)
		return 0;

	if (ch->yesde_type != type)
		return 0;

	yesde_len = le32_to_cpu(ch->len);
	if (yesde_len != len)
		return 0;

	if (type != UBIFS_DATA_NODE || !c->yes_chk_data_crc || c->mounting ||
	    c->remounting_rw) {
		crc = crc32(UBIFS_CRC32_INIT, buf + 8, yesde_len - 8);
		yesde_crc = le32_to_cpu(ch->crc);
		if (crc != yesde_crc)
			return 0;
	}

	err = ubifs_yesde_check_hash(c, buf, zbr->hash);
	if (err) {
		ubifs_bad_hash(c, buf, zbr->hash, lnum, offs);
		return 0;
	}

	return 1;
}

/**
 * fallible_read_yesde - try to read a leaf yesde.
 * @c: UBIFS file-system description object
 * @key:  key of yesde to read
 * @zbr:  position of yesde
 * @yesde: yesde returned
 *
 * This function tries to read a yesde and returns %1 if the yesde is read, %0
 * if the yesde is yest present, and a negative error code in the case of error.
 */
static int fallible_read_yesde(struct ubifs_info *c, const union ubifs_key *key,
			      struct ubifs_zbranch *zbr, void *yesde)
{
	int ret;

	dbg_tnck(key, "LEB %d:%d, key ", zbr->lnum, zbr->offs);

	ret = try_read_yesde(c, yesde, key_type(c, key), zbr);
	if (ret == 1) {
		union ubifs_key yesde_key;
		struct ubifs_dent_yesde *dent = yesde;

		/* All yesdes have key in the same place */
		key_read(c, &dent->key, &yesde_key);
		if (keys_cmp(c, key, &yesde_key) != 0)
			ret = 0;
	}
	if (ret == 0 && c->replaying)
		dbg_mntk(key, "dangling branch LEB %d:%d len %d, key ",
			zbr->lnum, zbr->offs, zbr->len);
	return ret;
}

/**
 * matches_name - determine if a direntry or xattr entry matches a given name.
 * @c: UBIFS file-system description object
 * @zbr: zbranch of dent
 * @nm: name to match
 *
 * This function checks if xentry/direntry referred by zbranch @zbr matches name
 * @nm. Returns %NAME_MATCHES if it does, %NAME_LESS if the name referred by
 * @zbr is less than @nm, and %NAME_GREATER if it is greater than @nm. In case
 * of failure, a negative error code is returned.
 */
static int matches_name(struct ubifs_info *c, struct ubifs_zbranch *zbr,
			const struct fscrypt_name *nm)
{
	struct ubifs_dent_yesde *dent;
	int nlen, err;

	/* If possible, match against the dent in the leaf yesde cache */
	if (!zbr->leaf) {
		dent = kmalloc(zbr->len, GFP_NOFS);
		if (!dent)
			return -ENOMEM;

		err = ubifs_tnc_read_yesde(c, zbr, dent);
		if (err)
			goto out_free;

		/* Add the yesde to the leaf yesde cache */
		err = lnc_add_directly(c, zbr, dent);
		if (err)
			goto out_free;
	} else
		dent = zbr->leaf;

	nlen = le16_to_cpu(dent->nlen);
	err = memcmp(dent->name, fname_name(nm), min_t(int, nlen, fname_len(nm)));
	if (err == 0) {
		if (nlen == fname_len(nm))
			return NAME_MATCHES;
		else if (nlen < fname_len(nm))
			return NAME_LESS;
		else
			return NAME_GREATER;
	} else if (err < 0)
		return NAME_LESS;
	else
		return NAME_GREATER;

out_free:
	kfree(dent);
	return err;
}

/**
 * get_zyesde - get a TNC zyesde that may yest be loaded yet.
 * @c: UBIFS file-system description object
 * @zyesde: parent zyesde
 * @n: zyesde branch slot number
 *
 * This function returns the zyesde or a negative error code.
 */
static struct ubifs_zyesde *get_zyesde(struct ubifs_info *c,
				     struct ubifs_zyesde *zyesde, int n)
{
	struct ubifs_zbranch *zbr;

	zbr = &zyesde->zbranch[n];
	if (zbr->zyesde)
		zyesde = zbr->zyesde;
	else
		zyesde = ubifs_load_zyesde(c, zbr, zyesde, n);
	return zyesde;
}

/**
 * tnc_next - find next TNC entry.
 * @c: UBIFS file-system description object
 * @zn: zyesde is passed and returned here
 * @n: zyesde branch slot number is passed and returned here
 *
 * This function returns %0 if the next TNC entry is found, %-ENOENT if there is
 * yes next entry, or a negative error code otherwise.
 */
static int tnc_next(struct ubifs_info *c, struct ubifs_zyesde **zn, int *n)
{
	struct ubifs_zyesde *zyesde = *zn;
	int nn = *n;

	nn += 1;
	if (nn < zyesde->child_cnt) {
		*n = nn;
		return 0;
	}
	while (1) {
		struct ubifs_zyesde *zp;

		zp = zyesde->parent;
		if (!zp)
			return -ENOENT;
		nn = zyesde->iip + 1;
		zyesde = zp;
		if (nn < zyesde->child_cnt) {
			zyesde = get_zyesde(c, zyesde, nn);
			if (IS_ERR(zyesde))
				return PTR_ERR(zyesde);
			while (zyesde->level != 0) {
				zyesde = get_zyesde(c, zyesde, 0);
				if (IS_ERR(zyesde))
					return PTR_ERR(zyesde);
			}
			nn = 0;
			break;
		}
	}
	*zn = zyesde;
	*n = nn;
	return 0;
}

/**
 * tnc_prev - find previous TNC entry.
 * @c: UBIFS file-system description object
 * @zn: zyesde is returned here
 * @n: zyesde branch slot number is passed and returned here
 *
 * This function returns %0 if the previous TNC entry is found, %-ENOENT if
 * there is yes next entry, or a negative error code otherwise.
 */
static int tnc_prev(struct ubifs_info *c, struct ubifs_zyesde **zn, int *n)
{
	struct ubifs_zyesde *zyesde = *zn;
	int nn = *n;

	if (nn > 0) {
		*n = nn - 1;
		return 0;
	}
	while (1) {
		struct ubifs_zyesde *zp;

		zp = zyesde->parent;
		if (!zp)
			return -ENOENT;
		nn = zyesde->iip - 1;
		zyesde = zp;
		if (nn >= 0) {
			zyesde = get_zyesde(c, zyesde, nn);
			if (IS_ERR(zyesde))
				return PTR_ERR(zyesde);
			while (zyesde->level != 0) {
				nn = zyesde->child_cnt - 1;
				zyesde = get_zyesde(c, zyesde, nn);
				if (IS_ERR(zyesde))
					return PTR_ERR(zyesde);
			}
			nn = zyesde->child_cnt - 1;
			break;
		}
	}
	*zn = zyesde;
	*n = nn;
	return 0;
}

/**
 * resolve_collision - resolve a collision.
 * @c: UBIFS file-system description object
 * @key: key of a directory or extended attribute entry
 * @zn: zyesde is returned here
 * @n: zbranch number is passed and returned here
 * @nm: name of the entry
 *
 * This function is called for "hashed" keys to make sure that the found key
 * really corresponds to the looked up yesde (directory or extended attribute
 * entry). It returns %1 and sets @zn and @n if the collision is resolved.
 * %0 is returned if @nm is yest found and @zn and @n are set to the previous
 * entry, i.e. to the entry after which @nm could follow if it were in TNC.
 * This means that @n may be set to %-1 if the leftmost key in @zn is the
 * previous one. A negative error code is returned on failures.
 */
static int resolve_collision(struct ubifs_info *c, const union ubifs_key *key,
			     struct ubifs_zyesde **zn, int *n,
			     const struct fscrypt_name *nm)
{
	int err;

	err = matches_name(c, &(*zn)->zbranch[*n], nm);
	if (unlikely(err < 0))
		return err;
	if (err == NAME_MATCHES)
		return 1;

	if (err == NAME_GREATER) {
		/* Look left */
		while (1) {
			err = tnc_prev(c, zn, n);
			if (err == -ENOENT) {
				ubifs_assert(c, *n == 0);
				*n = -1;
				return 0;
			}
			if (err < 0)
				return err;
			if (keys_cmp(c, &(*zn)->zbranch[*n].key, key)) {
				/*
				 * We have found the branch after which we would
				 * like to insert, but inserting in this zyesde
				 * may still be wrong. Consider the following 3
				 * zyesdes, in the case where we are resolving a
				 * collision with Key2.
				 *
				 *                  zyesde zp
				 *            ----------------------
				 * level 1     |  Key0  |  Key1  |
				 *            -----------------------
				 *                 |            |
				 *       zyesde za  |            |  zyesde zb
				 *          ------------      ------------
				 * level 0  |  Key0  |        |  Key2  |
				 *          ------------      ------------
				 *
				 * The lookup finds Key2 in zyesde zb. Lets say
				 * there is yes match and the name is greater so
				 * we look left. When we find Key0, we end up
				 * here. If we return yesw, we will insert into
				 * zyesde za at slot n = 1.  But that is invalid
				 * according to the parent's keys.  Key2 must
				 * be inserted into zyesde zb.
				 *
				 * Note, this problem is yest relevant for the
				 * case when we go right, because
				 * 'tnc_insert()' would correct the parent key.
				 */
				if (*n == (*zn)->child_cnt - 1) {
					err = tnc_next(c, zn, n);
					if (err) {
						/* Should be impossible */
						ubifs_assert(c, 0);
						if (err == -ENOENT)
							err = -EINVAL;
						return err;
					}
					ubifs_assert(c, *n == 0);
					*n = -1;
				}
				return 0;
			}
			err = matches_name(c, &(*zn)->zbranch[*n], nm);
			if (err < 0)
				return err;
			if (err == NAME_LESS)
				return 0;
			if (err == NAME_MATCHES)
				return 1;
			ubifs_assert(c, err == NAME_GREATER);
		}
	} else {
		int nn = *n;
		struct ubifs_zyesde *zyesde = *zn;

		/* Look right */
		while (1) {
			err = tnc_next(c, &zyesde, &nn);
			if (err == -ENOENT)
				return 0;
			if (err < 0)
				return err;
			if (keys_cmp(c, &zyesde->zbranch[nn].key, key))
				return 0;
			err = matches_name(c, &zyesde->zbranch[nn], nm);
			if (err < 0)
				return err;
			if (err == NAME_GREATER)
				return 0;
			*zn = zyesde;
			*n = nn;
			if (err == NAME_MATCHES)
				return 1;
			ubifs_assert(c, err == NAME_LESS);
		}
	}
}

/**
 * fallible_matches_name - determine if a dent matches a given name.
 * @c: UBIFS file-system description object
 * @zbr: zbranch of dent
 * @nm: name to match
 *
 * This is a "fallible" version of 'matches_name()' function which does yest
 * panic if the direntry/xentry referred by @zbr does yest exist on the media.
 *
 * This function checks if xentry/direntry referred by zbranch @zbr matches name
 * @nm. Returns %NAME_MATCHES it does, %NAME_LESS if the name referred by @zbr
 * is less than @nm, %NAME_GREATER if it is greater than @nm, and @NOT_ON_MEDIA
 * if xentry/direntry referred by @zbr does yest exist on the media. A negative
 * error code is returned in case of failure.
 */
static int fallible_matches_name(struct ubifs_info *c,
				 struct ubifs_zbranch *zbr,
				 const struct fscrypt_name *nm)
{
	struct ubifs_dent_yesde *dent;
	int nlen, err;

	/* If possible, match against the dent in the leaf yesde cache */
	if (!zbr->leaf) {
		dent = kmalloc(zbr->len, GFP_NOFS);
		if (!dent)
			return -ENOMEM;

		err = fallible_read_yesde(c, &zbr->key, zbr, dent);
		if (err < 0)
			goto out_free;
		if (err == 0) {
			/* The yesde was yest present */
			err = NOT_ON_MEDIA;
			goto out_free;
		}
		ubifs_assert(c, err == 1);

		err = lnc_add_directly(c, zbr, dent);
		if (err)
			goto out_free;
	} else
		dent = zbr->leaf;

	nlen = le16_to_cpu(dent->nlen);
	err = memcmp(dent->name, fname_name(nm), min_t(int, nlen, fname_len(nm)));
	if (err == 0) {
		if (nlen == fname_len(nm))
			return NAME_MATCHES;
		else if (nlen < fname_len(nm))
			return NAME_LESS;
		else
			return NAME_GREATER;
	} else if (err < 0)
		return NAME_LESS;
	else
		return NAME_GREATER;

out_free:
	kfree(dent);
	return err;
}

/**
 * fallible_resolve_collision - resolve a collision even if yesdes are missing.
 * @c: UBIFS file-system description object
 * @key: key
 * @zn: zyesde is returned here
 * @n: branch number is passed and returned here
 * @nm: name of directory entry
 * @adding: indicates caller is adding a key to the TNC
 *
 * This is a "fallible" version of the 'resolve_collision()' function which
 * does yest panic if one of the yesdes referred to by TNC does yest exist on the
 * media. This may happen when replaying the journal if a deleted yesde was
 * Garbage-collected and the commit was yest done. A branch that refers to a yesde
 * that is yest present is called a dangling branch. The following are the return
 * codes for this function:
 *  o if @nm was found, %1 is returned and @zn and @n are set to the found
 *    branch;
 *  o if we are @adding and @nm was yest found, %0 is returned;
 *  o if we are yest @adding and @nm was yest found, but a dangling branch was
 *    found, then %1 is returned and @zn and @n are set to the dangling branch;
 *  o a negative error code is returned in case of failure.
 */
static int fallible_resolve_collision(struct ubifs_info *c,
				      const union ubifs_key *key,
				      struct ubifs_zyesde **zn, int *n,
				      const struct fscrypt_name *nm,
				      int adding)
{
	struct ubifs_zyesde *o_zyesde = NULL, *zyesde = *zn;
	int uninitialized_var(o_n), err, cmp, unsure = 0, nn = *n;

	cmp = fallible_matches_name(c, &zyesde->zbranch[nn], nm);
	if (unlikely(cmp < 0))
		return cmp;
	if (cmp == NAME_MATCHES)
		return 1;
	if (cmp == NOT_ON_MEDIA) {
		o_zyesde = zyesde;
		o_n = nn;
		/*
		 * We are unlucky and hit a dangling branch straight away.
		 * Now we do yest really kyesw where to go to find the needed
		 * branch - to the left or to the right. Well, let's try left.
		 */
		unsure = 1;
	} else if (!adding)
		unsure = 1; /* Remove a dangling branch wherever it is */

	if (cmp == NAME_GREATER || unsure) {
		/* Look left */
		while (1) {
			err = tnc_prev(c, zn, n);
			if (err == -ENOENT) {
				ubifs_assert(c, *n == 0);
				*n = -1;
				break;
			}
			if (err < 0)
				return err;
			if (keys_cmp(c, &(*zn)->zbranch[*n].key, key)) {
				/* See comments in 'resolve_collision()' */
				if (*n == (*zn)->child_cnt - 1) {
					err = tnc_next(c, zn, n);
					if (err) {
						/* Should be impossible */
						ubifs_assert(c, 0);
						if (err == -ENOENT)
							err = -EINVAL;
						return err;
					}
					ubifs_assert(c, *n == 0);
					*n = -1;
				}
				break;
			}
			err = fallible_matches_name(c, &(*zn)->zbranch[*n], nm);
			if (err < 0)
				return err;
			if (err == NAME_MATCHES)
				return 1;
			if (err == NOT_ON_MEDIA) {
				o_zyesde = *zn;
				o_n = *n;
				continue;
			}
			if (!adding)
				continue;
			if (err == NAME_LESS)
				break;
			else
				unsure = 0;
		}
	}

	if (cmp == NAME_LESS || unsure) {
		/* Look right */
		*zn = zyesde;
		*n = nn;
		while (1) {
			err = tnc_next(c, &zyesde, &nn);
			if (err == -ENOENT)
				break;
			if (err < 0)
				return err;
			if (keys_cmp(c, &zyesde->zbranch[nn].key, key))
				break;
			err = fallible_matches_name(c, &zyesde->zbranch[nn], nm);
			if (err < 0)
				return err;
			if (err == NAME_GREATER)
				break;
			*zn = zyesde;
			*n = nn;
			if (err == NAME_MATCHES)
				return 1;
			if (err == NOT_ON_MEDIA) {
				o_zyesde = zyesde;
				o_n = nn;
			}
		}
	}

	/* Never match a dangling branch when adding */
	if (adding || !o_zyesde)
		return 0;

	dbg_mntk(key, "dangling match LEB %d:%d len %d key ",
		o_zyesde->zbranch[o_n].lnum, o_zyesde->zbranch[o_n].offs,
		o_zyesde->zbranch[o_n].len);
	*zn = o_zyesde;
	*n = o_n;
	return 1;
}

/**
 * matches_position - determine if a zbranch matches a given position.
 * @zbr: zbranch of dent
 * @lnum: LEB number of dent to match
 * @offs: offset of dent to match
 *
 * This function returns %1 if @lnum:@offs matches, and %0 otherwise.
 */
static int matches_position(struct ubifs_zbranch *zbr, int lnum, int offs)
{
	if (zbr->lnum == lnum && zbr->offs == offs)
		return 1;
	else
		return 0;
}

/**
 * resolve_collision_directly - resolve a collision directly.
 * @c: UBIFS file-system description object
 * @key: key of directory entry
 * @zn: zyesde is passed and returned here
 * @n: zbranch number is passed and returned here
 * @lnum: LEB number of dent yesde to match
 * @offs: offset of dent yesde to match
 *
 * This function is used for "hashed" keys to make sure the found directory or
 * extended attribute entry yesde is what was looked for. It is used when the
 * flash address of the right yesde is kyeswn (@lnum:@offs) which makes it much
 * easier to resolve collisions (yes need to read entries and match full
 * names). This function returns %1 and sets @zn and @n if the collision is
 * resolved, %0 if @lnum:@offs is yest found and @zn and @n are set to the
 * previous directory entry. Otherwise a negative error code is returned.
 */
static int resolve_collision_directly(struct ubifs_info *c,
				      const union ubifs_key *key,
				      struct ubifs_zyesde **zn, int *n,
				      int lnum, int offs)
{
	struct ubifs_zyesde *zyesde;
	int nn, err;

	zyesde = *zn;
	nn = *n;
	if (matches_position(&zyesde->zbranch[nn], lnum, offs))
		return 1;

	/* Look left */
	while (1) {
		err = tnc_prev(c, &zyesde, &nn);
		if (err == -ENOENT)
			break;
		if (err < 0)
			return err;
		if (keys_cmp(c, &zyesde->zbranch[nn].key, key))
			break;
		if (matches_position(&zyesde->zbranch[nn], lnum, offs)) {
			*zn = zyesde;
			*n = nn;
			return 1;
		}
	}

	/* Look right */
	zyesde = *zn;
	nn = *n;
	while (1) {
		err = tnc_next(c, &zyesde, &nn);
		if (err == -ENOENT)
			return 0;
		if (err < 0)
			return err;
		if (keys_cmp(c, &zyesde->zbranch[nn].key, key))
			return 0;
		*zn = zyesde;
		*n = nn;
		if (matches_position(&zyesde->zbranch[nn], lnum, offs))
			return 1;
	}
}

/**
 * dirty_cow_bottom_up - dirty a zyesde and its ancestors.
 * @c: UBIFS file-system description object
 * @zyesde: zyesde to dirty
 *
 * If we do yest have a unique key that resides in a zyesde, then we canyest
 * dirty that zyesde from the top down (i.e. by using lookup_level0_dirty)
 * This function records the path back to the last dirty ancestor, and then
 * dirties the zyesdes on that path.
 */
static struct ubifs_zyesde *dirty_cow_bottom_up(struct ubifs_info *c,
					       struct ubifs_zyesde *zyesde)
{
	struct ubifs_zyesde *zp;
	int *path = c->bottom_up_buf, p = 0;

	ubifs_assert(c, c->zroot.zyesde);
	ubifs_assert(c, zyesde);
	if (c->zroot.zyesde->level > BOTTOM_UP_HEIGHT) {
		kfree(c->bottom_up_buf);
		c->bottom_up_buf = kmalloc_array(c->zroot.zyesde->level,
						 sizeof(int),
						 GFP_NOFS);
		if (!c->bottom_up_buf)
			return ERR_PTR(-ENOMEM);
		path = c->bottom_up_buf;
	}
	if (c->zroot.zyesde->level) {
		/* Go up until parent is dirty */
		while (1) {
			int n;

			zp = zyesde->parent;
			if (!zp)
				break;
			n = zyesde->iip;
			ubifs_assert(c, p < c->zroot.zyesde->level);
			path[p++] = n;
			if (!zp->cnext && ubifs_zn_dirty(zyesde))
				break;
			zyesde = zp;
		}
	}

	/* Come back down, dirtying as we go */
	while (1) {
		struct ubifs_zbranch *zbr;

		zp = zyesde->parent;
		if (zp) {
			ubifs_assert(c, path[p - 1] >= 0);
			ubifs_assert(c, path[p - 1] < zp->child_cnt);
			zbr = &zp->zbranch[path[--p]];
			zyesde = dirty_cow_zyesde(c, zbr);
		} else {
			ubifs_assert(c, zyesde == c->zroot.zyesde);
			zyesde = dirty_cow_zyesde(c, &c->zroot);
		}
		if (IS_ERR(zyesde) || !p)
			break;
		ubifs_assert(c, path[p - 1] >= 0);
		ubifs_assert(c, path[p - 1] < zyesde->child_cnt);
		zyesde = zyesde->zbranch[path[p - 1]].zyesde;
	}

	return zyesde;
}

/**
 * ubifs_lookup_level0 - search for zero-level zyesde.
 * @c: UBIFS file-system description object
 * @key:  key to lookup
 * @zn: zyesde is returned here
 * @n: zyesde branch slot number is returned here
 *
 * This function looks up the TNC tree and search for zero-level zyesde which
 * refers key @key. The found zero-level zyesde is returned in @zn. There are 3
 * cases:
 *   o exact match, i.e. the found zero-level zyesde contains key @key, then %1
 *     is returned and slot number of the matched branch is stored in @n;
 *   o yest exact match, which means that zero-level zyesde does yest contain
 *     @key, then %0 is returned and slot number of the closest branch or %-1
 *     is stored in @n; In this case calling tnc_next() is mandatory.
 *   o @key is so small that it is even less than the lowest key of the
 *     leftmost zero-level yesde, then %0 is returned and %0 is stored in @n.
 *
 * Note, when the TNC tree is traversed, some zyesdes may be absent, then this
 * function reads corresponding indexing yesdes and inserts them to TNC. In
 * case of failure, a negative error code is returned.
 */
int ubifs_lookup_level0(struct ubifs_info *c, const union ubifs_key *key,
			struct ubifs_zyesde **zn, int *n)
{
	int err, exact;
	struct ubifs_zyesde *zyesde;
	time64_t time = ktime_get_seconds();

	dbg_tnck(key, "search key ");
	ubifs_assert(c, key_type(c, key) < UBIFS_INVALID_KEY);

	zyesde = c->zroot.zyesde;
	if (unlikely(!zyesde)) {
		zyesde = ubifs_load_zyesde(c, &c->zroot, NULL, 0);
		if (IS_ERR(zyesde))
			return PTR_ERR(zyesde);
	}

	zyesde->time = time;

	while (1) {
		struct ubifs_zbranch *zbr;

		exact = ubifs_search_zbranch(c, zyesde, key, n);

		if (zyesde->level == 0)
			break;

		if (*n < 0)
			*n = 0;
		zbr = &zyesde->zbranch[*n];

		if (zbr->zyesde) {
			zyesde->time = time;
			zyesde = zbr->zyesde;
			continue;
		}

		/* zyesde is yest in TNC cache, load it from the media */
		zyesde = ubifs_load_zyesde(c, zbr, zyesde, *n);
		if (IS_ERR(zyesde))
			return PTR_ERR(zyesde);
	}

	*zn = zyesde;
	if (exact || !is_hash_key(c, key) || *n != -1) {
		dbg_tnc("found %d, lvl %d, n %d", exact, zyesde->level, *n);
		return exact;
	}

	/*
	 * Here is a tricky place. We have yest found the key and this is a
	 * "hashed" key, which may collide. The rest of the code deals with
	 * situations like this:
	 *
	 *                  | 3 | 5 |
	 *                  /       \
	 *          | 3 | 5 |      | 6 | 7 | (x)
	 *
	 * Or more a complex example:
	 *
	 *                | 1 | 5 |
	 *                /       \
	 *       | 1 | 3 |         | 5 | 8 |
	 *              \           /
	 *          | 5 | 5 |   | 6 | 7 | (x)
	 *
	 * In the examples, if we are looking for key "5", we may reach yesdes
	 * marked with "(x)". In this case what we have do is to look at the
	 * left and see if there is "5" key there. If there is, we have to
	 * return it.
	 *
	 * Note, this whole situation is possible because we allow to have
	 * elements which are equivalent to the next key in the parent in the
	 * children of current zyesde. For example, this happens if we split a
	 * zyesde like this: | 3 | 5 | 5 | 6 | 7 |, which results in something
	 * like this:
	 *                      | 3 | 5 |
	 *                       /     \
	 *                | 3 | 5 |   | 5 | 6 | 7 |
	 *                              ^
	 * And this becomes what is at the first "picture" after key "5" marked
	 * with "^" is removed. What could be done is we could prohibit
	 * splitting in the middle of the colliding sequence. Also, when
	 * removing the leftmost key, we would have to correct the key of the
	 * parent yesde, which would introduce additional complications. Namely,
	 * if we changed the leftmost key of the parent zyesde, the garbage
	 * collector would be unable to find it (GC is doing this when GC'ing
	 * indexing LEBs). Although we already have an additional RB-tree where
	 * we save such changed zyesdes (see 'ins_clr_old_idx_zyesde()') until
	 * after the commit. But anyway, this does yest look easy to implement
	 * so we did yest try this.
	 */
	err = tnc_prev(c, &zyesde, n);
	if (err == -ENOENT) {
		dbg_tnc("found 0, lvl %d, n -1", zyesde->level);
		*n = -1;
		return 0;
	}
	if (unlikely(err < 0))
		return err;
	if (keys_cmp(c, key, &zyesde->zbranch[*n].key)) {
		dbg_tnc("found 0, lvl %d, n -1", zyesde->level);
		*n = -1;
		return 0;
	}

	dbg_tnc("found 1, lvl %d, n %d", zyesde->level, *n);
	*zn = zyesde;
	return 1;
}

/**
 * lookup_level0_dirty - search for zero-level zyesde dirtying.
 * @c: UBIFS file-system description object
 * @key:  key to lookup
 * @zn: zyesde is returned here
 * @n: zyesde branch slot number is returned here
 *
 * This function looks up the TNC tree and search for zero-level zyesde which
 * refers key @key. The found zero-level zyesde is returned in @zn. There are 3
 * cases:
 *   o exact match, i.e. the found zero-level zyesde contains key @key, then %1
 *     is returned and slot number of the matched branch is stored in @n;
 *   o yest exact match, which means that zero-level zyesde does yest contain @key
 *     then %0 is returned and slot number of the closed branch is stored in
 *     @n;
 *   o @key is so small that it is even less than the lowest key of the
 *     leftmost zero-level yesde, then %0 is returned and %-1 is stored in @n.
 *
 * Additionally all zyesdes in the path from the root to the located zero-level
 * zyesde are marked as dirty.
 *
 * Note, when the TNC tree is traversed, some zyesdes may be absent, then this
 * function reads corresponding indexing yesdes and inserts them to TNC. In
 * case of failure, a negative error code is returned.
 */
static int lookup_level0_dirty(struct ubifs_info *c, const union ubifs_key *key,
			       struct ubifs_zyesde **zn, int *n)
{
	int err, exact;
	struct ubifs_zyesde *zyesde;
	time64_t time = ktime_get_seconds();

	dbg_tnck(key, "search and dirty key ");

	zyesde = c->zroot.zyesde;
	if (unlikely(!zyesde)) {
		zyesde = ubifs_load_zyesde(c, &c->zroot, NULL, 0);
		if (IS_ERR(zyesde))
			return PTR_ERR(zyesde);
	}

	zyesde = dirty_cow_zyesde(c, &c->zroot);
	if (IS_ERR(zyesde))
		return PTR_ERR(zyesde);

	zyesde->time = time;

	while (1) {
		struct ubifs_zbranch *zbr;

		exact = ubifs_search_zbranch(c, zyesde, key, n);

		if (zyesde->level == 0)
			break;

		if (*n < 0)
			*n = 0;
		zbr = &zyesde->zbranch[*n];

		if (zbr->zyesde) {
			zyesde->time = time;
			zyesde = dirty_cow_zyesde(c, zbr);
			if (IS_ERR(zyesde))
				return PTR_ERR(zyesde);
			continue;
		}

		/* zyesde is yest in TNC cache, load it from the media */
		zyesde = ubifs_load_zyesde(c, zbr, zyesde, *n);
		if (IS_ERR(zyesde))
			return PTR_ERR(zyesde);
		zyesde = dirty_cow_zyesde(c, zbr);
		if (IS_ERR(zyesde))
			return PTR_ERR(zyesde);
	}

	*zn = zyesde;
	if (exact || !is_hash_key(c, key) || *n != -1) {
		dbg_tnc("found %d, lvl %d, n %d", exact, zyesde->level, *n);
		return exact;
	}

	/*
	 * See huge comment at 'lookup_level0_dirty()' what is the rest of the
	 * code.
	 */
	err = tnc_prev(c, &zyesde, n);
	if (err == -ENOENT) {
		*n = -1;
		dbg_tnc("found 0, lvl %d, n -1", zyesde->level);
		return 0;
	}
	if (unlikely(err < 0))
		return err;
	if (keys_cmp(c, key, &zyesde->zbranch[*n].key)) {
		*n = -1;
		dbg_tnc("found 0, lvl %d, n -1", zyesde->level);
		return 0;
	}

	if (zyesde->cnext || !ubifs_zn_dirty(zyesde)) {
		zyesde = dirty_cow_bottom_up(c, zyesde);
		if (IS_ERR(zyesde))
			return PTR_ERR(zyesde);
	}

	dbg_tnc("found 1, lvl %d, n %d", zyesde->level, *n);
	*zn = zyesde;
	return 1;
}

/**
 * maybe_leb_gced - determine if a LEB may have been garbage collected.
 * @c: UBIFS file-system description object
 * @lnum: LEB number
 * @gc_seq1: garbage collection sequence number
 *
 * This function determines if @lnum may have been garbage collected since
 * sequence number @gc_seq1. If it may have been then %1 is returned, otherwise
 * %0 is returned.
 */
static int maybe_leb_gced(struct ubifs_info *c, int lnum, int gc_seq1)
{
	int gc_seq2, gced_lnum;

	gced_lnum = c->gced_lnum;
	smp_rmb();
	gc_seq2 = c->gc_seq;
	/* Same seq means yes GC */
	if (gc_seq1 == gc_seq2)
		return 0;
	/* Different by more than 1 means we don't kyesw */
	if (gc_seq1 + 1 != gc_seq2)
		return 1;
	/*
	 * We have seen the sequence number has increased by 1. Now we need to
	 * be sure we read the right LEB number, so read it again.
	 */
	smp_rmb();
	if (gced_lnum != c->gced_lnum)
		return 1;
	/* Finally we can check lnum */
	if (gced_lnum == lnum)
		return 1;
	return 0;
}

/**
 * ubifs_tnc_locate - look up a file-system yesde and return it and its location.
 * @c: UBIFS file-system description object
 * @key: yesde key to lookup
 * @yesde: the yesde is returned here
 * @lnum: LEB number is returned here
 * @offs: offset is returned here
 *
 * This function looks up and reads yesde with key @key. The caller has to make
 * sure the @yesde buffer is large eyesugh to fit the yesde. Returns zero in case
 * of success, %-ENOENT if the yesde was yest found, and a negative error code in
 * case of failure. The yesde location can be returned in @lnum and @offs.
 */
int ubifs_tnc_locate(struct ubifs_info *c, const union ubifs_key *key,
		     void *yesde, int *lnum, int *offs)
{
	int found, n, err, safely = 0, gc_seq1;
	struct ubifs_zyesde *zyesde;
	struct ubifs_zbranch zbr, *zt;

again:
	mutex_lock(&c->tnc_mutex);
	found = ubifs_lookup_level0(c, key, &zyesde, &n);
	if (!found) {
		err = -ENOENT;
		goto out;
	} else if (found < 0) {
		err = found;
		goto out;
	}
	zt = &zyesde->zbranch[n];
	if (lnum) {
		*lnum = zt->lnum;
		*offs = zt->offs;
	}
	if (is_hash_key(c, key)) {
		/*
		 * In this case the leaf yesde cache gets used, so we pass the
		 * address of the zbranch and keep the mutex locked
		 */
		err = tnc_read_hashed_yesde(c, zt, yesde);
		goto out;
	}
	if (safely) {
		err = ubifs_tnc_read_yesde(c, zt, yesde);
		goto out;
	}
	/* Drop the TNC mutex prematurely and race with garbage collection */
	zbr = zyesde->zbranch[n];
	gc_seq1 = c->gc_seq;
	mutex_unlock(&c->tnc_mutex);

	if (ubifs_get_wbuf(c, zbr.lnum)) {
		/* We do yest GC journal heads */
		err = ubifs_tnc_read_yesde(c, &zbr, yesde);
		return err;
	}

	err = fallible_read_yesde(c, key, &zbr, yesde);
	if (err <= 0 || maybe_leb_gced(c, zbr.lnum, gc_seq1)) {
		/*
		 * The yesde may have been GC'ed out from under us so try again
		 * while keeping the TNC mutex locked.
		 */
		safely = 1;
		goto again;
	}
	return 0;

out:
	mutex_unlock(&c->tnc_mutex);
	return err;
}

/**
 * ubifs_tnc_get_bu_keys - lookup keys for bulk-read.
 * @c: UBIFS file-system description object
 * @bu: bulk-read parameters and results
 *
 * Lookup consecutive data yesde keys for the same iyesde that reside
 * consecutively in the same LEB. This function returns zero in case of success
 * and a negative error code in case of failure.
 *
 * Note, if the bulk-read buffer length (@bu->buf_len) is kyeswn, this function
 * makes sure bulk-read yesdes fit the buffer. Otherwise, this function prepares
 * maximum possible amount of yesdes for bulk-read.
 */
int ubifs_tnc_get_bu_keys(struct ubifs_info *c, struct bu_info *bu)
{
	int n, err = 0, lnum = -1, uninitialized_var(offs);
	int uninitialized_var(len);
	unsigned int block = key_block(c, &bu->key);
	struct ubifs_zyesde *zyesde;

	bu->cnt = 0;
	bu->blk_cnt = 0;
	bu->eof = 0;

	mutex_lock(&c->tnc_mutex);
	/* Find first key */
	err = ubifs_lookup_level0(c, &bu->key, &zyesde, &n);
	if (err < 0)
		goto out;
	if (err) {
		/* Key found */
		len = zyesde->zbranch[n].len;
		/* The buffer must be big eyesugh for at least 1 yesde */
		if (len > bu->buf_len) {
			err = -EINVAL;
			goto out;
		}
		/* Add this key */
		bu->zbranch[bu->cnt++] = zyesde->zbranch[n];
		bu->blk_cnt += 1;
		lnum = zyesde->zbranch[n].lnum;
		offs = ALIGN(zyesde->zbranch[n].offs + len, 8);
	}
	while (1) {
		struct ubifs_zbranch *zbr;
		union ubifs_key *key;
		unsigned int next_block;

		/* Find next key */
		err = tnc_next(c, &zyesde, &n);
		if (err)
			goto out;
		zbr = &zyesde->zbranch[n];
		key = &zbr->key;
		/* See if there is ayesther data key for this file */
		if (key_inum(c, key) != key_inum(c, &bu->key) ||
		    key_type(c, key) != UBIFS_DATA_KEY) {
			err = -ENOENT;
			goto out;
		}
		if (lnum < 0) {
			/* First key found */
			lnum = zbr->lnum;
			offs = ALIGN(zbr->offs + zbr->len, 8);
			len = zbr->len;
			if (len > bu->buf_len) {
				err = -EINVAL;
				goto out;
			}
		} else {
			/*
			 * The data yesdes must be in consecutive positions in
			 * the same LEB.
			 */
			if (zbr->lnum != lnum || zbr->offs != offs)
				goto out;
			offs += ALIGN(zbr->len, 8);
			len = ALIGN(len, 8) + zbr->len;
			/* Must yest exceed buffer length */
			if (len > bu->buf_len)
				goto out;
		}
		/* Allow for holes */
		next_block = key_block(c, key);
		bu->blk_cnt += (next_block - block - 1);
		if (bu->blk_cnt >= UBIFS_MAX_BULK_READ)
			goto out;
		block = next_block;
		/* Add this key */
		bu->zbranch[bu->cnt++] = *zbr;
		bu->blk_cnt += 1;
		/* See if we have room for more */
		if (bu->cnt >= UBIFS_MAX_BULK_READ)
			goto out;
		if (bu->blk_cnt >= UBIFS_MAX_BULK_READ)
			goto out;
	}
out:
	if (err == -ENOENT) {
		bu->eof = 1;
		err = 0;
	}
	bu->gc_seq = c->gc_seq;
	mutex_unlock(&c->tnc_mutex);
	if (err)
		return err;
	/*
	 * An eyesrmous hole could cause bulk-read to encompass too many
	 * page cache pages, so limit the number here.
	 */
	if (bu->blk_cnt > UBIFS_MAX_BULK_READ)
		bu->blk_cnt = UBIFS_MAX_BULK_READ;
	/*
	 * Ensure that bulk-read covers a whole number of page cache
	 * pages.
	 */
	if (UBIFS_BLOCKS_PER_PAGE == 1 ||
	    !(bu->blk_cnt & (UBIFS_BLOCKS_PER_PAGE - 1)))
		return 0;
	if (bu->eof) {
		/* At the end of file we can round up */
		bu->blk_cnt += UBIFS_BLOCKS_PER_PAGE - 1;
		return 0;
	}
	/* Exclude data yesdes that do yest make up a whole page cache page */
	block = key_block(c, &bu->key) + bu->blk_cnt;
	block &= ~(UBIFS_BLOCKS_PER_PAGE - 1);
	while (bu->cnt) {
		if (key_block(c, &bu->zbranch[bu->cnt - 1].key) < block)
			break;
		bu->cnt -= 1;
	}
	return 0;
}

/**
 * read_wbuf - bulk-read from a LEB with a wbuf.
 * @wbuf: wbuf that may overlap the read
 * @buf: buffer into which to read
 * @len: read length
 * @lnum: LEB number from which to read
 * @offs: offset from which to read
 *
 * This functions returns %0 on success or a negative error code on failure.
 */
static int read_wbuf(struct ubifs_wbuf *wbuf, void *buf, int len, int lnum,
		     int offs)
{
	const struct ubifs_info *c = wbuf->c;
	int rlen, overlap;

	dbg_io("LEB %d:%d, length %d", lnum, offs, len);
	ubifs_assert(c, wbuf && lnum >= 0 && lnum < c->leb_cnt && offs >= 0);
	ubifs_assert(c, !(offs & 7) && offs < c->leb_size);
	ubifs_assert(c, offs + len <= c->leb_size);

	spin_lock(&wbuf->lock);
	overlap = (lnum == wbuf->lnum && offs + len > wbuf->offs);
	if (!overlap) {
		/* We may safely unlock the write-buffer and read the data */
		spin_unlock(&wbuf->lock);
		return ubifs_leb_read(c, lnum, buf, offs, len, 0);
	}

	/* Don't read under wbuf */
	rlen = wbuf->offs - offs;
	if (rlen < 0)
		rlen = 0;

	/* Copy the rest from the write-buffer */
	memcpy(buf + rlen, wbuf->buf + offs + rlen - wbuf->offs, len - rlen);
	spin_unlock(&wbuf->lock);

	if (rlen > 0)
		/* Read everything that goes before write-buffer */
		return ubifs_leb_read(c, lnum, buf, offs, rlen, 0);

	return 0;
}

/**
 * validate_data_yesde - validate data yesdes for bulk-read.
 * @c: UBIFS file-system description object
 * @buf: buffer containing data yesde to validate
 * @zbr: zbranch of data yesde to validate
 *
 * This functions returns %0 on success or a negative error code on failure.
 */
static int validate_data_yesde(struct ubifs_info *c, void *buf,
			      struct ubifs_zbranch *zbr)
{
	union ubifs_key key1;
	struct ubifs_ch *ch = buf;
	int err, len;

	if (ch->yesde_type != UBIFS_DATA_NODE) {
		ubifs_err(c, "bad yesde type (%d but expected %d)",
			  ch->yesde_type, UBIFS_DATA_NODE);
		goto out_err;
	}

	err = ubifs_check_yesde(c, buf, zbr->lnum, zbr->offs, 0, 0);
	if (err) {
		ubifs_err(c, "expected yesde type %d", UBIFS_DATA_NODE);
		goto out;
	}

	err = ubifs_yesde_check_hash(c, buf, zbr->hash);
	if (err) {
		ubifs_bad_hash(c, buf, zbr->hash, zbr->lnum, zbr->offs);
		return err;
	}

	len = le32_to_cpu(ch->len);
	if (len != zbr->len) {
		ubifs_err(c, "bad yesde length %d, expected %d", len, zbr->len);
		goto out_err;
	}

	/* Make sure the key of the read yesde is correct */
	key_read(c, buf + UBIFS_KEY_OFFSET, &key1);
	if (!keys_eq(c, &zbr->key, &key1)) {
		ubifs_err(c, "bad key in yesde at LEB %d:%d",
			  zbr->lnum, zbr->offs);
		dbg_tnck(&zbr->key, "looked for key ");
		dbg_tnck(&key1, "found yesde's key ");
		goto out_err;
	}

	return 0;

out_err:
	err = -EINVAL;
out:
	ubifs_err(c, "bad yesde at LEB %d:%d", zbr->lnum, zbr->offs);
	ubifs_dump_yesde(c, buf);
	dump_stack();
	return err;
}

/**
 * ubifs_tnc_bulk_read - read a number of data yesdes in one go.
 * @c: UBIFS file-system description object
 * @bu: bulk-read parameters and results
 *
 * This functions reads and validates the data yesdes that were identified by the
 * 'ubifs_tnc_get_bu_keys()' function. This functions returns %0 on success,
 * -EAGAIN to indicate a race with GC, or ayesther negative error code on
 * failure.
 */
int ubifs_tnc_bulk_read(struct ubifs_info *c, struct bu_info *bu)
{
	int lnum = bu->zbranch[0].lnum, offs = bu->zbranch[0].offs, len, err, i;
	struct ubifs_wbuf *wbuf;
	void *buf;

	len = bu->zbranch[bu->cnt - 1].offs;
	len += bu->zbranch[bu->cnt - 1].len - offs;
	if (len > bu->buf_len) {
		ubifs_err(c, "buffer too small %d vs %d", bu->buf_len, len);
		return -EINVAL;
	}

	/* Do the read */
	wbuf = ubifs_get_wbuf(c, lnum);
	if (wbuf)
		err = read_wbuf(wbuf, bu->buf, len, lnum, offs);
	else
		err = ubifs_leb_read(c, lnum, bu->buf, offs, len, 0);

	/* Check for a race with GC */
	if (maybe_leb_gced(c, lnum, bu->gc_seq))
		return -EAGAIN;

	if (err && err != -EBADMSG) {
		ubifs_err(c, "failed to read from LEB %d:%d, error %d",
			  lnum, offs, err);
		dump_stack();
		dbg_tnck(&bu->key, "key ");
		return err;
	}

	/* Validate the yesdes read */
	buf = bu->buf;
	for (i = 0; i < bu->cnt; i++) {
		err = validate_data_yesde(c, buf, &bu->zbranch[i]);
		if (err)
			return err;
		buf = buf + ALIGN(bu->zbranch[i].len, 8);
	}

	return 0;
}

/**
 * do_lookup_nm- look up a "hashed" yesde.
 * @c: UBIFS file-system description object
 * @key: yesde key to lookup
 * @yesde: the yesde is returned here
 * @nm: yesde name
 *
 * This function looks up and reads a yesde which contains name hash in the key.
 * Since the hash may have collisions, there may be many yesdes with the same
 * key, so we have to sequentially look to all of them until the needed one is
 * found. This function returns zero in case of success, %-ENOENT if the yesde
 * was yest found, and a negative error code in case of failure.
 */
static int do_lookup_nm(struct ubifs_info *c, const union ubifs_key *key,
			void *yesde, const struct fscrypt_name *nm)
{
	int found, n, err;
	struct ubifs_zyesde *zyesde;

	dbg_tnck(key, "key ");
	mutex_lock(&c->tnc_mutex);
	found = ubifs_lookup_level0(c, key, &zyesde, &n);
	if (!found) {
		err = -ENOENT;
		goto out_unlock;
	} else if (found < 0) {
		err = found;
		goto out_unlock;
	}

	ubifs_assert(c, n >= 0);

	err = resolve_collision(c, key, &zyesde, &n, nm);
	dbg_tnc("rc returned %d, zyesde %p, n %d", err, zyesde, n);
	if (unlikely(err < 0))
		goto out_unlock;
	if (err == 0) {
		err = -ENOENT;
		goto out_unlock;
	}

	err = tnc_read_hashed_yesde(c, &zyesde->zbranch[n], yesde);

out_unlock:
	mutex_unlock(&c->tnc_mutex);
	return err;
}

/**
 * ubifs_tnc_lookup_nm - look up a "hashed" yesde.
 * @c: UBIFS file-system description object
 * @key: yesde key to lookup
 * @yesde: the yesde is returned here
 * @nm: yesde name
 *
 * This function looks up and reads a yesde which contains name hash in the key.
 * Since the hash may have collisions, there may be many yesdes with the same
 * key, so we have to sequentially look to all of them until the needed one is
 * found. This function returns zero in case of success, %-ENOENT if the yesde
 * was yest found, and a negative error code in case of failure.
 */
int ubifs_tnc_lookup_nm(struct ubifs_info *c, const union ubifs_key *key,
			void *yesde, const struct fscrypt_name *nm)
{
	int err, len;
	const struct ubifs_dent_yesde *dent = yesde;

	/*
	 * We assume that in most of the cases there are yes name collisions and
	 * 'ubifs_tnc_lookup()' returns us the right direntry.
	 */
	err = ubifs_tnc_lookup(c, key, yesde);
	if (err)
		return err;

	len = le16_to_cpu(dent->nlen);
	if (fname_len(nm) == len && !memcmp(dent->name, fname_name(nm), len))
		return 0;

	/*
	 * Unluckily, there are hash collisions and we have to iterate over
	 * them look at each direntry with colliding name hash sequentially.
	 */

	return do_lookup_nm(c, key, yesde, nm);
}

static int search_dh_cookie(struct ubifs_info *c, const union ubifs_key *key,
			    struct ubifs_dent_yesde *dent, uint32_t cookie,
			    struct ubifs_zyesde **zn, int *n, int exact)
{
	int err;
	struct ubifs_zyesde *zyesde = *zn;
	struct ubifs_zbranch *zbr;
	union ubifs_key *dkey;

	if (!exact) {
		err = tnc_next(c, &zyesde, n);
		if (err)
			return err;
	}

	for (;;) {
		zbr = &zyesde->zbranch[*n];
		dkey = &zbr->key;

		if (key_inum(c, dkey) != key_inum(c, key) ||
		    key_type(c, dkey) != key_type(c, key)) {
			return -ENOENT;
		}

		err = tnc_read_hashed_yesde(c, zbr, dent);
		if (err)
			return err;

		if (key_hash(c, key) == key_hash(c, dkey) &&
		    le32_to_cpu(dent->cookie) == cookie) {
			*zn = zyesde;
			return 0;
		}

		err = tnc_next(c, &zyesde, n);
		if (err)
			return err;
	}
}

static int do_lookup_dh(struct ubifs_info *c, const union ubifs_key *key,
			struct ubifs_dent_yesde *dent, uint32_t cookie)
{
	int n, err;
	struct ubifs_zyesde *zyesde;
	union ubifs_key start_key;

	ubifs_assert(c, is_hash_key(c, key));

	lowest_dent_key(c, &start_key, key_inum(c, key));

	mutex_lock(&c->tnc_mutex);
	err = ubifs_lookup_level0(c, &start_key, &zyesde, &n);
	if (unlikely(err < 0))
		goto out_unlock;

	err = search_dh_cookie(c, key, dent, cookie, &zyesde, &n, err);

out_unlock:
	mutex_unlock(&c->tnc_mutex);
	return err;
}

/**
 * ubifs_tnc_lookup_dh - look up a "double hashed" yesde.
 * @c: UBIFS file-system description object
 * @key: yesde key to lookup
 * @yesde: the yesde is returned here
 * @cookie: yesde cookie for collision resolution
 *
 * This function looks up and reads a yesde which contains name hash in the key.
 * Since the hash may have collisions, there may be many yesdes with the same
 * key, so we have to sequentially look to all of them until the needed one
 * with the same cookie value is found.
 * This function returns zero in case of success, %-ENOENT if the yesde
 * was yest found, and a negative error code in case of failure.
 */
int ubifs_tnc_lookup_dh(struct ubifs_info *c, const union ubifs_key *key,
			void *yesde, uint32_t cookie)
{
	int err;
	const struct ubifs_dent_yesde *dent = yesde;

	if (!c->double_hash)
		return -EOPNOTSUPP;

	/*
	 * We assume that in most of the cases there are yes name collisions and
	 * 'ubifs_tnc_lookup()' returns us the right direntry.
	 */
	err = ubifs_tnc_lookup(c, key, yesde);
	if (err)
		return err;

	if (le32_to_cpu(dent->cookie) == cookie)
		return 0;

	/*
	 * Unluckily, there are hash collisions and we have to iterate over
	 * them look at each direntry with colliding name hash sequentially.
	 */
	return do_lookup_dh(c, key, yesde, cookie);
}

/**
 * correct_parent_keys - correct parent zyesdes' keys.
 * @c: UBIFS file-system description object
 * @zyesde: zyesde to correct parent zyesdes for
 *
 * This is a helper function for 'tnc_insert()'. When the key of the leftmost
 * zbranch changes, keys of parent zyesdes have to be corrected. This helper
 * function is called in such situations and corrects the keys if needed.
 */
static void correct_parent_keys(const struct ubifs_info *c,
				struct ubifs_zyesde *zyesde)
{
	union ubifs_key *key, *key1;

	ubifs_assert(c, zyesde->parent);
	ubifs_assert(c, zyesde->iip == 0);

	key = &zyesde->zbranch[0].key;
	key1 = &zyesde->parent->zbranch[0].key;

	while (keys_cmp(c, key, key1) < 0) {
		key_copy(c, key, key1);
		zyesde = zyesde->parent;
		zyesde->alt = 1;
		if (!zyesde->parent || zyesde->iip)
			break;
		key1 = &zyesde->parent->zbranch[0].key;
	}
}

/**
 * insert_zbranch - insert a zbranch into a zyesde.
 * @c: UBIFS file-system description object
 * @zyesde: zyesde into which to insert
 * @zbr: zbranch to insert
 * @n: slot number to insert to
 *
 * This is a helper function for 'tnc_insert()'. UBIFS does yest allow "gaps" in
 * zyesde's array of zbranches and keeps zbranches consolidated, so when a new
 * zbranch has to be inserted to the @zyesde->zbranches[]' array at the @n-th
 * slot, zbranches starting from @n have to be moved right.
 */
static void insert_zbranch(struct ubifs_info *c, struct ubifs_zyesde *zyesde,
			   const struct ubifs_zbranch *zbr, int n)
{
	int i;

	ubifs_assert(c, ubifs_zn_dirty(zyesde));

	if (zyesde->level) {
		for (i = zyesde->child_cnt; i > n; i--) {
			zyesde->zbranch[i] = zyesde->zbranch[i - 1];
			if (zyesde->zbranch[i].zyesde)
				zyesde->zbranch[i].zyesde->iip = i;
		}
		if (zbr->zyesde)
			zbr->zyesde->iip = n;
	} else
		for (i = zyesde->child_cnt; i > n; i--)
			zyesde->zbranch[i] = zyesde->zbranch[i - 1];

	zyesde->zbranch[n] = *zbr;
	zyesde->child_cnt += 1;

	/*
	 * After inserting at slot zero, the lower bound of the key range of
	 * this zyesde may have changed. If this zyesde is subsequently split
	 * then the upper bound of the key range may change, and furthermore
	 * it could change to be lower than the original lower bound. If that
	 * happens, then it will yes longer be possible to find this zyesde in the
	 * TNC using the key from the index yesde on flash. That is bad because
	 * if it is yest found, we will assume it is obsolete and may overwrite
	 * it. Then if there is an unclean unmount, we will start using the
	 * old index which will be broken.
	 *
	 * So we first mark zyesdes that have insertions at slot zero, and then
	 * if they are split we add their lnum/offs to the old_idx tree.
	 */
	if (n == 0)
		zyesde->alt = 1;
}

/**
 * tnc_insert - insert a yesde into TNC.
 * @c: UBIFS file-system description object
 * @zyesde: zyesde to insert into
 * @zbr: branch to insert
 * @n: slot number to insert new zbranch to
 *
 * This function inserts a new yesde described by @zbr into zyesde @zyesde. If
 * zyesde does yest have a free slot for new zbranch, it is split. Parent zyesdes
 * are splat as well if needed. Returns zero in case of success or a negative
 * error code in case of failure.
 */
static int tnc_insert(struct ubifs_info *c, struct ubifs_zyesde *zyesde,
		      struct ubifs_zbranch *zbr, int n)
{
	struct ubifs_zyesde *zn, *zi, *zp;
	int i, keep, move, appending = 0;
	union ubifs_key *key = &zbr->key, *key1;

	ubifs_assert(c, n >= 0 && n <= c->fayesut);

	/* Implement naive insert for yesw */
again:
	zp = zyesde->parent;
	if (zyesde->child_cnt < c->fayesut) {
		ubifs_assert(c, n != c->fayesut);
		dbg_tnck(key, "inserted at %d level %d, key ", n, zyesde->level);

		insert_zbranch(c, zyesde, zbr, n);

		/* Ensure parent's key is correct */
		if (n == 0 && zp && zyesde->iip == 0)
			correct_parent_keys(c, zyesde);

		return 0;
	}

	/*
	 * Unfortunately, @zyesde does yest have more empty slots and we have to
	 * split it.
	 */
	dbg_tnck(key, "splitting level %d, key ", zyesde->level);

	if (zyesde->alt)
		/*
		 * We can yes longer be sure of finding this zyesde by key, so we
		 * record it in the old_idx tree.
		 */
		ins_clr_old_idx_zyesde(c, zyesde);

	zn = kzalloc(c->max_zyesde_sz, GFP_NOFS);
	if (!zn)
		return -ENOMEM;
	zn->parent = zp;
	zn->level = zyesde->level;

	/* Decide where to split */
	if (zyesde->level == 0 && key_type(c, key) == UBIFS_DATA_KEY) {
		/* Try yest to split consecutive data keys */
		if (n == c->fayesut) {
			key1 = &zyesde->zbranch[n - 1].key;
			if (key_inum(c, key1) == key_inum(c, key) &&
			    key_type(c, key1) == UBIFS_DATA_KEY)
				appending = 1;
		} else
			goto check_split;
	} else if (appending && n != c->fayesut) {
		/* Try yest to split consecutive data keys */
		appending = 0;
check_split:
		if (n >= (c->fayesut + 1) / 2) {
			key1 = &zyesde->zbranch[0].key;
			if (key_inum(c, key1) == key_inum(c, key) &&
			    key_type(c, key1) == UBIFS_DATA_KEY) {
				key1 = &zyesde->zbranch[n].key;
				if (key_inum(c, key1) != key_inum(c, key) ||
				    key_type(c, key1) != UBIFS_DATA_KEY) {
					keep = n;
					move = c->fayesut - keep;
					zi = zyesde;
					goto do_split;
				}
			}
		}
	}

	if (appending) {
		keep = c->fayesut;
		move = 0;
	} else {
		keep = (c->fayesut + 1) / 2;
		move = c->fayesut - keep;
	}

	/*
	 * Although we don't at present, we could look at the neighbors and see
	 * if we can move some zbranches there.
	 */

	if (n < keep) {
		/* Insert into existing zyesde */
		zi = zyesde;
		move += 1;
		keep -= 1;
	} else {
		/* Insert into new zyesde */
		zi = zn;
		n -= keep;
		/* Re-parent */
		if (zn->level != 0)
			zbr->zyesde->parent = zn;
	}

do_split:

	__set_bit(DIRTY_ZNODE, &zn->flags);
	atomic_long_inc(&c->dirty_zn_cnt);

	zn->child_cnt = move;
	zyesde->child_cnt = keep;

	dbg_tnc("moving %d, keeping %d", move, keep);

	/* Move zbranch */
	for (i = 0; i < move; i++) {
		zn->zbranch[i] = zyesde->zbranch[keep + i];
		/* Re-parent */
		if (zn->level != 0)
			if (zn->zbranch[i].zyesde) {
				zn->zbranch[i].zyesde->parent = zn;
				zn->zbranch[i].zyesde->iip = i;
			}
	}

	/* Insert new key and branch */
	dbg_tnck(key, "inserting at %d level %d, key ", n, zn->level);

	insert_zbranch(c, zi, zbr, n);

	/* Insert new zyesde (produced by spitting) into the parent */
	if (zp) {
		if (n == 0 && zi == zyesde && zyesde->iip == 0)
			correct_parent_keys(c, zyesde);

		/* Locate insertion point */
		n = zyesde->iip + 1;

		/* Tail recursion */
		zbr->key = zn->zbranch[0].key;
		zbr->zyesde = zn;
		zbr->lnum = 0;
		zbr->offs = 0;
		zbr->len = 0;
		zyesde = zp;

		goto again;
	}

	/* We have to split root zyesde */
	dbg_tnc("creating new zroot at level %d", zyesde->level + 1);

	zi = kzalloc(c->max_zyesde_sz, GFP_NOFS);
	if (!zi)
		return -ENOMEM;

	zi->child_cnt = 2;
	zi->level = zyesde->level + 1;

	__set_bit(DIRTY_ZNODE, &zi->flags);
	atomic_long_inc(&c->dirty_zn_cnt);

	zi->zbranch[0].key = zyesde->zbranch[0].key;
	zi->zbranch[0].zyesde = zyesde;
	zi->zbranch[0].lnum = c->zroot.lnum;
	zi->zbranch[0].offs = c->zroot.offs;
	zi->zbranch[0].len = c->zroot.len;
	zi->zbranch[1].key = zn->zbranch[0].key;
	zi->zbranch[1].zyesde = zn;

	c->zroot.lnum = 0;
	c->zroot.offs = 0;
	c->zroot.len = 0;
	c->zroot.zyesde = zi;

	zn->parent = zi;
	zn->iip = 1;
	zyesde->parent = zi;
	zyesde->iip = 0;

	return 0;
}

/**
 * ubifs_tnc_add - add a yesde to TNC.
 * @c: UBIFS file-system description object
 * @key: key to add
 * @lnum: LEB number of yesde
 * @offs: yesde offset
 * @len: yesde length
 * @hash: The hash over the yesde
 *
 * This function adds a yesde with key @key to TNC. The yesde may be new or it may
 * obsolete some existing one. Returns %0 on success or negative error code on
 * failure.
 */
int ubifs_tnc_add(struct ubifs_info *c, const union ubifs_key *key, int lnum,
		  int offs, int len, const u8 *hash)
{
	int found, n, err = 0;
	struct ubifs_zyesde *zyesde;

	mutex_lock(&c->tnc_mutex);
	dbg_tnck(key, "%d:%d, len %d, key ", lnum, offs, len);
	found = lookup_level0_dirty(c, key, &zyesde, &n);
	if (!found) {
		struct ubifs_zbranch zbr;

		zbr.zyesde = NULL;
		zbr.lnum = lnum;
		zbr.offs = offs;
		zbr.len = len;
		ubifs_copy_hash(c, hash, zbr.hash);
		key_copy(c, key, &zbr.key);
		err = tnc_insert(c, zyesde, &zbr, n + 1);
	} else if (found == 1) {
		struct ubifs_zbranch *zbr = &zyesde->zbranch[n];

		lnc_free(zbr);
		err = ubifs_add_dirt(c, zbr->lnum, zbr->len);
		zbr->lnum = lnum;
		zbr->offs = offs;
		zbr->len = len;
		ubifs_copy_hash(c, hash, zbr->hash);
	} else
		err = found;
	if (!err)
		err = dbg_check_tnc(c, 0);
	mutex_unlock(&c->tnc_mutex);

	return err;
}

/**
 * ubifs_tnc_replace - replace a yesde in the TNC only if the old yesde is found.
 * @c: UBIFS file-system description object
 * @key: key to add
 * @old_lnum: LEB number of old yesde
 * @old_offs: old yesde offset
 * @lnum: LEB number of yesde
 * @offs: yesde offset
 * @len: yesde length
 *
 * This function replaces a yesde with key @key in the TNC only if the old yesde
 * is found.  This function is called by garbage collection when yesde are moved.
 * Returns %0 on success or negative error code on failure.
 */
int ubifs_tnc_replace(struct ubifs_info *c, const union ubifs_key *key,
		      int old_lnum, int old_offs, int lnum, int offs, int len)
{
	int found, n, err = 0;
	struct ubifs_zyesde *zyesde;

	mutex_lock(&c->tnc_mutex);
	dbg_tnck(key, "old LEB %d:%d, new LEB %d:%d, len %d, key ", old_lnum,
		 old_offs, lnum, offs, len);
	found = lookup_level0_dirty(c, key, &zyesde, &n);
	if (found < 0) {
		err = found;
		goto out_unlock;
	}

	if (found == 1) {
		struct ubifs_zbranch *zbr = &zyesde->zbranch[n];

		found = 0;
		if (zbr->lnum == old_lnum && zbr->offs == old_offs) {
			lnc_free(zbr);
			err = ubifs_add_dirt(c, zbr->lnum, zbr->len);
			if (err)
				goto out_unlock;
			zbr->lnum = lnum;
			zbr->offs = offs;
			zbr->len = len;
			found = 1;
		} else if (is_hash_key(c, key)) {
			found = resolve_collision_directly(c, key, &zyesde, &n,
							   old_lnum, old_offs);
			dbg_tnc("rc returned %d, zyesde %p, n %d, LEB %d:%d",
				found, zyesde, n, old_lnum, old_offs);
			if (found < 0) {
				err = found;
				goto out_unlock;
			}

			if (found) {
				/* Ensure the zyesde is dirtied */
				if (zyesde->cnext || !ubifs_zn_dirty(zyesde)) {
					zyesde = dirty_cow_bottom_up(c, zyesde);
					if (IS_ERR(zyesde)) {
						err = PTR_ERR(zyesde);
						goto out_unlock;
					}
				}
				zbr = &zyesde->zbranch[n];
				lnc_free(zbr);
				err = ubifs_add_dirt(c, zbr->lnum,
						     zbr->len);
				if (err)
					goto out_unlock;
				zbr->lnum = lnum;
				zbr->offs = offs;
				zbr->len = len;
			}
		}
	}

	if (!found)
		err = ubifs_add_dirt(c, lnum, len);

	if (!err)
		err = dbg_check_tnc(c, 0);

out_unlock:
	mutex_unlock(&c->tnc_mutex);
	return err;
}

/**
 * ubifs_tnc_add_nm - add a "hashed" yesde to TNC.
 * @c: UBIFS file-system description object
 * @key: key to add
 * @lnum: LEB number of yesde
 * @offs: yesde offset
 * @len: yesde length
 * @hash: The hash over the yesde
 * @nm: yesde name
 *
 * This is the same as 'ubifs_tnc_add()' but it should be used with keys which
 * may have collisions, like directory entry keys.
 */
int ubifs_tnc_add_nm(struct ubifs_info *c, const union ubifs_key *key,
		     int lnum, int offs, int len, const u8 *hash,
		     const struct fscrypt_name *nm)
{
	int found, n, err = 0;
	struct ubifs_zyesde *zyesde;

	mutex_lock(&c->tnc_mutex);
	dbg_tnck(key, "LEB %d:%d, key ", lnum, offs);
	found = lookup_level0_dirty(c, key, &zyesde, &n);
	if (found < 0) {
		err = found;
		goto out_unlock;
	}

	if (found == 1) {
		if (c->replaying)
			found = fallible_resolve_collision(c, key, &zyesde, &n,
							   nm, 1);
		else
			found = resolve_collision(c, key, &zyesde, &n, nm);
		dbg_tnc("rc returned %d, zyesde %p, n %d", found, zyesde, n);
		if (found < 0) {
			err = found;
			goto out_unlock;
		}

		/* Ensure the zyesde is dirtied */
		if (zyesde->cnext || !ubifs_zn_dirty(zyesde)) {
			zyesde = dirty_cow_bottom_up(c, zyesde);
			if (IS_ERR(zyesde)) {
				err = PTR_ERR(zyesde);
				goto out_unlock;
			}
		}

		if (found == 1) {
			struct ubifs_zbranch *zbr = &zyesde->zbranch[n];

			lnc_free(zbr);
			err = ubifs_add_dirt(c, zbr->lnum, zbr->len);
			zbr->lnum = lnum;
			zbr->offs = offs;
			zbr->len = len;
			ubifs_copy_hash(c, hash, zbr->hash);
			goto out_unlock;
		}
	}

	if (!found) {
		struct ubifs_zbranch zbr;

		zbr.zyesde = NULL;
		zbr.lnum = lnum;
		zbr.offs = offs;
		zbr.len = len;
		ubifs_copy_hash(c, hash, zbr.hash);
		key_copy(c, key, &zbr.key);
		err = tnc_insert(c, zyesde, &zbr, n + 1);
		if (err)
			goto out_unlock;
		if (c->replaying) {
			/*
			 * We did yest find it in the index so there may be a
			 * dangling branch still in the index. So we remove it
			 * by passing 'ubifs_tnc_remove_nm()' the same key but
			 * an unmatchable name.
			 */
			struct fscrypt_name yesname = { .disk_name = { .name = "", .len = 1 } };

			err = dbg_check_tnc(c, 0);
			mutex_unlock(&c->tnc_mutex);
			if (err)
				return err;
			return ubifs_tnc_remove_nm(c, key, &yesname);
		}
	}

out_unlock:
	if (!err)
		err = dbg_check_tnc(c, 0);
	mutex_unlock(&c->tnc_mutex);
	return err;
}

/**
 * tnc_delete - delete a zyesde form TNC.
 * @c: UBIFS file-system description object
 * @zyesde: zyesde to delete from
 * @n: zbranch slot number to delete
 *
 * This function deletes a leaf yesde from @n-th slot of @zyesde. Returns zero in
 * case of success and a negative error code in case of failure.
 */
static int tnc_delete(struct ubifs_info *c, struct ubifs_zyesde *zyesde, int n)
{
	struct ubifs_zbranch *zbr;
	struct ubifs_zyesde *zp;
	int i, err;

	/* Delete without merge for yesw */
	ubifs_assert(c, zyesde->level == 0);
	ubifs_assert(c, n >= 0 && n < c->fayesut);
	dbg_tnck(&zyesde->zbranch[n].key, "deleting key ");

	zbr = &zyesde->zbranch[n];
	lnc_free(zbr);

	err = ubifs_add_dirt(c, zbr->lnum, zbr->len);
	if (err) {
		ubifs_dump_zyesde(c, zyesde);
		return err;
	}

	/* We do yest "gap" zbranch slots */
	for (i = n; i < zyesde->child_cnt - 1; i++)
		zyesde->zbranch[i] = zyesde->zbranch[i + 1];
	zyesde->child_cnt -= 1;

	if (zyesde->child_cnt > 0)
		return 0;

	/*
	 * This was the last zbranch, we have to delete this zyesde from the
	 * parent.
	 */

	do {
		ubifs_assert(c, !ubifs_zn_obsolete(zyesde));
		ubifs_assert(c, ubifs_zn_dirty(zyesde));

		zp = zyesde->parent;
		n = zyesde->iip;

		atomic_long_dec(&c->dirty_zn_cnt);

		err = insert_old_idx_zyesde(c, zyesde);
		if (err)
			return err;

		if (zyesde->cnext) {
			__set_bit(OBSOLETE_ZNODE, &zyesde->flags);
			atomic_long_inc(&c->clean_zn_cnt);
			atomic_long_inc(&ubifs_clean_zn_cnt);
		} else
			kfree(zyesde);
		zyesde = zp;
	} while (zyesde->child_cnt == 1); /* while removing last child */

	/* Remove from zyesde, entry n - 1 */
	zyesde->child_cnt -= 1;
	ubifs_assert(c, zyesde->level != 0);
	for (i = n; i < zyesde->child_cnt; i++) {
		zyesde->zbranch[i] = zyesde->zbranch[i + 1];
		if (zyesde->zbranch[i].zyesde)
			zyesde->zbranch[i].zyesde->iip = i;
	}

	/*
	 * If this is the root and it has only 1 child then
	 * collapse the tree.
	 */
	if (!zyesde->parent) {
		while (zyesde->child_cnt == 1 && zyesde->level != 0) {
			zp = zyesde;
			zbr = &zyesde->zbranch[0];
			zyesde = get_zyesde(c, zyesde, 0);
			if (IS_ERR(zyesde))
				return PTR_ERR(zyesde);
			zyesde = dirty_cow_zyesde(c, zbr);
			if (IS_ERR(zyesde))
				return PTR_ERR(zyesde);
			zyesde->parent = NULL;
			zyesde->iip = 0;
			if (c->zroot.len) {
				err = insert_old_idx(c, c->zroot.lnum,
						     c->zroot.offs);
				if (err)
					return err;
			}
			c->zroot.lnum = zbr->lnum;
			c->zroot.offs = zbr->offs;
			c->zroot.len = zbr->len;
			c->zroot.zyesde = zyesde;
			ubifs_assert(c, !ubifs_zn_obsolete(zp));
			ubifs_assert(c, ubifs_zn_dirty(zp));
			atomic_long_dec(&c->dirty_zn_cnt);

			if (zp->cnext) {
				__set_bit(OBSOLETE_ZNODE, &zp->flags);
				atomic_long_inc(&c->clean_zn_cnt);
				atomic_long_inc(&ubifs_clean_zn_cnt);
			} else
				kfree(zp);
		}
	}

	return 0;
}

/**
 * ubifs_tnc_remove - remove an index entry of a yesde.
 * @c: UBIFS file-system description object
 * @key: key of yesde
 *
 * Returns %0 on success or negative error code on failure.
 */
int ubifs_tnc_remove(struct ubifs_info *c, const union ubifs_key *key)
{
	int found, n, err = 0;
	struct ubifs_zyesde *zyesde;

	mutex_lock(&c->tnc_mutex);
	dbg_tnck(key, "key ");
	found = lookup_level0_dirty(c, key, &zyesde, &n);
	if (found < 0) {
		err = found;
		goto out_unlock;
	}
	if (found == 1)
		err = tnc_delete(c, zyesde, n);
	if (!err)
		err = dbg_check_tnc(c, 0);

out_unlock:
	mutex_unlock(&c->tnc_mutex);
	return err;
}

/**
 * ubifs_tnc_remove_nm - remove an index entry for a "hashed" yesde.
 * @c: UBIFS file-system description object
 * @key: key of yesde
 * @nm: directory entry name
 *
 * Returns %0 on success or negative error code on failure.
 */
int ubifs_tnc_remove_nm(struct ubifs_info *c, const union ubifs_key *key,
			const struct fscrypt_name *nm)
{
	int n, err;
	struct ubifs_zyesde *zyesde;

	mutex_lock(&c->tnc_mutex);
	dbg_tnck(key, "key ");
	err = lookup_level0_dirty(c, key, &zyesde, &n);
	if (err < 0)
		goto out_unlock;

	if (err) {
		if (c->replaying)
			err = fallible_resolve_collision(c, key, &zyesde, &n,
							 nm, 0);
		else
			err = resolve_collision(c, key, &zyesde, &n, nm);
		dbg_tnc("rc returned %d, zyesde %p, n %d", err, zyesde, n);
		if (err < 0)
			goto out_unlock;
		if (err) {
			/* Ensure the zyesde is dirtied */
			if (zyesde->cnext || !ubifs_zn_dirty(zyesde)) {
				zyesde = dirty_cow_bottom_up(c, zyesde);
				if (IS_ERR(zyesde)) {
					err = PTR_ERR(zyesde);
					goto out_unlock;
				}
			}
			err = tnc_delete(c, zyesde, n);
		}
	}

out_unlock:
	if (!err)
		err = dbg_check_tnc(c, 0);
	mutex_unlock(&c->tnc_mutex);
	return err;
}

/**
 * ubifs_tnc_remove_dh - remove an index entry for a "double hashed" yesde.
 * @c: UBIFS file-system description object
 * @key: key of yesde
 * @cookie: yesde cookie for collision resolution
 *
 * Returns %0 on success or negative error code on failure.
 */
int ubifs_tnc_remove_dh(struct ubifs_info *c, const union ubifs_key *key,
			uint32_t cookie)
{
	int n, err;
	struct ubifs_zyesde *zyesde;
	struct ubifs_dent_yesde *dent;
	struct ubifs_zbranch *zbr;

	if (!c->double_hash)
		return -EOPNOTSUPP;

	mutex_lock(&c->tnc_mutex);
	err = lookup_level0_dirty(c, key, &zyesde, &n);
	if (err <= 0)
		goto out_unlock;

	zbr = &zyesde->zbranch[n];
	dent = kmalloc(UBIFS_MAX_DENT_NODE_SZ, GFP_NOFS);
	if (!dent) {
		err = -ENOMEM;
		goto out_unlock;
	}

	err = tnc_read_hashed_yesde(c, zbr, dent);
	if (err)
		goto out_free;

	/* If the cookie does yest match, we're facing a hash collision. */
	if (le32_to_cpu(dent->cookie) != cookie) {
		union ubifs_key start_key;

		lowest_dent_key(c, &start_key, key_inum(c, key));

		err = ubifs_lookup_level0(c, &start_key, &zyesde, &n);
		if (unlikely(err < 0))
			goto out_free;

		err = search_dh_cookie(c, key, dent, cookie, &zyesde, &n, err);
		if (err)
			goto out_free;
	}

	if (zyesde->cnext || !ubifs_zn_dirty(zyesde)) {
		zyesde = dirty_cow_bottom_up(c, zyesde);
		if (IS_ERR(zyesde)) {
			err = PTR_ERR(zyesde);
			goto out_free;
		}
	}
	err = tnc_delete(c, zyesde, n);

out_free:
	kfree(dent);
out_unlock:
	if (!err)
		err = dbg_check_tnc(c, 0);
	mutex_unlock(&c->tnc_mutex);
	return err;
}

/**
 * key_in_range - determine if a key falls within a range of keys.
 * @c: UBIFS file-system description object
 * @key: key to check
 * @from_key: lowest key in range
 * @to_key: highest key in range
 *
 * This function returns %1 if the key is in range and %0 otherwise.
 */
static int key_in_range(struct ubifs_info *c, union ubifs_key *key,
			union ubifs_key *from_key, union ubifs_key *to_key)
{
	if (keys_cmp(c, key, from_key) < 0)
		return 0;
	if (keys_cmp(c, key, to_key) > 0)
		return 0;
	return 1;
}

/**
 * ubifs_tnc_remove_range - remove index entries in range.
 * @c: UBIFS file-system description object
 * @from_key: lowest key to remove
 * @to_key: highest key to remove
 *
 * This function removes index entries starting at @from_key and ending at
 * @to_key.  This function returns zero in case of success and a negative error
 * code in case of failure.
 */
int ubifs_tnc_remove_range(struct ubifs_info *c, union ubifs_key *from_key,
			   union ubifs_key *to_key)
{
	int i, n, k, err = 0;
	struct ubifs_zyesde *zyesde;
	union ubifs_key *key;

	mutex_lock(&c->tnc_mutex);
	while (1) {
		/* Find first level 0 zyesde that contains keys to remove */
		err = ubifs_lookup_level0(c, from_key, &zyesde, &n);
		if (err < 0)
			goto out_unlock;

		if (err)
			key = from_key;
		else {
			err = tnc_next(c, &zyesde, &n);
			if (err == -ENOENT) {
				err = 0;
				goto out_unlock;
			}
			if (err < 0)
				goto out_unlock;
			key = &zyesde->zbranch[n].key;
			if (!key_in_range(c, key, from_key, to_key)) {
				err = 0;
				goto out_unlock;
			}
		}

		/* Ensure the zyesde is dirtied */
		if (zyesde->cnext || !ubifs_zn_dirty(zyesde)) {
			zyesde = dirty_cow_bottom_up(c, zyesde);
			if (IS_ERR(zyesde)) {
				err = PTR_ERR(zyesde);
				goto out_unlock;
			}
		}

		/* Remove all keys in range except the first */
		for (i = n + 1, k = 0; i < zyesde->child_cnt; i++, k++) {
			key = &zyesde->zbranch[i].key;
			if (!key_in_range(c, key, from_key, to_key))
				break;
			lnc_free(&zyesde->zbranch[i]);
			err = ubifs_add_dirt(c, zyesde->zbranch[i].lnum,
					     zyesde->zbranch[i].len);
			if (err) {
				ubifs_dump_zyesde(c, zyesde);
				goto out_unlock;
			}
			dbg_tnck(key, "removing key ");
		}
		if (k) {
			for (i = n + 1 + k; i < zyesde->child_cnt; i++)
				zyesde->zbranch[i - k] = zyesde->zbranch[i];
			zyesde->child_cnt -= k;
		}

		/* Now delete the first */
		err = tnc_delete(c, zyesde, n);
		if (err)
			goto out_unlock;
	}

out_unlock:
	if (!err)
		err = dbg_check_tnc(c, 0);
	mutex_unlock(&c->tnc_mutex);
	return err;
}

/**
 * ubifs_tnc_remove_iyes - remove an iyesde from TNC.
 * @c: UBIFS file-system description object
 * @inum: iyesde number to remove
 *
 * This function remove iyesde @inum and all the extended attributes associated
 * with the ayesde from TNC and returns zero in case of success or a negative
 * error code in case of failure.
 */
int ubifs_tnc_remove_iyes(struct ubifs_info *c, iyes_t inum)
{
	union ubifs_key key1, key2;
	struct ubifs_dent_yesde *xent, *pxent = NULL;
	struct fscrypt_name nm = {0};

	dbg_tnc("iyes %lu", (unsigned long)inum);

	/*
	 * Walk all extended attribute entries and remove them together with
	 * corresponding extended attribute iyesdes.
	 */
	lowest_xent_key(c, &key1, inum);
	while (1) {
		iyes_t xattr_inum;
		int err;

		xent = ubifs_tnc_next_ent(c, &key1, &nm);
		if (IS_ERR(xent)) {
			err = PTR_ERR(xent);
			if (err == -ENOENT)
				break;
			return err;
		}

		xattr_inum = le64_to_cpu(xent->inum);
		dbg_tnc("xent '%s', iyes %lu", xent->name,
			(unsigned long)xattr_inum);

		ubifs_evict_xattr_iyesde(c, xattr_inum);

		fname_name(&nm) = xent->name;
		fname_len(&nm) = le16_to_cpu(xent->nlen);
		err = ubifs_tnc_remove_nm(c, &key1, &nm);
		if (err) {
			kfree(xent);
			return err;
		}

		lowest_iyes_key(c, &key1, xattr_inum);
		highest_iyes_key(c, &key2, xattr_inum);
		err = ubifs_tnc_remove_range(c, &key1, &key2);
		if (err) {
			kfree(xent);
			return err;
		}

		kfree(pxent);
		pxent = xent;
		key_read(c, &xent->key, &key1);
	}

	kfree(pxent);
	lowest_iyes_key(c, &key1, inum);
	highest_iyes_key(c, &key2, inum);

	return ubifs_tnc_remove_range(c, &key1, &key2);
}

/**
 * ubifs_tnc_next_ent - walk directory or extended attribute entries.
 * @c: UBIFS file-system description object
 * @key: key of last entry
 * @nm: name of last entry found or %NULL
 *
 * This function finds and reads the next directory or extended attribute entry
 * after the given key (@key) if there is one. @nm is used to resolve
 * collisions.
 *
 * If the name of the current entry is yest kyeswn and only the key is kyeswn,
 * @nm->name has to be %NULL. In this case the semantics of this function is a
 * little bit different and it returns the entry corresponding to this key, yest
 * the next one. If the key was yest found, the closest "right" entry is
 * returned.
 *
 * If the fist entry has to be found, @key has to contain the lowest possible
 * key value for this iyesde and @name has to be %NULL.
 *
 * This function returns the found directory or extended attribute entry yesde
 * in case of success, %-ENOENT is returned if yes entry was found, and a
 * negative error code is returned in case of failure.
 */
struct ubifs_dent_yesde *ubifs_tnc_next_ent(struct ubifs_info *c,
					   union ubifs_key *key,
					   const struct fscrypt_name *nm)
{
	int n, err, type = key_type(c, key);
	struct ubifs_zyesde *zyesde;
	struct ubifs_dent_yesde *dent;
	struct ubifs_zbranch *zbr;
	union ubifs_key *dkey;

	dbg_tnck(key, "key ");
	ubifs_assert(c, is_hash_key(c, key));

	mutex_lock(&c->tnc_mutex);
	err = ubifs_lookup_level0(c, key, &zyesde, &n);
	if (unlikely(err < 0))
		goto out_unlock;

	if (fname_len(nm) > 0) {
		if (err) {
			/* Handle collisions */
			if (c->replaying)
				err = fallible_resolve_collision(c, key, &zyesde, &n,
							 nm, 0);
			else
				err = resolve_collision(c, key, &zyesde, &n, nm);
			dbg_tnc("rc returned %d, zyesde %p, n %d",
				err, zyesde, n);
			if (unlikely(err < 0))
				goto out_unlock;
		}

		/* Now find next entry */
		err = tnc_next(c, &zyesde, &n);
		if (unlikely(err))
			goto out_unlock;
	} else {
		/*
		 * The full name of the entry was yest given, in which case the
		 * behavior of this function is a little different and it
		 * returns current entry, yest the next one.
		 */
		if (!err) {
			/*
			 * However, the given key does yest exist in the TNC
			 * tree and @zyesde/@n variables contain the closest
			 * "preceding" element. Switch to the next one.
			 */
			err = tnc_next(c, &zyesde, &n);
			if (err)
				goto out_unlock;
		}
	}

	zbr = &zyesde->zbranch[n];
	dent = kmalloc(zbr->len, GFP_NOFS);
	if (unlikely(!dent)) {
		err = -ENOMEM;
		goto out_unlock;
	}

	/*
	 * The above 'tnc_next()' call could lead us to the next iyesde, check
	 * this.
	 */
	dkey = &zbr->key;
	if (key_inum(c, dkey) != key_inum(c, key) ||
	    key_type(c, dkey) != type) {
		err = -ENOENT;
		goto out_free;
	}

	err = tnc_read_hashed_yesde(c, zbr, dent);
	if (unlikely(err))
		goto out_free;

	mutex_unlock(&c->tnc_mutex);
	return dent;

out_free:
	kfree(dent);
out_unlock:
	mutex_unlock(&c->tnc_mutex);
	return ERR_PTR(err);
}

/**
 * tnc_destroy_cnext - destroy left-over obsolete zyesdes from a failed commit.
 * @c: UBIFS file-system description object
 *
 * Destroy left-over obsolete zyesdes from a failed commit.
 */
static void tnc_destroy_cnext(struct ubifs_info *c)
{
	struct ubifs_zyesde *cnext;

	if (!c->cnext)
		return;
	ubifs_assert(c, c->cmt_state == COMMIT_BROKEN);
	cnext = c->cnext;
	do {
		struct ubifs_zyesde *zyesde = cnext;

		cnext = cnext->cnext;
		if (ubifs_zn_obsolete(zyesde))
			kfree(zyesde);
	} while (cnext && cnext != c->cnext);
}

/**
 * ubifs_tnc_close - close TNC subsystem and free all related resources.
 * @c: UBIFS file-system description object
 */
void ubifs_tnc_close(struct ubifs_info *c)
{
	tnc_destroy_cnext(c);
	if (c->zroot.zyesde) {
		long n, freed;

		n = atomic_long_read(&c->clean_zn_cnt);
		freed = ubifs_destroy_tnc_subtree(c, c->zroot.zyesde);
		ubifs_assert(c, freed == n);
		atomic_long_sub(n, &ubifs_clean_zn_cnt);
	}
	kfree(c->gap_lebs);
	kfree(c->ilebs);
	destroy_old_idx(c);
}

/**
 * left_zyesde - get the zyesde to the left.
 * @c: UBIFS file-system description object
 * @zyesde: zyesde
 *
 * This function returns a pointer to the zyesde to the left of @zyesde or NULL if
 * there is yest one. A negative error code is returned on failure.
 */
static struct ubifs_zyesde *left_zyesde(struct ubifs_info *c,
				      struct ubifs_zyesde *zyesde)
{
	int level = zyesde->level;

	while (1) {
		int n = zyesde->iip - 1;

		/* Go up until we can go left */
		zyesde = zyesde->parent;
		if (!zyesde)
			return NULL;
		if (n >= 0) {
			/* Now go down the rightmost branch to 'level' */
			zyesde = get_zyesde(c, zyesde, n);
			if (IS_ERR(zyesde))
				return zyesde;
			while (zyesde->level != level) {
				n = zyesde->child_cnt - 1;
				zyesde = get_zyesde(c, zyesde, n);
				if (IS_ERR(zyesde))
					return zyesde;
			}
			break;
		}
	}
	return zyesde;
}

/**
 * right_zyesde - get the zyesde to the right.
 * @c: UBIFS file-system description object
 * @zyesde: zyesde
 *
 * This function returns a pointer to the zyesde to the right of @zyesde or NULL
 * if there is yest one. A negative error code is returned on failure.
 */
static struct ubifs_zyesde *right_zyesde(struct ubifs_info *c,
				       struct ubifs_zyesde *zyesde)
{
	int level = zyesde->level;

	while (1) {
		int n = zyesde->iip + 1;

		/* Go up until we can go right */
		zyesde = zyesde->parent;
		if (!zyesde)
			return NULL;
		if (n < zyesde->child_cnt) {
			/* Now go down the leftmost branch to 'level' */
			zyesde = get_zyesde(c, zyesde, n);
			if (IS_ERR(zyesde))
				return zyesde;
			while (zyesde->level != level) {
				zyesde = get_zyesde(c, zyesde, 0);
				if (IS_ERR(zyesde))
					return zyesde;
			}
			break;
		}
	}
	return zyesde;
}

/**
 * lookup_zyesde - find a particular indexing yesde from TNC.
 * @c: UBIFS file-system description object
 * @key: index yesde key to lookup
 * @level: index yesde level
 * @lnum: index yesde LEB number
 * @offs: index yesde offset
 *
 * This function searches an indexing yesde by its first key @key and its
 * address @lnum:@offs. It looks up the indexing tree by pulling all indexing
 * yesdes it traverses to TNC. This function is called for indexing yesdes which
 * were found on the media by scanning, for example when garbage-collecting or
 * when doing in-the-gaps commit. This means that the indexing yesde which is
 * looked for does yest have to have exactly the same leftmost key @key, because
 * the leftmost key may have been changed, in which case TNC will contain a
 * dirty zyesde which still refers the same @lnum:@offs. This function is clever
 * eyesugh to recognize such indexing yesdes.
 *
 * Note, if a zyesde was deleted or changed too much, then this function will
 * yest find it. For situations like this UBIFS has the old index RB-tree
 * (indexed by @lnum:@offs).
 *
 * This function returns a pointer to the zyesde found or %NULL if it is yest
 * found. A negative error code is returned on failure.
 */
static struct ubifs_zyesde *lookup_zyesde(struct ubifs_info *c,
					union ubifs_key *key, int level,
					int lnum, int offs)
{
	struct ubifs_zyesde *zyesde, *zn;
	int n, nn;

	ubifs_assert(c, key_type(c, key) < UBIFS_INVALID_KEY);

	/*
	 * The arguments have probably been read off flash, so don't assume
	 * they are valid.
	 */
	if (level < 0)
		return ERR_PTR(-EINVAL);

	/* Get the root zyesde */
	zyesde = c->zroot.zyesde;
	if (!zyesde) {
		zyesde = ubifs_load_zyesde(c, &c->zroot, NULL, 0);
		if (IS_ERR(zyesde))
			return zyesde;
	}
	/* Check if it is the one we are looking for */
	if (c->zroot.lnum == lnum && c->zroot.offs == offs)
		return zyesde;
	/* Descend to the parent level i.e. (level + 1) */
	if (level >= zyesde->level)
		return NULL;
	while (1) {
		ubifs_search_zbranch(c, zyesde, key, &n);
		if (n < 0) {
			/*
			 * We reached a zyesde where the leftmost key is greater
			 * than the key we are searching for. This is the same
			 * situation as the one described in a huge comment at
			 * the end of the 'ubifs_lookup_level0()' function. And
			 * for exactly the same reasons we have to try to look
			 * left before giving up.
			 */
			zyesde = left_zyesde(c, zyesde);
			if (!zyesde)
				return NULL;
			if (IS_ERR(zyesde))
				return zyesde;
			ubifs_search_zbranch(c, zyesde, key, &n);
			ubifs_assert(c, n >= 0);
		}
		if (zyesde->level == level + 1)
			break;
		zyesde = get_zyesde(c, zyesde, n);
		if (IS_ERR(zyesde))
			return zyesde;
	}
	/* Check if the child is the one we are looking for */
	if (zyesde->zbranch[n].lnum == lnum && zyesde->zbranch[n].offs == offs)
		return get_zyesde(c, zyesde, n);
	/* If the key is unique, there is yeswhere else to look */
	if (!is_hash_key(c, key))
		return NULL;
	/*
	 * The key is yest unique and so may be also in the zyesdes to either
	 * side.
	 */
	zn = zyesde;
	nn = n;
	/* Look left */
	while (1) {
		/* Move one branch to the left */
		if (n)
			n -= 1;
		else {
			zyesde = left_zyesde(c, zyesde);
			if (!zyesde)
				break;
			if (IS_ERR(zyesde))
				return zyesde;
			n = zyesde->child_cnt - 1;
		}
		/* Check it */
		if (zyesde->zbranch[n].lnum == lnum &&
		    zyesde->zbranch[n].offs == offs)
			return get_zyesde(c, zyesde, n);
		/* Stop if the key is less than the one we are looking for */
		if (keys_cmp(c, &zyesde->zbranch[n].key, key) < 0)
			break;
	}
	/* Back to the middle */
	zyesde = zn;
	n = nn;
	/* Look right */
	while (1) {
		/* Move one branch to the right */
		if (++n >= zyesde->child_cnt) {
			zyesde = right_zyesde(c, zyesde);
			if (!zyesde)
				break;
			if (IS_ERR(zyesde))
				return zyesde;
			n = 0;
		}
		/* Check it */
		if (zyesde->zbranch[n].lnum == lnum &&
		    zyesde->zbranch[n].offs == offs)
			return get_zyesde(c, zyesde, n);
		/* Stop if the key is greater than the one we are looking for */
		if (keys_cmp(c, &zyesde->zbranch[n].key, key) > 0)
			break;
	}
	return NULL;
}

/**
 * is_idx_yesde_in_tnc - determine if an index yesde is in the TNC.
 * @c: UBIFS file-system description object
 * @key: key of index yesde
 * @level: index yesde level
 * @lnum: LEB number of index yesde
 * @offs: offset of index yesde
 *
 * This function returns %0 if the index yesde is yest referred to in the TNC, %1
 * if the index yesde is referred to in the TNC and the corresponding zyesde is
 * dirty, %2 if an index yesde is referred to in the TNC and the corresponding
 * zyesde is clean, and a negative error code in case of failure.
 *
 * Note, the @key argument has to be the key of the first child. Also yeste,
 * this function relies on the fact that 0:0 is never a valid LEB number and
 * offset for a main-area yesde.
 */
int is_idx_yesde_in_tnc(struct ubifs_info *c, union ubifs_key *key, int level,
		       int lnum, int offs)
{
	struct ubifs_zyesde *zyesde;

	zyesde = lookup_zyesde(c, key, level, lnum, offs);
	if (!zyesde)
		return 0;
	if (IS_ERR(zyesde))
		return PTR_ERR(zyesde);

	return ubifs_zn_dirty(zyesde) ? 1 : 2;
}

/**
 * is_leaf_yesde_in_tnc - determine if a yesn-indexing yest is in the TNC.
 * @c: UBIFS file-system description object
 * @key: yesde key
 * @lnum: yesde LEB number
 * @offs: yesde offset
 *
 * This function returns %1 if the yesde is referred to in the TNC, %0 if it is
 * yest, and a negative error code in case of failure.
 *
 * Note, this function relies on the fact that 0:0 is never a valid LEB number
 * and offset for a main-area yesde.
 */
static int is_leaf_yesde_in_tnc(struct ubifs_info *c, union ubifs_key *key,
			       int lnum, int offs)
{
	struct ubifs_zbranch *zbr;
	struct ubifs_zyesde *zyesde, *zn;
	int n, found, err, nn;
	const int unique = !is_hash_key(c, key);

	found = ubifs_lookup_level0(c, key, &zyesde, &n);
	if (found < 0)
		return found; /* Error code */
	if (!found)
		return 0;
	zbr = &zyesde->zbranch[n];
	if (lnum == zbr->lnum && offs == zbr->offs)
		return 1; /* Found it */
	if (unique)
		return 0;
	/*
	 * Because the key is yest unique, we have to look left
	 * and right as well
	 */
	zn = zyesde;
	nn = n;
	/* Look left */
	while (1) {
		err = tnc_prev(c, &zyesde, &n);
		if (err == -ENOENT)
			break;
		if (err)
			return err;
		if (keys_cmp(c, key, &zyesde->zbranch[n].key))
			break;
		zbr = &zyesde->zbranch[n];
		if (lnum == zbr->lnum && offs == zbr->offs)
			return 1; /* Found it */
	}
	/* Look right */
	zyesde = zn;
	n = nn;
	while (1) {
		err = tnc_next(c, &zyesde, &n);
		if (err) {
			if (err == -ENOENT)
				return 0;
			return err;
		}
		if (keys_cmp(c, key, &zyesde->zbranch[n].key))
			break;
		zbr = &zyesde->zbranch[n];
		if (lnum == zbr->lnum && offs == zbr->offs)
			return 1; /* Found it */
	}
	return 0;
}

/**
 * ubifs_tnc_has_yesde - determine whether a yesde is in the TNC.
 * @c: UBIFS file-system description object
 * @key: yesde key
 * @level: index yesde level (if it is an index yesde)
 * @lnum: yesde LEB number
 * @offs: yesde offset
 * @is_idx: yesn-zero if the yesde is an index yesde
 *
 * This function returns %1 if the yesde is in the TNC, %0 if it is yest, and a
 * negative error code in case of failure. For index yesdes, @key has to be the
 * key of the first child. An index yesde is considered to be in the TNC only if
 * the corresponding zyesde is clean or has yest been loaded.
 */
int ubifs_tnc_has_yesde(struct ubifs_info *c, union ubifs_key *key, int level,
		       int lnum, int offs, int is_idx)
{
	int err;

	mutex_lock(&c->tnc_mutex);
	if (is_idx) {
		err = is_idx_yesde_in_tnc(c, key, level, lnum, offs);
		if (err < 0)
			goto out_unlock;
		if (err == 1)
			/* The index yesde was found but it was dirty */
			err = 0;
		else if (err == 2)
			/* The index yesde was found and it was clean */
			err = 1;
		else
			BUG_ON(err != 0);
	} else
		err = is_leaf_yesde_in_tnc(c, key, lnum, offs);

out_unlock:
	mutex_unlock(&c->tnc_mutex);
	return err;
}

/**
 * ubifs_dirty_idx_yesde - dirty an index yesde.
 * @c: UBIFS file-system description object
 * @key: index yesde key
 * @level: index yesde level
 * @lnum: index yesde LEB number
 * @offs: index yesde offset
 *
 * This function loads and dirties an index yesde so that it can be garbage
 * collected. The @key argument has to be the key of the first child. This
 * function relies on the fact that 0:0 is never a valid LEB number and offset
 * for a main-area yesde. Returns %0 on success and a negative error code on
 * failure.
 */
int ubifs_dirty_idx_yesde(struct ubifs_info *c, union ubifs_key *key, int level,
			 int lnum, int offs)
{
	struct ubifs_zyesde *zyesde;
	int err = 0;

	mutex_lock(&c->tnc_mutex);
	zyesde = lookup_zyesde(c, key, level, lnum, offs);
	if (!zyesde)
		goto out_unlock;
	if (IS_ERR(zyesde)) {
		err = PTR_ERR(zyesde);
		goto out_unlock;
	}
	zyesde = dirty_cow_bottom_up(c, zyesde);
	if (IS_ERR(zyesde)) {
		err = PTR_ERR(zyesde);
		goto out_unlock;
	}

out_unlock:
	mutex_unlock(&c->tnc_mutex);
	return err;
}

/**
 * dbg_check_iyesde_size - check if iyesde size is correct.
 * @c: UBIFS file-system description object
 * @inum: iyesde number
 * @size: iyesde size
 *
 * This function makes sure that the iyesde size (@size) is correct and it does
 * yest have any pages beyond @size. Returns zero if the iyesde is OK, %-EINVAL
 * if it has a data page beyond @size, and other negative error code in case of
 * other errors.
 */
int dbg_check_iyesde_size(struct ubifs_info *c, const struct iyesde *iyesde,
			 loff_t size)
{
	int err, n;
	union ubifs_key from_key, to_key, *key;
	struct ubifs_zyesde *zyesde;
	unsigned int block;

	if (!S_ISREG(iyesde->i_mode))
		return 0;
	if (!dbg_is_chk_gen(c))
		return 0;

	block = (size + UBIFS_BLOCK_SIZE - 1) >> UBIFS_BLOCK_SHIFT;
	data_key_init(c, &from_key, iyesde->i_iyes, block);
	highest_data_key(c, &to_key, iyesde->i_iyes);

	mutex_lock(&c->tnc_mutex);
	err = ubifs_lookup_level0(c, &from_key, &zyesde, &n);
	if (err < 0)
		goto out_unlock;

	if (err) {
		key = &from_key;
		goto out_dump;
	}

	err = tnc_next(c, &zyesde, &n);
	if (err == -ENOENT) {
		err = 0;
		goto out_unlock;
	}
	if (err < 0)
		goto out_unlock;

	ubifs_assert(c, err == 0);
	key = &zyesde->zbranch[n].key;
	if (!key_in_range(c, key, &from_key, &to_key))
		goto out_unlock;

out_dump:
	block = key_block(c, key);
	ubifs_err(c, "iyesde %lu has size %lld, but there are data at offset %lld",
		  (unsigned long)iyesde->i_iyes, size,
		  ((loff_t)block) << UBIFS_BLOCK_SHIFT);
	mutex_unlock(&c->tnc_mutex);
	ubifs_dump_iyesde(c, iyesde);
	dump_stack();
	return -EINVAL;

out_unlock:
	mutex_unlock(&c->tnc_mutex);
	return err;
}
