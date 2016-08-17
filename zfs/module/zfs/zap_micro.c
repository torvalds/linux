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
 * Copyright (c) 2011, 2014 by Delphix. All rights reserved.
 * Copyright (c) 2014 Spectra Logic Corporation, All rights reserved.
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

static int mzap_upgrade(zap_t **zapp, dmu_tx_t *tx, zap_flags_t flags);

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
			int i;
			const uint64_t *wp = zn->zn_key_norm;

			ASSERT(zn->zn_key_intlen == 8);
			for (i = 0; i < zn->zn_key_norm_numints; wp++, i++) {
				int j;
				uint64_t word = *wp;

				for (j = 0; j < zn->zn_key_intlen; j++) {
					h = (h >> 8) ^
					    zfs_crc64_table[(h ^ word) & 0xFF];
					word >>= NBBY;
				}
			}
		} else {
			int i, len;
			const uint8_t *cp = zn->zn_key_norm;

			/*
			 * We previously stored the terminating null on
			 * disk, but didn't hash it, so we need to
			 * continue to not hash it.  (The
			 * zn_key_*_numints includes the terminating
			 * null for non-binary keys.)
			 */
			len = zn->zn_key_norm_numints - 1;

			ASSERT(zn->zn_key_intlen == 1);
			for (i = 0; i < len; cp++, i++) {
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
zap_normalize(zap_t *zap, const char *name, char *namenorm)
{
	size_t inlen, outlen;
	int err;

	ASSERT(!(zap_getflags(zap) & ZAP_FLAG_UINT64_KEY));

	inlen = strlen(name) + 1;
	outlen = ZAP_MAXNAMELEN;

	err = 0;
	(void) u8_textprep_str((char *)name, &inlen, namenorm, &outlen,
	    zap->zap_normflags | U8_TEXTPREP_IGNORE_NULL |
	    U8_TEXTPREP_IGNORE_INVALID, U8_UNICODE_LATEST, &err);

	return (err);
}

boolean_t
zap_match(zap_name_t *zn, const char *matchname)
{
	ASSERT(!(zap_getflags(zn->zn_zap) & ZAP_FLAG_UINT64_KEY));

	if (zn->zn_matchtype == MT_FIRST) {
		char norm[ZAP_MAXNAMELEN];

		if (zap_normalize(zn->zn_zap, matchname, norm) != 0)
			return (B_FALSE);

		return (strcmp(zn->zn_key_norm, norm) == 0);
	} else {
		/* MT_BEST or MT_EXACT */
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
	if (zap->zap_normflags) {
		if (zap_normalize(zap, key, zn->zn_normbuf) != 0) {
			zap_name_free(zn);
			return (NULL);
		}
		zn->zn_key_norm = zn->zn_normbuf;
		zn->zn_key_norm_numints = strlen(zn->zn_key_norm) + 1;
	} else {
		if (mt != MT_EXACT) {
			zap_name_free(zn);
			return (NULL);
		}
		zn->zn_key_norm = zn->zn_key_orig;
		zn->zn_key_norm_numints = zn->zn_key_orig_numints;
	}

	zn->zn_hash = zap_hash(zn);
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
	zn->zn_matchtype = MT_EXACT;

	zn->zn_hash = zap_hash(zn);
	return (zn);
}

static void
mzap_byteswap(mzap_phys_t *buf, size_t size)
{
	int i, max;
	buf->mz_block_type = BSWAP_64(buf->mz_block_type);
	buf->mz_salt = BSWAP_64(buf->mz_salt);
	buf->mz_normflags = BSWAP_64(buf->mz_normflags);
	max = (size / MZAP_ENT_LEN) - 1;
	for (i = 0; i < max; i++) {
		buf->mz_chunk[i].mze_value =
		    BSWAP_64(buf->mz_chunk[i].mze_value);
		buf->mz_chunk[i].mze_cd =
		    BSWAP_32(buf->mz_chunk[i].mze_cd);
	}
}

void
zap_byteswap(void *buf, size_t size)
{
	uint64_t block_type;

	block_type = *(uint64_t *)buf;

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

	if (mze1->mze_hash > mze2->mze_hash)
		return (+1);
	if (mze1->mze_hash < mze2->mze_hash)
		return (-1);
	if (mze1->mze_cd > mze2->mze_cd)
		return (+1);
	if (mze1->mze_cd < mze2->mze_cd)
		return (-1);
	return (0);
}

static void
mze_insert(zap_t *zap, int chunkid, uint64_t hash)
{
	mzap_ent_t *mze;

	ASSERT(zap->zap_ismicro);
	ASSERT(RW_WRITE_HELD(&zap->zap_rwlock));

	mze = kmem_alloc(sizeof (mzap_ent_t), KM_SLEEP);
	mze->mze_chunkid = chunkid;
	mze->mze_hash = hash;
	mze->mze_cd = MZE_PHYS(zap, mze)->mze_cd;
	ASSERT(MZE_PHYS(zap, mze)->mze_name[0] != 0);
	avl_add(&zap->zap_m.zap_avl, mze);
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

again:
	mze = avl_find(avl, &mze_tofind, &idx);
	if (mze == NULL)
		mze = avl_nearest(avl, idx, AVL_AFTER);
	for (; mze && mze->mze_hash == zn->zn_hash; mze = AVL_NEXT(avl, mze)) {
		ASSERT3U(mze->mze_cd, ==, MZE_PHYS(zn->zn_zap, mze)->mze_cd);
		if (zap_match(zn, MZE_PHYS(zn->zn_zap, mze)->mze_name))
			return (mze);
	}
	if (zn->zn_matchtype == MT_BEST) {
		zn->zn_matchtype = MT_FIRST;
		goto again;
	}
	return (NULL);
}

static uint32_t
mze_find_unused_cd(zap_t *zap, uint64_t hash)
{
	mzap_ent_t mze_tofind;
	mzap_ent_t *mze;
	avl_index_t idx;
	avl_tree_t *avl = &zap->zap_m.zap_avl;
	uint32_t cd;

	ASSERT(zap->zap_ismicro);
	ASSERT(RW_LOCK_HELD(&zap->zap_rwlock));

	mze_tofind.mze_hash = hash;
	mze_tofind.mze_cd = 0;

	cd = 0;
	for (mze = avl_find(avl, &mze_tofind, &idx);
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

	while ((mze = avl_destroy_nodes(&zap->zap_m.zap_avl, &avlcookie)))
		kmem_free(mze, sizeof (mzap_ent_t));
	avl_destroy(&zap->zap_m.zap_avl);
}

static zap_t *
mzap_open(objset_t *os, uint64_t obj, dmu_buf_t *db)
{
	zap_t *winner;
	zap_t *zap;
	int i;

	ASSERT3U(MZAP_ENT_LEN, ==, sizeof (mzap_ent_phys_t));

	zap = kmem_zalloc(sizeof (zap_t), KM_SLEEP);
	rw_init(&zap->zap_rwlock, NULL, RW_DEFAULT, NULL);
	rw_enter(&zap->zap_rwlock, RW_WRITER);
	zap->zap_objset = os;
	zap->zap_object = obj;
	zap->zap_dbuf = db;

	if (*(uint64_t *)db->db_data != ZBT_MICRO) {
		mutex_init(&zap->zap_f.zap_num_entries_mtx, 0, 0, 0);
		zap->zap_f.zap_block_shift = highbit64(db->db_size) - 1;
	} else {
		zap->zap_ismicro = TRUE;
	}

	/*
	 * Make sure that zap_ismicro is set before we let others see
	 * it, because zap_lockdir() checks zap_ismicro without the lock
	 * held.
	 */
	dmu_buf_init_user(&zap->zap_dbu, zap_evict, &zap->zap_dbuf);
	winner = dmu_buf_set_user(db, &zap->zap_dbu);

	if (winner != NULL) {
		rw_exit(&zap->zap_rwlock);
		rw_destroy(&zap->zap_rwlock);
		if (!zap->zap_ismicro)
			mutex_destroy(&zap->zap_f.zap_num_entries_mtx);
		kmem_free(zap, sizeof (zap_t));
		return (winner);
	}

	if (zap->zap_ismicro) {
		zap->zap_salt = zap_m_phys(zap)->mz_salt;
		zap->zap_normflags = zap_m_phys(zap)->mz_normflags;
		zap->zap_m.zap_num_chunks = db->db_size / MZAP_ENT_LEN - 1;
		avl_create(&zap->zap_m.zap_avl, mze_compare,
		    sizeof (mzap_ent_t), offsetof(mzap_ent_t, mze_node));

		for (i = 0; i < zap->zap_m.zap_num_chunks; i++) {
			mzap_ent_phys_t *mze =
			    &zap_m_phys(zap)->mz_chunk[i];
			if (mze->mze_name[0]) {
				zap_name_t *zn;

				zap->zap_m.zap_num_entries++;
				zn = zap_name_alloc(zap, mze->mze_name,
				    MT_EXACT);
				mze_insert(zap, i, zn->zn_hash);
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
}

int
zap_lockdir(objset_t *os, uint64_t obj, dmu_tx_t *tx,
    krw_t lti, boolean_t fatreader, boolean_t adding, zap_t **zapp)
{
	dmu_object_info_t doi;
	zap_t *zap;
	dmu_buf_t *db;
	krw_t lt;
	int err;

	*zapp = NULL;

	err = dmu_buf_hold(os, obj, 0, NULL, &db, DMU_READ_NO_PREFETCH);
	if (err)
		return (err);

	dmu_object_info_from_db(db, &doi);
	if (DMU_OT_BYTESWAP(doi.doi_type) != DMU_BSWAP_ZAP)
		return (SET_ERROR(EINVAL));

	zap = dmu_buf_get_user(db);
	if (zap == NULL)
		zap = mzap_open(os, obj, db);

	/*
	 * We're checking zap_ismicro without the lock held, in order to
	 * tell what type of lock we want.  Once we have some sort of
	 * lock, see if it really is the right type.  In practice this
	 * can only be different if it was upgraded from micro to fat,
	 * and micro wanted WRITER but fat only needs READER.
	 */
	lt = (!zap->zap_ismicro && fatreader) ? RW_READER : lti;
	rw_enter(&zap->zap_rwlock, lt);
	if (lt != ((!zap->zap_ismicro && fatreader) ? RW_READER : lti)) {
		/* it was upgraded, now we only need reader */
		ASSERT(lt == RW_WRITER);
		ASSERT(RW_READER ==
		    ((!zap->zap_ismicro && fatreader) ? RW_READER : lti));
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
			return (mzap_upgrade(zapp, tx, 0));
		}
		err = dmu_object_set_blocksize(os, obj, newsz, 0, tx);
		ASSERT0(err);
		zap->zap_m.zap_num_chunks =
		    db->db_size / MZAP_ENT_LEN - 1;
	}

	*zapp = zap;
	return (0);
}

void
zap_unlockdir(zap_t *zap)
{
	rw_exit(&zap->zap_rwlock);
	dmu_buf_rele(zap->zap_dbuf, NULL);
}

static int
mzap_upgrade(zap_t **zapp, dmu_tx_t *tx, zap_flags_t flags)
{
	mzap_phys_t *mzp;
	int i, sz, nchunks;
	int err = 0;
	zap_t *zap = *zapp;

	ASSERT(RW_WRITE_HELD(&zap->zap_rwlock));

	sz = zap->zap_dbuf->db_size;
	mzp = zio_buf_alloc(sz);
	bcopy(zap->zap_dbuf->db_data, mzp, sz);
	nchunks = zap->zap_m.zap_num_chunks;

	if (!flags) {
		err = dmu_object_set_blocksize(zap->zap_objset, zap->zap_object,
		    1ULL << fzap_default_block_shift, 0, tx);
		if (err) {
			zio_buf_free(mzp, sz);
			return (err);
		}
	}

	dprintf("upgrading obj=%llu with %u chunks\n",
	    zap->zap_object, nchunks);
	/* XXX destroy the avl later, so we can use the stored hash value */
	mze_destroy(zap);

	fzap_upgrade(zap, tx, flags);

	for (i = 0; i < nchunks; i++) {
		mzap_ent_phys_t *mze = &mzp->mz_chunk[i];
		zap_name_t *zn;
		if (mze->mze_name[0] == 0)
			continue;
		dprintf("adding %s=%llu\n",
		    mze->mze_name, mze->mze_value);
		zn = zap_name_alloc(zap, mze->mze_name, MT_EXACT);
		err = fzap_add_cd(zn, 8, 1, &mze->mze_value, mze->mze_cd, tx);
		zap = zn->zn_zap;	/* fzap_add_cd() may change zap */
		zap_name_free(zn);
		if (err)
			break;
	}
	zio_buf_free(mzp, sz);
	*zapp = zap;
	return (err);
}

void
mzap_create_impl(objset_t *os, uint64_t obj, int normflags, zap_flags_t flags,
    dmu_tx_t *tx)
{
	dmu_buf_t *db;
	mzap_phys_t *zp;

	VERIFY(0 == dmu_buf_hold(os, obj, 0, FTAG, &db, DMU_READ_NO_PREFETCH));

#ifdef ZFS_DEBUG
	{
		dmu_object_info_t doi;
		dmu_object_info_from_db(db, &doi);
		ASSERT3U(DMU_OT_BYTESWAP(doi.doi_type), ==, DMU_BSWAP_ZAP);
	}
#endif

	dmu_buf_will_dirty(db, tx);
	zp = db->db_data;
	zp->mz_block_type = ZBT_MICRO;
	zp->mz_salt = ((uintptr_t)db ^ (uintptr_t)tx ^ (obj << 1)) | 1ULL;
	zp->mz_normflags = normflags;
	dmu_buf_rele(db, FTAG);

	if (flags != 0) {
		zap_t *zap;
		/* Only fat zap supports flags; upgrade immediately. */
		VERIFY(0 == zap_lockdir(os, obj, tx, RW_WRITER,
		    B_FALSE, B_FALSE, &zap));
		VERIFY3U(0, ==, mzap_upgrade(&zap, tx, flags));
		zap_unlockdir(zap);
	}
}

int
zap_create_claim(objset_t *os, uint64_t obj, dmu_object_type_t ot,
    dmu_object_type_t bonustype, int bonuslen, dmu_tx_t *tx)
{
	return (zap_create_claim_norm(os, obj,
	    0, ot, bonustype, bonuslen, tx));
}

int
zap_create_claim_norm(objset_t *os, uint64_t obj, int normflags,
    dmu_object_type_t ot,
    dmu_object_type_t bonustype, int bonuslen, dmu_tx_t *tx)
{
	int err;

	err = dmu_object_claim(os, obj, ot, 0, bonustype, bonuslen, tx);
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
zap_create_norm(objset_t *os, int normflags, dmu_object_type_t ot,
    dmu_object_type_t bonustype, int bonuslen, dmu_tx_t *tx)
{
	uint64_t obj = dmu_object_alloc(os, ot, 0, bonustype, bonuslen, tx);

	mzap_create_impl(os, obj, normflags, 0, tx);
	return (obj);
}

uint64_t
zap_create_flags(objset_t *os, int normflags, zap_flags_t flags,
    dmu_object_type_t ot, int leaf_blockshift, int indirect_blockshift,
    dmu_object_type_t bonustype, int bonuslen, dmu_tx_t *tx)
{
	uint64_t obj = dmu_object_alloc(os, ot, 0, bonustype, bonuslen, tx);

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
zap_evict(void *dbu)
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
	int err;

	err = zap_lockdir(os, zapobj, NULL, RW_READER, TRUE, FALSE, &zap);
	if (err)
		return (err);
	if (!zap->zap_ismicro) {
		err = fzap_count(zap, count);
	} else {
		*count = zap->zap_m.zap_num_entries;
	}
	zap_unlockdir(zap);
	return (err);
}

/*
 * zn may be NULL; if not specified, it will be computed if needed.
 * See also the comment above zap_entry_normalization_conflict().
 */
static boolean_t
mzap_normalization_conflict(zap_t *zap, zap_name_t *zn, mzap_ent_t *mze)
{
	mzap_ent_t *other;
	int direction = AVL_BEFORE;
	boolean_t allocdzn = B_FALSE;

	if (zap->zap_normflags == 0)
		return (B_FALSE);

again:
	for (other = avl_walk(&zap->zap_m.zap_avl, mze, direction);
	    other && other->mze_hash == mze->mze_hash;
	    other = avl_walk(&zap->zap_m.zap_avl, other, direction)) {

		if (zn == NULL) {
			zn = zap_name_alloc(zap, MZE_PHYS(zap, mze)->mze_name,
			    MT_FIRST);
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
	    num_integers, buf, MT_EXACT, NULL, 0, NULL));
}

int
zap_lookup_norm(objset_t *os, uint64_t zapobj, const char *name,
    uint64_t integer_size, uint64_t num_integers, void *buf,
    matchtype_t mt, char *realname, int rn_len,
    boolean_t *ncp)
{
	zap_t *zap;
	int err;
	mzap_ent_t *mze;
	zap_name_t *zn;

	err = zap_lockdir(os, zapobj, NULL, RW_READER, TRUE, FALSE, &zap);
	if (err)
		return (err);
	zn = zap_name_alloc(zap, name, mt);
	if (zn == NULL) {
		zap_unlockdir(zap);
		return (SET_ERROR(ENOTSUP));
	}

	if (!zap->zap_ismicro) {
		err = fzap_lookup(zn, integer_size, num_integers, buf,
		    realname, rn_len, ncp);
	} else {
		mze = mze_find(zn);
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
	zap_unlockdir(zap);
	return (err);
}

int
zap_prefetch(objset_t *os, uint64_t zapobj, const char *name)
{
	zap_t *zap;
	int err;
	zap_name_t *zn;

	err = zap_lockdir(os, zapobj, NULL, RW_READER, TRUE, FALSE, &zap);
	if (err)
		return (err);
	zn = zap_name_alloc(zap, name, MT_EXACT);
	if (zn == NULL) {
		zap_unlockdir(zap);
		return (SET_ERROR(ENOTSUP));
	}

	fzap_prefetch(zn);
	zap_name_free(zn);
	zap_unlockdir(zap);
	return (err);
}

int
zap_prefetch_uint64(objset_t *os, uint64_t zapobj, const uint64_t *key,
    int key_numints)
{
	zap_t *zap;
	int err;
	zap_name_t *zn;

	err = zap_lockdir(os, zapobj, NULL, RW_READER, TRUE, FALSE, &zap);
	if (err)
		return (err);
	zn = zap_name_alloc_uint64(zap, key, key_numints);
	if (zn == NULL) {
		zap_unlockdir(zap);
		return (SET_ERROR(ENOTSUP));
	}

	fzap_prefetch(zn);
	zap_name_free(zn);
	zap_unlockdir(zap);
	return (err);
}

int
zap_lookup_uint64(objset_t *os, uint64_t zapobj, const uint64_t *key,
    int key_numints, uint64_t integer_size, uint64_t num_integers, void *buf)
{
	zap_t *zap;
	int err;
	zap_name_t *zn;

	err = zap_lockdir(os, zapobj, NULL, RW_READER, TRUE, FALSE, &zap);
	if (err)
		return (err);
	zn = zap_name_alloc_uint64(zap, key, key_numints);
	if (zn == NULL) {
		zap_unlockdir(zap);
		return (SET_ERROR(ENOTSUP));
	}

	err = fzap_lookup(zn, integer_size, num_integers, buf,
	    NULL, 0, NULL);
	zap_name_free(zn);
	zap_unlockdir(zap);
	return (err);
}

int
zap_contains(objset_t *os, uint64_t zapobj, const char *name)
{
	int err = zap_lookup_norm(os, zapobj, name, 0,
	    0, NULL, MT_EXACT, NULL, 0, NULL);
	if (err == EOVERFLOW || err == EINVAL)
		err = 0; /* found, but skipped reading the value */
	return (err);
}

int
zap_length(objset_t *os, uint64_t zapobj, const char *name,
    uint64_t *integer_size, uint64_t *num_integers)
{
	zap_t *zap;
	int err;
	mzap_ent_t *mze;
	zap_name_t *zn;

	err = zap_lockdir(os, zapobj, NULL, RW_READER, TRUE, FALSE, &zap);
	if (err)
		return (err);
	zn = zap_name_alloc(zap, name, MT_EXACT);
	if (zn == NULL) {
		zap_unlockdir(zap);
		return (SET_ERROR(ENOTSUP));
	}
	if (!zap->zap_ismicro) {
		err = fzap_length(zn, integer_size, num_integers);
	} else {
		mze = mze_find(zn);
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
	zap_unlockdir(zap);
	return (err);
}

int
zap_length_uint64(objset_t *os, uint64_t zapobj, const uint64_t *key,
    int key_numints, uint64_t *integer_size, uint64_t *num_integers)
{
	zap_t *zap;
	int err;
	zap_name_t *zn;

	err = zap_lockdir(os, zapobj, NULL, RW_READER, TRUE, FALSE, &zap);
	if (err)
		return (err);
	zn = zap_name_alloc_uint64(zap, key, key_numints);
	if (zn == NULL) {
		zap_unlockdir(zap);
		return (SET_ERROR(ENOTSUP));
	}
	err = fzap_length(zn, integer_size, num_integers);
	zap_name_free(zn);
	zap_unlockdir(zap);
	return (err);
}

static void
mzap_addent(zap_name_t *zn, uint64_t value)
{
	int i;
	zap_t *zap = zn->zn_zap;
	int start = zap->zap_m.zap_alloc_next;
	uint32_t cd;

	ASSERT(RW_WRITE_HELD(&zap->zap_rwlock));

#ifdef ZFS_DEBUG
	for (i = 0; i < zap->zap_m.zap_num_chunks; i++) {
		ASSERTV(mzap_ent_phys_t *mze);
		ASSERT(mze = &zap_m_phys(zap)->mz_chunk[i]);
		ASSERT(strcmp(zn->zn_key_orig, mze->mze_name) != 0);
	}
#endif

	cd = mze_find_unused_cd(zap, zn->zn_hash);
	/* given the limited size of the microzap, this can't happen */
	ASSERT(cd < zap_maxcd(zap));

again:
	for (i = start; i < zap->zap_m.zap_num_chunks; i++) {
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
			mze_insert(zap, i, zn->zn_hash);
			return;
		}
	}
	if (start != 0) {
		start = 0;
		goto again;
	}
	cmn_err(CE_PANIC, "out of entries!");
}

int
zap_add(objset_t *os, uint64_t zapobj, const char *key,
    int integer_size, uint64_t num_integers,
    const void *val, dmu_tx_t *tx)
{
	zap_t *zap;
	int err;
	mzap_ent_t *mze;
	const uint64_t *intval = val;
	zap_name_t *zn;

	err = zap_lockdir(os, zapobj, tx, RW_WRITER, TRUE, TRUE, &zap);
	if (err)
		return (err);
	zn = zap_name_alloc(zap, key, MT_EXACT);
	if (zn == NULL) {
		zap_unlockdir(zap);
		return (SET_ERROR(ENOTSUP));
	}
	if (!zap->zap_ismicro) {
		err = fzap_add(zn, integer_size, num_integers, val, tx);
		zap = zn->zn_zap;	/* fzap_add() may change zap */
	} else if (integer_size != 8 || num_integers != 1 ||
	    strlen(key) >= MZAP_NAME_LEN) {
		err = mzap_upgrade(&zn->zn_zap, tx, 0);
		if (err == 0)
			err = fzap_add(zn, integer_size, num_integers, val, tx);
		zap = zn->zn_zap;	/* fzap_add() may change zap */
	} else {
		mze = mze_find(zn);
		if (mze != NULL) {
			err = SET_ERROR(EEXIST);
		} else {
			mzap_addent(zn, *intval);
		}
	}
	ASSERT(zap == zn->zn_zap);
	zap_name_free(zn);
	if (zap != NULL)	/* may be NULL if fzap_add() failed */
		zap_unlockdir(zap);
	return (err);
}

int
zap_add_uint64(objset_t *os, uint64_t zapobj, const uint64_t *key,
    int key_numints, int integer_size, uint64_t num_integers,
    const void *val, dmu_tx_t *tx)
{
	zap_t *zap;
	int err;
	zap_name_t *zn;

	err = zap_lockdir(os, zapobj, tx, RW_WRITER, TRUE, TRUE, &zap);
	if (err)
		return (err);
	zn = zap_name_alloc_uint64(zap, key, key_numints);
	if (zn == NULL) {
		zap_unlockdir(zap);
		return (SET_ERROR(ENOTSUP));
	}
	err = fzap_add(zn, integer_size, num_integers, val, tx);
	zap = zn->zn_zap;	/* fzap_add() may change zap */
	zap_name_free(zn);
	if (zap != NULL)	/* may be NULL if fzap_add() failed */
		zap_unlockdir(zap);
	return (err);
}

int
zap_update(objset_t *os, uint64_t zapobj, const char *name,
    int integer_size, uint64_t num_integers, const void *val, dmu_tx_t *tx)
{
	zap_t *zap;
	mzap_ent_t *mze;
	const uint64_t *intval = val;
	zap_name_t *zn;
	int err;

#ifdef ZFS_DEBUG
	uint64_t oldval;

	/*
	 * If there is an old value, it shouldn't change across the
	 * lockdir (eg, due to bprewrite's xlation).
	 */
	if (integer_size == 8 && num_integers == 1)
		(void) zap_lookup(os, zapobj, name, 8, 1, &oldval);
#endif

	err = zap_lockdir(os, zapobj, tx, RW_WRITER, TRUE, TRUE, &zap);
	if (err)
		return (err);
	zn = zap_name_alloc(zap, name, MT_EXACT);
	if (zn == NULL) {
		zap_unlockdir(zap);
		return (SET_ERROR(ENOTSUP));
	}
	if (!zap->zap_ismicro) {
		err = fzap_update(zn, integer_size, num_integers, val, tx);
		zap = zn->zn_zap;	/* fzap_update() may change zap */
	} else if (integer_size != 8 || num_integers != 1 ||
	    strlen(name) >= MZAP_NAME_LEN) {
		dprintf("upgrading obj %llu: intsz=%u numint=%llu name=%s\n",
		    zapobj, integer_size, num_integers, name);
		err = mzap_upgrade(&zn->zn_zap, tx, 0);
		if (err == 0)
			err = fzap_update(zn, integer_size, num_integers,
			    val, tx);
		zap = zn->zn_zap;	/* fzap_update() may change zap */
	} else {
		mze = mze_find(zn);
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
		zap_unlockdir(zap);
	return (err);
}

int
zap_update_uint64(objset_t *os, uint64_t zapobj, const uint64_t *key,
    int key_numints,
    int integer_size, uint64_t num_integers, const void *val, dmu_tx_t *tx)
{
	zap_t *zap;
	zap_name_t *zn;
	int err;

	err = zap_lockdir(os, zapobj, tx, RW_WRITER, TRUE, TRUE, &zap);
	if (err)
		return (err);
	zn = zap_name_alloc_uint64(zap, key, key_numints);
	if (zn == NULL) {
		zap_unlockdir(zap);
		return (SET_ERROR(ENOTSUP));
	}
	err = fzap_update(zn, integer_size, num_integers, val, tx);
	zap = zn->zn_zap;	/* fzap_update() may change zap */
	zap_name_free(zn);
	if (zap != NULL)	/* may be NULL if fzap_upgrade() failed */
		zap_unlockdir(zap);
	return (err);
}

int
zap_remove(objset_t *os, uint64_t zapobj, const char *name, dmu_tx_t *tx)
{
	return (zap_remove_norm(os, zapobj, name, MT_EXACT, tx));
}

int
zap_remove_norm(objset_t *os, uint64_t zapobj, const char *name,
    matchtype_t mt, dmu_tx_t *tx)
{
	zap_t *zap;
	int err;
	mzap_ent_t *mze;
	zap_name_t *zn;

	err = zap_lockdir(os, zapobj, tx, RW_WRITER, TRUE, FALSE, &zap);
	if (err)
		return (err);
	zn = zap_name_alloc(zap, name, mt);
	if (zn == NULL) {
		zap_unlockdir(zap);
		return (SET_ERROR(ENOTSUP));
	}
	if (!zap->zap_ismicro) {
		err = fzap_remove(zn, tx);
	} else {
		mze = mze_find(zn);
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
	zap_unlockdir(zap);
	return (err);
}

int
zap_remove_uint64(objset_t *os, uint64_t zapobj, const uint64_t *key,
    int key_numints, dmu_tx_t *tx)
{
	zap_t *zap;
	int err;
	zap_name_t *zn;

	err = zap_lockdir(os, zapobj, tx, RW_WRITER, TRUE, FALSE, &zap);
	if (err)
		return (err);
	zn = zap_name_alloc_uint64(zap, key, key_numints);
	if (zn == NULL) {
		zap_unlockdir(zap);
		return (SET_ERROR(ENOTSUP));
	}
	err = fzap_remove(zn, tx);
	zap_name_free(zn);
	zap_unlockdir(zap);
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
		zap_unlockdir(zc->zc_zap);
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
	avl_index_t idx;
	mzap_ent_t mze_tofind;
	mzap_ent_t *mze;

	if (zc->zc_hash == -1ULL)
		return (SET_ERROR(ENOENT));

	if (zc->zc_zap == NULL) {
		int hb;
		err = zap_lockdir(zc->zc_objset, zc->zc_zapobj, NULL,
		    RW_READER, TRUE, FALSE, &zc->zc_zap);
		if (err)
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
		mze_tofind.mze_hash = zc->zc_hash;
		mze_tofind.mze_cd = zc->zc_cd;

		mze = avl_find(&zc->zc_zap->zap_m.zap_avl, &mze_tofind, &idx);
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
zap_get_stats(objset_t *os, uint64_t zapobj, zap_stats_t *zs)
{
	int err;
	zap_t *zap;

	err = zap_lockdir(os, zapobj, NULL, RW_READER, TRUE, FALSE, &zap);
	if (err)
		return (err);

	bzero(zs, sizeof (zap_stats_t));

	if (zap->zap_ismicro) {
		zs->zs_blocksize = zap->zap_dbuf->db_size;
		zs->zs_num_entries = zap->zap_m.zap_num_entries;
		zs->zs_num_blocks = 1;
	} else {
		fzap_get_stats(zap, zs);
	}
	zap_unlockdir(zap);
	return (0);
}

int
zap_count_write(objset_t *os, uint64_t zapobj, const char *name, int add,
    uint64_t *towrite, uint64_t *tooverwrite)
{
	zap_t *zap;
	int err = 0;

	/*
	 * Since, we don't have a name, we cannot figure out which blocks will
	 * be affected in this operation. So, account for the worst case :
	 * - 3 blocks overwritten: target leaf, ptrtbl block, header block
	 * - 4 new blocks written if adding:
	 * 	- 2 blocks for possibly split leaves,
	 * 	- 2 grown ptrtbl blocks
	 *
	 * This also accomodates the case where an add operation to a fairly
	 * large microzap results in a promotion to fatzap.
	 */
	if (name == NULL) {
		*towrite += (3 + (add ? 4 : 0)) * SPA_OLD_MAXBLOCKSIZE;
		return (err);
	}

	/*
	 * We lock the zap with adding == FALSE. Because, if we pass
	 * the actual value of add, it could trigger a mzap_upgrade().
	 * At present we are just evaluating the possibility of this operation
	 * and hence we donot want to trigger an upgrade.
	 */
	err = zap_lockdir(os, zapobj, NULL, RW_READER, TRUE, FALSE, &zap);
	if (err)
		return (err);

	if (!zap->zap_ismicro) {
		zap_name_t *zn = zap_name_alloc(zap, name, MT_EXACT);
		if (zn) {
			err = fzap_count_write(zn, add, towrite,
			    tooverwrite);
			zap_name_free(zn);
		} else {
			/*
			 * We treat this case as similar to (name == NULL)
			 */
			*towrite += (3 + (add ? 4 : 0)) * SPA_OLD_MAXBLOCKSIZE;
		}
	} else {
		/*
		 * We are here if (name != NULL) and this is a micro-zap.
		 * We account for the header block depending on whether it
		 * is freeable.
		 *
		 * Incase of an add-operation it is hard to find out
		 * if this add will promote this microzap to fatzap.
		 * Hence, we consider the worst case and account for the
		 * blocks assuming this microzap would be promoted to a
		 * fatzap.
		 *
		 * 1 block overwritten  : header block
		 * 4 new blocks written : 2 new split leaf, 2 grown
		 *			ptrtbl blocks
		 */
		if (dmu_buf_freeable(zap->zap_dbuf))
			*tooverwrite += MZAP_MAX_BLKSZ;
		else
			*towrite += MZAP_MAX_BLKSZ;

		if (add) {
			*towrite += 4 * MZAP_MAX_BLKSZ;
		}
	}

	zap_unlockdir(zap);
	return (err);
}

#if defined(_KERNEL) && defined(HAVE_SPL)
EXPORT_SYMBOL(zap_create);
EXPORT_SYMBOL(zap_create_norm);
EXPORT_SYMBOL(zap_create_flags);
EXPORT_SYMBOL(zap_create_claim);
EXPORT_SYMBOL(zap_create_claim_norm);
EXPORT_SYMBOL(zap_destroy);
EXPORT_SYMBOL(zap_lookup);
EXPORT_SYMBOL(zap_lookup_norm);
EXPORT_SYMBOL(zap_lookup_uint64);
EXPORT_SYMBOL(zap_contains);
EXPORT_SYMBOL(zap_prefetch);
EXPORT_SYMBOL(zap_prefetch_uint64);
EXPORT_SYMBOL(zap_count_write);
EXPORT_SYMBOL(zap_add);
EXPORT_SYMBOL(zap_add_uint64);
EXPORT_SYMBOL(zap_update);
EXPORT_SYMBOL(zap_update_uint64);
EXPORT_SYMBOL(zap_length);
EXPORT_SYMBOL(zap_length_uint64);
EXPORT_SYMBOL(zap_remove);
EXPORT_SYMBOL(zap_remove_norm);
EXPORT_SYMBOL(zap_remove_uint64);
EXPORT_SYMBOL(zap_count);
EXPORT_SYMBOL(zap_value_search);
EXPORT_SYMBOL(zap_join);
EXPORT_SYMBOL(zap_join_increment);
EXPORT_SYMBOL(zap_add_int);
EXPORT_SYMBOL(zap_remove_int);
EXPORT_SYMBOL(zap_lookup_int);
EXPORT_SYMBOL(zap_increment_int);
EXPORT_SYMBOL(zap_add_int_key);
EXPORT_SYMBOL(zap_lookup_int_key);
EXPORT_SYMBOL(zap_increment);
EXPORT_SYMBOL(zap_cursor_init);
EXPORT_SYMBOL(zap_cursor_fini);
EXPORT_SYMBOL(zap_cursor_retrieve);
EXPORT_SYMBOL(zap_cursor_advance);
EXPORT_SYMBOL(zap_cursor_serialize);
EXPORT_SYMBOL(zap_cursor_init_serialized);
EXPORT_SYMBOL(zap_get_stats);
#endif
