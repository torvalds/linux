/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2011, 2017 by Delphix. All rights reserved.
 * Copyright (c) 2014 Spectra Logic Corporation, All rights reserved.
 * Copyright (c) 2014 Integros [integros.com]
 * Copyright 2017 Nexenta Systems, Inc.
 */

#include <sys/zio.h>
#include <sys/spa.h>
#include <sys/dmu.h>
#include <sys/zfs_context.h>
#include <sys/zap.h>
#include <sys/refcount.h>
#include <sys/zap_impl.h>
#include <sys/zap_leaf.h>
#include <sys/avl.h>
#include <sys/arc.h>
#include <sys/dmu_objset.h>

#ifdef _KERNEL
#include <sys/sunddi.h>
#endif

extern inline mzap_phys_t *zap_m_phys(zap_t *zap);

static int mzap_upgrade(zap_t **zapp,
    void *tag, dmu_tx_t *tx, zap_flags_t flags);

uint64_t
zap_getflags(zap_t *zap)
{
	if (zap->zap_ismicro)
		return (0);
	return (zap_f_phys(zap)->zap_flags);
}

int
zap_hashbits(zap_t *zap)
{
	if (zap_getflags(zap) & ZAP_FLAG_HASH64)
		return (48);
	else
		return (28);
}

uint32_t
zap_maxcd(zap_t *zap)
{
	if (zap_getflags(zap) & ZAP_FLAG_HASH64)
		return ((1<<16)-1);
	else
		return (-1U);
}

static uint64_t
zap_hash(zap_name_t *zn)
{
	zap_t *zap = zn->zn_zap;
	uint64_t h = 0;

	if (zap_getflags(zap) & ZAP_FLAG_PRE_HASHED_KEY) {
		ASSERT(zap_getflags(zap) & ZAP_FLAG_UINT64_KEY);
		h = *(uint64_t *)zn->zn_key_orig;
	} else {
		h = zap->zap_salt;
		ASSERT(h != 0);
		ASSERT(zfs_crc64_table[128] == ZFS_CRC64_POLY);

		if (zap_getflags(zap) & ZAP_FLAG_UINT64_KEY) {
			const uint64_t *wp = zn->zn_key_norm;

			ASSERT(zn->zn_key_intlen == 8);
			for (int i = 0; i < zn->zn_key_norm_numints;
			    wp++, i++) {
				uint64_t word = *wp;

				for (int j = 0; j < zn->zn_key_intlen; j++) {
					h = (h >> 8) ^
					    zfs_crc64_table[(h ^ word) & 0xFF];
					word >>= NBBY;
				}
			}
		} else {
			const uint8_t *cp = zn->zn_key_norm;

			/*
			 * We previously stored the terminating null on
			 * disk, but didn't hash it, so we need to
			 * continue to not hash it.  (The
			 * zn_key_*_numints includes the terminating
			 * null for non-binary keys.)
			 */
			int len = zn->zn_key_norm_numints - 1;

			ASSERT(zn->zn_key_intlen == 1);
			for (int i = 0; i < len; cp++, i++) {
				h = (h >> 8) ^
				    zfs_crc64_table[(h ^ *cp) & 0xFF];
			}
		}
	}
	/*
	 * Don't use all 64 bits, since we need some in the cookie for
	 * the collision differentiator.  We MUST use the high bits,
	 * since those are the ones that we first pay attention to when
	 * chosing the bucket.
	 */
	h &= ~((1ULL << (64 - zap_hashbits(zap))) - 1);

	return (h);
}

static int
zap_normalize(zap_t *zap, const char *name, char *namenorm, int normflags)
{
	ASSERT(!(zap_getflags(zap) & ZAP_FLAG_UINT64_KEY));

	size_t inlen = strlen(name) + 1;
	size_t outlen = ZAP_MAXNAMELEN;

	int err = 0;
	(void) u8_textprep_str((char *)name, &inlen, namenorm, &outlen,
	    normflags | U8_TEXTPREP_IGNORE_NULL | U8_TEXTPREP_IGNORE_INVALID,
	    U8_UNICODE_LATEST, &err);

	return (err);
}

boolean_t
zap_match(zap_name_t *zn, const char *matchname)
{
	ASSERT(!(zap_getflags(zn->zn_zap) & ZAP_FLAG_UINT64_KEY));

	if (zn->zn_matchtype & MT_NORMALIZE) {
		char norm[ZAP_MAXNAMELEN];

		if (zap_normalize(zn->zn_zap, matchname, norm,
		    zn->zn_normflags) != 0)
			return (B_FALSE);

		return (strcmp(zn->zn_key_norm, norm) == 0);
	} else {
		return (strcmp(zn->zn_key_orig, matchname) == 0);
	}
}

void
zap_name_free(zap_name_t *zn)
{
	kmem_free(zn, sizeof (zap_name_t));
}

zap_name_t *
zap_name_alloc(zap_t *zap, const char *key, matchtype_t mt)
{
	zap_name_t *zn = kmem_alloc(sizeof (zap_name_t), KM_SLEEP);

	zn->zn_zap = zap;
	zn->zn_key_intlen = sizeof (*key);
	zn->zn_key_orig = key;
	zn->zn_key_orig_numints = strlen(zn->zn_key_orig) + 1;
	zn->zn_matchtype = mt;
	zn->zn_normflags = zap->zap_normflags;

	/*
	 * If we're dealing with a case sensitive lookup on a mixed or
	 * insensitive fs, remove U8_TEXTPREP_TOUPPER or the lookup
	 * will fold case to all caps overriding the lookup request.
	 */
	if (mt & MT_MATCH_CASE)
		zn->zn_normflags &= ~U8_TEXTPREP_TOUPPER;

	if (zap->zap_normflags) {
		/*
		 * We *must* use zap_normflags because this normalization is
		 * what the hash is computed from.
		 */
		if (zap_normalize(zap, key, zn->zn_normbuf,
		    zap->zap_normflags) != 0) {
			zap_name_free(zn);
			return (NULL);
		}
		zn->zn_key_norm = zn->zn_normbuf;
		zn->zn_key_norm_numints = strlen(zn->zn_key_norm) + 1;
	} else {
		if (mt != 0) {
			zap_name_free(zn);
			return (NULL);
		}
		zn->zn_key_norm = zn->zn_key_orig;
		zn->zn_key_norm_numints = zn->zn_key_orig_numints;
	}

	zn->zn_hash = zap_hash(zn);

	if (zap->zap_normflags != zn->zn_normflags) {
		/*
		 * We *must* use zn_normflags because this normalization is
		 * what the matching is based on.  (Not the hash!)
		 */
		if (zap_normalize(zap, key, zn->zn_normbuf,
		    zn->zn_normflags) != 0) {
			zap_name_free(zn);
			return (NULL);
		}
		zn->zn_key_norm_numints = strlen(zn->zn_key_norm) + 1;
	}

	return (zn);
}

zap_name_t *
zap_name_alloc_uint64(zap_t *zap, const uint64_t *key, int numints)
{
	zap_name_t *zn = kmem_alloc(sizeof (zap_name_t), KM_SLEEP);

	ASSERT(zap->zap_normflags == 0);
	zn->zn_zap = zap;
	zn->zn_key_intlen = sizeof (*key);
	zn->zn_key_orig = zn->zn_key_norm = key;
	zn->zn_key_orig_numints = zn->zn_key_norm_numints = numints;
	zn->zn_matchtype = 0;

	zn->zn_hash = zap_hash(zn);
	return (zn);
}

static void
mzap_byteswap(mzap_phys_t *buf, size_t size)
{
	buf->mz_block_type = BSWAP_64(buf->mz_block_type);
	buf->mz_salt = BSWAP_64(buf->mz_salt);
	buf->mz_normflags = BSWAP_64(buf->mz_normflags);
	int max = (size / MZAP_ENT_LEN) - 1;
	for (int i = 0; i < max; i++) {
		buf->mz_chunk[i].mze_value =
		    BSWAP_64(buf->mz_chunk[i].mze_value);
		buf->mz_chunk[i].mze_cd =
		    BSWAP_32(buf->mz_chunk[i].mze_cd);
	}
}

void
zap_byteswap(void *buf, size_t size)
{
	uint64_t block_type = *(uint64_t *)buf;

	if (block_type == ZBT_MICRO || block_type == BSWAP_64(ZBT_MICRO)) {
		/* ASSERT(magic == ZAP_LEAF_MAGIC); */
		mzap_byteswap(buf, size);
	} else {
		fzap_byteswap(buf, size);
	}
}

static int
mze_compare(const void *arg1, const void *arg2)
{
	const mzap_ent_t *mze1 = arg1;
	const mzap_ent_t *mze2 = arg2;

	int cmp = AVL_CMP(mze1->mze_hash, mze2->mze_hash);
	if (likely(cmp))
		return (cmp);

	return (AVL_CMP(mze1->mze_cd, mze2->mze_cd));
}

static int
mze_insert(zap_t *zap, int chunkid, uint64_t hash)
{
	avl_index_t idx;

	ASSERT(zap->zap_ismicro);
	ASSERT(RW_WRITE_HELD(&zap->zap_rwlock));

	mzap_ent_t *mze = kmem_alloc(sizeof (mzap_ent_t), KM_SLEEP);
	mze->mze_chunkid = chunkid;
	mze->mze_hash = hash;
	mze->mze_cd = MZE_PHYS(zap, mze)->mze_cd;
	ASSERT(MZE_PHYS(zap, mze)->mze_name[0] != 0);
	if (avl_find(&zap->zap_m.zap_avl, mze, &idx) != NULL) {
		kmem_free(mze, sizeof (mzap_ent_t));
		return (EEXIST);
	}
	avl_insert(&zap->zap_m.zap_avl, mze, idx);
	return (0);
}

static mzap_ent_t *
mze_find(zap_name_t *zn)
{
	mzap_ent_t mze_tofind;
	mzap_ent_t *mze;
	avl_index_t idx;
	avl_tree_t *avl = &zn->zn_zap->zap_m.zap_avl;

	ASSERT(zn->zn_zap->zap_ismicro);
	ASSERT(RW_LOCK_HELD(&zn->zn_zap->zap_rwlock));

	mze_tofind.mze_hash = zn->zn_hash;
	mze_tofind.mze_cd = 0;

	mze = avl_find(avl, &mze_tofind, &idx);
	if (mze == NULL)
		mze = avl_nearest(avl, idx, AVL_AFTER);
	for (; mze && mze->mze_hash == zn->zn_hash; mze = AVL_NEXT(avl, mze)) {
		ASSERT3U(mze->mze_cd, ==, MZE_PHYS(zn->zn_zap, mze)->mze_cd);
		if (zap_match(zn, MZE_PHYS(zn->zn_zap, mze)->mze_name))
			return (mze);
	}

	return (NULL);
}

static uint32_t
mze_find_unused_cd(zap_t *zap, uint64_t hash)
{
	mzap_ent_t mze_tofind;
	avl_index_t idx;
	avl_tree_t *avl = &zap->zap_m.zap_avl;

	ASSERT(zap->zap_ismicro);
	ASSERT(RW_LOCK_HELD(&zap->zap_rwlock));

	mze_tofind.mze_hash = hash;
	mze_tofind.mze_cd = 0;

	uint32_t cd = 0;
	for (mzap_ent_t *mze = avl_find(avl, &mze_tofind, &idx);
	    mze && mze->mze_hash == hash; mze = AVL_NEXT(avl, mze)) {
		if (mze->mze_cd != cd)
			break;
		cd++;
	}

	return (cd);
}

static void
mze_remove(zap_t *zap, mzap_ent_t *mze)
{
	ASSERT(zap->zap_ismicro);
	ASSERT(RW_WRITE_HELD(&zap->zap_rwlock));

	avl_remove(&zap->zap_m.zap_avl, mze);
	kmem_free(mze, sizeof (mzap_ent_t));
}

static void
mze_destroy(zap_t *zap)
{
	mzap_ent_t *mze;
	void *avlcookie = NULL;

	while (mze = avl_destroy_nodes(&zap->zap_m.zap_avl, &avlcookie))
		kmem_free(mze, sizeof (mzap_ent_t));
	avl_destroy(&zap->zap_m.zap_avl);
}

static zap_t *
mzap_open(objset_t *os, uint64_t obj, dmu_buf_t *db)
{
	zap_t *winner;
	uint64_t *zap_hdr = (uint64_t *)db->db_data;
	uint64_t zap_block_type = zap_hdr[0];
	uint64_t zap_magic = zap_hdr[1];

	ASSERT3U(MZAP_ENT_LEN, ==, sizeof (mzap_ent_phys_t));

	zap_t *zap = kmem_zalloc(sizeof (zap_t), KM_SLEEP);
	rw_init(&zap->zap_rwlock, 0, 0, 0);
	rw_enter(&zap->zap_rwlock, RW_WRITER);
	zap->zap_objset = os;
	zap->zap_object = obj;
	zap->zap_dbuf = db;

	if (zap_block_type != ZBT_MICRO) {
		mutex_init(&zap->zap_f.zap_num_entries_mtx, 0, 0, 0);
		zap->zap_f.zap_block_shift = highbit64(db->db_size) - 1;
		if (zap_block_type != ZBT_HEADER || zap_magic != ZAP_MAGIC) {
			winner = NULL;	/* No actual winner here... */
			goto handle_winner;
		}
	} else {
		zap->zap_ismicro = TRUE;
	}

	/*
	 * Make sure that zap_ismicro is set before we let others see
	 * it, because zap_lockdir() checks zap_ismicro without the lock
	 * held.
	 */
	dmu_buf_init_user(&zap->zap_dbu, zap_evict_sync, NULL, &zap->zap_dbuf);
	winner = dmu_buf_set_user(db, &zap->zap_dbu);

	if (winner != NULL)
		goto handle_winner;

	if (zap->zap_ismicro) {
		zap->zap_salt = zap_m_phys(zap)->mz_salt;
		zap->zap_normflags = zap_m_phys(zap)->mz_normflags;
		zap->zap_m.zap_num_chunks = db->db_size / MZAP_ENT_LEN - 1;
		avl_create(&zap->zap_m.zap_avl, mze_compare,
		    sizeof (mzap_ent_t), offsetof(mzap_ent_t, mze_node));

		for (int i = 0; i < zap->zap_m.zap_num_chunks; i++) {
			mzap_ent_phys_t *mze =
			    &zap_m_phys(zap)->mz_chunk[i];
			if (mze->mze_name[0]) {
				zap_name_t *zn;

				zn = zap_name_alloc(zap, mze->mze_name, 0);
				if (mze_insert(zap, i, zn->zn_hash) == 0)
					zap->zap_m.zap_num_entries++;
				else {
					printf("ZFS WARNING: Duplicated ZAP "
					    "entry detected (%s).\n",
					    mze->mze_name);
				}
				zap_name_free(zn);
			}
		}
	} else {
		zap->zap_salt = zap_f_phys(zap)->zap_salt;
		zap->zap_normflags = zap_f_phys(zap)->zap_normflags;

		ASSERT3U(sizeof (struct zap_leaf_header), ==,
		    2*ZAP_LEAF_CHUNKSIZE);

		/*
		 * The embedded pointer table should not overlap the
		 * other members.
		 */
		ASSERT3P(&ZAP_EMBEDDED_PTRTBL_ENT(zap, 0), >,
		    &zap_f_phys(zap)->zap_salt);

		/*
		 * The embedded pointer table should end at the end of
		 * the block
		 */
		ASSERT3U((uintptr_t)&ZAP_EMBEDDED_PTRTBL_ENT(zap,
		    1<<ZAP_EMBEDDED_PTRTBL_SHIFT(zap)) -
		    (uintptr_t)zap_f_phys(zap), ==,
		    zap->zap_dbuf->db_size);
	}
	rw_exit(&zap->zap_rwlock);
	return (zap);

handle_winner:
	rw_exit(&zap->zap_rwlock);
	rw_destroy(&zap->zap_rwlock);
	if (!zap->zap_ismicro)
		mutex_destroy(&zap->zap_f.zap_num_entries_mtx);
	kmem_free(zap, sizeof (zap_t));
	return (winner);
}

/*
 * This routine "consumes" the caller's hold on the dbuf, which must
 * have the specified tag.
 */
static int
zap_lockdir_impl(dmu_buf_t *db, void *tag, dmu_tx_t *tx,
    krw_t lti, boolean_t fatreader, boolean_t adding, zap_t **zapp)
{
	ASSERT0(db->db_offset);
	objset_t *os = dmu_buf_get_objset(db);
	uint64_t obj = db->db_object;

	*zapp = NULL;

	zap_t *zap = dmu_buf_get_user(db);
	if (zap == NULL) {
		zap = mzap_open(os, obj, db);
		if (zap == NULL) {
			/*
			 * mzap_open() didn't like what it saw on-disk.
			 * Check for corruption!
			 */
			return (SET_ERROR(EIO));
		}
	}

	/*
	 * We're checking zap_ismicro without the lock held, in order to
	 * tell what type of lock we want.  Once we have some sort of
	 * lock, see if it really is the right type.  In practice this
	 * can only be different if it was upgraded from micro to fat,
	 * and micro wanted WRITER but fat only needs READER.
	 */
	krw_t lt = (!zap->zap_ismicro && fatreader) ? RW_READER : lti;
	rw_enter(&zap->zap_rwlock, lt);
	if (lt != ((!zap->zap_ismicro && fatreader) ? RW_READER : lti)) {
		/* it was upgraded, now we only need reader */
		ASSERT(lt == RW_WRITER);
		ASSERT(RW_READER ==
		    (!zap->zap_ismicro && fatreader) ? RW_READER : lti);
		rw_downgrade(&zap->zap_rwlock);
		lt = RW_READER;
	}

	zap->zap_objset = os;

	if (lt == RW_WRITER)
		dmu_buf_will_dirty(db, tx);

	ASSERT3P(zap->zap_dbuf, ==, db);

	ASSERT(!zap->zap_ismicro ||
	    zap->zap_m.zap_num_entries <= zap->zap_m.zap_num_chunks);
	if (zap->zap_ismicro && tx && adding &&
	    zap->zap_m.zap_num_entries == zap->zap_m.zap_num_chunks) {
		uint64_t newsz = db->db_size + SPA_MINBLOCKSIZE;
		if (newsz > MZAP_MAX_BLKSZ) {
			dprintf("upgrading obj %llu: num_entries=%u\n",
			    obj, zap->zap_m.zap_num_entries);
			*zapp = zap;
			int err = mzap_upgrade(zapp, tag, tx, 0);
			if (err != 0)
				rw_exit(&zap->zap_rwlock);
			return (err);
		}
		VERIFY0(dmu_object_set_blocksize(os, obj, newsz, 0, tx));
		zap->zap_m.zap_num_chunks =
		    db->db_size / MZAP_ENT_LEN - 1;
	}

	*zapp = zap;
	return (0);
}

static int
zap_lockdir_by_dnode(dnode_t *dn, dmu_tx_t *tx,
    krw_t lti, boolean_t fatreader, boolean_t adding, void *tag, zap_t **zapp)
{
	dmu_buf_t *db;

	int err = dmu_buf_hold_by_dnode(dn, 0, tag, &db, DMU_READ_NO_PREFETCH);
	if (err != 0) {
		return (err);
	}
#ifdef ZFS_DEBUG
	{
		dmu_object_info_t doi;
		dmu_object_info_from_db(db, &doi);
		ASSERT3U(DMU_OT_BYTESWAP(doi.doi_type), ==, DMU_BSWAP_ZAP);
	}
#endif

	err = zap_lockdir_impl(db, tag, tx, lti, fatreader, adding, zapp);
	if (err != 0) {
		dmu_buf_rele(db, tag);
	}
	return (err);
}

int
zap_lockdir(objset_t *os, uint64_t obj, dmu_tx_t *tx,
    krw_t lti, boolean_t fatreader, boolean_t adding, void *tag, zap_t **zapp)
{
	dmu_buf_t *db;

	int err = dmu_buf_hold(os, obj, 0, tag, &db, DMU_READ_NO_PREFETCH);
	if (err != 0)
		return (err);
#ifdef ZFS_DEBUG
	{
		dmu_object_info_t doi;
		dmu_object_info_from_db(db, &doi);
		ASSERT3U(DMU_OT_BYTESWAP(doi.doi_type), ==, DMU_BSWAP_ZAP);
	}
#endif
	err = zap_lockdir_impl(db, tag, tx, lti, fatreader, adding, zapp);
	if (err != 0)
		dmu_buf_rele(db, tag);
	return (err);
}

void
zap_unlockdir(zap_t *zap, void *tag)
{
	rw_exit(&zap->zap_rwlock);
	dmu_buf_rele(zap->zap_dbuf, tag);
}

static int
mzap_upgrade(zap_t **zapp, void *tag, dmu_tx_t *tx, zap_flags_t flags)
{
	int err = 0;
	zap_t *zap = *zapp;

	ASSERT(RW_WRITE_HELD(&zap->zap_rwlock));

	int sz = zap->zap_dbuf->db_size;
	mzap_phys_t *mzp = zio_buf_alloc(sz);
	bcopy(zap->zap_dbuf->db_data, mzp, sz);
	int nchunks = zap->zap_m.zap_num_chunks;

	if (!flags) {
		err = dmu_object_set_blocksize(zap->zap_objset, zap->zap_object,
		    1ULL << fzap_default_block_shift, 0, tx);
		if (err != 0) {
			zio_buf_free(mzp, sz);
			return (err);
		}
	}

	dprintf("upgrading obj=%llu with %u chunks\n",
	    zap->zap_object, nchunks);
	/* XXX destroy the avl later, so we can use the stored hash value */
	mze_destroy(zap);

	fzap_upgrade(zap, tx, flags);

	for (int i = 0; i < nchunks; i++) {
		mzap_ent_phys_t *mze = &mzp->mz_chunk[i];
		if (mze->mze_name[0] == 0)
			continue;
		dprintf("adding %s=%llu\n",
		    mze->mze_name, mze->mze_value);
		zap_name_t *zn = zap_name_alloc(zap, mze->mze_name, 0);
		err = fzap_add_cd(zn, 8, 1, &mze->mze_value, mze->mze_cd,
		    tag, tx);
		zap = zn->zn_zap;	/* fzap_add_cd() may change zap */
		zap_name_free(zn);
		if (err != 0)
			break;
	}
	zio_buf_free(mzp, sz);
	*zapp = zap;
	return (err);
}

/*
 * The "normflags" determine the behavior of the matchtype_t which is
 * passed to zap_lookup_norm().  Names which have the same normalized
 * version will be stored with the same hash value, and therefore we can
 * perform normalization-insensitive lookups.  We can be Unicode form-
 * insensitive and/or case-insensitive.  The following flags are valid for
 * "normflags":
 *
 * U8_TEXTPREP_NFC
 * U8_TEXTPREP_NFD
 * U8_TEXTPREP_NFKC
 * U8_TEXTPREP_NFKD
 * U8_TEXTPREP_TOUPPER
 *
 * The *_NF* (Normalization Form) flags are mutually exclusive; at most one
 * of them may be supplied.
 */
void
mzap_create_impl(objset_t *os, uint64_t obj, int normflags, zap_flags_t flags,
    dmu_tx_t *tx)
{
	dmu_buf_t *db;

	VERIFY0(dmu_buf_hold(os, obj, 0, FTAG, &db, DMU_READ_NO_PREFETCH));

	dmu_buf_will_dirty(db, tx);
	mzap_phys_t *zp = db->db_data;
	zp->mz_block_type = ZBT_MICRO;
	zp->mz_salt = ((uintptr_t)db ^ (uintptr_t)tx ^ (obj << 1)) | 1ULL;
	zp->mz_normflags = normflags;

	if (flags != 0) {
		zap_t *zap;
		/* Only fat zap supports flags; upgrade immediately. */
		VERIFY0(zap_lockdir_impl(db, FTAG, tx, RW_WRITER,
		    B_FALSE, B_FALSE, &zap));
		VERIFY0(mzap_upgrade(&zap, FTAG, tx, flags));
		zap_unlockdir(zap, FTAG);
	} else {
		dmu_buf_rele(db, FTAG);
	}
}

int
zap_create_claim(objset_t *os, uint64_t obj, dmu_object_type_t ot,
    dmu_object_type_t bonustype, int bonuslen, dmu_tx_t *tx)
{
	return (zap_create_claim_dnsize(os, obj, ot, bonustype, bonuslen,
	    0, tx));
}

int
zap_create_claim_dnsize(objset_t *os, uint64_t obj, dmu_object_type_t ot,
    dmu_object_type_t bonustype, int bonuslen, int dnodesize, dmu_tx_t *tx)
{
	return (zap_create_claim_norm_dnsize(os, obj,
	    0, ot, bonustype, bonuslen, dnodesize, tx));
}

int
zap_create_claim_norm(objset_t *os, uint64_t obj, int normflags,
    dmu_object_type_t ot,
    dmu_object_type_t bonustype, int bonuslen, dmu_tx_t *tx)
{
	return (zap_create_claim_norm_dnsize(os, obj, normflags, ot, bonustype,
	    bonuslen, 0, tx));
}

int
zap_create_claim_norm_dnsize(objset_t *os, uint64_t obj, int normflags,
    dmu_object_type_t ot, dmu_object_type_t bonustype, int bonuslen,
    int dnodesize, dmu_tx_t *tx)
 {
 	int err;
 
	err = dmu_object_claim_dnsize(os, obj, ot, 0, bonustype, bonuslen,
	    dnodesize, tx);
	if (err != 0)
		return (err);
	mzap_create_impl(os, obj, normflags, 0, tx);
	return (0);
}

uint64_t
zap_create(objset_t *os, dmu_object_type_t ot,
    dmu_object_type_t bonustype, int bonuslen, dmu_tx_t *tx)
{
	return (zap_create_norm(os, 0, ot, bonustype, bonuslen, tx));
}

uint64_t
zap_create_dnsize(objset_t *os, dmu_object_type_t ot,
    dmu_object_type_t bonustype, int bonuslen, int dnodesize, dmu_tx_t *tx)
{
	return (zap_create_norm_dnsize(os, 0, ot, bonustype, bonuslen,
	    dnodesize, tx));
}

uint64_t
zap_create_norm(objset_t *os, int normflags, dmu_object_type_t ot,
    dmu_object_type_t bonustype, int bonuslen, dmu_tx_t *tx)
{
	ASSERT3U(DMU_OT_BYTESWAP(ot), ==, DMU_BSWAP_ZAP);
	return (zap_create_norm_dnsize(os, normflags, ot, bonustype, bonuslen,
	    0, tx));
}

uint64_t
zap_create_norm_dnsize(objset_t *os, int normflags, dmu_object_type_t ot,
    dmu_object_type_t bonustype, int bonuslen, int dnodesize, dmu_tx_t *tx)
{
	uint64_t obj = dmu_object_alloc_dnsize(os, ot, 0, bonustype, bonuslen,
	    dnodesize, tx);

	mzap_create_impl(os, obj, normflags, 0, tx);
	return (obj);
}

uint64_t
zap_create_flags(objset_t *os, int normflags, zap_flags_t flags,
    dmu_object_type_t ot, int leaf_blockshift, int indirect_blockshift,
    dmu_object_type_t bonustype, int bonuslen, dmu_tx_t *tx)
{
	ASSERT3U(DMU_OT_BYTESWAP(ot), ==, DMU_BSWAP_ZAP);
	return (zap_create_flags_dnsize(os, normflags, flags, ot,
	    leaf_blockshift, indirect_blockshift, bonustype, bonuslen, 0, tx));
}

uint64_t
zap_create_flags_dnsize(objset_t *os, int normflags, zap_flags_t flags,
    dmu_object_type_t ot, int leaf_blockshift, int indirect_blockshift,
    dmu_object_type_t bonustype, int bonuslen, int dnodesize, dmu_tx_t *tx)
{
	uint64_t obj = dmu_object_alloc_dnsize(os, ot, 0, bonustype, bonuslen,
	    dnodesize, tx);

	ASSERT(leaf_blockshift >= SPA_MINBLOCKSHIFT &&
	    leaf_blockshift <= SPA_OLD_MAXBLOCKSHIFT &&
	    indirect_blockshift >= SPA_MINBLOCKSHIFT &&
	    indirect_blockshift <= SPA_OLD_MAXBLOCKSHIFT);

	VERIFY(dmu_object_set_blocksize(os, obj,
	    1ULL << leaf_blockshift, indirect_blockshift, tx) == 0);

	mzap_create_impl(os, obj, normflags, flags, tx);
	return (obj);
}

int
zap_destroy(objset_t *os, uint64_t zapobj, dmu_tx_t *tx)
{
	/*
	 * dmu_object_free will free the object number and free the
	 * data.  Freeing the data will cause our pageout function to be
	 * called, which will destroy our data (zap_leaf_t's and zap_t).
	 */

	return (dmu_object_free(os, zapobj, tx));
}

void
zap_evict_sync(void *dbu)
{
	zap_t *zap = dbu;

	rw_destroy(&zap->zap_rwlock);

	if (zap->zap_ismicro)
		mze_destroy(zap);
	else
		mutex_destroy(&zap->zap_f.zap_num_entries_mtx);

	kmem_free(zap, sizeof (zap_t));
}

int
zap_count(objset_t *os, uint64_t zapobj, uint64_t *count)
{
	zap_t *zap;

	int err =
	    zap_lockdir(os, zapobj, NULL, RW_READER, TRUE, FALSE, FTAG, &zap);
	if (err != 0)
		return (err);
	if (!zap->zap_ismicro) {
		err = fzap_count(zap, count);
	} else {
		*count = zap->zap_m.zap_num_entries;
	}
	zap_unlockdir(zap, FTAG);
	return (err);
}

/*
 * zn may be NULL; if not specified, it will be computed if needed.
 * See also the comment above zap_entry_normalization_conflict().
 */
static boolean_t
mzap_normalization_conflict(zap_t *zap, zap_name_t *zn, mzap_ent_t *mze)
{
	int direction = AVL_BEFORE;
	boolean_t allocdzn = B_FALSE;

	if (zap->zap_normflags == 0)
		return (B_FALSE);

again:
	for (mzap_ent_t *other = avl_walk(&zap->zap_m.zap_avl, mze, direction);
	    other && other->mze_hash == mze->mze_hash;
	    other = avl_walk(&zap->zap_m.zap_avl, other, direction)) {

		if (zn == NULL) {
			zn = zap_name_alloc(zap, MZE_PHYS(zap, mze)->mze_name,
			    MT_NORMALIZE);
			allocdzn = B_TRUE;
		}
		if (zap_match(zn, MZE_PHYS(zap, other)->mze_name)) {
			if (allocdzn)
				zap_name_free(zn);
			return (B_TRUE);
		}
	}

	if (direction == AVL_BEFORE) {
		direction = AVL_AFTER;
		goto again;
	}

	if (allocdzn)
		zap_name_free(zn);
	return (B_FALSE);
}

/*
 * Routines for manipulating attributes.
 */

int
zap_lookup(objset_t *os, uint64_t zapobj, const char *name,
    uint64_t integer_size, uint64_t num_integers, void *buf)
{
	return (zap_lookup_norm(os, zapobj, name, integer_size,
	    num_integers, buf, 0, NULL, 0, NULL));
}

static int
zap_lookup_impl(zap_t *zap, const char *name,
    uint64_t integer_size, uint64_t num_integers, void *buf,
    matchtype_t mt, char *realname, int rn_len,
    boolean_t *ncp)
{
	int err = 0;

	zap_name_t *zn = zap_name_alloc(zap, name, mt);
	if (zn == NULL)
		return (SET_ERROR(ENOTSUP));

	if (!zap->zap_ismicro) {
		err = fzap_lookup(zn, integer_size, num_integers, buf,
		    realname, rn_len, ncp);
	} else {
		mzap_ent_t *mze = mze_find(zn);
		if (mze == NULL) {
			err = SET_ERROR(ENOENT);
		} else {
			if (num_integers < 1) {
				err = SET_ERROR(EOVERFLOW);
			} else if (integer_size != 8) {
				err = SET_ERROR(EINVAL);
			} else {
				*(uint64_t *)buf =
				    MZE_PHYS(zap, mze)->mze_value;
				(void) strlcpy(realname,
				    MZE_PHYS(zap, mze)->mze_name, rn_len);
				if (ncp) {
					*ncp = mzap_normalization_conflict(zap,
					    zn, mze);
				}
			}
		}
	}
	zap_name_free(zn);
	return (err);
}

int
zap_lookup_norm(objset_t *os, uint64_t zapobj, const char *name,
    uint64_t integer_size, uint64_t num_integers, void *buf,
    matchtype_t mt, char *realname, int rn_len,
    boolean_t *ncp)
{
	zap_t *zap;

	int err =
	    zap_lockdir(os, zapobj, NULL, RW_READER, TRUE, FALSE, FTAG, &zap);
	if (err != 0)
		return (err);
	err = zap_lookup_impl(zap, name, integer_size,
	    num_integers, buf, mt, realname, rn_len, ncp);
	zap_unlockdir(zap, FTAG);
	return (err);
}

int
zap_lookup_by_dnode(dnode_t *dn, const char *name,
    uint64_t integer_size, uint64_t num_integers, void *buf)
{
	return (zap_lookup_norm_by_dnode(dn, name, integer_size,
	    num_integers, buf, 0, NULL, 0, NULL));
}

int
zap_lookup_norm_by_dnode(dnode_t *dn, const char *name,
    uint64_t integer_size, uint64_t num_integers, void *buf,
    matchtype_t mt, char *realname, int rn_len,
    boolean_t *ncp)
{
	zap_t *zap;

	int err = zap_lockdir_by_dnode(dn, NULL, RW_READER, TRUE, FALSE,
	    FTAG, &zap);
	if (err != 0)
		return (err);
	err = zap_lookup_impl(zap, name, integer_size,
	    num_integers, buf, mt, realname, rn_len, ncp);
	zap_unlockdir(zap, FTAG);
	return (err);
}

int
zap_prefetch_uint64(objset_t *os, uint64_t zapobj, const uint64_t *key,
    int key_numints)
{
	zap_t *zap;

	int err =
	    zap_lockdir(os, zapobj, NULL, RW_READER, TRUE, FALSE, FTAG, &zap);
	if (err != 0)
		return (err);
	zap_name_t *zn = zap_name_alloc_uint64(zap, key, key_numints);
	if (zn == NULL) {
		zap_unlockdir(zap, FTAG);
		return (SET_ERROR(ENOTSUP));
	}

	fzap_prefetch(zn);
	zap_name_free(zn);
	zap_unlockdir(zap, FTAG);
	return (err);
}

int
zap_lookup_uint64(objset_t *os, uint64_t zapobj, const uint64_t *key,
    int key_numints, uint64_t integer_size, uint64_t num_integers, void *buf)
{
	zap_t *zap;

	int err =
	    zap_lockdir(os, zapobj, NULL, RW_READER, TRUE, FALSE, FTAG, &zap);
	if (err != 0)
		return (err);
	zap_name_t *zn = zap_name_alloc_uint64(zap, key, key_numints);
	if (zn == NULL) {
		zap_unlockdir(zap, FTAG);
		return (SET_ERROR(ENOTSUP));
	}

	err = fzap_lookup(zn, integer_size, num_integers, buf,
	    NULL, 0, NULL);
	zap_name_free(zn);
	zap_unlockdir(zap, FTAG);
	return (err);
}

int
zap_contains(objset_t *os, uint64_t zapobj, const char *name)
{
	int err = zap_lookup_norm(os, zapobj, name, 0,
	    0, NULL, 0, NULL, 0, NULL);
	if (err == EOVERFLOW || err == EINVAL)
		err = 0; /* found, but skipped reading the value */
	return (err);
}

int
zap_length(objset_t *os, uint64_t zapobj, const char *name,
    uint64_t *integer_size, uint64_t *num_integers)
{
	zap_t *zap;

	int err =
	    zap_lockdir(os, zapobj, NULL, RW_READER, TRUE, FALSE, FTAG, &zap);
	if (err != 0)
		return (err);
	zap_name_t *zn = zap_name_alloc(zap, name, 0);
	if (zn == NULL) {
		zap_unlockdir(zap, FTAG);
		return (SET_ERROR(ENOTSUP));
	}
	if (!zap->zap_ismicro) {
		err = fzap_length(zn, integer_size, num_integers);
	} else {
		mzap_ent_t *mze = mze_find(zn);
		if (mze == NULL) {
			err = SET_ERROR(ENOENT);
		} else {
			if (integer_size)
				*integer_size = 8;
			if (num_integers)
				*num_integers = 1;
		}
	}
	zap_name_free(zn);
	zap_unlockdir(zap, FTAG);
	return (err);
}

int
zap_length_uint64(objset_t *os, uint64_t zapobj, const uint64_t *key,
    int key_numints, uint64_t *integer_size, uint64_t *num_integers)
{
	zap_t *zap;

	int err =
	    zap_lockdir(os, zapobj, NULL, RW_READER, TRUE, FALSE, FTAG, &zap);
	if (err != 0)
		return (err);
	zap_name_t *zn = zap_name_alloc_uint64(zap, key, key_numints);
	if (zn == NULL) {
		zap_unlockdir(zap, FTAG);
		return (SET_ERROR(ENOTSUP));
	}
	err = fzap_length(zn, integer_size, num_integers);
	zap_name_free(zn);
	zap_unlockdir(zap, FTAG);
	return (err);
}

static void
mzap_addent(zap_name_t *zn, uint64_t value)
{
	zap_t *zap = zn->zn_zap;
	int start = zap->zap_m.zap_alloc_next;

	ASSERT(RW_WRITE_HELD(&zap->zap_rwlock));

#ifdef ZFS_DEBUG
	for (int i = 0; i < zap->zap_m.zap_num_chunks; i++) {
		mzap_ent_phys_t *mze = &zap_m_phys(zap)->mz_chunk[i];
		ASSERT(strcmp(zn->zn_key_orig, mze->mze_name) != 0);
	}
#endif

	uint32_t cd = mze_find_unused_cd(zap, zn->zn_hash);
	/* given the limited size of the microzap, this can't happen */
	ASSERT(cd < zap_maxcd(zap));

again:
	for (int i = start; i < zap->zap_m.zap_num_chunks; i++) {
		mzap_ent_phys_t *mze = &zap_m_phys(zap)->mz_chunk[i];
		if (mze->mze_name[0] == 0) {
			mze->mze_value = value;
			mze->mze_cd = cd;
			(void) strcpy(mze->mze_name, zn->zn_key_orig);
			zap->zap_m.zap_num_entries++;
			zap->zap_m.zap_alloc_next = i+1;
			if (zap->zap_m.zap_alloc_next ==
			    zap->zap_m.zap_num_chunks)
				zap->zap_m.zap_alloc_next = 0;
			VERIFY(0 == mze_insert(zap, i, zn->zn_hash));
			return;
		}
	}
	if (start != 0) {
		start = 0;
		goto again;
	}
	ASSERT(!"out of entries!");
}

static int
zap_add_impl(zap_t *zap, const char *key,
    int integer_size, uint64_t num_integers,
    const void *val, dmu_tx_t *tx, void *tag)
{
	const uint64_t *intval = val;
	int err = 0;

	zap_name_t *zn = zap_name_alloc(zap, key, 0);
	if (zn == NULL) {
		zap_unlockdir(zap, tag);
		return (SET_ERROR(ENOTSUP));
	}
	if (!zap->zap_ismicro) {
		err = fzap_add(zn, integer_size, num_integers, val, tag, tx);
		zap = zn->zn_zap;	/* fzap_add() may change zap */
	} else if (integer_size != 8 || num_integers != 1 ||
	    strlen(key) >= MZAP_NAME_LEN) {
		err = mzap_upgrade(&zn->zn_zap, tag, tx, 0);
		if (err == 0) {
			err = fzap_add(zn, integer_size, num_integers, val,
			    tag, tx);
		}
		zap = zn->zn_zap;	/* fzap_add() may change zap */
	} else {
		if (mze_find(zn) != NULL) {
			err = SET_ERROR(EEXIST);
		} else {
			mzap_addent(zn, *intval);
		}
	}
	ASSERT(zap == zn->zn_zap);
	zap_name_free(zn);
	if (zap != NULL)	/* may be NULL if fzap_add() failed */
		zap_unlockdir(zap, tag);
	return (err);
}

int
zap_add(objset_t *os, uint64_t zapobj, const char *key,
    int integer_size, uint64_t num_integers,
    const void *val, dmu_tx_t *tx)
{
	zap_t *zap;
	int err;

	err = zap_lockdir(os, zapobj, tx, RW_WRITER, TRUE, TRUE, FTAG, &zap);
	if (err != 0)
		return (err);
	err = zap_add_impl(zap, key, integer_size, num_integers, val, tx, FTAG);
	/* zap_add_impl() calls zap_unlockdir() */
	return (err);
}

int
zap_add_by_dnode(dnode_t *dn, const char *key,
    int integer_size, uint64_t num_integers,
    const void *val, dmu_tx_t *tx)
{
	zap_t *zap;
	int err;

	err = zap_lockdir_by_dnode(dn, tx, RW_WRITER, TRUE, TRUE, FTAG, &zap);
	if (err != 0)
		return (err);
	err = zap_add_impl(zap, key, integer_size, num_integers, val, tx, FTAG);
	/* zap_add_impl() calls zap_unlockdir() */
	return (err);
}

int
zap_add_uint64(objset_t *os, uint64_t zapobj, const uint64_t *key,
    int key_numints, int integer_size, uint64_t num_integers,
    const void *val, dmu_tx_t *tx)
{
	zap_t *zap;

	int err =
	    zap_lockdir(os, zapobj, tx, RW_WRITER, TRUE, TRUE, FTAG, &zap);
	if (err != 0)
		return (err);
	zap_name_t *zn = zap_name_alloc_uint64(zap, key, key_numints);
	if (zn == NULL) {
		zap_unlockdir(zap, FTAG);
		return (SET_ERROR(ENOTSUP));
	}
	err = fzap_add(zn, integer_size, num_integers, val, FTAG, tx);
	zap = zn->zn_zap;	/* fzap_add() may change zap */
	zap_name_free(zn);
	if (zap != NULL)	/* may be NULL if fzap_add() failed */
		zap_unlockdir(zap, FTAG);
	return (err);
}

int
zap_update(objset_t *os, uint64_t zapobj, const char *name,
    int integer_size, uint64_t num_integers, const void *val, dmu_tx_t *tx)
{
	zap_t *zap;
	uint64_t oldval;
	const uint64_t *intval = val;

#ifdef ZFS_DEBUG
	/*
	 * If there is an old value, it shouldn't change across the
	 * lockdir (eg, due to bprewrite's xlation).
	 */
	if (integer_size == 8 && num_integers == 1)
		(void) zap_lookup(os, zapobj, name, 8, 1, &oldval);
#endif

	int err =
	    zap_lockdir(os, zapobj, tx, RW_WRITER, TRUE, TRUE, FTAG, &zap);
	if (err != 0)
		return (err);
	zap_name_t *zn = zap_name_alloc(zap, name, 0);
	if (zn == NULL) {
		zap_unlockdir(zap, FTAG);
		return (SET_ERROR(ENOTSUP));
	}
	if (!zap->zap_ismicro) {
		err = fzap_update(zn, integer_size, num_integers, val,
		    FTAG, tx);
		zap = zn->zn_zap;	/* fzap_update() may change zap */
	} else if (integer_size != 8 || num_integers != 1 ||
	    strlen(name) >= MZAP_NAME_LEN) {
		dprintf("upgrading obj %llu: intsz=%u numint=%llu name=%s\n",
		    zapobj, integer_size, num_integers, name);
		err = mzap_upgrade(&zn->zn_zap, FTAG, tx, 0);
		if (err == 0) {
			err = fzap_update(zn, integer_size, num_integers,
			    val, FTAG, tx);
		}
		zap = zn->zn_zap;	/* fzap_update() may change zap */
	} else {
		mzap_ent_t *mze = mze_find(zn);
		if (mze != NULL) {
			ASSERT3U(MZE_PHYS(zap, mze)->mze_value, ==, oldval);
			MZE_PHYS(zap, mze)->mze_value = *intval;
		} else {
			mzap_addent(zn, *intval);
		}
	}
	ASSERT(zap == zn->zn_zap);
	zap_name_free(zn);
	if (zap != NULL)	/* may be NULL if fzap_upgrade() failed */
		zap_unlockdir(zap, FTAG);
	return (err);
}

int
zap_update_uint64(objset_t *os, uint64_t zapobj, const uint64_t *key,
    int key_numints,
    int integer_size, uint64_t num_integers, const void *val, dmu_tx_t *tx)
{
	zap_t *zap;

	int err =
	    zap_lockdir(os, zapobj, tx, RW_WRITER, TRUE, TRUE, FTAG, &zap);
	if (err != 0)
		return (err);
	zap_name_t *zn = zap_name_alloc_uint64(zap, key, key_numints);
	if (zn == NULL) {
		zap_unlockdir(zap, FTAG);
		return (SET_ERROR(ENOTSUP));
	}
	err = fzap_update(zn, integer_size, num_integers, val, FTAG, tx);
	zap = zn->zn_zap;	/* fzap_update() may change zap */
	zap_name_free(zn);
	if (zap != NULL)	/* may be NULL if fzap_upgrade() failed */
		zap_unlockdir(zap, FTAG);
	return (err);
}

int
zap_remove(objset_t *os, uint64_t zapobj, const char *name, dmu_tx_t *tx)
{
	return (zap_remove_norm(os, zapobj, name, 0, tx));
}

static int
zap_remove_impl(zap_t *zap, const char *name,
    matchtype_t mt, dmu_tx_t *tx)
{
	int err = 0;

	zap_name_t *zn = zap_name_alloc(zap, name, mt);
	if (zn == NULL)
		return (SET_ERROR(ENOTSUP));
	if (!zap->zap_ismicro) {
		err = fzap_remove(zn, tx);
	} else {
		mzap_ent_t *mze = mze_find(zn);
		if (mze == NULL) {
			err = SET_ERROR(ENOENT);
		} else {
			zap->zap_m.zap_num_entries--;
			bzero(&zap_m_phys(zap)->mz_chunk[mze->mze_chunkid],
			    sizeof (mzap_ent_phys_t));
			mze_remove(zap, mze);
		}
	}
	zap_name_free(zn);
	return (err);
}

int
zap_remove_norm(objset_t *os, uint64_t zapobj, const char *name,
    matchtype_t mt, dmu_tx_t *tx)
{
	zap_t *zap;
	int err;

	err = zap_lockdir(os, zapobj, tx, RW_WRITER, TRUE, FALSE, FTAG, &zap);
	if (err)
		return (err);
	err = zap_remove_impl(zap, name, mt, tx);
	zap_unlockdir(zap, FTAG);
	return (err);
}

int
zap_remove_by_dnode(dnode_t *dn, const char *name, dmu_tx_t *tx)
{
	zap_t *zap;
	int err;

	err = zap_lockdir_by_dnode(dn, tx, RW_WRITER, TRUE, FALSE, FTAG, &zap);
	if (err)
		return (err);
	err = zap_remove_impl(zap, name, 0, tx);
	zap_unlockdir(zap, FTAG);
	return (err);
}

int
zap_remove_uint64(objset_t *os, uint64_t zapobj, const uint64_t *key,
    int key_numints, dmu_tx_t *tx)
{
	zap_t *zap;

	int err =
	    zap_lockdir(os, zapobj, tx, RW_WRITER, TRUE, FALSE, FTAG, &zap);
	if (err != 0)
		return (err);
	zap_name_t *zn = zap_name_alloc_uint64(zap, key, key_numints);
	if (zn == NULL) {
		zap_unlockdir(zap, FTAG);
		return (SET_ERROR(ENOTSUP));
	}
	err = fzap_remove(zn, tx);
	zap_name_free(zn);
	zap_unlockdir(zap, FTAG);
	return (err);
}

/*
 * Routines for iterating over the attributes.
 */

void
zap_cursor_init_serialized(zap_cursor_t *zc, objset_t *os, uint64_t zapobj,
    uint64_t serialized)
{
	zc->zc_objset = os;
	zc->zc_zap = NULL;
	zc->zc_leaf = NULL;
	zc->zc_zapobj = zapobj;
	zc->zc_serialized = serialized;
	zc->zc_hash = 0;
	zc->zc_cd = 0;
}

void
zap_cursor_init(zap_cursor_t *zc, objset_t *os, uint64_t zapobj)
{
	zap_cursor_init_serialized(zc, os, zapobj, 0);
}

void
zap_cursor_fini(zap_cursor_t *zc)
{
	if (zc->zc_zap) {
		rw_enter(&zc->zc_zap->zap_rwlock, RW_READER);
		zap_unlockdir(zc->zc_zap, NULL);
		zc->zc_zap = NULL;
	}
	if (zc->zc_leaf) {
		rw_enter(&zc->zc_leaf->l_rwlock, RW_READER);
		zap_put_leaf(zc->zc_leaf);
		zc->zc_leaf = NULL;
	}
	zc->zc_objset = NULL;
}

uint64_t
zap_cursor_serialize(zap_cursor_t *zc)
{
	if (zc->zc_hash == -1ULL)
		return (-1ULL);
	if (zc->zc_zap == NULL)
		return (zc->zc_serialized);
	ASSERT((zc->zc_hash & zap_maxcd(zc->zc_zap)) == 0);
	ASSERT(zc->zc_cd < zap_maxcd(zc->zc_zap));

	/*
	 * We want to keep the high 32 bits of the cursor zero if we can, so
	 * that 32-bit programs can access this.  So usually use a small
	 * (28-bit) hash value so we can fit 4 bits of cd into the low 32-bits
	 * of the cursor.
	 *
	 * [ collision differentiator | zap_hashbits()-bit hash value ]
	 */
	return ((zc->zc_hash >> (64 - zap_hashbits(zc->zc_zap))) |
	    ((uint64_t)zc->zc_cd << zap_hashbits(zc->zc_zap)));
}

int
zap_cursor_retrieve(zap_cursor_t *zc, zap_attribute_t *za)
{
	int err;

	if (zc->zc_hash == -1ULL)
		return (SET_ERROR(ENOENT));

	if (zc->zc_zap == NULL) {
		int hb;
		err = zap_lockdir(zc->zc_objset, zc->zc_zapobj, NULL,
		    RW_READER, TRUE, FALSE, NULL, &zc->zc_zap);
		if (err != 0)
			return (err);

		/*
		 * To support zap_cursor_init_serialized, advance, retrieve,
		 * we must add to the existing zc_cd, which may already
		 * be 1 due to the zap_cursor_advance.
		 */
		ASSERT(zc->zc_hash == 0);
		hb = zap_hashbits(zc->zc_zap);
		zc->zc_hash = zc->zc_serialized << (64 - hb);
		zc->zc_cd += zc->zc_serialized >> hb;
		if (zc->zc_cd >= zap_maxcd(zc->zc_zap)) /* corrupt serialized */
			zc->zc_cd = 0;
	} else {
		rw_enter(&zc->zc_zap->zap_rwlock, RW_READER);
	}
	if (!zc->zc_zap->zap_ismicro) {
		err = fzap_cursor_retrieve(zc->zc_zap, zc, za);
	} else {
		avl_index_t idx;
		mzap_ent_t mze_tofind;

		mze_tofind.mze_hash = zc->zc_hash;
		mze_tofind.mze_cd = zc->zc_cd;

		mzap_ent_t *mze =
		    avl_find(&zc->zc_zap->zap_m.zap_avl, &mze_tofind, &idx);
		if (mze == NULL) {
			mze = avl_nearest(&zc->zc_zap->zap_m.zap_avl,
			    idx, AVL_AFTER);
		}
		if (mze) {
			mzap_ent_phys_t *mzep = MZE_PHYS(zc->zc_zap, mze);
			ASSERT3U(mze->mze_cd, ==, mzep->mze_cd);
			za->za_normalization_conflict =
			    mzap_normalization_conflict(zc->zc_zap, NULL, mze);
			za->za_integer_length = 8;
			za->za_num_integers = 1;
			za->za_first_integer = mzep->mze_value;
			(void) strcpy(za->za_name, mzep->mze_name);
			zc->zc_hash = mze->mze_hash;
			zc->zc_cd = mze->mze_cd;
			err = 0;
		} else {
			zc->zc_hash = -1ULL;
			err = SET_ERROR(ENOENT);
		}
	}
	rw_exit(&zc->zc_zap->zap_rwlock);
	return (err);
}

void
zap_cursor_advance(zap_cursor_t *zc)
{
	if (zc->zc_hash == -1ULL)
		return;
	zc->zc_cd++;
}

int
zap_cursor_move_to_key(zap_cursor_t *zc, const char *name, matchtype_t mt)
{
	int err = 0;
	mzap_ent_t *mze;
	zap_name_t *zn;

	if (zc->zc_zap == NULL) {
		err = zap_lockdir(zc->zc_objset, zc->zc_zapobj, NULL,
		    RW_READER, TRUE, FALSE, FTAG, &zc->zc_zap);
		if (err)
			return (err);
	} else {
		rw_enter(&zc->zc_zap->zap_rwlock, RW_READER);
	}

	zn = zap_name_alloc(zc->zc_zap, name, mt);
	if (zn == NULL) {
		rw_exit(&zc->zc_zap->zap_rwlock);
		return (SET_ERROR(ENOTSUP));
	}

	if (!zc->zc_zap->zap_ismicro) {
		err = fzap_cursor_move_to_key(zc, zn);
	} else {
		mze = mze_find(zn);
		if (mze == NULL) {
			err = SET_ERROR(ENOENT);
			goto out;
		}
		zc->zc_hash = mze->mze_hash;
		zc->zc_cd = mze->mze_cd;
	}

out:
	zap_name_free(zn);
	rw_exit(&zc->zc_zap->zap_rwlock);
	return (err);
}

int
zap_get_stats(objset_t *os, uint64_t zapobj, zap_stats_t *zs)
{
	zap_t *zap;

	int err =
	    zap_lockdir(os, zapobj, NULL, RW_READER, TRUE, FALSE, FTAG, &zap);
	if (err != 0)
		return (err);

	bzero(zs, sizeof (zap_stats_t));

	if (zap->zap_ismicro) {
		zs->zs_blocksize = zap->zap_dbuf->db_size;
		zs->zs_num_entries = zap->zap_m.zap_num_entries;
		zs->zs_num_blocks = 1;
	} else {
		fzap_get_stats(zap, zs);
	}
	zap_unlockdir(zap, FTAG);
	return (0);
}
