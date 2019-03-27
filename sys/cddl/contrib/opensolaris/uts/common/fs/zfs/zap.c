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
 * Copyright (c) 2012, 2016 by Delphix. All rights reserved.
 * Copyright (c) 2014 Spectra Logic Corporation, All rights reserved.
 */

/*
 * This file contains the top half of the zfs directory structure
 * implementation. The bottom half is in zap_leaf.c.
 *
 * The zdir is an extendable hash data structure. There is a table of
 * pointers to buckets (zap_t->zd_data->zd_leafs). The buckets are
 * each a constant size and hold a variable number of directory entries.
 * The buckets (aka "leaf nodes") are implemented in zap_leaf.c.
 *
 * The pointer table holds a power of 2 number of pointers.
 * (1<<zap_t->zd_data->zd_phys->zd_prefix_len).  The bucket pointed to
 * by the pointer at index i in the table holds entries whose hash value
 * has a zd_prefix_len - bit prefix
 */

#include <sys/spa.h>
#include <sys/dmu.h>
#include <sys/zfs_context.h>
#include <sys/zfs_znode.h>
#include <sys/fs/zfs.h>
#include <sys/zap.h>
#include <sys/refcount.h>
#include <sys/zap_impl.h>
#include <sys/zap_leaf.h>

int fzap_default_block_shift = 14; /* 16k blocksize */

extern inline zap_phys_t *zap_f_phys(zap_t *zap);

static uint64_t zap_allocate_blocks(zap_t *zap, int nblocks);

void
fzap_byteswap(void *vbuf, size_t size)
{
	uint64_t block_type = *(uint64_t *)vbuf;

	if (block_type == ZBT_LEAF || block_type == BSWAP_64(ZBT_LEAF))
		zap_leaf_byteswap(vbuf, size);
	else {
		/* it's a ptrtbl block */
		byteswap_uint64_array(vbuf, size);
	}
}

void
fzap_upgrade(zap_t *zap, dmu_tx_t *tx, zap_flags_t flags)
{
	ASSERT(RW_WRITE_HELD(&zap->zap_rwlock));
	zap->zap_ismicro = FALSE;

	zap->zap_dbu.dbu_evict_func_sync = zap_evict_sync;
	zap->zap_dbu.dbu_evict_func_async = NULL;

	mutex_init(&zap->zap_f.zap_num_entries_mtx, 0, 0, 0);
	zap->zap_f.zap_block_shift = highbit64(zap->zap_dbuf->db_size) - 1;

	zap_phys_t *zp = zap_f_phys(zap);
	/*
	 * explicitly zero it since it might be coming from an
	 * initialized microzap
	 */
	bzero(zap->zap_dbuf->db_data, zap->zap_dbuf->db_size);
	zp->zap_block_type = ZBT_HEADER;
	zp->zap_magic = ZAP_MAGIC;

	zp->zap_ptrtbl.zt_shift = ZAP_EMBEDDED_PTRTBL_SHIFT(zap);

	zp->zap_freeblk = 2;		/* block 1 will be the first leaf */
	zp->zap_num_leafs = 1;
	zp->zap_num_entries = 0;
	zp->zap_salt = zap->zap_salt;
	zp->zap_normflags = zap->zap_normflags;
	zp->zap_flags = flags;

	/* block 1 will be the first leaf */
	for (int i = 0; i < (1<<zp->zap_ptrtbl.zt_shift); i++)
		ZAP_EMBEDDED_PTRTBL_ENT(zap, i) = 1;

	/*
	 * set up block 1 - the first leaf
	 */
	dmu_buf_t *db;
	VERIFY0(dmu_buf_hold(zap->zap_objset, zap->zap_object,
	    1<<FZAP_BLOCK_SHIFT(zap), FTAG, &db, DMU_READ_NO_PREFETCH));
	dmu_buf_will_dirty(db, tx);

	zap_leaf_t *l = kmem_zalloc(sizeof (zap_leaf_t), KM_SLEEP);
	l->l_dbuf = db;

	zap_leaf_init(l, zp->zap_normflags != 0);

	kmem_free(l, sizeof (zap_leaf_t));
	dmu_buf_rele(db, FTAG);
}

static int
zap_tryupgradedir(zap_t *zap, dmu_tx_t *tx)
{
	if (RW_WRITE_HELD(&zap->zap_rwlock))
		return (1);
	if (rw_tryupgrade(&zap->zap_rwlock)) {
		dmu_buf_will_dirty(zap->zap_dbuf, tx);
		return (1);
	}
	return (0);
}

/*
 * Generic routines for dealing with the pointer & cookie tables.
 */

static int
zap_table_grow(zap_t *zap, zap_table_phys_t *tbl,
    void (*transfer_func)(const uint64_t *src, uint64_t *dst, int n),
    dmu_tx_t *tx)
{
	uint64_t newblk;
	int bs = FZAP_BLOCK_SHIFT(zap);
	int hepb = 1<<(bs-4);
	/* hepb = half the number of entries in a block */

	ASSERT(RW_WRITE_HELD(&zap->zap_rwlock));
	ASSERT(tbl->zt_blk != 0);
	ASSERT(tbl->zt_numblks > 0);

	if (tbl->zt_nextblk != 0) {
		newblk = tbl->zt_nextblk;
	} else {
		newblk = zap_allocate_blocks(zap, tbl->zt_numblks * 2);
		tbl->zt_nextblk = newblk;
		ASSERT0(tbl->zt_blks_copied);
		dmu_prefetch(zap->zap_objset, zap->zap_object, 0,
		    tbl->zt_blk << bs, tbl->zt_numblks << bs,
		    ZIO_PRIORITY_SYNC_READ);
	}

	/*
	 * Copy the ptrtbl from the old to new location.
	 */

	uint64_t b = tbl->zt_blks_copied;
	dmu_buf_t *db_old;
	int err = dmu_buf_hold(zap->zap_objset, zap->zap_object,
	    (tbl->zt_blk + b) << bs, FTAG, &db_old, DMU_READ_NO_PREFETCH);
	if (err != 0)
		return (err);

	/* first half of entries in old[b] go to new[2*b+0] */
	dmu_buf_t *db_new;
	VERIFY0(dmu_buf_hold(zap->zap_objset, zap->zap_object,
	    (newblk + 2*b+0) << bs, FTAG, &db_new, DMU_READ_NO_PREFETCH));
	dmu_buf_will_dirty(db_new, tx);
	transfer_func(db_old->db_data, db_new->db_data, hepb);
	dmu_buf_rele(db_new, FTAG);

	/* second half of entries in old[b] go to new[2*b+1] */
	VERIFY0(dmu_buf_hold(zap->zap_objset, zap->zap_object,
	    (newblk + 2*b+1) << bs, FTAG, &db_new, DMU_READ_NO_PREFETCH));
	dmu_buf_will_dirty(db_new, tx);
	transfer_func((uint64_t *)db_old->db_data + hepb,
	    db_new->db_data, hepb);
	dmu_buf_rele(db_new, FTAG);

	dmu_buf_rele(db_old, FTAG);

	tbl->zt_blks_copied++;

	dprintf("copied block %llu of %llu\n",
	    tbl->zt_blks_copied, tbl->zt_numblks);

	if (tbl->zt_blks_copied == tbl->zt_numblks) {
		(void) dmu_free_range(zap->zap_objset, zap->zap_object,
		    tbl->zt_blk << bs, tbl->zt_numblks << bs, tx);

		tbl->zt_blk = newblk;
		tbl->zt_numblks *= 2;
		tbl->zt_shift++;
		tbl->zt_nextblk = 0;
		tbl->zt_blks_copied = 0;

		dprintf("finished; numblocks now %llu (%lluk entries)\n",
		    tbl->zt_numblks, 1<<(tbl->zt_shift-10));
	}

	return (0);
}

static int
zap_table_store(zap_t *zap, zap_table_phys_t *tbl, uint64_t idx, uint64_t val,
    dmu_tx_t *tx)
{
	int bs = FZAP_BLOCK_SHIFT(zap);

	ASSERT(RW_LOCK_HELD(&zap->zap_rwlock));
	ASSERT(tbl->zt_blk != 0);

	dprintf("storing %llx at index %llx\n", val, idx);

	uint64_t blk = idx >> (bs-3);
	uint64_t off = idx & ((1<<(bs-3))-1);

	dmu_buf_t *db;
	int err = dmu_buf_hold(zap->zap_objset, zap->zap_object,
	    (tbl->zt_blk + blk) << bs, FTAG, &db, DMU_READ_NO_PREFETCH);
	if (err != 0)
		return (err);
	dmu_buf_will_dirty(db, tx);

	if (tbl->zt_nextblk != 0) {
		uint64_t idx2 = idx * 2;
		uint64_t blk2 = idx2 >> (bs-3);
		uint64_t off2 = idx2 & ((1<<(bs-3))-1);
		dmu_buf_t *db2;

		err = dmu_buf_hold(zap->zap_objset, zap->zap_object,
		    (tbl->zt_nextblk + blk2) << bs, FTAG, &db2,
		    DMU_READ_NO_PREFETCH);
		if (err != 0) {
			dmu_buf_rele(db, FTAG);
			return (err);
		}
		dmu_buf_will_dirty(db2, tx);
		((uint64_t *)db2->db_data)[off2] = val;
		((uint64_t *)db2->db_data)[off2+1] = val;
		dmu_buf_rele(db2, FTAG);
	}

	((uint64_t *)db->db_data)[off] = val;
	dmu_buf_rele(db, FTAG);

	return (0);
}

static int
zap_table_load(zap_t *zap, zap_table_phys_t *tbl, uint64_t idx, uint64_t *valp)
{
	int bs = FZAP_BLOCK_SHIFT(zap);

	ASSERT(RW_LOCK_HELD(&zap->zap_rwlock));

	uint64_t blk = idx >> (bs-3);
	uint64_t off = idx & ((1<<(bs-3))-1);

	/*
	 * Note: this is equivalent to dmu_buf_hold(), but we use
	 * _dnode_enter / _by_dnode because it's faster because we don't
	 * have to hold the dnode.
	 */
	dnode_t *dn = dmu_buf_dnode_enter(zap->zap_dbuf);
	dmu_buf_t *db;
	int err = dmu_buf_hold_by_dnode(dn,
	    (tbl->zt_blk + blk) << bs, FTAG, &db, DMU_READ_NO_PREFETCH);
	dmu_buf_dnode_exit(zap->zap_dbuf);
	if (err != 0)
		return (err);
	*valp = ((uint64_t *)db->db_data)[off];
	dmu_buf_rele(db, FTAG);

	if (tbl->zt_nextblk != 0) {
		/*
		 * read the nextblk for the sake of i/o error checking,
		 * so that zap_table_load() will catch errors for
		 * zap_table_store.
		 */
		blk = (idx*2) >> (bs-3);

		dn = dmu_buf_dnode_enter(zap->zap_dbuf);
		err = dmu_buf_hold_by_dnode(dn,
		    (tbl->zt_nextblk + blk) << bs, FTAG, &db,
		    DMU_READ_NO_PREFETCH);
		dmu_buf_dnode_exit(zap->zap_dbuf);
		if (err == 0)
			dmu_buf_rele(db, FTAG);
	}
	return (err);
}

/*
 * Routines for growing the ptrtbl.
 */

static void
zap_ptrtbl_transfer(const uint64_t *src, uint64_t *dst, int n)
{
	for (int i = 0; i < n; i++) {
		uint64_t lb = src[i];
		dst[2 * i + 0] = lb;
		dst[2 * i + 1] = lb;
	}
}

static int
zap_grow_ptrtbl(zap_t *zap, dmu_tx_t *tx)
{
	/*
	 * The pointer table should never use more hash bits than we
	 * have (otherwise we'd be using useless zero bits to index it).
	 * If we are within 2 bits of running out, stop growing, since
	 * this is already an aberrant condition.
	 */
	if (zap_f_phys(zap)->zap_ptrtbl.zt_shift >= zap_hashbits(zap) - 2)
		return (SET_ERROR(ENOSPC));

	if (zap_f_phys(zap)->zap_ptrtbl.zt_numblks == 0) {
		/*
		 * We are outgrowing the "embedded" ptrtbl (the one
		 * stored in the header block).  Give it its own entire
		 * block, which will double the size of the ptrtbl.
		 */
		ASSERT3U(zap_f_phys(zap)->zap_ptrtbl.zt_shift, ==,
		    ZAP_EMBEDDED_PTRTBL_SHIFT(zap));
		ASSERT0(zap_f_phys(zap)->zap_ptrtbl.zt_blk);

		uint64_t newblk = zap_allocate_blocks(zap, 1);
		dmu_buf_t *db_new;
		int err = dmu_buf_hold(zap->zap_objset, zap->zap_object,
		    newblk << FZAP_BLOCK_SHIFT(zap), FTAG, &db_new,
		    DMU_READ_NO_PREFETCH);
		if (err != 0)
			return (err);
		dmu_buf_will_dirty(db_new, tx);
		zap_ptrtbl_transfer(&ZAP_EMBEDDED_PTRTBL_ENT(zap, 0),
		    db_new->db_data, 1 << ZAP_EMBEDDED_PTRTBL_SHIFT(zap));
		dmu_buf_rele(db_new, FTAG);

		zap_f_phys(zap)->zap_ptrtbl.zt_blk = newblk;
		zap_f_phys(zap)->zap_ptrtbl.zt_numblks = 1;
		zap_f_phys(zap)->zap_ptrtbl.zt_shift++;

		ASSERT3U(1ULL << zap_f_phys(zap)->zap_ptrtbl.zt_shift, ==,
		    zap_f_phys(zap)->zap_ptrtbl.zt_numblks <<
		    (FZAP_BLOCK_SHIFT(zap)-3));

		return (0);
	} else {
		return (zap_table_grow(zap, &zap_f_phys(zap)->zap_ptrtbl,
		    zap_ptrtbl_transfer, tx));
	}
}

static void
zap_increment_num_entries(zap_t *zap, int delta, dmu_tx_t *tx)
{
	dmu_buf_will_dirty(zap->zap_dbuf, tx);
	mutex_enter(&zap->zap_f.zap_num_entries_mtx);
	ASSERT(delta > 0 || zap_f_phys(zap)->zap_num_entries >= -delta);
	zap_f_phys(zap)->zap_num_entries += delta;
	mutex_exit(&zap->zap_f.zap_num_entries_mtx);
}

static uint64_t
zap_allocate_blocks(zap_t *zap, int nblocks)
{
	ASSERT(RW_WRITE_HELD(&zap->zap_rwlock));
	uint64_t newblk = zap_f_phys(zap)->zap_freeblk;
	zap_f_phys(zap)->zap_freeblk += nblocks;
	return (newblk);
}

static void
zap_leaf_evict_sync(void *dbu)
{
	zap_leaf_t *l = dbu;

	rw_destroy(&l->l_rwlock);
	kmem_free(l, sizeof (zap_leaf_t));
}

static zap_leaf_t *
zap_create_leaf(zap_t *zap, dmu_tx_t *tx)
{
	zap_leaf_t *l = kmem_zalloc(sizeof (zap_leaf_t), KM_SLEEP);

	ASSERT(RW_WRITE_HELD(&zap->zap_rwlock));

	rw_init(&l->l_rwlock, 0, 0, 0);
	rw_enter(&l->l_rwlock, RW_WRITER);
	l->l_blkid = zap_allocate_blocks(zap, 1);
	l->l_dbuf = NULL;

	VERIFY0(dmu_buf_hold(zap->zap_objset, zap->zap_object,
	    l->l_blkid << FZAP_BLOCK_SHIFT(zap), NULL, &l->l_dbuf,
	    DMU_READ_NO_PREFETCH));
	dmu_buf_init_user(&l->l_dbu, zap_leaf_evict_sync, NULL, &l->l_dbuf);
	VERIFY3P(NULL, ==, dmu_buf_set_user(l->l_dbuf, &l->l_dbu));
	dmu_buf_will_dirty(l->l_dbuf, tx);

	zap_leaf_init(l, zap->zap_normflags != 0);

	zap_f_phys(zap)->zap_num_leafs++;

	return (l);
}

int
fzap_count(zap_t *zap, uint64_t *count)
{
	ASSERT(!zap->zap_ismicro);
	mutex_enter(&zap->zap_f.zap_num_entries_mtx); /* unnecessary */
	*count = zap_f_phys(zap)->zap_num_entries;
	mutex_exit(&zap->zap_f.zap_num_entries_mtx);
	return (0);
}

/*
 * Routines for obtaining zap_leaf_t's
 */

void
zap_put_leaf(zap_leaf_t *l)
{
	rw_exit(&l->l_rwlock);
	dmu_buf_rele(l->l_dbuf, NULL);
}

static zap_leaf_t *
zap_open_leaf(uint64_t blkid, dmu_buf_t *db)
{
	ASSERT(blkid != 0);

	zap_leaf_t *l = kmem_zalloc(sizeof (zap_leaf_t), KM_SLEEP);
	rw_init(&l->l_rwlock, 0, 0, 0);
	rw_enter(&l->l_rwlock, RW_WRITER);
	l->l_blkid = blkid;
	l->l_bs = highbit64(db->db_size) - 1;
	l->l_dbuf = db;

	dmu_buf_init_user(&l->l_dbu, zap_leaf_evict_sync, NULL, &l->l_dbuf);
	zap_leaf_t *winner = dmu_buf_set_user(db, &l->l_dbu);

	rw_exit(&l->l_rwlock);
	if (winner != NULL) {
		/* someone else set it first */
		zap_leaf_evict_sync(&l->l_dbu);
		l = winner;
	}

	/*
	 * lhr_pad was previously used for the next leaf in the leaf
	 * chain.  There should be no chained leafs (as we have removed
	 * support for them).
	 */
	ASSERT0(zap_leaf_phys(l)->l_hdr.lh_pad1);

	/*
	 * There should be more hash entries than there can be
	 * chunks to put in the hash table
	 */
	ASSERT3U(ZAP_LEAF_HASH_NUMENTRIES(l), >, ZAP_LEAF_NUMCHUNKS(l) / 3);

	/* The chunks should begin at the end of the hash table */
	ASSERT3P(&ZAP_LEAF_CHUNK(l, 0), ==,
	    &zap_leaf_phys(l)->l_hash[ZAP_LEAF_HASH_NUMENTRIES(l)]);

	/* The chunks should end at the end of the block */
	ASSERT3U((uintptr_t)&ZAP_LEAF_CHUNK(l, ZAP_LEAF_NUMCHUNKS(l)) -
	    (uintptr_t)zap_leaf_phys(l), ==, l->l_dbuf->db_size);

	return (l);
}

static int
zap_get_leaf_byblk(zap_t *zap, uint64_t blkid, dmu_tx_t *tx, krw_t lt,
    zap_leaf_t **lp)
{
	dmu_buf_t *db;

	ASSERT(RW_LOCK_HELD(&zap->zap_rwlock));

	int bs = FZAP_BLOCK_SHIFT(zap);
	dnode_t *dn = dmu_buf_dnode_enter(zap->zap_dbuf);
	int err = dmu_buf_hold_by_dnode(dn,
	    blkid << bs, NULL, &db, DMU_READ_NO_PREFETCH);
	dmu_buf_dnode_exit(zap->zap_dbuf);
	if (err != 0)
		return (err);

	ASSERT3U(db->db_object, ==, zap->zap_object);
	ASSERT3U(db->db_offset, ==, blkid << bs);
	ASSERT3U(db->db_size, ==, 1 << bs);
	ASSERT(blkid != 0);

	zap_leaf_t *l = dmu_buf_get_user(db);

	if (l == NULL)
		l = zap_open_leaf(blkid, db);

	rw_enter(&l->l_rwlock, lt);
	/*
	 * Must lock before dirtying, otherwise zap_leaf_phys(l) could change,
	 * causing ASSERT below to fail.
	 */
	if (lt == RW_WRITER)
		dmu_buf_will_dirty(db, tx);
	ASSERT3U(l->l_blkid, ==, blkid);
	ASSERT3P(l->l_dbuf, ==, db);
	ASSERT3U(zap_leaf_phys(l)->l_hdr.lh_block_type, ==, ZBT_LEAF);
	ASSERT3U(zap_leaf_phys(l)->l_hdr.lh_magic, ==, ZAP_LEAF_MAGIC);

	*lp = l;
	return (0);
}

static int
zap_idx_to_blk(zap_t *zap, uint64_t idx, uint64_t *valp)
{
	ASSERT(RW_LOCK_HELD(&zap->zap_rwlock));

	if (zap_f_phys(zap)->zap_ptrtbl.zt_numblks == 0) {
		ASSERT3U(idx, <,
		    (1ULL << zap_f_phys(zap)->zap_ptrtbl.zt_shift));
		*valp = ZAP_EMBEDDED_PTRTBL_ENT(zap, idx);
		return (0);
	} else {
		return (zap_table_load(zap, &zap_f_phys(zap)->zap_ptrtbl,
		    idx, valp));
	}
}

static int
zap_set_idx_to_blk(zap_t *zap, uint64_t idx, uint64_t blk, dmu_tx_t *tx)
{
	ASSERT(tx != NULL);
	ASSERT(RW_WRITE_HELD(&zap->zap_rwlock));

	if (zap_f_phys(zap)->zap_ptrtbl.zt_blk == 0) {
		ZAP_EMBEDDED_PTRTBL_ENT(zap, idx) = blk;
		return (0);
	} else {
		return (zap_table_store(zap, &zap_f_phys(zap)->zap_ptrtbl,
		    idx, blk, tx));
	}
}

static int
zap_deref_leaf(zap_t *zap, uint64_t h, dmu_tx_t *tx, krw_t lt, zap_leaf_t **lp)
{
	uint64_t blk;

	ASSERT(zap->zap_dbuf == NULL ||
	    zap_f_phys(zap) == zap->zap_dbuf->db_data);

	/* Reality check for corrupt zap objects (leaf or header). */
	if ((zap_f_phys(zap)->zap_block_type != ZBT_LEAF &&
	    zap_f_phys(zap)->zap_block_type != ZBT_HEADER) ||
	    zap_f_phys(zap)->zap_magic != ZAP_MAGIC) {
		return (SET_ERROR(EIO));
	}

	uint64_t idx = ZAP_HASH_IDX(h, zap_f_phys(zap)->zap_ptrtbl.zt_shift);
	int err = zap_idx_to_blk(zap, idx, &blk);
	if (err != 0)
		return (err);
	err = zap_get_leaf_byblk(zap, blk, tx, lt, lp);

	ASSERT(err ||
	    ZAP_HASH_IDX(h, zap_leaf_phys(*lp)->l_hdr.lh_prefix_len) ==
	    zap_leaf_phys(*lp)->l_hdr.lh_prefix);
	return (err);
}

static int
zap_expand_leaf(zap_name_t *zn, zap_leaf_t *l,
    void *tag, dmu_tx_t *tx, zap_leaf_t **lp)
{
	zap_t *zap = zn->zn_zap;
	uint64_t hash = zn->zn_hash;
	int err;
	int old_prefix_len = zap_leaf_phys(l)->l_hdr.lh_prefix_len;

	ASSERT3U(old_prefix_len, <=, zap_f_phys(zap)->zap_ptrtbl.zt_shift);
	ASSERT(RW_LOCK_HELD(&zap->zap_rwlock));

	ASSERT3U(ZAP_HASH_IDX(hash, old_prefix_len), ==,
	    zap_leaf_phys(l)->l_hdr.lh_prefix);

	if (zap_tryupgradedir(zap, tx) == 0 ||
	    old_prefix_len == zap_f_phys(zap)->zap_ptrtbl.zt_shift) {
		/* We failed to upgrade, or need to grow the pointer table */
		objset_t *os = zap->zap_objset;
		uint64_t object = zap->zap_object;

		zap_put_leaf(l);
		zap_unlockdir(zap, tag);
		err = zap_lockdir(os, object, tx, RW_WRITER,
		    FALSE, FALSE, tag, &zn->zn_zap);
		zap = zn->zn_zap;
		if (err != 0)
			return (err);
		ASSERT(!zap->zap_ismicro);

		while (old_prefix_len ==
		    zap_f_phys(zap)->zap_ptrtbl.zt_shift) {
			err = zap_grow_ptrtbl(zap, tx);
			if (err != 0)
				return (err);
		}

		err = zap_deref_leaf(zap, hash, tx, RW_WRITER, &l);
		if (err != 0)
			return (err);

		if (zap_leaf_phys(l)->l_hdr.lh_prefix_len != old_prefix_len) {
			/* it split while our locks were down */
			*lp = l;
			return (0);
		}
	}
	ASSERT(RW_WRITE_HELD(&zap->zap_rwlock));
	ASSERT3U(old_prefix_len, <, zap_f_phys(zap)->zap_ptrtbl.zt_shift);
	ASSERT3U(ZAP_HASH_IDX(hash, old_prefix_len), ==,
	    zap_leaf_phys(l)->l_hdr.lh_prefix);

	int prefix_diff = zap_f_phys(zap)->zap_ptrtbl.zt_shift -
	    (old_prefix_len + 1);
	uint64_t sibling =
	    (ZAP_HASH_IDX(hash, old_prefix_len + 1) | 1) << prefix_diff;

	/* check for i/o errors before doing zap_leaf_split */
	for (int i = 0; i < (1ULL << prefix_diff); i++) {
		uint64_t blk;
		err = zap_idx_to_blk(zap, sibling + i, &blk);
		if (err != 0)
			return (err);
		ASSERT3U(blk, ==, l->l_blkid);
	}

	zap_leaf_t *nl = zap_create_leaf(zap, tx);
	zap_leaf_split(l, nl, zap->zap_normflags != 0);

	/* set sibling pointers */
	for (int i = 0; i < (1ULL << prefix_diff); i++) {
		err = zap_set_idx_to_blk(zap, sibling + i, nl->l_blkid, tx);
		ASSERT0(err); /* we checked for i/o errors above */
	}

	if (hash & (1ULL << (64 - zap_leaf_phys(l)->l_hdr.lh_prefix_len))) {
		/* we want the sibling */
		zap_put_leaf(l);
		*lp = nl;
	} else {
		zap_put_leaf(nl);
		*lp = l;
	}

	return (0);
}

static void
zap_put_leaf_maybe_grow_ptrtbl(zap_name_t *zn, zap_leaf_t *l,
    void *tag, dmu_tx_t *tx)
{
	zap_t *zap = zn->zn_zap;
	int shift = zap_f_phys(zap)->zap_ptrtbl.zt_shift;
	int leaffull = (zap_leaf_phys(l)->l_hdr.lh_prefix_len == shift &&
	    zap_leaf_phys(l)->l_hdr.lh_nfree < ZAP_LEAF_LOW_WATER);

	zap_put_leaf(l);

	if (leaffull || zap_f_phys(zap)->zap_ptrtbl.zt_nextblk) {
		/*
		 * We are in the middle of growing the pointer table, or
		 * this leaf will soon make us grow it.
		 */
		if (zap_tryupgradedir(zap, tx) == 0) {
			objset_t *os = zap->zap_objset;
			uint64_t zapobj = zap->zap_object;

			zap_unlockdir(zap, tag);
			int err = zap_lockdir(os, zapobj, tx,
			    RW_WRITER, FALSE, FALSE, tag, &zn->zn_zap);
			zap = zn->zn_zap;
			if (err != 0)
				return;
		}

		/* could have finished growing while our locks were down */
		if (zap_f_phys(zap)->zap_ptrtbl.zt_shift == shift)
			(void) zap_grow_ptrtbl(zap, tx);
	}
}

static int
fzap_checkname(zap_name_t *zn)
{
	if (zn->zn_key_orig_numints * zn->zn_key_intlen > ZAP_MAXNAMELEN)
		return (SET_ERROR(ENAMETOOLONG));
	return (0);
}

static int
fzap_checksize(uint64_t integer_size, uint64_t num_integers)
{
	/* Only integer sizes supported by C */
	switch (integer_size) {
	case 1:
	case 2:
	case 4:
	case 8:
		break;
	default:
		return (SET_ERROR(EINVAL));
	}

	if (integer_size * num_integers > ZAP_MAXVALUELEN)
		return (E2BIG);

	return (0);
}

static int
fzap_check(zap_name_t *zn, uint64_t integer_size, uint64_t num_integers)
{
	int err = fzap_checkname(zn);
	if (err != 0)
		return (err);
	return (fzap_checksize(integer_size, num_integers));
}

/*
 * Routines for manipulating attributes.
 */
int
fzap_lookup(zap_name_t *zn,
    uint64_t integer_size, uint64_t num_integers, void *buf,
    char *realname, int rn_len, boolean_t *ncp)
{
	zap_leaf_t *l;
	zap_entry_handle_t zeh;

	int err = fzap_checkname(zn);
	if (err != 0)
		return (err);

	err = zap_deref_leaf(zn->zn_zap, zn->zn_hash, NULL, RW_READER, &l);
	if (err != 0)
		return (err);
	err = zap_leaf_lookup(l, zn, &zeh);
	if (err == 0) {
		if ((err = fzap_checksize(integer_size, num_integers)) != 0) {
			zap_put_leaf(l);
			return (err);
		}

		err = zap_entry_read(&zeh, integer_size, num_integers, buf);
		(void) zap_entry_read_name(zn->zn_zap, &zeh, rn_len, realname);
		if (ncp) {
			*ncp = zap_entry_normalization_conflict(&zeh,
			    zn, NULL, zn->zn_zap);
		}
	}

	zap_put_leaf(l);
	return (err);
}

int
fzap_add_cd(zap_name_t *zn,
    uint64_t integer_size, uint64_t num_integers,
    const void *val, uint32_t cd, void *tag, dmu_tx_t *tx)
{
	zap_leaf_t *l;
	int err;
	zap_entry_handle_t zeh;
	zap_t *zap = zn->zn_zap;

	ASSERT(RW_LOCK_HELD(&zap->zap_rwlock));
	ASSERT(!zap->zap_ismicro);
	ASSERT(fzap_check(zn, integer_size, num_integers) == 0);

	err = zap_deref_leaf(zap, zn->zn_hash, tx, RW_WRITER, &l);
	if (err != 0)
		return (err);
retry:
	err = zap_leaf_lookup(l, zn, &zeh);
	if (err == 0) {
		err = SET_ERROR(EEXIST);
		goto out;
	}
	if (err != ENOENT)
		goto out;

	err = zap_entry_create(l, zn, cd,
	    integer_size, num_integers, val, &zeh);

	if (err == 0) {
		zap_increment_num_entries(zap, 1, tx);
	} else if (err == EAGAIN) {
		err = zap_expand_leaf(zn, l, tag, tx, &l);
		zap = zn->zn_zap;	/* zap_expand_leaf() may change zap */
		if (err == 0)
			goto retry;
	}

out:
	if (zap != NULL)
		zap_put_leaf_maybe_grow_ptrtbl(zn, l, tag, tx);
	return (err);
}

int
fzap_add(zap_name_t *zn,
    uint64_t integer_size, uint64_t num_integers,
    const void *val, void *tag, dmu_tx_t *tx)
{
	int err = fzap_check(zn, integer_size, num_integers);
	if (err != 0)
		return (err);

	return (fzap_add_cd(zn, integer_size, num_integers,
	    val, ZAP_NEED_CD, tag, tx));
}

int
fzap_update(zap_name_t *zn,
    int integer_size, uint64_t num_integers, const void *val,
    void *tag, dmu_tx_t *tx)
{
	zap_leaf_t *l;
	int err;
	boolean_t create;
	zap_entry_handle_t zeh;
	zap_t *zap = zn->zn_zap;

	ASSERT(RW_LOCK_HELD(&zap->zap_rwlock));
	err = fzap_check(zn, integer_size, num_integers);
	if (err != 0)
		return (err);

	err = zap_deref_leaf(zap, zn->zn_hash, tx, RW_WRITER, &l);
	if (err != 0)
		return (err);
retry:
	err = zap_leaf_lookup(l, zn, &zeh);
	create = (err == ENOENT);
	ASSERT(err == 0 || err == ENOENT);

	if (create) {
		err = zap_entry_create(l, zn, ZAP_NEED_CD,
		    integer_size, num_integers, val, &zeh);
		if (err == 0)
			zap_increment_num_entries(zap, 1, tx);
	} else {
		err = zap_entry_update(&zeh, integer_size, num_integers, val);
	}

	if (err == EAGAIN) {
		err = zap_expand_leaf(zn, l, tag, tx, &l);
		zap = zn->zn_zap;	/* zap_expand_leaf() may change zap */
		if (err == 0)
			goto retry;
	}

	if (zap != NULL)
		zap_put_leaf_maybe_grow_ptrtbl(zn, l, tag, tx);
	return (err);
}

int
fzap_length(zap_name_t *zn,
    uint64_t *integer_size, uint64_t *num_integers)
{
	zap_leaf_t *l;
	int err;
	zap_entry_handle_t zeh;

	err = zap_deref_leaf(zn->zn_zap, zn->zn_hash, NULL, RW_READER, &l);
	if (err != 0)
		return (err);
	err = zap_leaf_lookup(l, zn, &zeh);
	if (err != 0)
		goto out;

	if (integer_size != 0)
		*integer_size = zeh.zeh_integer_size;
	if (num_integers != 0)
		*num_integers = zeh.zeh_num_integers;
out:
	zap_put_leaf(l);
	return (err);
}

int
fzap_remove(zap_name_t *zn, dmu_tx_t *tx)
{
	zap_leaf_t *l;
	int err;
	zap_entry_handle_t zeh;

	err = zap_deref_leaf(zn->zn_zap, zn->zn_hash, tx, RW_WRITER, &l);
	if (err != 0)
		return (err);
	err = zap_leaf_lookup(l, zn, &zeh);
	if (err == 0) {
		zap_entry_remove(&zeh);
		zap_increment_num_entries(zn->zn_zap, -1, tx);
	}
	zap_put_leaf(l);
	return (err);
}

void
fzap_prefetch(zap_name_t *zn)
{
	uint64_t blk;
	zap_t *zap = zn->zn_zap;

	uint64_t idx = ZAP_HASH_IDX(zn->zn_hash,
	    zap_f_phys(zap)->zap_ptrtbl.zt_shift);
	if (zap_idx_to_blk(zap, idx, &blk) != 0)
		return;
	int bs = FZAP_BLOCK_SHIFT(zap);
	dmu_prefetch(zap->zap_objset, zap->zap_object, 0, blk << bs, 1 << bs,
	    ZIO_PRIORITY_SYNC_READ);
}

/*
 * Helper functions for consumers.
 */

uint64_t
zap_create_link(objset_t *os, dmu_object_type_t ot, uint64_t parent_obj,
    const char *name, dmu_tx_t *tx)
{
	return (zap_create_link_dnsize(os, ot, parent_obj, name, 0, tx));
}

uint64_t
zap_create_link_dnsize(objset_t *os, dmu_object_type_t ot, uint64_t parent_obj,
    const char *name, int dnodesize, dmu_tx_t *tx)
{
 	uint64_t new_obj;
 
	VERIFY((new_obj = zap_create_dnsize(os, ot, DMU_OT_NONE, 0,
	    dnodesize, tx)) > 0);
	VERIFY0(zap_add(os, parent_obj, name, sizeof (uint64_t), 1, &new_obj,
	    tx));

	return (new_obj);
}

int
zap_value_search(objset_t *os, uint64_t zapobj, uint64_t value, uint64_t mask,
    char *name)
{
	zap_cursor_t zc;
	int err;

	if (mask == 0)
		mask = -1ULL;

	zap_attribute_t *za = kmem_alloc(sizeof (*za), KM_SLEEP);
	for (zap_cursor_init(&zc, os, zapobj);
	    (err = zap_cursor_retrieve(&zc, za)) == 0;
	    zap_cursor_advance(&zc)) {
		if ((za->za_first_integer & mask) == (value & mask)) {
			(void) strcpy(name, za->za_name);
			break;
		}
	}
	zap_cursor_fini(&zc);
	kmem_free(za, sizeof (*za));
	return (err);
}

int
zap_join(objset_t *os, uint64_t fromobj, uint64_t intoobj, dmu_tx_t *tx)
{
	zap_cursor_t zc;
	int err = 0;

	zap_attribute_t *za = kmem_alloc(sizeof (*za), KM_SLEEP);
	for (zap_cursor_init(&zc, os, fromobj);
	    zap_cursor_retrieve(&zc, za) == 0;
	    (void) zap_cursor_advance(&zc)) {
		if (za->za_integer_length != 8 || za->za_num_integers != 1) {
			err = SET_ERROR(EINVAL);
			break;
		}
		err = zap_add(os, intoobj, za->za_name,
		    8, 1, &za->za_first_integer, tx);
		if (err != 0)
			break;
	}
	zap_cursor_fini(&zc);
	kmem_free(za, sizeof (*za));
	return (err);
}

int
zap_join_key(objset_t *os, uint64_t fromobj, uint64_t intoobj,
    uint64_t value, dmu_tx_t *tx)
{
	zap_cursor_t zc;
	int err = 0;

	zap_attribute_t *za = kmem_alloc(sizeof (*za), KM_SLEEP);
	for (zap_cursor_init(&zc, os, fromobj);
	    zap_cursor_retrieve(&zc, za) == 0;
	    (void) zap_cursor_advance(&zc)) {
		if (za->za_integer_length != 8 || za->za_num_integers != 1) {
			err = SET_ERROR(EINVAL);
			break;
		}
		err = zap_add(os, intoobj, za->za_name,
		    8, 1, &value, tx);
		if (err != 0)
			break;
	}
	zap_cursor_fini(&zc);
	kmem_free(za, sizeof (*za));
	return (err);
}

int
zap_join_increment(objset_t *os, uint64_t fromobj, uint64_t intoobj,
    dmu_tx_t *tx)
{
	zap_cursor_t zc;
	int err = 0;

	zap_attribute_t *za = kmem_alloc(sizeof (*za), KM_SLEEP);
	for (zap_cursor_init(&zc, os, fromobj);
	    zap_cursor_retrieve(&zc, za) == 0;
	    (void) zap_cursor_advance(&zc)) {
		uint64_t delta = 0;

		if (za->za_integer_length != 8 || za->za_num_integers != 1) {
			err = SET_ERROR(EINVAL);
			break;
		}

		err = zap_lookup(os, intoobj, za->za_name, 8, 1, &delta);
		if (err != 0 && err != ENOENT)
			break;
		delta += za->za_first_integer;
		err = zap_update(os, intoobj, za->za_name, 8, 1, &delta, tx);
		if (err != 0)
			break;
	}
	zap_cursor_fini(&zc);
	kmem_free(za, sizeof (*za));
	return (err);
}

int
zap_add_int(objset_t *os, uint64_t obj, uint64_t value, dmu_tx_t *tx)
{
	char name[20];

	(void) snprintf(name, sizeof (name), "%llx", (longlong_t)value);
	return (zap_add(os, obj, name, 8, 1, &value, tx));
}

int
zap_remove_int(objset_t *os, uint64_t obj, uint64_t value, dmu_tx_t *tx)
{
	char name[20];

	(void) snprintf(name, sizeof (name), "%llx", (longlong_t)value);
	return (zap_remove(os, obj, name, tx));
}

int
zap_lookup_int(objset_t *os, uint64_t obj, uint64_t value)
{
	char name[20];

	(void) snprintf(name, sizeof (name), "%llx", (longlong_t)value);
	return (zap_lookup(os, obj, name, 8, 1, &value));
}

int
zap_add_int_key(objset_t *os, uint64_t obj,
    uint64_t key, uint64_t value, dmu_tx_t *tx)
{
	char name[20];

	(void) snprintf(name, sizeof (name), "%llx", (longlong_t)key);
	return (zap_add(os, obj, name, 8, 1, &value, tx));
}

int
zap_update_int_key(objset_t *os, uint64_t obj,
    uint64_t key, uint64_t value, dmu_tx_t *tx)
{
	char name[20];

	(void) snprintf(name, sizeof (name), "%llx", (longlong_t)key);
	return (zap_update(os, obj, name, 8, 1, &value, tx));
}

int
zap_lookup_int_key(objset_t *os, uint64_t obj, uint64_t key, uint64_t *valuep)
{
	char name[20];

	(void) snprintf(name, sizeof (name), "%llx", (longlong_t)key);
	return (zap_lookup(os, obj, name, 8, 1, valuep));
}

int
zap_increment(objset_t *os, uint64_t obj, const char *name, int64_t delta,
    dmu_tx_t *tx)
{
	uint64_t value = 0;

	if (delta == 0)
		return (0);

	int err = zap_lookup(os, obj, name, 8, 1, &value);
	if (err != 0 && err != ENOENT)
		return (err);
	value += delta;
	if (value == 0)
		err = zap_remove(os, obj, name, tx);
	else
		err = zap_update(os, obj, name, 8, 1, &value, tx);
	return (err);
}

int
zap_increment_int(objset_t *os, uint64_t obj, uint64_t key, int64_t delta,
    dmu_tx_t *tx)
{
	char name[20];

	(void) snprintf(name, sizeof (name), "%llx", (longlong_t)key);
	return (zap_increment(os, obj, name, delta, tx));
}

/*
 * Routines for iterating over the attributes.
 */

int
fzap_cursor_retrieve(zap_t *zap, zap_cursor_t *zc, zap_attribute_t *za)
{
	int err = ENOENT;
	zap_entry_handle_t zeh;
	zap_leaf_t *l;

	/* retrieve the next entry at or after zc_hash/zc_cd */
	/* if no entry, return ENOENT */

	if (zc->zc_leaf &&
	    (ZAP_HASH_IDX(zc->zc_hash,
	    zap_leaf_phys(zc->zc_leaf)->l_hdr.lh_prefix_len) !=
	    zap_leaf_phys(zc->zc_leaf)->l_hdr.lh_prefix)) {
		rw_enter(&zc->zc_leaf->l_rwlock, RW_READER);
		zap_put_leaf(zc->zc_leaf);
		zc->zc_leaf = NULL;
	}

again:
	if (zc->zc_leaf == NULL) {
		err = zap_deref_leaf(zap, zc->zc_hash, NULL, RW_READER,
		    &zc->zc_leaf);
		if (err != 0)
			return (err);
	} else {
		rw_enter(&zc->zc_leaf->l_rwlock, RW_READER);
	}
	l = zc->zc_leaf;

	err = zap_leaf_lookup_closest(l, zc->zc_hash, zc->zc_cd, &zeh);

	if (err == ENOENT) {
		uint64_t nocare =
		    (1ULL << (64 - zap_leaf_phys(l)->l_hdr.lh_prefix_len)) - 1;
		zc->zc_hash = (zc->zc_hash & ~nocare) + nocare + 1;
		zc->zc_cd = 0;
		if (zap_leaf_phys(l)->l_hdr.lh_prefix_len == 0 ||
		    zc->zc_hash == 0) {
			zc->zc_hash = -1ULL;
		} else {
			zap_put_leaf(zc->zc_leaf);
			zc->zc_leaf = NULL;
			goto again;
		}
	}

	if (err == 0) {
		zc->zc_hash = zeh.zeh_hash;
		zc->zc_cd = zeh.zeh_cd;
		za->za_integer_length = zeh.zeh_integer_size;
		za->za_num_integers = zeh.zeh_num_integers;
		if (zeh.zeh_num_integers == 0) {
			za->za_first_integer = 0;
		} else {
			err = zap_entry_read(&zeh, 8, 1, &za->za_first_integer);
			ASSERT(err == 0 || err == EOVERFLOW);
		}
		err = zap_entry_read_name(zap, &zeh,
		    sizeof (za->za_name), za->za_name);
		ASSERT(err == 0);

		za->za_normalization_conflict =
		    zap_entry_normalization_conflict(&zeh,
		    NULL, za->za_name, zap);
	}
	rw_exit(&zc->zc_leaf->l_rwlock);
	return (err);
}

static void
zap_stats_ptrtbl(zap_t *zap, uint64_t *tbl, int len, zap_stats_t *zs)
{
	uint64_t lastblk = 0;

	/*
	 * NB: if a leaf has more pointers than an entire ptrtbl block
	 * can hold, then it'll be accounted for more than once, since
	 * we won't have lastblk.
	 */
	for (int i = 0; i < len; i++) {
		zap_leaf_t *l;

		if (tbl[i] == lastblk)
			continue;
		lastblk = tbl[i];

		int err = zap_get_leaf_byblk(zap, tbl[i], NULL, RW_READER, &l);
		if (err == 0) {
			zap_leaf_stats(zap, l, zs);
			zap_put_leaf(l);
		}
	}
}

int
fzap_cursor_move_to_key(zap_cursor_t *zc, zap_name_t *zn)
{
	int err;
	zap_leaf_t *l;
	zap_entry_handle_t zeh;

	if (zn->zn_key_orig_numints * zn->zn_key_intlen > ZAP_MAXNAMELEN)
		return (SET_ERROR(ENAMETOOLONG));

	err = zap_deref_leaf(zc->zc_zap, zn->zn_hash, NULL, RW_READER, &l);
	if (err != 0)
		return (err);

	err = zap_leaf_lookup(l, zn, &zeh);
	if (err != 0)
		return (err);

	zc->zc_leaf = l;
	zc->zc_hash = zeh.zeh_hash;
	zc->zc_cd = zeh.zeh_cd;

	return (err);
}

void
fzap_get_stats(zap_t *zap, zap_stats_t *zs)
{
	int bs = FZAP_BLOCK_SHIFT(zap);
	zs->zs_blocksize = 1ULL << bs;

	/*
	 * Set zap_phys_t fields
	 */
	zs->zs_num_leafs = zap_f_phys(zap)->zap_num_leafs;
	zs->zs_num_entries = zap_f_phys(zap)->zap_num_entries;
	zs->zs_num_blocks = zap_f_phys(zap)->zap_freeblk;
	zs->zs_block_type = zap_f_phys(zap)->zap_block_type;
	zs->zs_magic = zap_f_phys(zap)->zap_magic;
	zs->zs_salt = zap_f_phys(zap)->zap_salt;

	/*
	 * Set zap_ptrtbl fields
	 */
	zs->zs_ptrtbl_len = 1ULL << zap_f_phys(zap)->zap_ptrtbl.zt_shift;
	zs->zs_ptrtbl_nextblk = zap_f_phys(zap)->zap_ptrtbl.zt_nextblk;
	zs->zs_ptrtbl_blks_copied =
	    zap_f_phys(zap)->zap_ptrtbl.zt_blks_copied;
	zs->zs_ptrtbl_zt_blk = zap_f_phys(zap)->zap_ptrtbl.zt_blk;
	zs->zs_ptrtbl_zt_numblks = zap_f_phys(zap)->zap_ptrtbl.zt_numblks;
	zs->zs_ptrtbl_zt_shift = zap_f_phys(zap)->zap_ptrtbl.zt_shift;

	if (zap_f_phys(zap)->zap_ptrtbl.zt_numblks == 0) {
		/* the ptrtbl is entirely in the header block. */
		zap_stats_ptrtbl(zap, &ZAP_EMBEDDED_PTRTBL_ENT(zap, 0),
		    1 << ZAP_EMBEDDED_PTRTBL_SHIFT(zap), zs);
	} else {
		dmu_prefetch(zap->zap_objset, zap->zap_object, 0,
		    zap_f_phys(zap)->zap_ptrtbl.zt_blk << bs,
		    zap_f_phys(zap)->zap_ptrtbl.zt_numblks << bs,
		    ZIO_PRIORITY_SYNC_READ);

		for (int b = 0; b < zap_f_phys(zap)->zap_ptrtbl.zt_numblks;
		    b++) {
			dmu_buf_t *db;
			int err;

			err = dmu_buf_hold(zap->zap_objset, zap->zap_object,
			    (zap_f_phys(zap)->zap_ptrtbl.zt_blk + b) << bs,
			    FTAG, &db, DMU_READ_NO_PREFETCH);
			if (err == 0) {
				zap_stats_ptrtbl(zap, db->db_data,
				    1<<(bs-3), zs);
				dmu_buf_rele(db, FTAG);
			}
		}
	}
}
