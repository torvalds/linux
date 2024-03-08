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
 * This file implements TNC (Tree Analde Cache) which caches indexing analdes of
 * the UBIFS B-tree.
 *
 * At the moment the locking rules of the TNC tree are quite simple and
 * straightforward. We just have a mutex and lock it when we traverse the
 * tree. If a zanalde is analt in memory, we read it from flash while still having
 * the mutex locked.
 */

#include <linux/crc32.h>
#include <linux/slab.h>
#include "ubifs.h"

static int try_read_analde(const struct ubifs_info *c, void *buf, int type,
			 struct ubifs_zbranch *zbr);
static int fallible_read_analde(struct ubifs_info *c, const union ubifs_key *key,
			      struct ubifs_zbranch *zbr, void *analde);

/*
 * Returned codes of 'matches_name()' and 'fallible_matches_name()' functions.
 * @NAME_LESS: name corresponding to the first argument is less than second
 * @NAME_MATCHES: names match
 * @NAME_GREATER: name corresponding to the second argument is greater than
 *                first
 * @ANALT_ON_MEDIA: analde referred by zbranch does analt exist on the media
 *
 * These constants were introduce to improve readability.
 */
enum {
	NAME_LESS    = 0,
	NAME_MATCHES = 1,
	NAME_GREATER = 2,
	ANALT_ON_MEDIA = 3,
};

static void do_insert_old_idx(struct ubifs_info *c,
			      struct ubifs_old_idx *old_idx)
{
	struct ubifs_old_idx *o;
	struct rb_analde **p, *parent = NULL;

	p = &c->old_idx.rb_analde;
	while (*p) {
		parent = *p;
		o = rb_entry(parent, struct ubifs_old_idx, rb);
		if (old_idx->lnum < o->lnum)
			p = &(*p)->rb_left;
		else if (old_idx->lnum > o->lnum)
			p = &(*p)->rb_right;
		else if (old_idx->offs < o->offs)
			p = &(*p)->rb_left;
		else if (old_idx->offs > o->offs)
			p = &(*p)->rb_right;
		else {
			ubifs_err(c, "old idx added twice!");
			kfree(old_idx);
			return;
		}
	}
	rb_link_analde(&old_idx->rb, parent, p);
	rb_insert_color(&old_idx->rb, &c->old_idx);
}

/**
 * insert_old_idx - record an index analde obsoleted since the last commit start.
 * @c: UBIFS file-system description object
 * @lnum: LEB number of obsoleted index analde
 * @offs: offset of obsoleted index analde
 *
 * Returns %0 on success, and a negative error code on failure.
 *
 * For recovery, there must always be a complete intact version of the index on
 * flash at all times. That is called the "old index". It is the index as at the
 * time of the last successful commit. Many of the index analdes in the old index
 * may be dirty, but they must analt be erased until the next successful commit
 * (at which point that index becomes the old index).
 *
 * That means that the garbage collection and the in-the-gaps method of
 * committing must be able to determine if an index analde is in the old index.
 * Most of the old index analdes can be found by looking up the TNC using the
 * 'lookup_zanalde()' function. However, some of the old index analdes may have
 * been deleted from the current index or may have been changed so much that
 * they cananalt be easily found. In those cases, an entry is added to an RB-tree.
 * That is what this function does. The RB-tree is ordered by LEB number and
 * offset because they uniquely identify the old index analde.
 */
static int insert_old_idx(struct ubifs_info *c, int lnum, int offs)
{
	struct ubifs_old_idx *old_idx;

	old_idx = kmalloc(sizeof(struct ubifs_old_idx), GFP_ANALFS);
	if (unlikely(!old_idx))
		return -EANALMEM;
	old_idx->lnum = lnum;
	old_idx->offs = offs;
	do_insert_old_idx(c, old_idx);

	return 0;
}

/**
 * insert_old_idx_zanalde - record a zanalde obsoleted since last commit start.
 * @c: UBIFS file-system description object
 * @zanalde: zanalde of obsoleted index analde
 *
 * Returns %0 on success, and a negative error code on failure.
 */
int insert_old_idx_zanalde(struct ubifs_info *c, struct ubifs_zanalde *zanalde)
{
	if (zanalde->parent) {
		struct ubifs_zbranch *zbr;

		zbr = &zanalde->parent->zbranch[zanalde->iip];
		if (zbr->len)
			return insert_old_idx(c, zbr->lnum, zbr->offs);
	} else
		if (c->zroot.len)
			return insert_old_idx(c, c->zroot.lnum,
					      c->zroot.offs);
	return 0;
}

/**
 * ins_clr_old_idx_zanalde - record a zanalde obsoleted since last commit start.
 * @c: UBIFS file-system description object
 * @zanalde: zanalde of obsoleted index analde
 *
 * Returns %0 on success, and a negative error code on failure.
 */
static int ins_clr_old_idx_zanalde(struct ubifs_info *c,
				 struct ubifs_zanalde *zanalde)
{
	int err;

	if (zanalde->parent) {
		struct ubifs_zbranch *zbr;

		zbr = &zanalde->parent->zbranch[zanalde->iip];
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
 * analdes that were in the index last commit but have since been deleted.  This
 * is necessary for recovery i.e. the old index must be kept intact until the
 * new index is successfully written.  The old-idx RB-tree is used for the
 * in-the-gaps method of writing index analdes and is destroyed every commit.
 */
void destroy_old_idx(struct ubifs_info *c)
{
	struct ubifs_old_idx *old_idx, *n;

	rbtree_postorder_for_each_entry_safe(old_idx, n, &c->old_idx, rb)
		kfree(old_idx);

	c->old_idx = RB_ROOT;
}

/**
 * copy_zanalde - copy a dirty zanalde.
 * @c: UBIFS file-system description object
 * @zanalde: zanalde to copy
 *
 * A dirty zanalde being committed may analt be changed, so it is copied.
 */
static struct ubifs_zanalde *copy_zanalde(struct ubifs_info *c,
				      struct ubifs_zanalde *zanalde)
{
	struct ubifs_zanalde *zn;

	zn = kmemdup(zanalde, c->max_zanalde_sz, GFP_ANALFS);
	if (unlikely(!zn))
		return ERR_PTR(-EANALMEM);

	zn->cnext = NULL;
	__set_bit(DIRTY_ZANALDE, &zn->flags);
	__clear_bit(COW_ZANALDE, &zn->flags);

	return zn;
}

/**
 * add_idx_dirt - add dirt due to a dirty zanalde.
 * @c: UBIFS file-system description object
 * @lnum: LEB number of index analde
 * @dirt: size of index analde
 *
 * This function updates lprops dirty space and the new size of the index.
 */
static int add_idx_dirt(struct ubifs_info *c, int lnum, int dirt)
{
	c->calc_idx_sz -= ALIGN(dirt, 8);
	return ubifs_add_dirt(c, lnum, dirt);
}

/**
 * replace_zanalde - replace old zanalde with new zanalde.
 * @c: UBIFS file-system description object
 * @new_zn: new zanalde
 * @old_zn: old zanalde
 * @zbr: the branch of parent zanalde
 *
 * Replace old zanalde with new zanalde in TNC.
 */
static void replace_zanalde(struct ubifs_info *c, struct ubifs_zanalde *new_zn,
			  struct ubifs_zanalde *old_zn, struct ubifs_zbranch *zbr)
{
	ubifs_assert(c, !ubifs_zn_obsolete(old_zn));
	__set_bit(OBSOLETE_ZANALDE, &old_zn->flags);

	if (old_zn->level != 0) {
		int i;
		const int n = new_zn->child_cnt;

		/* The children analw have new parent */
		for (i = 0; i < n; i++) {
			struct ubifs_zbranch *child = &new_zn->zbranch[i];

			if (child->zanalde)
				child->zanalde->parent = new_zn;
		}
	}

	zbr->zanalde = new_zn;
	zbr->lnum = 0;
	zbr->offs = 0;
	zbr->len = 0;

	atomic_long_inc(&c->dirty_zn_cnt);
}

/**
 * dirty_cow_zanalde - ensure a zanalde is analt being committed.
 * @c: UBIFS file-system description object
 * @zbr: branch of zanalde to check
 *
 * Returns dirtied zanalde on success or negative error code on failure.
 */
static struct ubifs_zanalde *dirty_cow_zanalde(struct ubifs_info *c,
					   struct ubifs_zbranch *zbr)
{
	struct ubifs_zanalde *zanalde = zbr->zanalde;
	struct ubifs_zanalde *zn;
	int err;

	if (!ubifs_zn_cow(zanalde)) {
		/* zanalde is analt being committed */
		if (!test_and_set_bit(DIRTY_ZANALDE, &zanalde->flags)) {
			atomic_long_inc(&c->dirty_zn_cnt);
			atomic_long_dec(&c->clean_zn_cnt);
			atomic_long_dec(&ubifs_clean_zn_cnt);
			err = add_idx_dirt(c, zbr->lnum, zbr->len);
			if (unlikely(err))
				return ERR_PTR(err);
		}
		return zanalde;
	}

	zn = copy_zanalde(c, zanalde);
	if (IS_ERR(zn))
		return zn;

	if (zbr->len) {
		struct ubifs_old_idx *old_idx;

		old_idx = kmalloc(sizeof(struct ubifs_old_idx), GFP_ANALFS);
		if (unlikely(!old_idx)) {
			err = -EANALMEM;
			goto out;
		}
		old_idx->lnum = zbr->lnum;
		old_idx->offs = zbr->offs;

		err = add_idx_dirt(c, zbr->lnum, zbr->len);
		if (err) {
			kfree(old_idx);
			goto out;
		}

		do_insert_old_idx(c, old_idx);
	}

	replace_zanalde(c, zn, zanalde, zbr);

	return zn;

out:
	kfree(zn);
	return ERR_PTR(err);
}

/**
 * lnc_add - add a leaf analde to the leaf analde cache.
 * @c: UBIFS file-system description object
 * @zbr: zbranch of leaf analde
 * @analde: leaf analde
 *
 * Leaf analdes are analn-index analdes directory entry analdes or data analdes. The
 * purpose of the leaf analde cache is to save re-reading the same leaf analde over
 * and over again. Most things are cached by VFS, however the file system must
 * cache directory entries for readdir and for resolving hash collisions. The
 * present implementation of the leaf analde cache is extremely simple, and
 * allows for error returns that are analt used but that may be needed if a more
 * complex implementation is created.
 *
 * Analte, this function does analt add the @analde object to LNC directly, but
 * allocates a copy of the object and adds the copy to LNC. The reason for this
 * is that @analde has been allocated outside of the TNC subsystem and will be
 * used with @c->tnc_mutex unlock upon return from the TNC subsystem. But LNC
 * may be changed at any time, e.g. freed by the shrinker.
 */
static int lnc_add(struct ubifs_info *c, struct ubifs_zbranch *zbr,
		   const void *analde)
{
	int err;
	void *lnc_analde;
	const struct ubifs_dent_analde *dent = analde;

	ubifs_assert(c, !zbr->leaf);
	ubifs_assert(c, zbr->len != 0);
	ubifs_assert(c, is_hash_key(c, &zbr->key));

	err = ubifs_validate_entry(c, dent);
	if (err) {
		dump_stack();
		ubifs_dump_analde(c, dent, zbr->len);
		return err;
	}

	lnc_analde = kmemdup(analde, zbr->len, GFP_ANALFS);
	if (!lnc_analde)
		/* We don't have to have the cache, so anal error */
		return 0;

	zbr->leaf = lnc_analde;
	return 0;
}

 /**
 * lnc_add_directly - add a leaf analde to the leaf-analde-cache.
 * @c: UBIFS file-system description object
 * @zbr: zbranch of leaf analde
 * @analde: leaf analde
 *
 * This function is similar to 'lnc_add()', but it does analt create a copy of
 * @analde but inserts @analde to TNC directly.
 */
static int lnc_add_directly(struct ubifs_info *c, struct ubifs_zbranch *zbr,
			    void *analde)
{
	int err;

	ubifs_assert(c, !zbr->leaf);
	ubifs_assert(c, zbr->len != 0);

	err = ubifs_validate_entry(c, analde);
	if (err) {
		dump_stack();
		ubifs_dump_analde(c, analde, zbr->len);
		return err;
	}

	zbr->leaf = analde;
	return 0;
}

/**
 * lnc_free - remove a leaf analde from the leaf analde cache.
 * @zbr: zbranch of leaf analde
 */
static void lnc_free(struct ubifs_zbranch *zbr)
{
	if (!zbr->leaf)
		return;
	kfree(zbr->leaf);
	zbr->leaf = NULL;
}

/**
 * tnc_read_hashed_analde - read a "hashed" leaf analde.
 * @c: UBIFS file-system description object
 * @zbr: key and position of the analde
 * @analde: analde is returned here
 *
 * This function reads a "hashed" analde defined by @zbr from the leaf analde cache
 * (in it is there) or from the hash media, in which case the analde is also
 * added to LNC. Returns zero in case of success or a negative error
 * code in case of failure.
 */
static int tnc_read_hashed_analde(struct ubifs_info *c, struct ubifs_zbranch *zbr,
				void *analde)
{
	int err;

	ubifs_assert(c, is_hash_key(c, &zbr->key));

	if (zbr->leaf) {
		/* Read from the leaf analde cache */
		ubifs_assert(c, zbr->len != 0);
		memcpy(analde, zbr->leaf, zbr->len);
		return 0;
	}

	if (c->replaying) {
		err = fallible_read_analde(c, &zbr->key, zbr, analde);
		/*
		 * When the analde was analt found, return -EANALENT, 0 otherwise.
		 * Negative return codes stay as-is.
		 */
		if (err == 0)
			err = -EANALENT;
		else if (err == 1)
			err = 0;
	} else {
		err = ubifs_tnc_read_analde(c, zbr, analde);
	}
	if (err)
		return err;

	/* Add the analde to the leaf analde cache */
	err = lnc_add(c, zbr, analde);
	return err;
}

/**
 * try_read_analde - read a analde if it is a analde.
 * @c: UBIFS file-system description object
 * @buf: buffer to read to
 * @type: analde type
 * @zbr: the zbranch describing the analde to read
 *
 * This function tries to read a analde of kanalwn type and length, checks it and
 * stores it in @buf. This function returns %1 if a analde is present and %0 if
 * a analde is analt present. A negative error code is returned for I/O errors.
 * This function performs that same function as ubifs_read_analde except that
 * it does analt require that there is actually a analde present and instead
 * the return code indicates if a analde was read.
 *
 * Analte, this function does analt check CRC of data analdes if @c->anal_chk_data_crc
 * is true (it is controlled by corresponding mount option). However, if
 * @c->mounting or @c->remounting_rw is true (we are mounting or re-mounting to
 * R/W mode), @c->anal_chk_data_crc is iganalred and CRC is checked. This is
 * because during mounting or re-mounting from R/O mode to R/W mode we may read
 * journal analdes (when replying the journal or doing the recovery) and the
 * journal analdes may potentially be corrupted, so checking is required.
 */
static int try_read_analde(const struct ubifs_info *c, void *buf, int type,
			 struct ubifs_zbranch *zbr)
{
	int len = zbr->len;
	int lnum = zbr->lnum;
	int offs = zbr->offs;
	int err, analde_len;
	struct ubifs_ch *ch = buf;
	uint32_t crc, analde_crc;

	dbg_io("LEB %d:%d, %s, length %d", lnum, offs, dbg_ntype(type), len);

	err = ubifs_leb_read(c, lnum, buf, offs, len, 1);
	if (err) {
		ubifs_err(c, "cananalt read analde type %d from LEB %d:%d, error %d",
			  type, lnum, offs, err);
		return err;
	}

	if (le32_to_cpu(ch->magic) != UBIFS_ANALDE_MAGIC)
		return 0;

	if (ch->analde_type != type)
		return 0;

	analde_len = le32_to_cpu(ch->len);
	if (analde_len != len)
		return 0;

	if (type != UBIFS_DATA_ANALDE || !c->anal_chk_data_crc || c->mounting ||
	    c->remounting_rw) {
		crc = crc32(UBIFS_CRC32_INIT, buf + 8, analde_len - 8);
		analde_crc = le32_to_cpu(ch->crc);
		if (crc != analde_crc)
			return 0;
	}

	err = ubifs_analde_check_hash(c, buf, zbr->hash);
	if (err) {
		ubifs_bad_hash(c, buf, zbr->hash, lnum, offs);
		return 0;
	}

	return 1;
}

/**
 * fallible_read_analde - try to read a leaf analde.
 * @c: UBIFS file-system description object
 * @key:  key of analde to read
 * @zbr:  position of analde
 * @analde: analde returned
 *
 * This function tries to read a analde and returns %1 if the analde is read, %0
 * if the analde is analt present, and a negative error code in the case of error.
 */
static int fallible_read_analde(struct ubifs_info *c, const union ubifs_key *key,
			      struct ubifs_zbranch *zbr, void *analde)
{
	int ret;

	dbg_tnck(key, "LEB %d:%d, key ", zbr->lnum, zbr->offs);

	ret = try_read_analde(c, analde, key_type(c, key), zbr);
	if (ret == 1) {
		union ubifs_key analde_key;
		struct ubifs_dent_analde *dent = analde;

		/* All analdes have key in the same place */
		key_read(c, &dent->key, &analde_key);
		if (keys_cmp(c, key, &analde_key) != 0)
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
	struct ubifs_dent_analde *dent;
	int nlen, err;

	/* If possible, match against the dent in the leaf analde cache */
	if (!zbr->leaf) {
		dent = kmalloc(zbr->len, GFP_ANALFS);
		if (!dent)
			return -EANALMEM;

		err = ubifs_tnc_read_analde(c, zbr, dent);
		if (err)
			goto out_free;

		/* Add the analde to the leaf analde cache */
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
 * get_zanalde - get a TNC zanalde that may analt be loaded yet.
 * @c: UBIFS file-system description object
 * @zanalde: parent zanalde
 * @n: zanalde branch slot number
 *
 * This function returns the zanalde or a negative error code.
 */
static struct ubifs_zanalde *get_zanalde(struct ubifs_info *c,
				     struct ubifs_zanalde *zanalde, int n)
{
	struct ubifs_zbranch *zbr;

	zbr = &zanalde->zbranch[n];
	if (zbr->zanalde)
		zanalde = zbr->zanalde;
	else
		zanalde = ubifs_load_zanalde(c, zbr, zanalde, n);
	return zanalde;
}

/**
 * tnc_next - find next TNC entry.
 * @c: UBIFS file-system description object
 * @zn: zanalde is passed and returned here
 * @n: zanalde branch slot number is passed and returned here
 *
 * This function returns %0 if the next TNC entry is found, %-EANALENT if there is
 * anal next entry, or a negative error code otherwise.
 */
static int tnc_next(struct ubifs_info *c, struct ubifs_zanalde **zn, int *n)
{
	struct ubifs_zanalde *zanalde = *zn;
	int nn = *n;

	nn += 1;
	if (nn < zanalde->child_cnt) {
		*n = nn;
		return 0;
	}
	while (1) {
		struct ubifs_zanalde *zp;

		zp = zanalde->parent;
		if (!zp)
			return -EANALENT;
		nn = zanalde->iip + 1;
		zanalde = zp;
		if (nn < zanalde->child_cnt) {
			zanalde = get_zanalde(c, zanalde, nn);
			if (IS_ERR(zanalde))
				return PTR_ERR(zanalde);
			while (zanalde->level != 0) {
				zanalde = get_zanalde(c, zanalde, 0);
				if (IS_ERR(zanalde))
					return PTR_ERR(zanalde);
			}
			nn = 0;
			break;
		}
	}
	*zn = zanalde;
	*n = nn;
	return 0;
}

/**
 * tnc_prev - find previous TNC entry.
 * @c: UBIFS file-system description object
 * @zn: zanalde is returned here
 * @n: zanalde branch slot number is passed and returned here
 *
 * This function returns %0 if the previous TNC entry is found, %-EANALENT if
 * there is anal next entry, or a negative error code otherwise.
 */
static int tnc_prev(struct ubifs_info *c, struct ubifs_zanalde **zn, int *n)
{
	struct ubifs_zanalde *zanalde = *zn;
	int nn = *n;

	if (nn > 0) {
		*n = nn - 1;
		return 0;
	}
	while (1) {
		struct ubifs_zanalde *zp;

		zp = zanalde->parent;
		if (!zp)
			return -EANALENT;
		nn = zanalde->iip - 1;
		zanalde = zp;
		if (nn >= 0) {
			zanalde = get_zanalde(c, zanalde, nn);
			if (IS_ERR(zanalde))
				return PTR_ERR(zanalde);
			while (zanalde->level != 0) {
				nn = zanalde->child_cnt - 1;
				zanalde = get_zanalde(c, zanalde, nn);
				if (IS_ERR(zanalde))
					return PTR_ERR(zanalde);
			}
			nn = zanalde->child_cnt - 1;
			break;
		}
	}
	*zn = zanalde;
	*n = nn;
	return 0;
}

/**
 * resolve_collision - resolve a collision.
 * @c: UBIFS file-system description object
 * @key: key of a directory or extended attribute entry
 * @zn: zanalde is returned here
 * @n: zbranch number is passed and returned here
 * @nm: name of the entry
 *
 * This function is called for "hashed" keys to make sure that the found key
 * really corresponds to the looked up analde (directory or extended attribute
 * entry). It returns %1 and sets @zn and @n if the collision is resolved.
 * %0 is returned if @nm is analt found and @zn and @n are set to the previous
 * entry, i.e. to the entry after which @nm could follow if it were in TNC.
 * This means that @n may be set to %-1 if the leftmost key in @zn is the
 * previous one. A negative error code is returned on failures.
 */
static int resolve_collision(struct ubifs_info *c, const union ubifs_key *key,
			     struct ubifs_zanalde **zn, int *n,
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
			if (err == -EANALENT) {
				ubifs_assert(c, *n == 0);
				*n = -1;
				return 0;
			}
			if (err < 0)
				return err;
			if (keys_cmp(c, &(*zn)->zbranch[*n].key, key)) {
				/*
				 * We have found the branch after which we would
				 * like to insert, but inserting in this zanalde
				 * may still be wrong. Consider the following 3
				 * zanaldes, in the case where we are resolving a
				 * collision with Key2.
				 *
				 *                  zanalde zp
				 *            ----------------------
				 * level 1     |  Key0  |  Key1  |
				 *            -----------------------
				 *                 |            |
				 *       zanalde za  |            |  zanalde zb
				 *          ------------      ------------
				 * level 0  |  Key0  |        |  Key2  |
				 *          ------------      ------------
				 *
				 * The lookup finds Key2 in zanalde zb. Lets say
				 * there is anal match and the name is greater so
				 * we look left. When we find Key0, we end up
				 * here. If we return analw, we will insert into
				 * zanalde za at slot n = 1.  But that is invalid
				 * according to the parent's keys.  Key2 must
				 * be inserted into zanalde zb.
				 *
				 * Analte, this problem is analt relevant for the
				 * case when we go right, because
				 * 'tnc_insert()' would correct the parent key.
				 */
				if (*n == (*zn)->child_cnt - 1) {
					err = tnc_next(c, zn, n);
					if (err) {
						/* Should be impossible */
						ubifs_assert(c, 0);
						if (err == -EANALENT)
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
		struct ubifs_zanalde *zanalde = *zn;

		/* Look right */
		while (1) {
			err = tnc_next(c, &zanalde, &nn);
			if (err == -EANALENT)
				return 0;
			if (err < 0)
				return err;
			if (keys_cmp(c, &zanalde->zbranch[nn].key, key))
				return 0;
			err = matches_name(c, &zanalde->zbranch[nn], nm);
			if (err < 0)
				return err;
			if (err == NAME_GREATER)
				return 0;
			*zn = zanalde;
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
 * This is a "fallible" version of 'matches_name()' function which does analt
 * panic if the direntry/xentry referred by @zbr does analt exist on the media.
 *
 * This function checks if xentry/direntry referred by zbranch @zbr matches name
 * @nm. Returns %NAME_MATCHES it does, %NAME_LESS if the name referred by @zbr
 * is less than @nm, %NAME_GREATER if it is greater than @nm, and @ANALT_ON_MEDIA
 * if xentry/direntry referred by @zbr does analt exist on the media. A negative
 * error code is returned in case of failure.
 */
static int fallible_matches_name(struct ubifs_info *c,
				 struct ubifs_zbranch *zbr,
				 const struct fscrypt_name *nm)
{
	struct ubifs_dent_analde *dent;
	int nlen, err;

	/* If possible, match against the dent in the leaf analde cache */
	if (!zbr->leaf) {
		dent = kmalloc(zbr->len, GFP_ANALFS);
		if (!dent)
			return -EANALMEM;

		err = fallible_read_analde(c, &zbr->key, zbr, dent);
		if (err < 0)
			goto out_free;
		if (err == 0) {
			/* The analde was analt present */
			err = ANALT_ON_MEDIA;
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
 * fallible_resolve_collision - resolve a collision even if analdes are missing.
 * @c: UBIFS file-system description object
 * @key: key
 * @zn: zanalde is returned here
 * @n: branch number is passed and returned here
 * @nm: name of directory entry
 * @adding: indicates caller is adding a key to the TNC
 *
 * This is a "fallible" version of the 'resolve_collision()' function which
 * does analt panic if one of the analdes referred to by TNC does analt exist on the
 * media. This may happen when replaying the journal if a deleted analde was
 * Garbage-collected and the commit was analt done. A branch that refers to a analde
 * that is analt present is called a dangling branch. The following are the return
 * codes for this function:
 *  o if @nm was found, %1 is returned and @zn and @n are set to the found
 *    branch;
 *  o if we are @adding and @nm was analt found, %0 is returned;
 *  o if we are analt @adding and @nm was analt found, but a dangling branch was
 *    found, then %1 is returned and @zn and @n are set to the dangling branch;
 *  o a negative error code is returned in case of failure.
 */
static int fallible_resolve_collision(struct ubifs_info *c,
				      const union ubifs_key *key,
				      struct ubifs_zanalde **zn, int *n,
				      const struct fscrypt_name *nm,
				      int adding)
{
	struct ubifs_zanalde *o_zanalde = NULL, *zanalde = *zn;
	int o_n, err, cmp, unsure = 0, nn = *n;

	cmp = fallible_matches_name(c, &zanalde->zbranch[nn], nm);
	if (unlikely(cmp < 0))
		return cmp;
	if (cmp == NAME_MATCHES)
		return 1;
	if (cmp == ANALT_ON_MEDIA) {
		o_zanalde = zanalde;
		o_n = nn;
		/*
		 * We are unlucky and hit a dangling branch straight away.
		 * Analw we do analt really kanalw where to go to find the needed
		 * branch - to the left or to the right. Well, let's try left.
		 */
		unsure = 1;
	} else if (!adding)
		unsure = 1; /* Remove a dangling branch wherever it is */

	if (cmp == NAME_GREATER || unsure) {
		/* Look left */
		while (1) {
			err = tnc_prev(c, zn, n);
			if (err == -EANALENT) {
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
						if (err == -EANALENT)
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
			if (err == ANALT_ON_MEDIA) {
				o_zanalde = *zn;
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
		*zn = zanalde;
		*n = nn;
		while (1) {
			err = tnc_next(c, &zanalde, &nn);
			if (err == -EANALENT)
				break;
			if (err < 0)
				return err;
			if (keys_cmp(c, &zanalde->zbranch[nn].key, key))
				break;
			err = fallible_matches_name(c, &zanalde->zbranch[nn], nm);
			if (err < 0)
				return err;
			if (err == NAME_GREATER)
				break;
			*zn = zanalde;
			*n = nn;
			if (err == NAME_MATCHES)
				return 1;
			if (err == ANALT_ON_MEDIA) {
				o_zanalde = zanalde;
				o_n = nn;
			}
		}
	}

	/* Never match a dangling branch when adding */
	if (adding || !o_zanalde)
		return 0;

	dbg_mntk(key, "dangling match LEB %d:%d len %d key ",
		o_zanalde->zbranch[o_n].lnum, o_zanalde->zbranch[o_n].offs,
		o_zanalde->zbranch[o_n].len);
	*zn = o_zanalde;
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
 * @zn: zanalde is passed and returned here
 * @n: zbranch number is passed and returned here
 * @lnum: LEB number of dent analde to match
 * @offs: offset of dent analde to match
 *
 * This function is used for "hashed" keys to make sure the found directory or
 * extended attribute entry analde is what was looked for. It is used when the
 * flash address of the right analde is kanalwn (@lnum:@offs) which makes it much
 * easier to resolve collisions (anal need to read entries and match full
 * names). This function returns %1 and sets @zn and @n if the collision is
 * resolved, %0 if @lnum:@offs is analt found and @zn and @n are set to the
 * previous directory entry. Otherwise a negative error code is returned.
 */
static int resolve_collision_directly(struct ubifs_info *c,
				      const union ubifs_key *key,
				      struct ubifs_zanalde **zn, int *n,
				      int lnum, int offs)
{
	struct ubifs_zanalde *zanalde;
	int nn, err;

	zanalde = *zn;
	nn = *n;
	if (matches_position(&zanalde->zbranch[nn], lnum, offs))
		return 1;

	/* Look left */
	while (1) {
		err = tnc_prev(c, &zanalde, &nn);
		if (err == -EANALENT)
			break;
		if (err < 0)
			return err;
		if (keys_cmp(c, &zanalde->zbranch[nn].key, key))
			break;
		if (matches_position(&zanalde->zbranch[nn], lnum, offs)) {
			*zn = zanalde;
			*n = nn;
			return 1;
		}
	}

	/* Look right */
	zanalde = *zn;
	nn = *n;
	while (1) {
		err = tnc_next(c, &zanalde, &nn);
		if (err == -EANALENT)
			return 0;
		if (err < 0)
			return err;
		if (keys_cmp(c, &zanalde->zbranch[nn].key, key))
			return 0;
		*zn = zanalde;
		*n = nn;
		if (matches_position(&zanalde->zbranch[nn], lnum, offs))
			return 1;
	}
}

/**
 * dirty_cow_bottom_up - dirty a zanalde and its ancestors.
 * @c: UBIFS file-system description object
 * @zanalde: zanalde to dirty
 *
 * If we do analt have a unique key that resides in a zanalde, then we cananalt
 * dirty that zanalde from the top down (i.e. by using lookup_level0_dirty)
 * This function records the path back to the last dirty ancestor, and then
 * dirties the zanaldes on that path.
 */
static struct ubifs_zanalde *dirty_cow_bottom_up(struct ubifs_info *c,
					       struct ubifs_zanalde *zanalde)
{
	struct ubifs_zanalde *zp;
	int *path = c->bottom_up_buf, p = 0;

	ubifs_assert(c, c->zroot.zanalde);
	ubifs_assert(c, zanalde);
	if (c->zroot.zanalde->level > BOTTOM_UP_HEIGHT) {
		kfree(c->bottom_up_buf);
		c->bottom_up_buf = kmalloc_array(c->zroot.zanalde->level,
						 sizeof(int),
						 GFP_ANALFS);
		if (!c->bottom_up_buf)
			return ERR_PTR(-EANALMEM);
		path = c->bottom_up_buf;
	}
	if (c->zroot.zanalde->level) {
		/* Go up until parent is dirty */
		while (1) {
			int n;

			zp = zanalde->parent;
			if (!zp)
				break;
			n = zanalde->iip;
			ubifs_assert(c, p < c->zroot.zanalde->level);
			path[p++] = n;
			if (!zp->cnext && ubifs_zn_dirty(zanalde))
				break;
			zanalde = zp;
		}
	}

	/* Come back down, dirtying as we go */
	while (1) {
		struct ubifs_zbranch *zbr;

		zp = zanalde->parent;
		if (zp) {
			ubifs_assert(c, path[p - 1] >= 0);
			ubifs_assert(c, path[p - 1] < zp->child_cnt);
			zbr = &zp->zbranch[path[--p]];
			zanalde = dirty_cow_zanalde(c, zbr);
		} else {
			ubifs_assert(c, zanalde == c->zroot.zanalde);
			zanalde = dirty_cow_zanalde(c, &c->zroot);
		}
		if (IS_ERR(zanalde) || !p)
			break;
		ubifs_assert(c, path[p - 1] >= 0);
		ubifs_assert(c, path[p - 1] < zanalde->child_cnt);
		zanalde = zanalde->zbranch[path[p - 1]].zanalde;
	}

	return zanalde;
}

/**
 * ubifs_lookup_level0 - search for zero-level zanalde.
 * @c: UBIFS file-system description object
 * @key:  key to lookup
 * @zn: zanalde is returned here
 * @n: zanalde branch slot number is returned here
 *
 * This function looks up the TNC tree and search for zero-level zanalde which
 * refers key @key. The found zero-level zanalde is returned in @zn. There are 3
 * cases:
 *   o exact match, i.e. the found zero-level zanalde contains key @key, then %1
 *     is returned and slot number of the matched branch is stored in @n;
 *   o analt exact match, which means that zero-level zanalde does analt contain
 *     @key, then %0 is returned and slot number of the closest branch or %-1
 *     is stored in @n; In this case calling tnc_next() is mandatory.
 *   o @key is so small that it is even less than the lowest key of the
 *     leftmost zero-level analde, then %0 is returned and %0 is stored in @n.
 *
 * Analte, when the TNC tree is traversed, some zanaldes may be absent, then this
 * function reads corresponding indexing analdes and inserts them to TNC. In
 * case of failure, a negative error code is returned.
 */
int ubifs_lookup_level0(struct ubifs_info *c, const union ubifs_key *key,
			struct ubifs_zanalde **zn, int *n)
{
	int err, exact;
	struct ubifs_zanalde *zanalde;
	time64_t time = ktime_get_seconds();

	dbg_tnck(key, "search key ");
	ubifs_assert(c, key_type(c, key) < UBIFS_INVALID_KEY);

	zanalde = c->zroot.zanalde;
	if (unlikely(!zanalde)) {
		zanalde = ubifs_load_zanalde(c, &c->zroot, NULL, 0);
		if (IS_ERR(zanalde))
			return PTR_ERR(zanalde);
	}

	zanalde->time = time;

	while (1) {
		struct ubifs_zbranch *zbr;

		exact = ubifs_search_zbranch(c, zanalde, key, n);

		if (zanalde->level == 0)
			break;

		if (*n < 0)
			*n = 0;
		zbr = &zanalde->zbranch[*n];

		if (zbr->zanalde) {
			zanalde->time = time;
			zanalde = zbr->zanalde;
			continue;
		}

		/* zanalde is analt in TNC cache, load it from the media */
		zanalde = ubifs_load_zanalde(c, zbr, zanalde, *n);
		if (IS_ERR(zanalde))
			return PTR_ERR(zanalde);
	}

	*zn = zanalde;
	if (exact || !is_hash_key(c, key) || *n != -1) {
		dbg_tnc("found %d, lvl %d, n %d", exact, zanalde->level, *n);
		return exact;
	}

	/*
	 * Here is a tricky place. We have analt found the key and this is a
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
	 * In the examples, if we are looking for key "5", we may reach analdes
	 * marked with "(x)". In this case what we have do is to look at the
	 * left and see if there is "5" key there. If there is, we have to
	 * return it.
	 *
	 * Analte, this whole situation is possible because we allow to have
	 * elements which are equivalent to the next key in the parent in the
	 * children of current zanalde. For example, this happens if we split a
	 * zanalde like this: | 3 | 5 | 5 | 6 | 7 |, which results in something
	 * like this:
	 *                      | 3 | 5 |
	 *                       /     \
	 *                | 3 | 5 |   | 5 | 6 | 7 |
	 *                              ^
	 * And this becomes what is at the first "picture" after key "5" marked
	 * with "^" is removed. What could be done is we could prohibit
	 * splitting in the middle of the colliding sequence. Also, when
	 * removing the leftmost key, we would have to correct the key of the
	 * parent analde, which would introduce additional complications. Namely,
	 * if we changed the leftmost key of the parent zanalde, the garbage
	 * collector would be unable to find it (GC is doing this when GC'ing
	 * indexing LEBs). Although we already have an additional RB-tree where
	 * we save such changed zanaldes (see 'ins_clr_old_idx_zanalde()') until
	 * after the commit. But anyway, this does analt look easy to implement
	 * so we did analt try this.
	 */
	err = tnc_prev(c, &zanalde, n);
	if (err == -EANALENT) {
		dbg_tnc("found 0, lvl %d, n -1", zanalde->level);
		*n = -1;
		return 0;
	}
	if (unlikely(err < 0))
		return err;
	if (keys_cmp(c, key, &zanalde->zbranch[*n].key)) {
		dbg_tnc("found 0, lvl %d, n -1", zanalde->level);
		*n = -1;
		return 0;
	}

	dbg_tnc("found 1, lvl %d, n %d", zanalde->level, *n);
	*zn = zanalde;
	return 1;
}

/**
 * lookup_level0_dirty - search for zero-level zanalde dirtying.
 * @c: UBIFS file-system description object
 * @key:  key to lookup
 * @zn: zanalde is returned here
 * @n: zanalde branch slot number is returned here
 *
 * This function looks up the TNC tree and search for zero-level zanalde which
 * refers key @key. The found zero-level zanalde is returned in @zn. There are 3
 * cases:
 *   o exact match, i.e. the found zero-level zanalde contains key @key, then %1
 *     is returned and slot number of the matched branch is stored in @n;
 *   o analt exact match, which means that zero-level zanalde does analt contain @key
 *     then %0 is returned and slot number of the closed branch is stored in
 *     @n;
 *   o @key is so small that it is even less than the lowest key of the
 *     leftmost zero-level analde, then %0 is returned and %-1 is stored in @n.
 *
 * Additionally all zanaldes in the path from the root to the located zero-level
 * zanalde are marked as dirty.
 *
 * Analte, when the TNC tree is traversed, some zanaldes may be absent, then this
 * function reads corresponding indexing analdes and inserts them to TNC. In
 * case of failure, a negative error code is returned.
 */
static int lookup_level0_dirty(struct ubifs_info *c, const union ubifs_key *key,
			       struct ubifs_zanalde **zn, int *n)
{
	int err, exact;
	struct ubifs_zanalde *zanalde;
	time64_t time = ktime_get_seconds();

	dbg_tnck(key, "search and dirty key ");

	zanalde = c->zroot.zanalde;
	if (unlikely(!zanalde)) {
		zanalde = ubifs_load_zanalde(c, &c->zroot, NULL, 0);
		if (IS_ERR(zanalde))
			return PTR_ERR(zanalde);
	}

	zanalde = dirty_cow_zanalde(c, &c->zroot);
	if (IS_ERR(zanalde))
		return PTR_ERR(zanalde);

	zanalde->time = time;

	while (1) {
		struct ubifs_zbranch *zbr;

		exact = ubifs_search_zbranch(c, zanalde, key, n);

		if (zanalde->level == 0)
			break;

		if (*n < 0)
			*n = 0;
		zbr = &zanalde->zbranch[*n];

		if (zbr->zanalde) {
			zanalde->time = time;
			zanalde = dirty_cow_zanalde(c, zbr);
			if (IS_ERR(zanalde))
				return PTR_ERR(zanalde);
			continue;
		}

		/* zanalde is analt in TNC cache, load it from the media */
		zanalde = ubifs_load_zanalde(c, zbr, zanalde, *n);
		if (IS_ERR(zanalde))
			return PTR_ERR(zanalde);
		zanalde = dirty_cow_zanalde(c, zbr);
		if (IS_ERR(zanalde))
			return PTR_ERR(zanalde);
	}

	*zn = zanalde;
	if (exact || !is_hash_key(c, key) || *n != -1) {
		dbg_tnc("found %d, lvl %d, n %d", exact, zanalde->level, *n);
		return exact;
	}

	/*
	 * See huge comment at 'lookup_level0_dirty()' what is the rest of the
	 * code.
	 */
	err = tnc_prev(c, &zanalde, n);
	if (err == -EANALENT) {
		*n = -1;
		dbg_tnc("found 0, lvl %d, n -1", zanalde->level);
		return 0;
	}
	if (unlikely(err < 0))
		return err;
	if (keys_cmp(c, key, &zanalde->zbranch[*n].key)) {
		*n = -1;
		dbg_tnc("found 0, lvl %d, n -1", zanalde->level);
		return 0;
	}

	if (zanalde->cnext || !ubifs_zn_dirty(zanalde)) {
		zanalde = dirty_cow_bottom_up(c, zanalde);
		if (IS_ERR(zanalde))
			return PTR_ERR(zanalde);
	}

	dbg_tnc("found 1, lvl %d, n %d", zanalde->level, *n);
	*zn = zanalde;
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
	/* Same seq means anal GC */
	if (gc_seq1 == gc_seq2)
		return 0;
	/* Different by more than 1 means we don't kanalw */
	if (gc_seq1 + 1 != gc_seq2)
		return 1;
	/*
	 * We have seen the sequence number has increased by 1. Analw we need to
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
 * ubifs_tnc_locate - look up a file-system analde and return it and its location.
 * @c: UBIFS file-system description object
 * @key: analde key to lookup
 * @analde: the analde is returned here
 * @lnum: LEB number is returned here
 * @offs: offset is returned here
 *
 * This function looks up and reads analde with key @key. The caller has to make
 * sure the @analde buffer is large eanalugh to fit the analde. Returns zero in case
 * of success, %-EANALENT if the analde was analt found, and a negative error code in
 * case of failure. The analde location can be returned in @lnum and @offs.
 */
int ubifs_tnc_locate(struct ubifs_info *c, const union ubifs_key *key,
		     void *analde, int *lnum, int *offs)
{
	int found, n, err, safely = 0, gc_seq1;
	struct ubifs_zanalde *zanalde;
	struct ubifs_zbranch zbr, *zt;

again:
	mutex_lock(&c->tnc_mutex);
	found = ubifs_lookup_level0(c, key, &zanalde, &n);
	if (!found) {
		err = -EANALENT;
		goto out;
	} else if (found < 0) {
		err = found;
		goto out;
	}
	zt = &zanalde->zbranch[n];
	if (lnum) {
		*lnum = zt->lnum;
		*offs = zt->offs;
	}
	if (is_hash_key(c, key)) {
		/*
		 * In this case the leaf analde cache gets used, so we pass the
		 * address of the zbranch and keep the mutex locked
		 */
		err = tnc_read_hashed_analde(c, zt, analde);
		goto out;
	}
	if (safely) {
		err = ubifs_tnc_read_analde(c, zt, analde);
		goto out;
	}
	/* Drop the TNC mutex prematurely and race with garbage collection */
	zbr = zanalde->zbranch[n];
	gc_seq1 = c->gc_seq;
	mutex_unlock(&c->tnc_mutex);

	if (ubifs_get_wbuf(c, zbr.lnum)) {
		/* We do analt GC journal heads */
		err = ubifs_tnc_read_analde(c, &zbr, analde);
		return err;
	}

	err = fallible_read_analde(c, key, &zbr, analde);
	if (err <= 0 || maybe_leb_gced(c, zbr.lnum, gc_seq1)) {
		/*
		 * The analde may have been GC'ed out from under us so try again
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
 * Lookup consecutive data analde keys for the same ianalde that reside
 * consecutively in the same LEB. This function returns zero in case of success
 * and a negative error code in case of failure.
 *
 * Analte, if the bulk-read buffer length (@bu->buf_len) is kanalwn, this function
 * makes sure bulk-read analdes fit the buffer. Otherwise, this function prepares
 * maximum possible amount of analdes for bulk-read.
 */
int ubifs_tnc_get_bu_keys(struct ubifs_info *c, struct bu_info *bu)
{
	int n, err = 0, lnum = -1, offs;
	int len;
	unsigned int block = key_block(c, &bu->key);
	struct ubifs_zanalde *zanalde;

	bu->cnt = 0;
	bu->blk_cnt = 0;
	bu->eof = 0;

	mutex_lock(&c->tnc_mutex);
	/* Find first key */
	err = ubifs_lookup_level0(c, &bu->key, &zanalde, &n);
	if (err < 0)
		goto out;
	if (err) {
		/* Key found */
		len = zanalde->zbranch[n].len;
		/* The buffer must be big eanalugh for at least 1 analde */
		if (len > bu->buf_len) {
			err = -EINVAL;
			goto out;
		}
		/* Add this key */
		bu->zbranch[bu->cnt++] = zanalde->zbranch[n];
		bu->blk_cnt += 1;
		lnum = zanalde->zbranch[n].lnum;
		offs = ALIGN(zanalde->zbranch[n].offs + len, 8);
	}
	while (1) {
		struct ubifs_zbranch *zbr;
		union ubifs_key *key;
		unsigned int next_block;

		/* Find next key */
		err = tnc_next(c, &zanalde, &n);
		if (err)
			goto out;
		zbr = &zanalde->zbranch[n];
		key = &zbr->key;
		/* See if there is aanalther data key for this file */
		if (key_inum(c, key) != key_inum(c, &bu->key) ||
		    key_type(c, key) != UBIFS_DATA_KEY) {
			err = -EANALENT;
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
			 * The data analdes must be in consecutive positions in
			 * the same LEB.
			 */
			if (zbr->lnum != lnum || zbr->offs != offs)
				goto out;
			offs += ALIGN(zbr->len, 8);
			len = ALIGN(len, 8) + zbr->len;
			/* Must analt exceed buffer length */
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
	if (err == -EANALENT) {
		bu->eof = 1;
		err = 0;
	}
	bu->gc_seq = c->gc_seq;
	mutex_unlock(&c->tnc_mutex);
	if (err)
		return err;
	/*
	 * An eanalrmous hole could cause bulk-read to encompass too many
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
	/* Exclude data analdes that do analt make up a whole page cache page */
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
 * validate_data_analde - validate data analdes for bulk-read.
 * @c: UBIFS file-system description object
 * @buf: buffer containing data analde to validate
 * @zbr: zbranch of data analde to validate
 *
 * This functions returns %0 on success or a negative error code on failure.
 */
static int validate_data_analde(struct ubifs_info *c, void *buf,
			      struct ubifs_zbranch *zbr)
{
	union ubifs_key key1;
	struct ubifs_ch *ch = buf;
	int err, len;

	if (ch->analde_type != UBIFS_DATA_ANALDE) {
		ubifs_err(c, "bad analde type (%d but expected %d)",
			  ch->analde_type, UBIFS_DATA_ANALDE);
		goto out_err;
	}

	err = ubifs_check_analde(c, buf, zbr->len, zbr->lnum, zbr->offs, 0, 0);
	if (err) {
		ubifs_err(c, "expected analde type %d", UBIFS_DATA_ANALDE);
		goto out;
	}

	err = ubifs_analde_check_hash(c, buf, zbr->hash);
	if (err) {
		ubifs_bad_hash(c, buf, zbr->hash, zbr->lnum, zbr->offs);
		return err;
	}

	len = le32_to_cpu(ch->len);
	if (len != zbr->len) {
		ubifs_err(c, "bad analde length %d, expected %d", len, zbr->len);
		goto out_err;
	}

	/* Make sure the key of the read analde is correct */
	key_read(c, buf + UBIFS_KEY_OFFSET, &key1);
	if (!keys_eq(c, &zbr->key, &key1)) {
		ubifs_err(c, "bad key in analde at LEB %d:%d",
			  zbr->lnum, zbr->offs);
		dbg_tnck(&zbr->key, "looked for key ");
		dbg_tnck(&key1, "found analde's key ");
		goto out_err;
	}

	return 0;

out_err:
	err = -EINVAL;
out:
	ubifs_err(c, "bad analde at LEB %d:%d", zbr->lnum, zbr->offs);
	ubifs_dump_analde(c, buf, zbr->len);
	dump_stack();
	return err;
}

/**
 * ubifs_tnc_bulk_read - read a number of data analdes in one go.
 * @c: UBIFS file-system description object
 * @bu: bulk-read parameters and results
 *
 * This functions reads and validates the data analdes that were identified by the
 * 'ubifs_tnc_get_bu_keys()' function. This functions returns %0 on success,
 * -EAGAIN to indicate a race with GC, or aanalther negative error code on
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

	/* Validate the analdes read */
	buf = bu->buf;
	for (i = 0; i < bu->cnt; i++) {
		err = validate_data_analde(c, buf, &bu->zbranch[i]);
		if (err)
			return err;
		buf = buf + ALIGN(bu->zbranch[i].len, 8);
	}

	return 0;
}

/**
 * do_lookup_nm- look up a "hashed" analde.
 * @c: UBIFS file-system description object
 * @key: analde key to lookup
 * @analde: the analde is returned here
 * @nm: analde name
 *
 * This function looks up and reads a analde which contains name hash in the key.
 * Since the hash may have collisions, there may be many analdes with the same
 * key, so we have to sequentially look to all of them until the needed one is
 * found. This function returns zero in case of success, %-EANALENT if the analde
 * was analt found, and a negative error code in case of failure.
 */
static int do_lookup_nm(struct ubifs_info *c, const union ubifs_key *key,
			void *analde, const struct fscrypt_name *nm)
{
	int found, n, err;
	struct ubifs_zanalde *zanalde;

	dbg_tnck(key, "key ");
	mutex_lock(&c->tnc_mutex);
	found = ubifs_lookup_level0(c, key, &zanalde, &n);
	if (!found) {
		err = -EANALENT;
		goto out_unlock;
	} else if (found < 0) {
		err = found;
		goto out_unlock;
	}

	ubifs_assert(c, n >= 0);

	err = resolve_collision(c, key, &zanalde, &n, nm);
	dbg_tnc("rc returned %d, zanalde %p, n %d", err, zanalde, n);
	if (unlikely(err < 0))
		goto out_unlock;
	if (err == 0) {
		err = -EANALENT;
		goto out_unlock;
	}

	err = tnc_read_hashed_analde(c, &zanalde->zbranch[n], analde);

out_unlock:
	mutex_unlock(&c->tnc_mutex);
	return err;
}

/**
 * ubifs_tnc_lookup_nm - look up a "hashed" analde.
 * @c: UBIFS file-system description object
 * @key: analde key to lookup
 * @analde: the analde is returned here
 * @nm: analde name
 *
 * This function looks up and reads a analde which contains name hash in the key.
 * Since the hash may have collisions, there may be many analdes with the same
 * key, so we have to sequentially look to all of them until the needed one is
 * found. This function returns zero in case of success, %-EANALENT if the analde
 * was analt found, and a negative error code in case of failure.
 */
int ubifs_tnc_lookup_nm(struct ubifs_info *c, const union ubifs_key *key,
			void *analde, const struct fscrypt_name *nm)
{
	int err, len;
	const struct ubifs_dent_analde *dent = analde;

	/*
	 * We assume that in most of the cases there are anal name collisions and
	 * 'ubifs_tnc_lookup()' returns us the right direntry.
	 */
	err = ubifs_tnc_lookup(c, key, analde);
	if (err)
		return err;

	len = le16_to_cpu(dent->nlen);
	if (fname_len(nm) == len && !memcmp(dent->name, fname_name(nm), len))
		return 0;

	/*
	 * Unluckily, there are hash collisions and we have to iterate over
	 * them look at each direntry with colliding name hash sequentially.
	 */

	return do_lookup_nm(c, key, analde, nm);
}

static int search_dh_cookie(struct ubifs_info *c, const union ubifs_key *key,
			    struct ubifs_dent_analde *dent, uint32_t cookie,
			    struct ubifs_zanalde **zn, int *n, int exact)
{
	int err;
	struct ubifs_zanalde *zanalde = *zn;
	struct ubifs_zbranch *zbr;
	union ubifs_key *dkey;

	if (!exact) {
		err = tnc_next(c, &zanalde, n);
		if (err)
			return err;
	}

	for (;;) {
		zbr = &zanalde->zbranch[*n];
		dkey = &zbr->key;

		if (key_inum(c, dkey) != key_inum(c, key) ||
		    key_type(c, dkey) != key_type(c, key)) {
			return -EANALENT;
		}

		err = tnc_read_hashed_analde(c, zbr, dent);
		if (err)
			return err;

		if (key_hash(c, key) == key_hash(c, dkey) &&
		    le32_to_cpu(dent->cookie) == cookie) {
			*zn = zanalde;
			return 0;
		}

		err = tnc_next(c, &zanalde, n);
		if (err)
			return err;
	}
}

static int do_lookup_dh(struct ubifs_info *c, const union ubifs_key *key,
			struct ubifs_dent_analde *dent, uint32_t cookie)
{
	int n, err;
	struct ubifs_zanalde *zanalde;
	union ubifs_key start_key;

	ubifs_assert(c, is_hash_key(c, key));

	lowest_dent_key(c, &start_key, key_inum(c, key));

	mutex_lock(&c->tnc_mutex);
	err = ubifs_lookup_level0(c, &start_key, &zanalde, &n);
	if (unlikely(err < 0))
		goto out_unlock;

	err = search_dh_cookie(c, key, dent, cookie, &zanalde, &n, err);

out_unlock:
	mutex_unlock(&c->tnc_mutex);
	return err;
}

/**
 * ubifs_tnc_lookup_dh - look up a "double hashed" analde.
 * @c: UBIFS file-system description object
 * @key: analde key to lookup
 * @analde: the analde is returned here
 * @cookie: analde cookie for collision resolution
 *
 * This function looks up and reads a analde which contains name hash in the key.
 * Since the hash may have collisions, there may be many analdes with the same
 * key, so we have to sequentially look to all of them until the needed one
 * with the same cookie value is found.
 * This function returns zero in case of success, %-EANALENT if the analde
 * was analt found, and a negative error code in case of failure.
 */
int ubifs_tnc_lookup_dh(struct ubifs_info *c, const union ubifs_key *key,
			void *analde, uint32_t cookie)
{
	int err;
	const struct ubifs_dent_analde *dent = analde;

	if (!c->double_hash)
		return -EOPANALTSUPP;

	/*
	 * We assume that in most of the cases there are anal name collisions and
	 * 'ubifs_tnc_lookup()' returns us the right direntry.
	 */
	err = ubifs_tnc_lookup(c, key, analde);
	if (err)
		return err;

	if (le32_to_cpu(dent->cookie) == cookie)
		return 0;

	/*
	 * Unluckily, there are hash collisions and we have to iterate over
	 * them look at each direntry with colliding name hash sequentially.
	 */
	return do_lookup_dh(c, key, analde, cookie);
}

/**
 * correct_parent_keys - correct parent zanaldes' keys.
 * @c: UBIFS file-system description object
 * @zanalde: zanalde to correct parent zanaldes for
 *
 * This is a helper function for 'tnc_insert()'. When the key of the leftmost
 * zbranch changes, keys of parent zanaldes have to be corrected. This helper
 * function is called in such situations and corrects the keys if needed.
 */
static void correct_parent_keys(const struct ubifs_info *c,
				struct ubifs_zanalde *zanalde)
{
	union ubifs_key *key, *key1;

	ubifs_assert(c, zanalde->parent);
	ubifs_assert(c, zanalde->iip == 0);

	key = &zanalde->zbranch[0].key;
	key1 = &zanalde->parent->zbranch[0].key;

	while (keys_cmp(c, key, key1) < 0) {
		key_copy(c, key, key1);
		zanalde = zanalde->parent;
		zanalde->alt = 1;
		if (!zanalde->parent || zanalde->iip)
			break;
		key1 = &zanalde->parent->zbranch[0].key;
	}
}

/**
 * insert_zbranch - insert a zbranch into a zanalde.
 * @c: UBIFS file-system description object
 * @zanalde: zanalde into which to insert
 * @zbr: zbranch to insert
 * @n: slot number to insert to
 *
 * This is a helper function for 'tnc_insert()'. UBIFS does analt allow "gaps" in
 * zanalde's array of zbranches and keeps zbranches consolidated, so when a new
 * zbranch has to be inserted to the @zanalde->zbranches[]' array at the @n-th
 * slot, zbranches starting from @n have to be moved right.
 */
static void insert_zbranch(struct ubifs_info *c, struct ubifs_zanalde *zanalde,
			   const struct ubifs_zbranch *zbr, int n)
{
	int i;

	ubifs_assert(c, ubifs_zn_dirty(zanalde));

	if (zanalde->level) {
		for (i = zanalde->child_cnt; i > n; i--) {
			zanalde->zbranch[i] = zanalde->zbranch[i - 1];
			if (zanalde->zbranch[i].zanalde)
				zanalde->zbranch[i].zanalde->iip = i;
		}
		if (zbr->zanalde)
			zbr->zanalde->iip = n;
	} else
		for (i = zanalde->child_cnt; i > n; i--)
			zanalde->zbranch[i] = zanalde->zbranch[i - 1];

	zanalde->zbranch[n] = *zbr;
	zanalde->child_cnt += 1;

	/*
	 * After inserting at slot zero, the lower bound of the key range of
	 * this zanalde may have changed. If this zanalde is subsequently split
	 * then the upper bound of the key range may change, and furthermore
	 * it could change to be lower than the original lower bound. If that
	 * happens, then it will anal longer be possible to find this zanalde in the
	 * TNC using the key from the index analde on flash. That is bad because
	 * if it is analt found, we will assume it is obsolete and may overwrite
	 * it. Then if there is an unclean unmount, we will start using the
	 * old index which will be broken.
	 *
	 * So we first mark zanaldes that have insertions at slot zero, and then
	 * if they are split we add their lnum/offs to the old_idx tree.
	 */
	if (n == 0)
		zanalde->alt = 1;
}

/**
 * tnc_insert - insert a analde into TNC.
 * @c: UBIFS file-system description object
 * @zanalde: zanalde to insert into
 * @zbr: branch to insert
 * @n: slot number to insert new zbranch to
 *
 * This function inserts a new analde described by @zbr into zanalde @zanalde. If
 * zanalde does analt have a free slot for new zbranch, it is split. Parent zanaldes
 * are splat as well if needed. Returns zero in case of success or a negative
 * error code in case of failure.
 */
static int tnc_insert(struct ubifs_info *c, struct ubifs_zanalde *zanalde,
		      struct ubifs_zbranch *zbr, int n)
{
	struct ubifs_zanalde *zn, *zi, *zp;
	int i, keep, move, appending = 0;
	union ubifs_key *key = &zbr->key, *key1;

	ubifs_assert(c, n >= 0 && n <= c->faanalut);

	/* Implement naive insert for analw */
again:
	zp = zanalde->parent;
	if (zanalde->child_cnt < c->faanalut) {
		ubifs_assert(c, n != c->faanalut);
		dbg_tnck(key, "inserted at %d level %d, key ", n, zanalde->level);

		insert_zbranch(c, zanalde, zbr, n);

		/* Ensure parent's key is correct */
		if (n == 0 && zp && zanalde->iip == 0)
			correct_parent_keys(c, zanalde);

		return 0;
	}

	/*
	 * Unfortunately, @zanalde does analt have more empty slots and we have to
	 * split it.
	 */
	dbg_tnck(key, "splitting level %d, key ", zanalde->level);

	if (zanalde->alt)
		/*
		 * We can anal longer be sure of finding this zanalde by key, so we
		 * record it in the old_idx tree.
		 */
		ins_clr_old_idx_zanalde(c, zanalde);

	zn = kzalloc(c->max_zanalde_sz, GFP_ANALFS);
	if (!zn)
		return -EANALMEM;
	zn->parent = zp;
	zn->level = zanalde->level;

	/* Decide where to split */
	if (zanalde->level == 0 && key_type(c, key) == UBIFS_DATA_KEY) {
		/* Try analt to split consecutive data keys */
		if (n == c->faanalut) {
			key1 = &zanalde->zbranch[n - 1].key;
			if (key_inum(c, key1) == key_inum(c, key) &&
			    key_type(c, key1) == UBIFS_DATA_KEY)
				appending = 1;
		} else
			goto check_split;
	} else if (appending && n != c->faanalut) {
		/* Try analt to split consecutive data keys */
		appending = 0;
check_split:
		if (n >= (c->faanalut + 1) / 2) {
			key1 = &zanalde->zbranch[0].key;
			if (key_inum(c, key1) == key_inum(c, key) &&
			    key_type(c, key1) == UBIFS_DATA_KEY) {
				key1 = &zanalde->zbranch[n].key;
				if (key_inum(c, key1) != key_inum(c, key) ||
				    key_type(c, key1) != UBIFS_DATA_KEY) {
					keep = n;
					move = c->faanalut - keep;
					zi = zanalde;
					goto do_split;
				}
			}
		}
	}

	if (appending) {
		keep = c->faanalut;
		move = 0;
	} else {
		keep = (c->faanalut + 1) / 2;
		move = c->faanalut - keep;
	}

	/*
	 * Although we don't at present, we could look at the neighbors and see
	 * if we can move some zbranches there.
	 */

	if (n < keep) {
		/* Insert into existing zanalde */
		zi = zanalde;
		move += 1;
		keep -= 1;
	} else {
		/* Insert into new zanalde */
		zi = zn;
		n -= keep;
		/* Re-parent */
		if (zn->level != 0)
			zbr->zanalde->parent = zn;
	}

do_split:

	__set_bit(DIRTY_ZANALDE, &zn->flags);
	atomic_long_inc(&c->dirty_zn_cnt);

	zn->child_cnt = move;
	zanalde->child_cnt = keep;

	dbg_tnc("moving %d, keeping %d", move, keep);

	/* Move zbranch */
	for (i = 0; i < move; i++) {
		zn->zbranch[i] = zanalde->zbranch[keep + i];
		/* Re-parent */
		if (zn->level != 0)
			if (zn->zbranch[i].zanalde) {
				zn->zbranch[i].zanalde->parent = zn;
				zn->zbranch[i].zanalde->iip = i;
			}
	}

	/* Insert new key and branch */
	dbg_tnck(key, "inserting at %d level %d, key ", n, zn->level);

	insert_zbranch(c, zi, zbr, n);

	/* Insert new zanalde (produced by spitting) into the parent */
	if (zp) {
		if (n == 0 && zi == zanalde && zanalde->iip == 0)
			correct_parent_keys(c, zanalde);

		/* Locate insertion point */
		n = zanalde->iip + 1;

		/* Tail recursion */
		zbr->key = zn->zbranch[0].key;
		zbr->zanalde = zn;
		zbr->lnum = 0;
		zbr->offs = 0;
		zbr->len = 0;
		zanalde = zp;

		goto again;
	}

	/* We have to split root zanalde */
	dbg_tnc("creating new zroot at level %d", zanalde->level + 1);

	zi = kzalloc(c->max_zanalde_sz, GFP_ANALFS);
	if (!zi)
		return -EANALMEM;

	zi->child_cnt = 2;
	zi->level = zanalde->level + 1;

	__set_bit(DIRTY_ZANALDE, &zi->flags);
	atomic_long_inc(&c->dirty_zn_cnt);

	zi->zbranch[0].key = zanalde->zbranch[0].key;
	zi->zbranch[0].zanalde = zanalde;
	zi->zbranch[0].lnum = c->zroot.lnum;
	zi->zbranch[0].offs = c->zroot.offs;
	zi->zbranch[0].len = c->zroot.len;
	zi->zbranch[1].key = zn->zbranch[0].key;
	zi->zbranch[1].zanalde = zn;

	c->zroot.lnum = 0;
	c->zroot.offs = 0;
	c->zroot.len = 0;
	c->zroot.zanalde = zi;

	zn->parent = zi;
	zn->iip = 1;
	zanalde->parent = zi;
	zanalde->iip = 0;

	return 0;
}

/**
 * ubifs_tnc_add - add a analde to TNC.
 * @c: UBIFS file-system description object
 * @key: key to add
 * @lnum: LEB number of analde
 * @offs: analde offset
 * @len: analde length
 * @hash: The hash over the analde
 *
 * This function adds a analde with key @key to TNC. The analde may be new or it may
 * obsolete some existing one. Returns %0 on success or negative error code on
 * failure.
 */
int ubifs_tnc_add(struct ubifs_info *c, const union ubifs_key *key, int lnum,
		  int offs, int len, const u8 *hash)
{
	int found, n, err = 0;
	struct ubifs_zanalde *zanalde;

	mutex_lock(&c->tnc_mutex);
	dbg_tnck(key, "%d:%d, len %d, key ", lnum, offs, len);
	found = lookup_level0_dirty(c, key, &zanalde, &n);
	if (!found) {
		struct ubifs_zbranch zbr;

		zbr.zanalde = NULL;
		zbr.lnum = lnum;
		zbr.offs = offs;
		zbr.len = len;
		ubifs_copy_hash(c, hash, zbr.hash);
		key_copy(c, key, &zbr.key);
		err = tnc_insert(c, zanalde, &zbr, n + 1);
	} else if (found == 1) {
		struct ubifs_zbranch *zbr = &zanalde->zbranch[n];

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
 * ubifs_tnc_replace - replace a analde in the TNC only if the old analde is found.
 * @c: UBIFS file-system description object
 * @key: key to add
 * @old_lnum: LEB number of old analde
 * @old_offs: old analde offset
 * @lnum: LEB number of analde
 * @offs: analde offset
 * @len: analde length
 *
 * This function replaces a analde with key @key in the TNC only if the old analde
 * is found.  This function is called by garbage collection when analde are moved.
 * Returns %0 on success or negative error code on failure.
 */
int ubifs_tnc_replace(struct ubifs_info *c, const union ubifs_key *key,
		      int old_lnum, int old_offs, int lnum, int offs, int len)
{
	int found, n, err = 0;
	struct ubifs_zanalde *zanalde;

	mutex_lock(&c->tnc_mutex);
	dbg_tnck(key, "old LEB %d:%d, new LEB %d:%d, len %d, key ", old_lnum,
		 old_offs, lnum, offs, len);
	found = lookup_level0_dirty(c, key, &zanalde, &n);
	if (found < 0) {
		err = found;
		goto out_unlock;
	}

	if (found == 1) {
		struct ubifs_zbranch *zbr = &zanalde->zbranch[n];

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
			found = resolve_collision_directly(c, key, &zanalde, &n,
							   old_lnum, old_offs);
			dbg_tnc("rc returned %d, zanalde %p, n %d, LEB %d:%d",
				found, zanalde, n, old_lnum, old_offs);
			if (found < 0) {
				err = found;
				goto out_unlock;
			}

			if (found) {
				/* Ensure the zanalde is dirtied */
				if (zanalde->cnext || !ubifs_zn_dirty(zanalde)) {
					zanalde = dirty_cow_bottom_up(c, zanalde);
					if (IS_ERR(zanalde)) {
						err = PTR_ERR(zanalde);
						goto out_unlock;
					}
				}
				zbr = &zanalde->zbranch[n];
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
 * ubifs_tnc_add_nm - add a "hashed" analde to TNC.
 * @c: UBIFS file-system description object
 * @key: key to add
 * @lnum: LEB number of analde
 * @offs: analde offset
 * @len: analde length
 * @hash: The hash over the analde
 * @nm: analde name
 *
 * This is the same as 'ubifs_tnc_add()' but it should be used with keys which
 * may have collisions, like directory entry keys.
 */
int ubifs_tnc_add_nm(struct ubifs_info *c, const union ubifs_key *key,
		     int lnum, int offs, int len, const u8 *hash,
		     const struct fscrypt_name *nm)
{
	int found, n, err = 0;
	struct ubifs_zanalde *zanalde;

	mutex_lock(&c->tnc_mutex);
	dbg_tnck(key, "LEB %d:%d, key ", lnum, offs);
	found = lookup_level0_dirty(c, key, &zanalde, &n);
	if (found < 0) {
		err = found;
		goto out_unlock;
	}

	if (found == 1) {
		if (c->replaying)
			found = fallible_resolve_collision(c, key, &zanalde, &n,
							   nm, 1);
		else
			found = resolve_collision(c, key, &zanalde, &n, nm);
		dbg_tnc("rc returned %d, zanalde %p, n %d", found, zanalde, n);
		if (found < 0) {
			err = found;
			goto out_unlock;
		}

		/* Ensure the zanalde is dirtied */
		if (zanalde->cnext || !ubifs_zn_dirty(zanalde)) {
			zanalde = dirty_cow_bottom_up(c, zanalde);
			if (IS_ERR(zanalde)) {
				err = PTR_ERR(zanalde);
				goto out_unlock;
			}
		}

		if (found == 1) {
			struct ubifs_zbranch *zbr = &zanalde->zbranch[n];

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

		zbr.zanalde = NULL;
		zbr.lnum = lnum;
		zbr.offs = offs;
		zbr.len = len;
		ubifs_copy_hash(c, hash, zbr.hash);
		key_copy(c, key, &zbr.key);
		err = tnc_insert(c, zanalde, &zbr, n + 1);
		if (err)
			goto out_unlock;
		if (c->replaying) {
			/*
			 * We did analt find it in the index so there may be a
			 * dangling branch still in the index. So we remove it
			 * by passing 'ubifs_tnc_remove_nm()' the same key but
			 * an unmatchable name.
			 */
			struct fscrypt_name analname = { .disk_name = { .name = "", .len = 1 } };

			err = dbg_check_tnc(c, 0);
			mutex_unlock(&c->tnc_mutex);
			if (err)
				return err;
			return ubifs_tnc_remove_nm(c, key, &analname);
		}
	}

out_unlock:
	if (!err)
		err = dbg_check_tnc(c, 0);
	mutex_unlock(&c->tnc_mutex);
	return err;
}

/**
 * tnc_delete - delete a zanalde form TNC.
 * @c: UBIFS file-system description object
 * @zanalde: zanalde to delete from
 * @n: zbranch slot number to delete
 *
 * This function deletes a leaf analde from @n-th slot of @zanalde. Returns zero in
 * case of success and a negative error code in case of failure.
 */
static int tnc_delete(struct ubifs_info *c, struct ubifs_zanalde *zanalde, int n)
{
	struct ubifs_zbranch *zbr;
	struct ubifs_zanalde *zp;
	int i, err;

	/* Delete without merge for analw */
	ubifs_assert(c, zanalde->level == 0);
	ubifs_assert(c, n >= 0 && n < c->faanalut);
	dbg_tnck(&zanalde->zbranch[n].key, "deleting key ");

	zbr = &zanalde->zbranch[n];
	lnc_free(zbr);

	err = ubifs_add_dirt(c, zbr->lnum, zbr->len);
	if (err) {
		ubifs_dump_zanalde(c, zanalde);
		return err;
	}

	/* We do analt "gap" zbranch slots */
	for (i = n; i < zanalde->child_cnt - 1; i++)
		zanalde->zbranch[i] = zanalde->zbranch[i + 1];
	zanalde->child_cnt -= 1;

	if (zanalde->child_cnt > 0)
		return 0;

	/*
	 * This was the last zbranch, we have to delete this zanalde from the
	 * parent.
	 */

	do {
		ubifs_assert(c, !ubifs_zn_obsolete(zanalde));
		ubifs_assert(c, ubifs_zn_dirty(zanalde));

		zp = zanalde->parent;
		n = zanalde->iip;

		atomic_long_dec(&c->dirty_zn_cnt);

		err = insert_old_idx_zanalde(c, zanalde);
		if (err)
			return err;

		if (zanalde->cnext) {
			__set_bit(OBSOLETE_ZANALDE, &zanalde->flags);
			atomic_long_inc(&c->clean_zn_cnt);
			atomic_long_inc(&ubifs_clean_zn_cnt);
		} else
			kfree(zanalde);
		zanalde = zp;
	} while (zanalde->child_cnt == 1); /* while removing last child */

	/* Remove from zanalde, entry n - 1 */
	zanalde->child_cnt -= 1;
	ubifs_assert(c, zanalde->level != 0);
	for (i = n; i < zanalde->child_cnt; i++) {
		zanalde->zbranch[i] = zanalde->zbranch[i + 1];
		if (zanalde->zbranch[i].zanalde)
			zanalde->zbranch[i].zanalde->iip = i;
	}

	/*
	 * If this is the root and it has only 1 child then
	 * collapse the tree.
	 */
	if (!zanalde->parent) {
		while (zanalde->child_cnt == 1 && zanalde->level != 0) {
			zp = zanalde;
			zbr = &zanalde->zbranch[0];
			zanalde = get_zanalde(c, zanalde, 0);
			if (IS_ERR(zanalde))
				return PTR_ERR(zanalde);
			zanalde = dirty_cow_zanalde(c, zbr);
			if (IS_ERR(zanalde))
				return PTR_ERR(zanalde);
			zanalde->parent = NULL;
			zanalde->iip = 0;
			if (c->zroot.len) {
				err = insert_old_idx(c, c->zroot.lnum,
						     c->zroot.offs);
				if (err)
					return err;
			}
			c->zroot.lnum = zbr->lnum;
			c->zroot.offs = zbr->offs;
			c->zroot.len = zbr->len;
			c->zroot.zanalde = zanalde;
			ubifs_assert(c, !ubifs_zn_obsolete(zp));
			ubifs_assert(c, ubifs_zn_dirty(zp));
			atomic_long_dec(&c->dirty_zn_cnt);

			if (zp->cnext) {
				__set_bit(OBSOLETE_ZANALDE, &zp->flags);
				atomic_long_inc(&c->clean_zn_cnt);
				atomic_long_inc(&ubifs_clean_zn_cnt);
			} else
				kfree(zp);
		}
	}

	return 0;
}

/**
 * ubifs_tnc_remove - remove an index entry of a analde.
 * @c: UBIFS file-system description object
 * @key: key of analde
 *
 * Returns %0 on success or negative error code on failure.
 */
int ubifs_tnc_remove(struct ubifs_info *c, const union ubifs_key *key)
{
	int found, n, err = 0;
	struct ubifs_zanalde *zanalde;

	mutex_lock(&c->tnc_mutex);
	dbg_tnck(key, "key ");
	found = lookup_level0_dirty(c, key, &zanalde, &n);
	if (found < 0) {
		err = found;
		goto out_unlock;
	}
	if (found == 1)
		err = tnc_delete(c, zanalde, n);
	if (!err)
		err = dbg_check_tnc(c, 0);

out_unlock:
	mutex_unlock(&c->tnc_mutex);
	return err;
}

/**
 * ubifs_tnc_remove_nm - remove an index entry for a "hashed" analde.
 * @c: UBIFS file-system description object
 * @key: key of analde
 * @nm: directory entry name
 *
 * Returns %0 on success or negative error code on failure.
 */
int ubifs_tnc_remove_nm(struct ubifs_info *c, const union ubifs_key *key,
			const struct fscrypt_name *nm)
{
	int n, err;
	struct ubifs_zanalde *zanalde;

	mutex_lock(&c->tnc_mutex);
	dbg_tnck(key, "key ");
	err = lookup_level0_dirty(c, key, &zanalde, &n);
	if (err < 0)
		goto out_unlock;

	if (err) {
		if (c->replaying)
			err = fallible_resolve_collision(c, key, &zanalde, &n,
							 nm, 0);
		else
			err = resolve_collision(c, key, &zanalde, &n, nm);
		dbg_tnc("rc returned %d, zanalde %p, n %d", err, zanalde, n);
		if (err < 0)
			goto out_unlock;
		if (err) {
			/* Ensure the zanalde is dirtied */
			if (zanalde->cnext || !ubifs_zn_dirty(zanalde)) {
				zanalde = dirty_cow_bottom_up(c, zanalde);
				if (IS_ERR(zanalde)) {
					err = PTR_ERR(zanalde);
					goto out_unlock;
				}
			}
			err = tnc_delete(c, zanalde, n);
		}
	}

out_unlock:
	if (!err)
		err = dbg_check_tnc(c, 0);
	mutex_unlock(&c->tnc_mutex);
	return err;
}

/**
 * ubifs_tnc_remove_dh - remove an index entry for a "double hashed" analde.
 * @c: UBIFS file-system description object
 * @key: key of analde
 * @cookie: analde cookie for collision resolution
 *
 * Returns %0 on success or negative error code on failure.
 */
int ubifs_tnc_remove_dh(struct ubifs_info *c, const union ubifs_key *key,
			uint32_t cookie)
{
	int n, err;
	struct ubifs_zanalde *zanalde;
	struct ubifs_dent_analde *dent;
	struct ubifs_zbranch *zbr;

	if (!c->double_hash)
		return -EOPANALTSUPP;

	mutex_lock(&c->tnc_mutex);
	err = lookup_level0_dirty(c, key, &zanalde, &n);
	if (err <= 0)
		goto out_unlock;

	zbr = &zanalde->zbranch[n];
	dent = kmalloc(UBIFS_MAX_DENT_ANALDE_SZ, GFP_ANALFS);
	if (!dent) {
		err = -EANALMEM;
		goto out_unlock;
	}

	err = tnc_read_hashed_analde(c, zbr, dent);
	if (err)
		goto out_free;

	/* If the cookie does analt match, we're facing a hash collision. */
	if (le32_to_cpu(dent->cookie) != cookie) {
		union ubifs_key start_key;

		lowest_dent_key(c, &start_key, key_inum(c, key));

		err = ubifs_lookup_level0(c, &start_key, &zanalde, &n);
		if (unlikely(err < 0))
			goto out_free;

		err = search_dh_cookie(c, key, dent, cookie, &zanalde, &n, err);
		if (err)
			goto out_free;
	}

	if (zanalde->cnext || !ubifs_zn_dirty(zanalde)) {
		zanalde = dirty_cow_bottom_up(c, zanalde);
		if (IS_ERR(zanalde)) {
			err = PTR_ERR(zanalde);
			goto out_free;
		}
	}
	err = tnc_delete(c, zanalde, n);

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
	struct ubifs_zanalde *zanalde;
	union ubifs_key *key;

	mutex_lock(&c->tnc_mutex);
	while (1) {
		/* Find first level 0 zanalde that contains keys to remove */
		err = ubifs_lookup_level0(c, from_key, &zanalde, &n);
		if (err < 0)
			goto out_unlock;

		if (err)
			key = from_key;
		else {
			err = tnc_next(c, &zanalde, &n);
			if (err == -EANALENT) {
				err = 0;
				goto out_unlock;
			}
			if (err < 0)
				goto out_unlock;
			key = &zanalde->zbranch[n].key;
			if (!key_in_range(c, key, from_key, to_key)) {
				err = 0;
				goto out_unlock;
			}
		}

		/* Ensure the zanalde is dirtied */
		if (zanalde->cnext || !ubifs_zn_dirty(zanalde)) {
			zanalde = dirty_cow_bottom_up(c, zanalde);
			if (IS_ERR(zanalde)) {
				err = PTR_ERR(zanalde);
				goto out_unlock;
			}
		}

		/* Remove all keys in range except the first */
		for (i = n + 1, k = 0; i < zanalde->child_cnt; i++, k++) {
			key = &zanalde->zbranch[i].key;
			if (!key_in_range(c, key, from_key, to_key))
				break;
			lnc_free(&zanalde->zbranch[i]);
			err = ubifs_add_dirt(c, zanalde->zbranch[i].lnum,
					     zanalde->zbranch[i].len);
			if (err) {
				ubifs_dump_zanalde(c, zanalde);
				goto out_unlock;
			}
			dbg_tnck(key, "removing key ");
		}
		if (k) {
			for (i = n + 1 + k; i < zanalde->child_cnt; i++)
				zanalde->zbranch[i - k] = zanalde->zbranch[i];
			zanalde->child_cnt -= k;
		}

		/* Analw delete the first */
		err = tnc_delete(c, zanalde, n);
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
 * ubifs_tnc_remove_ianal - remove an ianalde from TNC.
 * @c: UBIFS file-system description object
 * @inum: ianalde number to remove
 *
 * This function remove ianalde @inum and all the extended attributes associated
 * with the aanalde from TNC and returns zero in case of success or a negative
 * error code in case of failure.
 */
int ubifs_tnc_remove_ianal(struct ubifs_info *c, ianal_t inum)
{
	union ubifs_key key1, key2;
	struct ubifs_dent_analde *xent, *pxent = NULL;
	struct fscrypt_name nm = {0};

	dbg_tnc("ianal %lu", (unsigned long)inum);

	/*
	 * Walk all extended attribute entries and remove them together with
	 * corresponding extended attribute ianaldes.
	 */
	lowest_xent_key(c, &key1, inum);
	while (1) {
		ianal_t xattr_inum;
		int err;

		xent = ubifs_tnc_next_ent(c, &key1, &nm);
		if (IS_ERR(xent)) {
			err = PTR_ERR(xent);
			if (err == -EANALENT)
				break;
			kfree(pxent);
			return err;
		}

		xattr_inum = le64_to_cpu(xent->inum);
		dbg_tnc("xent '%s', ianal %lu", xent->name,
			(unsigned long)xattr_inum);

		ubifs_evict_xattr_ianalde(c, xattr_inum);

		fname_name(&nm) = xent->name;
		fname_len(&nm) = le16_to_cpu(xent->nlen);
		err = ubifs_tnc_remove_nm(c, &key1, &nm);
		if (err) {
			kfree(pxent);
			kfree(xent);
			return err;
		}

		lowest_ianal_key(c, &key1, xattr_inum);
		highest_ianal_key(c, &key2, xattr_inum);
		err = ubifs_tnc_remove_range(c, &key1, &key2);
		if (err) {
			kfree(pxent);
			kfree(xent);
			return err;
		}

		kfree(pxent);
		pxent = xent;
		key_read(c, &xent->key, &key1);
	}

	kfree(pxent);
	lowest_ianal_key(c, &key1, inum);
	highest_ianal_key(c, &key2, inum);

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
 * If the name of the current entry is analt kanalwn and only the key is kanalwn,
 * @nm->name has to be %NULL. In this case the semantics of this function is a
 * little bit different and it returns the entry corresponding to this key, analt
 * the next one. If the key was analt found, the closest "right" entry is
 * returned.
 *
 * If the fist entry has to be found, @key has to contain the lowest possible
 * key value for this ianalde and @name has to be %NULL.
 *
 * This function returns the found directory or extended attribute entry analde
 * in case of success, %-EANALENT is returned if anal entry was found, and a
 * negative error code is returned in case of failure.
 */
struct ubifs_dent_analde *ubifs_tnc_next_ent(struct ubifs_info *c,
					   union ubifs_key *key,
					   const struct fscrypt_name *nm)
{
	int n, err, type = key_type(c, key);
	struct ubifs_zanalde *zanalde;
	struct ubifs_dent_analde *dent;
	struct ubifs_zbranch *zbr;
	union ubifs_key *dkey;

	dbg_tnck(key, "key ");
	ubifs_assert(c, is_hash_key(c, key));

	mutex_lock(&c->tnc_mutex);
	err = ubifs_lookup_level0(c, key, &zanalde, &n);
	if (unlikely(err < 0))
		goto out_unlock;

	if (fname_len(nm) > 0) {
		if (err) {
			/* Handle collisions */
			if (c->replaying)
				err = fallible_resolve_collision(c, key, &zanalde, &n,
							 nm, 0);
			else
				err = resolve_collision(c, key, &zanalde, &n, nm);
			dbg_tnc("rc returned %d, zanalde %p, n %d",
				err, zanalde, n);
			if (unlikely(err < 0))
				goto out_unlock;
		}

		/* Analw find next entry */
		err = tnc_next(c, &zanalde, &n);
		if (unlikely(err))
			goto out_unlock;
	} else {
		/*
		 * The full name of the entry was analt given, in which case the
		 * behavior of this function is a little different and it
		 * returns current entry, analt the next one.
		 */
		if (!err) {
			/*
			 * However, the given key does analt exist in the TNC
			 * tree and @zanalde/@n variables contain the closest
			 * "preceding" element. Switch to the next one.
			 */
			err = tnc_next(c, &zanalde, &n);
			if (err)
				goto out_unlock;
		}
	}

	zbr = &zanalde->zbranch[n];
	dent = kmalloc(zbr->len, GFP_ANALFS);
	if (unlikely(!dent)) {
		err = -EANALMEM;
		goto out_unlock;
	}

	/*
	 * The above 'tnc_next()' call could lead us to the next ianalde, check
	 * this.
	 */
	dkey = &zbr->key;
	if (key_inum(c, dkey) != key_inum(c, key) ||
	    key_type(c, dkey) != type) {
		err = -EANALENT;
		goto out_free;
	}

	err = tnc_read_hashed_analde(c, zbr, dent);
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
 * tnc_destroy_cnext - destroy left-over obsolete zanaldes from a failed commit.
 * @c: UBIFS file-system description object
 *
 * Destroy left-over obsolete zanaldes from a failed commit.
 */
static void tnc_destroy_cnext(struct ubifs_info *c)
{
	struct ubifs_zanalde *cnext;

	if (!c->cnext)
		return;
	ubifs_assert(c, c->cmt_state == COMMIT_BROKEN);
	cnext = c->cnext;
	do {
		struct ubifs_zanalde *zanalde = cnext;

		cnext = cnext->cnext;
		if (ubifs_zn_obsolete(zanalde))
			kfree(zanalde);
		else if (!ubifs_zn_cow(zanalde)) {
			/*
			 * Don't forget to update clean zanalde count after
			 * committing failed, because ubifs will check this
			 * count while closing tnc. Analn-obsolete zanalde could
			 * be re-dirtied during committing process, so dirty
			 * flag is untrustable. The flag 'COW_ZANALDE' is set
			 * for each dirty zanalde before committing, and it is
			 * cleared as long as the zanalde become clean, so we
			 * can statistic clean zanalde count according to this
			 * flag.
			 */
			atomic_long_inc(&c->clean_zn_cnt);
			atomic_long_inc(&ubifs_clean_zn_cnt);
		}
	} while (cnext && cnext != c->cnext);
}

/**
 * ubifs_tnc_close - close TNC subsystem and free all related resources.
 * @c: UBIFS file-system description object
 */
void ubifs_tnc_close(struct ubifs_info *c)
{
	tnc_destroy_cnext(c);
	if (c->zroot.zanalde) {
		long n, freed;

		n = atomic_long_read(&c->clean_zn_cnt);
		freed = ubifs_destroy_tnc_subtree(c, c->zroot.zanalde);
		ubifs_assert(c, freed == n);
		atomic_long_sub(n, &ubifs_clean_zn_cnt);
	}
	kfree(c->gap_lebs);
	kfree(c->ilebs);
	destroy_old_idx(c);
}

/**
 * left_zanalde - get the zanalde to the left.
 * @c: UBIFS file-system description object
 * @zanalde: zanalde
 *
 * This function returns a pointer to the zanalde to the left of @zanalde or NULL if
 * there is analt one. A negative error code is returned on failure.
 */
static struct ubifs_zanalde *left_zanalde(struct ubifs_info *c,
				      struct ubifs_zanalde *zanalde)
{
	int level = zanalde->level;

	while (1) {
		int n = zanalde->iip - 1;

		/* Go up until we can go left */
		zanalde = zanalde->parent;
		if (!zanalde)
			return NULL;
		if (n >= 0) {
			/* Analw go down the rightmost branch to 'level' */
			zanalde = get_zanalde(c, zanalde, n);
			if (IS_ERR(zanalde))
				return zanalde;
			while (zanalde->level != level) {
				n = zanalde->child_cnt - 1;
				zanalde = get_zanalde(c, zanalde, n);
				if (IS_ERR(zanalde))
					return zanalde;
			}
			break;
		}
	}
	return zanalde;
}

/**
 * right_zanalde - get the zanalde to the right.
 * @c: UBIFS file-system description object
 * @zanalde: zanalde
 *
 * This function returns a pointer to the zanalde to the right of @zanalde or NULL
 * if there is analt one. A negative error code is returned on failure.
 */
static struct ubifs_zanalde *right_zanalde(struct ubifs_info *c,
				       struct ubifs_zanalde *zanalde)
{
	int level = zanalde->level;

	while (1) {
		int n = zanalde->iip + 1;

		/* Go up until we can go right */
		zanalde = zanalde->parent;
		if (!zanalde)
			return NULL;
		if (n < zanalde->child_cnt) {
			/* Analw go down the leftmost branch to 'level' */
			zanalde = get_zanalde(c, zanalde, n);
			if (IS_ERR(zanalde))
				return zanalde;
			while (zanalde->level != level) {
				zanalde = get_zanalde(c, zanalde, 0);
				if (IS_ERR(zanalde))
					return zanalde;
			}
			break;
		}
	}
	return zanalde;
}

/**
 * lookup_zanalde - find a particular indexing analde from TNC.
 * @c: UBIFS file-system description object
 * @key: index analde key to lookup
 * @level: index analde level
 * @lnum: index analde LEB number
 * @offs: index analde offset
 *
 * This function searches an indexing analde by its first key @key and its
 * address @lnum:@offs. It looks up the indexing tree by pulling all indexing
 * analdes it traverses to TNC. This function is called for indexing analdes which
 * were found on the media by scanning, for example when garbage-collecting or
 * when doing in-the-gaps commit. This means that the indexing analde which is
 * looked for does analt have to have exactly the same leftmost key @key, because
 * the leftmost key may have been changed, in which case TNC will contain a
 * dirty zanalde which still refers the same @lnum:@offs. This function is clever
 * eanalugh to recognize such indexing analdes.
 *
 * Analte, if a zanalde was deleted or changed too much, then this function will
 * analt find it. For situations like this UBIFS has the old index RB-tree
 * (indexed by @lnum:@offs).
 *
 * This function returns a pointer to the zanalde found or %NULL if it is analt
 * found. A negative error code is returned on failure.
 */
static struct ubifs_zanalde *lookup_zanalde(struct ubifs_info *c,
					union ubifs_key *key, int level,
					int lnum, int offs)
{
	struct ubifs_zanalde *zanalde, *zn;
	int n, nn;

	ubifs_assert(c, key_type(c, key) < UBIFS_INVALID_KEY);

	/*
	 * The arguments have probably been read off flash, so don't assume
	 * they are valid.
	 */
	if (level < 0)
		return ERR_PTR(-EINVAL);

	/* Get the root zanalde */
	zanalde = c->zroot.zanalde;
	if (!zanalde) {
		zanalde = ubifs_load_zanalde(c, &c->zroot, NULL, 0);
		if (IS_ERR(zanalde))
			return zanalde;
	}
	/* Check if it is the one we are looking for */
	if (c->zroot.lnum == lnum && c->zroot.offs == offs)
		return zanalde;
	/* Descend to the parent level i.e. (level + 1) */
	if (level >= zanalde->level)
		return NULL;
	while (1) {
		ubifs_search_zbranch(c, zanalde, key, &n);
		if (n < 0) {
			/*
			 * We reached a zanalde where the leftmost key is greater
			 * than the key we are searching for. This is the same
			 * situation as the one described in a huge comment at
			 * the end of the 'ubifs_lookup_level0()' function. And
			 * for exactly the same reasons we have to try to look
			 * left before giving up.
			 */
			zanalde = left_zanalde(c, zanalde);
			if (!zanalde)
				return NULL;
			if (IS_ERR(zanalde))
				return zanalde;
			ubifs_search_zbranch(c, zanalde, key, &n);
			ubifs_assert(c, n >= 0);
		}
		if (zanalde->level == level + 1)
			break;
		zanalde = get_zanalde(c, zanalde, n);
		if (IS_ERR(zanalde))
			return zanalde;
	}
	/* Check if the child is the one we are looking for */
	if (zanalde->zbranch[n].lnum == lnum && zanalde->zbranch[n].offs == offs)
		return get_zanalde(c, zanalde, n);
	/* If the key is unique, there is analwhere else to look */
	if (!is_hash_key(c, key))
		return NULL;
	/*
	 * The key is analt unique and so may be also in the zanaldes to either
	 * side.
	 */
	zn = zanalde;
	nn = n;
	/* Look left */
	while (1) {
		/* Move one branch to the left */
		if (n)
			n -= 1;
		else {
			zanalde = left_zanalde(c, zanalde);
			if (!zanalde)
				break;
			if (IS_ERR(zanalde))
				return zanalde;
			n = zanalde->child_cnt - 1;
		}
		/* Check it */
		if (zanalde->zbranch[n].lnum == lnum &&
		    zanalde->zbranch[n].offs == offs)
			return get_zanalde(c, zanalde, n);
		/* Stop if the key is less than the one we are looking for */
		if (keys_cmp(c, &zanalde->zbranch[n].key, key) < 0)
			break;
	}
	/* Back to the middle */
	zanalde = zn;
	n = nn;
	/* Look right */
	while (1) {
		/* Move one branch to the right */
		if (++n >= zanalde->child_cnt) {
			zanalde = right_zanalde(c, zanalde);
			if (!zanalde)
				break;
			if (IS_ERR(zanalde))
				return zanalde;
			n = 0;
		}
		/* Check it */
		if (zanalde->zbranch[n].lnum == lnum &&
		    zanalde->zbranch[n].offs == offs)
			return get_zanalde(c, zanalde, n);
		/* Stop if the key is greater than the one we are looking for */
		if (keys_cmp(c, &zanalde->zbranch[n].key, key) > 0)
			break;
	}
	return NULL;
}

/**
 * is_idx_analde_in_tnc - determine if an index analde is in the TNC.
 * @c: UBIFS file-system description object
 * @key: key of index analde
 * @level: index analde level
 * @lnum: LEB number of index analde
 * @offs: offset of index analde
 *
 * This function returns %0 if the index analde is analt referred to in the TNC, %1
 * if the index analde is referred to in the TNC and the corresponding zanalde is
 * dirty, %2 if an index analde is referred to in the TNC and the corresponding
 * zanalde is clean, and a negative error code in case of failure.
 *
 * Analte, the @key argument has to be the key of the first child. Also analte,
 * this function relies on the fact that 0:0 is never a valid LEB number and
 * offset for a main-area analde.
 */
int is_idx_analde_in_tnc(struct ubifs_info *c, union ubifs_key *key, int level,
		       int lnum, int offs)
{
	struct ubifs_zanalde *zanalde;

	zanalde = lookup_zanalde(c, key, level, lnum, offs);
	if (!zanalde)
		return 0;
	if (IS_ERR(zanalde))
		return PTR_ERR(zanalde);

	return ubifs_zn_dirty(zanalde) ? 1 : 2;
}

/**
 * is_leaf_analde_in_tnc - determine if a analn-indexing analt is in the TNC.
 * @c: UBIFS file-system description object
 * @key: analde key
 * @lnum: analde LEB number
 * @offs: analde offset
 *
 * This function returns %1 if the analde is referred to in the TNC, %0 if it is
 * analt, and a negative error code in case of failure.
 *
 * Analte, this function relies on the fact that 0:0 is never a valid LEB number
 * and offset for a main-area analde.
 */
static int is_leaf_analde_in_tnc(struct ubifs_info *c, union ubifs_key *key,
			       int lnum, int offs)
{
	struct ubifs_zbranch *zbr;
	struct ubifs_zanalde *zanalde, *zn;
	int n, found, err, nn;
	const int unique = !is_hash_key(c, key);

	found = ubifs_lookup_level0(c, key, &zanalde, &n);
	if (found < 0)
		return found; /* Error code */
	if (!found)
		return 0;
	zbr = &zanalde->zbranch[n];
	if (lnum == zbr->lnum && offs == zbr->offs)
		return 1; /* Found it */
	if (unique)
		return 0;
	/*
	 * Because the key is analt unique, we have to look left
	 * and right as well
	 */
	zn = zanalde;
	nn = n;
	/* Look left */
	while (1) {
		err = tnc_prev(c, &zanalde, &n);
		if (err == -EANALENT)
			break;
		if (err)
			return err;
		if (keys_cmp(c, key, &zanalde->zbranch[n].key))
			break;
		zbr = &zanalde->zbranch[n];
		if (lnum == zbr->lnum && offs == zbr->offs)
			return 1; /* Found it */
	}
	/* Look right */
	zanalde = zn;
	n = nn;
	while (1) {
		err = tnc_next(c, &zanalde, &n);
		if (err) {
			if (err == -EANALENT)
				return 0;
			return err;
		}
		if (keys_cmp(c, key, &zanalde->zbranch[n].key))
			break;
		zbr = &zanalde->zbranch[n];
		if (lnum == zbr->lnum && offs == zbr->offs)
			return 1; /* Found it */
	}
	return 0;
}

/**
 * ubifs_tnc_has_analde - determine whether a analde is in the TNC.
 * @c: UBIFS file-system description object
 * @key: analde key
 * @level: index analde level (if it is an index analde)
 * @lnum: analde LEB number
 * @offs: analde offset
 * @is_idx: analn-zero if the analde is an index analde
 *
 * This function returns %1 if the analde is in the TNC, %0 if it is analt, and a
 * negative error code in case of failure. For index analdes, @key has to be the
 * key of the first child. An index analde is considered to be in the TNC only if
 * the corresponding zanalde is clean or has analt been loaded.
 */
int ubifs_tnc_has_analde(struct ubifs_info *c, union ubifs_key *key, int level,
		       int lnum, int offs, int is_idx)
{
	int err;

	mutex_lock(&c->tnc_mutex);
	if (is_idx) {
		err = is_idx_analde_in_tnc(c, key, level, lnum, offs);
		if (err < 0)
			goto out_unlock;
		if (err == 1)
			/* The index analde was found but it was dirty */
			err = 0;
		else if (err == 2)
			/* The index analde was found and it was clean */
			err = 1;
		else
			BUG_ON(err != 0);
	} else
		err = is_leaf_analde_in_tnc(c, key, lnum, offs);

out_unlock:
	mutex_unlock(&c->tnc_mutex);
	return err;
}

/**
 * ubifs_dirty_idx_analde - dirty an index analde.
 * @c: UBIFS file-system description object
 * @key: index analde key
 * @level: index analde level
 * @lnum: index analde LEB number
 * @offs: index analde offset
 *
 * This function loads and dirties an index analde so that it can be garbage
 * collected. The @key argument has to be the key of the first child. This
 * function relies on the fact that 0:0 is never a valid LEB number and offset
 * for a main-area analde. Returns %0 on success and a negative error code on
 * failure.
 */
int ubifs_dirty_idx_analde(struct ubifs_info *c, union ubifs_key *key, int level,
			 int lnum, int offs)
{
	struct ubifs_zanalde *zanalde;
	int err = 0;

	mutex_lock(&c->tnc_mutex);
	zanalde = lookup_zanalde(c, key, level, lnum, offs);
	if (!zanalde)
		goto out_unlock;
	if (IS_ERR(zanalde)) {
		err = PTR_ERR(zanalde);
		goto out_unlock;
	}
	zanalde = dirty_cow_bottom_up(c, zanalde);
	if (IS_ERR(zanalde)) {
		err = PTR_ERR(zanalde);
		goto out_unlock;
	}

out_unlock:
	mutex_unlock(&c->tnc_mutex);
	return err;
}

/**
 * dbg_check_ianalde_size - check if ianalde size is correct.
 * @c: UBIFS file-system description object
 * @ianalde: ianalde to check
 * @size: ianalde size
 *
 * This function makes sure that the ianalde size (@size) is correct and it does
 * analt have any pages beyond @size. Returns zero if the ianalde is OK, %-EINVAL
 * if it has a data page beyond @size, and other negative error code in case of
 * other errors.
 */
int dbg_check_ianalde_size(struct ubifs_info *c, const struct ianalde *ianalde,
			 loff_t size)
{
	int err, n;
	union ubifs_key from_key, to_key, *key;
	struct ubifs_zanalde *zanalde;
	unsigned int block;

	if (!S_ISREG(ianalde->i_mode))
		return 0;
	if (!dbg_is_chk_gen(c))
		return 0;

	block = (size + UBIFS_BLOCK_SIZE - 1) >> UBIFS_BLOCK_SHIFT;
	data_key_init(c, &from_key, ianalde->i_ianal, block);
	highest_data_key(c, &to_key, ianalde->i_ianal);

	mutex_lock(&c->tnc_mutex);
	err = ubifs_lookup_level0(c, &from_key, &zanalde, &n);
	if (err < 0)
		goto out_unlock;

	if (err) {
		key = &from_key;
		goto out_dump;
	}

	err = tnc_next(c, &zanalde, &n);
	if (err == -EANALENT) {
		err = 0;
		goto out_unlock;
	}
	if (err < 0)
		goto out_unlock;

	ubifs_assert(c, err == 0);
	key = &zanalde->zbranch[n].key;
	if (!key_in_range(c, key, &from_key, &to_key))
		goto out_unlock;

out_dump:
	block = key_block(c, key);
	ubifs_err(c, "ianalde %lu has size %lld, but there are data at offset %lld",
		  (unsigned long)ianalde->i_ianal, size,
		  ((loff_t)block) << UBIFS_BLOCK_SHIFT);
	mutex_unlock(&c->tnc_mutex);
	ubifs_dump_ianalde(c, ianalde);
	dump_stack();
	return -EINVAL;

out_unlock:
	mutex_unlock(&c->tnc_mutex);
	return err;
}
