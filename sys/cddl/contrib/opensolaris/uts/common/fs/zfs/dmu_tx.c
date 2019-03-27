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
 * Copyright 2011 Nexenta Systems, Inc.  All rights reserved.
 * Copyright (c) 2012, 2017 by Delphix. All rights reserved.
 * Copyright (c) 2014 Integros [integros.com]
 */

#include <sys/dmu.h>
#include <sys/dmu_impl.h>
#include <sys/dbuf.h>
#include <sys/dmu_tx.h>
#include <sys/dmu_objset.h>
#include <sys/dsl_dataset.h>
#include <sys/dsl_dir.h>
#include <sys/dsl_pool.h>
#include <sys/zap_impl.h>
#include <sys/spa.h>
#include <sys/sa.h>
#include <sys/sa_impl.h>
#include <sys/zfs_context.h>
#include <sys/varargs.h>

typedef void (*dmu_tx_hold_func_t)(dmu_tx_t *tx, struct dnode *dn,
    uint64_t arg1, uint64_t arg2);


dmu_tx_t *
dmu_tx_create_dd(dsl_dir_t *dd)
{
	dmu_tx_t *tx = kmem_zalloc(sizeof (dmu_tx_t), KM_SLEEP);
	tx->tx_dir = dd;
	if (dd != NULL)
		tx->tx_pool = dd->dd_pool;
	list_create(&tx->tx_holds, sizeof (dmu_tx_hold_t),
	    offsetof(dmu_tx_hold_t, txh_node));
	list_create(&tx->tx_callbacks, sizeof (dmu_tx_callback_t),
	    offsetof(dmu_tx_callback_t, dcb_node));
	tx->tx_start = gethrtime();
	return (tx);
}

dmu_tx_t *
dmu_tx_create(objset_t *os)
{
	dmu_tx_t *tx = dmu_tx_create_dd(os->os_dsl_dataset->ds_dir);
	tx->tx_objset = os;
	return (tx);
}

dmu_tx_t *
dmu_tx_create_assigned(struct dsl_pool *dp, uint64_t txg)
{
	dmu_tx_t *tx = dmu_tx_create_dd(NULL);

	txg_verify(dp->dp_spa, txg);
	tx->tx_pool = dp;
	tx->tx_txg = txg;
	tx->tx_anyobj = TRUE;

	return (tx);
}

int
dmu_tx_is_syncing(dmu_tx_t *tx)
{
	return (tx->tx_anyobj);
}

int
dmu_tx_private_ok(dmu_tx_t *tx)
{
	return (tx->tx_anyobj);
}

static dmu_tx_hold_t *
dmu_tx_hold_dnode_impl(dmu_tx_t *tx, dnode_t *dn, enum dmu_tx_hold_type type,
    uint64_t arg1, uint64_t arg2)
{
	dmu_tx_hold_t *txh;

	if (dn != NULL) {
		(void) refcount_add(&dn->dn_holds, tx);
		if (tx->tx_txg != 0) {
			mutex_enter(&dn->dn_mtx);
			/*
			 * dn->dn_assigned_txg == tx->tx_txg doesn't pose a
			 * problem, but there's no way for it to happen (for
			 * now, at least).
			 */
			ASSERT(dn->dn_assigned_txg == 0);
			dn->dn_assigned_txg = tx->tx_txg;
			(void) refcount_add(&dn->dn_tx_holds, tx);
			mutex_exit(&dn->dn_mtx);
		}
	}

	txh = kmem_zalloc(sizeof (dmu_tx_hold_t), KM_SLEEP);
	txh->txh_tx = tx;
	txh->txh_dnode = dn;
	refcount_create(&txh->txh_space_towrite);
	refcount_create(&txh->txh_memory_tohold);
	txh->txh_type = type;
	txh->txh_arg1 = arg1;
	txh->txh_arg2 = arg2;
	list_insert_tail(&tx->tx_holds, txh);

	return (txh);
}

static dmu_tx_hold_t *
dmu_tx_hold_object_impl(dmu_tx_t *tx, objset_t *os, uint64_t object,
    enum dmu_tx_hold_type type, uint64_t arg1, uint64_t arg2)
{
	dnode_t *dn = NULL;
	dmu_tx_hold_t *txh;
	int err;

	if (object != DMU_NEW_OBJECT) {
		err = dnode_hold(os, object, FTAG, &dn);
		if (err != 0) {
			tx->tx_err = err;
			return (NULL);
		}
	}
	txh = dmu_tx_hold_dnode_impl(tx, dn, type, arg1, arg2);
	if (dn != NULL)
		dnode_rele(dn, FTAG);
	return (txh);
}

void
dmu_tx_add_new_object(dmu_tx_t *tx, dnode_t *dn)
{
	/*
	 * If we're syncing, they can manipulate any object anyhow, and
	 * the hold on the dnode_t can cause problems.
	 */
	if (!dmu_tx_is_syncing(tx))
		(void) dmu_tx_hold_dnode_impl(tx, dn, THT_NEWOBJECT, 0, 0);
}

/*
 * This function reads specified data from disk.  The specified data will
 * be needed to perform the transaction -- i.e, it will be read after
 * we do dmu_tx_assign().  There are two reasons that we read the data now
 * (before dmu_tx_assign()):
 *
 * 1. Reading it now has potentially better performance.  The transaction
 * has not yet been assigned, so the TXG is not held open, and also the
 * caller typically has less locks held when calling dmu_tx_hold_*() than
 * after the transaction has been assigned.  This reduces the lock (and txg)
 * hold times, thus reducing lock contention.
 *
 * 2. It is easier for callers (primarily the ZPL) to handle i/o errors
 * that are detected before they start making changes to the DMU state
 * (i.e. now).  Once the transaction has been assigned, and some DMU
 * state has been changed, it can be difficult to recover from an i/o
 * error (e.g. to undo the changes already made in memory at the DMU
 * layer).  Typically code to do so does not exist in the caller -- it
 * assumes that the data has already been cached and thus i/o errors are
 * not possible.
 *
 * It has been observed that the i/o initiated here can be a performance
 * problem, and it appears to be optional, because we don't look at the
 * data which is read.  However, removing this read would only serve to
 * move the work elsewhere (after the dmu_tx_assign()), where it may
 * have a greater impact on performance (in addition to the impact on
 * fault tolerance noted above).
 */
static int
dmu_tx_check_ioerr(zio_t *zio, dnode_t *dn, int level, uint64_t blkid)
{
	int err;
	dmu_buf_impl_t *db;

	rw_enter(&dn->dn_struct_rwlock, RW_READER);
	db = dbuf_hold_level(dn, level, blkid, FTAG);
	rw_exit(&dn->dn_struct_rwlock);
	if (db == NULL)
		return (SET_ERROR(EIO));
	err = dbuf_read(db, zio, DB_RF_CANFAIL | DB_RF_NOPREFETCH);
	dbuf_rele(db, FTAG);
	return (err);
}

/* ARGSUSED */
static void
dmu_tx_count_write(dmu_tx_hold_t *txh, uint64_t off, uint64_t len)
{
	dnode_t *dn = txh->txh_dnode;
	int err = 0;

	if (len == 0)
		return;

	(void) refcount_add_many(&txh->txh_space_towrite, len, FTAG);

	if (refcount_count(&txh->txh_space_towrite) > 2 * DMU_MAX_ACCESS)
		err = SET_ERROR(EFBIG);

	if (dn == NULL)
		return;

	/*
	 * For i/o error checking, read the blocks that will be needed
	 * to perform the write: the first and last level-0 blocks (if
	 * they are not aligned, i.e. if they are partial-block writes),
	 * and all the level-1 blocks.
	 */
	if (dn->dn_maxblkid == 0) {
		if (off < dn->dn_datablksz &&
		    (off > 0 || len < dn->dn_datablksz)) {
			err = dmu_tx_check_ioerr(NULL, dn, 0, 0);
			if (err != 0) {
				txh->txh_tx->tx_err = err;
			}
		}
	} else {
		zio_t *zio = zio_root(dn->dn_objset->os_spa,
		    NULL, NULL, ZIO_FLAG_CANFAIL);

		/* first level-0 block */
		uint64_t start = off >> dn->dn_datablkshift;
		if (P2PHASE(off, dn->dn_datablksz) || len < dn->dn_datablksz) {
			err = dmu_tx_check_ioerr(zio, dn, 0, start);
			if (err != 0) {
				txh->txh_tx->tx_err = err;
			}
		}

		/* last level-0 block */
		uint64_t end = (off + len - 1) >> dn->dn_datablkshift;
		if (end != start && end <= dn->dn_maxblkid &&
		    P2PHASE(off + len, dn->dn_datablksz)) {
			err = dmu_tx_check_ioerr(zio, dn, 0, end);
			if (err != 0) {
				txh->txh_tx->tx_err = err;
			}
		}

		/* level-1 blocks */
		if (dn->dn_nlevels > 1) {
			int shft = dn->dn_indblkshift - SPA_BLKPTRSHIFT;
			for (uint64_t i = (start >> shft) + 1;
			    i < end >> shft; i++) {
				err = dmu_tx_check_ioerr(zio, dn, 1, i);
				if (err != 0) {
					txh->txh_tx->tx_err = err;
				}
			}
		}

		err = zio_wait(zio);
		if (err != 0) {
			txh->txh_tx->tx_err = err;
		}
	}
}

static void
dmu_tx_count_dnode(dmu_tx_hold_t *txh)
{
	(void) refcount_add_many(&txh->txh_space_towrite, DNODE_MIN_SIZE, FTAG);
}

void
dmu_tx_hold_write(dmu_tx_t *tx, uint64_t object, uint64_t off, int len)
{
	dmu_tx_hold_t *txh;

	ASSERT0(tx->tx_txg);
	ASSERT3U(len, <=, DMU_MAX_ACCESS);
	ASSERT(len == 0 || UINT64_MAX - off >= len - 1);

	txh = dmu_tx_hold_object_impl(tx, tx->tx_objset,
	    object, THT_WRITE, off, len);
	if (txh != NULL) {
		dmu_tx_count_write(txh, off, len);
		dmu_tx_count_dnode(txh);
	}
}

void
dmu_tx_hold_remap_l1indirect(dmu_tx_t *tx, uint64_t object)
{
	dmu_tx_hold_t *txh;

	ASSERT(tx->tx_txg == 0);
	txh = dmu_tx_hold_object_impl(tx, tx->tx_objset,
	    object, THT_WRITE, 0, 0);
	if (txh == NULL)
		return;

	dnode_t *dn = txh->txh_dnode;
	(void) refcount_add_many(&txh->txh_space_towrite,
	    1ULL << dn->dn_indblkshift, FTAG);
	dmu_tx_count_dnode(txh);
}

void
dmu_tx_hold_write_by_dnode(dmu_tx_t *tx, dnode_t *dn, uint64_t off, int len)
{
	dmu_tx_hold_t *txh;

	ASSERT0(tx->tx_txg);
	ASSERT3U(len, <=, DMU_MAX_ACCESS);
	ASSERT(len == 0 || UINT64_MAX - off >= len - 1);

	txh = dmu_tx_hold_dnode_impl(tx, dn, THT_WRITE, off, len);
	if (txh != NULL) {
		dmu_tx_count_write(txh, off, len);
		dmu_tx_count_dnode(txh);
	}
}

/*
 * This function marks the transaction as being a "net free".  The end
 * result is that refquotas will be disabled for this transaction, and
 * this transaction will be able to use half of the pool space overhead
 * (see dsl_pool_adjustedsize()).  Therefore this function should only
 * be called for transactions that we expect will not cause a net increase
 * in the amount of space used (but it's OK if that is occasionally not true).
 */
void
dmu_tx_mark_netfree(dmu_tx_t *tx)
{
	tx->tx_netfree = B_TRUE;
}

static void
dmu_tx_hold_free_impl(dmu_tx_hold_t *txh, uint64_t off, uint64_t len)
{
	dmu_tx_t *tx;
	dnode_t *dn;
	int err;

	tx = txh->txh_tx;
	ASSERT(tx->tx_txg == 0);

	dn = txh->txh_dnode;
	dmu_tx_count_dnode(txh);

	if (off >= (dn->dn_maxblkid + 1) * dn->dn_datablksz)
		return;
	if (len == DMU_OBJECT_END)
		len = (dn->dn_maxblkid + 1) * dn->dn_datablksz - off;


	/*
	 * For i/o error checking, we read the first and last level-0
	 * blocks if they are not aligned, and all the level-1 blocks.
	 *
	 * Note:  dbuf_free_range() assumes that we have not instantiated
	 * any level-0 dbufs that will be completely freed.  Therefore we must
	 * exercise care to not read or count the first and last blocks
	 * if they are blocksize-aligned.
	 */
	if (dn->dn_datablkshift == 0) {
		if (off != 0 || len < dn->dn_datablksz)
			dmu_tx_count_write(txh, 0, dn->dn_datablksz);
	} else {
		/* first block will be modified if it is not aligned */
		if (!IS_P2ALIGNED(off, 1 << dn->dn_datablkshift))
			dmu_tx_count_write(txh, off, 1);
		/* last block will be modified if it is not aligned */
		if (!IS_P2ALIGNED(off + len, 1 << dn->dn_datablkshift))
			dmu_tx_count_write(txh, off + len, 1);
	}

	/*
	 * Check level-1 blocks.
	 */
	if (dn->dn_nlevels > 1) {
		int shift = dn->dn_datablkshift + dn->dn_indblkshift -
		    SPA_BLKPTRSHIFT;
		uint64_t start = off >> shift;
		uint64_t end = (off + len) >> shift;

		ASSERT(dn->dn_indblkshift != 0);

		/*
		 * dnode_reallocate() can result in an object with indirect
		 * blocks having an odd data block size.  In this case,
		 * just check the single block.
		 */
		if (dn->dn_datablkshift == 0)
			start = end = 0;

		zio_t *zio = zio_root(tx->tx_pool->dp_spa,
		    NULL, NULL, ZIO_FLAG_CANFAIL);
		for (uint64_t i = start; i <= end; i++) {
			uint64_t ibyte = i << shift;
			err = dnode_next_offset(dn, 0, &ibyte, 2, 1, 0);
			i = ibyte >> shift;
			if (err == ESRCH || i > end)
				break;
			if (err != 0) {
				tx->tx_err = err;
				(void) zio_wait(zio);
				return;
			}

			(void) refcount_add_many(&txh->txh_memory_tohold,
			    1 << dn->dn_indblkshift, FTAG);

			err = dmu_tx_check_ioerr(zio, dn, 1, i);
			if (err != 0) {
				tx->tx_err = err;
				(void) zio_wait(zio);
				return;
			}
		}
		err = zio_wait(zio);
		if (err != 0) {
			tx->tx_err = err;
			return;
		}
	}
}

void
dmu_tx_hold_free(dmu_tx_t *tx, uint64_t object, uint64_t off, uint64_t len)
{
	dmu_tx_hold_t *txh;

	txh = dmu_tx_hold_object_impl(tx, tx->tx_objset,
	    object, THT_FREE, off, len);
	if (txh != NULL)
		(void) dmu_tx_hold_free_impl(txh, off, len);
}

void
dmu_tx_hold_free_by_dnode(dmu_tx_t *tx, dnode_t *dn, uint64_t off, uint64_t len)
{
	dmu_tx_hold_t *txh;

	txh = dmu_tx_hold_dnode_impl(tx, dn, THT_FREE, off, len);
	if (txh != NULL)
		(void) dmu_tx_hold_free_impl(txh, off, len);
}

static void
dmu_tx_hold_zap_impl(dmu_tx_hold_t *txh, const char *name)
{
	dmu_tx_t *tx = txh->txh_tx;
	dnode_t *dn;
	int err;

	ASSERT(tx->tx_txg == 0);

	dn = txh->txh_dnode;

	dmu_tx_count_dnode(txh);

	/*
	 * Modifying a almost-full microzap is around the worst case (128KB)
	 *
	 * If it is a fat zap, the worst case would be 7*16KB=112KB:
	 * - 3 blocks overwritten: target leaf, ptrtbl block, header block
	 * - 4 new blocks written if adding:
	 *    - 2 blocks for possibly split leaves,
	 *    - 2 grown ptrtbl blocks
	 */
	(void) refcount_add_many(&txh->txh_space_towrite,
	    MZAP_MAX_BLKSZ, FTAG);

	if (dn == NULL)
		return;

	ASSERT3P(DMU_OT_BYTESWAP(dn->dn_type), ==, DMU_BSWAP_ZAP);

	if (dn->dn_maxblkid == 0 || name == NULL) {
		/*
		 * This is a microzap (only one block), or we don't know
		 * the name.  Check the first block for i/o errors.
		 */
		err = dmu_tx_check_ioerr(NULL, dn, 0, 0);
		if (err != 0) {
			tx->tx_err = err;
		}
	} else {
		/*
		 * Access the name so that we'll check for i/o errors to
		 * the leaf blocks, etc.  We ignore ENOENT, as this name
		 * may not yet exist.
		 */
		err = zap_lookup_by_dnode(dn, name, 8, 0, NULL);
		if (err == EIO || err == ECKSUM || err == ENXIO) {
			tx->tx_err = err;
		}
	}
}

void
dmu_tx_hold_zap(dmu_tx_t *tx, uint64_t object, int add, const char *name)
{
	dmu_tx_hold_t *txh;

	ASSERT0(tx->tx_txg);

	txh = dmu_tx_hold_object_impl(tx, tx->tx_objset,
	    object, THT_ZAP, add, (uintptr_t)name);
	if (txh != NULL)
		dmu_tx_hold_zap_impl(txh, name);
}

void
dmu_tx_hold_zap_by_dnode(dmu_tx_t *tx, dnode_t *dn, int add, const char *name)
{
	dmu_tx_hold_t *txh;

	ASSERT0(tx->tx_txg);
	ASSERT(dn != NULL);

	txh = dmu_tx_hold_dnode_impl(tx, dn, THT_ZAP, add, (uintptr_t)name);
	if (txh != NULL)
		dmu_tx_hold_zap_impl(txh, name);
}

void
dmu_tx_hold_bonus(dmu_tx_t *tx, uint64_t object)
{
	dmu_tx_hold_t *txh;

	ASSERT(tx->tx_txg == 0);

	txh = dmu_tx_hold_object_impl(tx, tx->tx_objset,
	    object, THT_BONUS, 0, 0);
	if (txh)
		dmu_tx_count_dnode(txh);
}

void
dmu_tx_hold_bonus_by_dnode(dmu_tx_t *tx, dnode_t *dn)
{
	dmu_tx_hold_t *txh;

	ASSERT0(tx->tx_txg);

	txh = dmu_tx_hold_dnode_impl(tx, dn, THT_BONUS, 0, 0);
	if (txh)
		dmu_tx_count_dnode(txh);
}

void
dmu_tx_hold_space(dmu_tx_t *tx, uint64_t space)
{
	dmu_tx_hold_t *txh;
	ASSERT(tx->tx_txg == 0);

	txh = dmu_tx_hold_object_impl(tx, tx->tx_objset,
	    DMU_NEW_OBJECT, THT_SPACE, space, 0);

	(void) refcount_add_many(&txh->txh_space_towrite, space, FTAG);
}

#ifdef ZFS_DEBUG
void
dmu_tx_dirty_buf(dmu_tx_t *tx, dmu_buf_impl_t *db)
{
	boolean_t match_object = B_FALSE;
	boolean_t match_offset = B_FALSE;

	DB_DNODE_ENTER(db);
	dnode_t *dn = DB_DNODE(db);
	ASSERT(tx->tx_txg != 0);
	ASSERT(tx->tx_objset == NULL || dn->dn_objset == tx->tx_objset);
	ASSERT3U(dn->dn_object, ==, db->db.db_object);

	if (tx->tx_anyobj) {
		DB_DNODE_EXIT(db);
		return;
	}

	/* XXX No checking on the meta dnode for now */
	if (db->db.db_object == DMU_META_DNODE_OBJECT) {
		DB_DNODE_EXIT(db);
		return;
	}

	for (dmu_tx_hold_t *txh = list_head(&tx->tx_holds); txh != NULL;
	    txh = list_next(&tx->tx_holds, txh)) {
		ASSERT(dn == NULL || dn->dn_assigned_txg == tx->tx_txg);
		if (txh->txh_dnode == dn && txh->txh_type != THT_NEWOBJECT)
			match_object = TRUE;
		if (txh->txh_dnode == NULL || txh->txh_dnode == dn) {
			int datablkshift = dn->dn_datablkshift ?
			    dn->dn_datablkshift : SPA_MAXBLOCKSHIFT;
			int epbs = dn->dn_indblkshift - SPA_BLKPTRSHIFT;
			int shift = datablkshift + epbs * db->db_level;
			uint64_t beginblk = shift >= 64 ? 0 :
			    (txh->txh_arg1 >> shift);
			uint64_t endblk = shift >= 64 ? 0 :
			    ((txh->txh_arg1 + txh->txh_arg2 - 1) >> shift);
			uint64_t blkid = db->db_blkid;

			/* XXX txh_arg2 better not be zero... */

			dprintf("found txh type %x beginblk=%llx endblk=%llx\n",
			    txh->txh_type, beginblk, endblk);

			switch (txh->txh_type) {
			case THT_WRITE:
				if (blkid >= beginblk && blkid <= endblk)
					match_offset = TRUE;
				/*
				 * We will let this hold work for the bonus
				 * or spill buffer so that we don't need to
				 * hold it when creating a new object.
				 */
				if (blkid == DMU_BONUS_BLKID ||
				    blkid == DMU_SPILL_BLKID)
					match_offset = TRUE;
				/*
				 * They might have to increase nlevels,
				 * thus dirtying the new TLIBs.  Or the
				 * might have to change the block size,
				 * thus dirying the new lvl=0 blk=0.
				 */
				if (blkid == 0)
					match_offset = TRUE;
				break;
			case THT_FREE:
				/*
				 * We will dirty all the level 1 blocks in
				 * the free range and perhaps the first and
				 * last level 0 block.
				 */
				if (blkid >= beginblk && (blkid <= endblk ||
				    txh->txh_arg2 == DMU_OBJECT_END))
					match_offset = TRUE;
				break;
			case THT_SPILL:
				if (blkid == DMU_SPILL_BLKID)
					match_offset = TRUE;
				break;
			case THT_BONUS:
				if (blkid == DMU_BONUS_BLKID)
					match_offset = TRUE;
				break;
			case THT_ZAP:
				match_offset = TRUE;
				break;
			case THT_NEWOBJECT:
				match_object = TRUE;
				break;
			default:
				ASSERT(!"bad txh_type");
			}
		}
		if (match_object && match_offset) {
			DB_DNODE_EXIT(db);
			return;
		}
	}
	DB_DNODE_EXIT(db);
	panic("dirtying dbuf obj=%llx lvl=%u blkid=%llx but not tx_held\n",
	    (u_longlong_t)db->db.db_object, db->db_level,
	    (u_longlong_t)db->db_blkid);
}
#endif

/*
 * If we can't do 10 iops, something is wrong.  Let us go ahead
 * and hit zfs_dirty_data_max.
 */
hrtime_t zfs_delay_max_ns = MSEC2NSEC(100);
int zfs_delay_resolution_ns = 100 * 1000; /* 100 microseconds */

/*
 * We delay transactions when we've determined that the backend storage
 * isn't able to accommodate the rate of incoming writes.
 *
 * If there is already a transaction waiting, we delay relative to when
 * that transaction finishes waiting.  This way the calculated min_time
 * is independent of the number of threads concurrently executing
 * transactions.
 *
 * If we are the only waiter, wait relative to when the transaction
 * started, rather than the current time.  This credits the transaction for
 * "time already served", e.g. reading indirect blocks.
 *
 * The minimum time for a transaction to take is calculated as:
 *     min_time = scale * (dirty - min) / (max - dirty)
 *     min_time is then capped at zfs_delay_max_ns.
 *
 * The delay has two degrees of freedom that can be adjusted via tunables.
 * The percentage of dirty data at which we start to delay is defined by
 * zfs_delay_min_dirty_percent. This should typically be at or above
 * zfs_vdev_async_write_active_max_dirty_percent so that we only start to
 * delay after writing at full speed has failed to keep up with the incoming
 * write rate. The scale of the curve is defined by zfs_delay_scale. Roughly
 * speaking, this variable determines the amount of delay at the midpoint of
 * the curve.
 *
 * delay
 *  10ms +-------------------------------------------------------------*+
 *       |                                                             *|
 *   9ms +                                                             *+
 *       |                                                             *|
 *   8ms +                                                             *+
 *       |                                                            * |
 *   7ms +                                                            * +
 *       |                                                            * |
 *   6ms +                                                            * +
 *       |                                                            * |
 *   5ms +                                                           *  +
 *       |                                                           *  |
 *   4ms +                                                           *  +
 *       |                                                           *  |
 *   3ms +                                                          *   +
 *       |                                                          *   |
 *   2ms +                                              (midpoint) *    +
 *       |                                                  |    **     |
 *   1ms +                                                  v ***       +
 *       |             zfs_delay_scale ---------->     ********         |
 *     0 +-------------------------------------*********----------------+
 *       0%                    <- zfs_dirty_data_max ->               100%
 *
 * Note that since the delay is added to the outstanding time remaining on the
 * most recent transaction, the delay is effectively the inverse of IOPS.
 * Here the midpoint of 500us translates to 2000 IOPS. The shape of the curve
 * was chosen such that small changes in the amount of accumulated dirty data
 * in the first 3/4 of the curve yield relatively small differences in the
 * amount of delay.
 *
 * The effects can be easier to understand when the amount of delay is
 * represented on a log scale:
 *
 * delay
 * 100ms +-------------------------------------------------------------++
 *       +                                                              +
 *       |                                                              |
 *       +                                                             *+
 *  10ms +                                                             *+
 *       +                                                           ** +
 *       |                                              (midpoint)  **  |
 *       +                                                  |     **    +
 *   1ms +                                                  v ****      +
 *       +             zfs_delay_scale ---------->        *****         +
 *       |                                             ****             |
 *       +                                          ****                +
 * 100us +                                        **                    +
 *       +                                       *                      +
 *       |                                      *                       |
 *       +                                     *                        +
 *  10us +                                     *                        +
 *       +                                                              +
 *       |                                                              |
 *       +                                                              +
 *       +--------------------------------------------------------------+
 *       0%                    <- zfs_dirty_data_max ->               100%
 *
 * Note here that only as the amount of dirty data approaches its limit does
 * the delay start to increase rapidly. The goal of a properly tuned system
 * should be to keep the amount of dirty data out of that range by first
 * ensuring that the appropriate limits are set for the I/O scheduler to reach
 * optimal throughput on the backend storage, and then by changing the value
 * of zfs_delay_scale to increase the steepness of the curve.
 */
static void
dmu_tx_delay(dmu_tx_t *tx, uint64_t dirty)
{
	dsl_pool_t *dp = tx->tx_pool;
	uint64_t delay_min_bytes =
	    zfs_dirty_data_max * zfs_delay_min_dirty_percent / 100;
	hrtime_t wakeup, min_tx_time, now;

	if (dirty <= delay_min_bytes)
		return;

	/*
	 * The caller has already waited until we are under the max.
	 * We make them pass us the amount of dirty data so we don't
	 * have to handle the case of it being >= the max, which could
	 * cause a divide-by-zero if it's == the max.
	 */
	ASSERT3U(dirty, <, zfs_dirty_data_max);

	now = gethrtime();
	min_tx_time = zfs_delay_scale *
	    (dirty - delay_min_bytes) / (zfs_dirty_data_max - dirty);
	if (now > tx->tx_start + min_tx_time)
		return;

	min_tx_time = MIN(min_tx_time, zfs_delay_max_ns);

	DTRACE_PROBE3(delay__mintime, dmu_tx_t *, tx, uint64_t, dirty,
	    uint64_t, min_tx_time);

	mutex_enter(&dp->dp_lock);
	wakeup = MAX(tx->tx_start + min_tx_time,
	    dp->dp_last_wakeup + min_tx_time);
	dp->dp_last_wakeup = wakeup;
	mutex_exit(&dp->dp_lock);

#ifdef _KERNEL
#ifdef illumos
	mutex_enter(&curthread->t_delay_lock);
	while (cv_timedwait_hires(&curthread->t_delay_cv,
	    &curthread->t_delay_lock, wakeup, zfs_delay_resolution_ns,
	    CALLOUT_FLAG_ABSOLUTE | CALLOUT_FLAG_ROUNDUP) > 0)
		continue;
	mutex_exit(&curthread->t_delay_lock);
#else
	pause_sbt("dmu_tx_delay", nstosbt(wakeup),
	    nstosbt(zfs_delay_resolution_ns), C_ABSOLUTE);
#endif
#else
	hrtime_t delta = wakeup - gethrtime();
	struct timespec ts;
	ts.tv_sec = delta / NANOSEC;
	ts.tv_nsec = delta % NANOSEC;
	(void) nanosleep(&ts, NULL);
#endif
}

/*
 * This routine attempts to assign the transaction to a transaction group.
 * To do so, we must determine if there is sufficient free space on disk.
 *
 * If this is a "netfree" transaction (i.e. we called dmu_tx_mark_netfree()
 * on it), then it is assumed that there is sufficient free space,
 * unless there's insufficient slop space in the pool (see the comment
 * above spa_slop_shift in spa_misc.c).
 *
 * If it is not a "netfree" transaction, then if the data already on disk
 * is over the allowed usage (e.g. quota), this will fail with EDQUOT or
 * ENOSPC.  Otherwise, if the current rough estimate of pending changes,
 * plus the rough estimate of this transaction's changes, may exceed the
 * allowed usage, then this will fail with ERESTART, which will cause the
 * caller to wait for the pending changes to be written to disk (by waiting
 * for the next TXG to open), and then check the space usage again.
 *
 * The rough estimate of pending changes is comprised of the sum of:
 *
 *  - this transaction's holds' txh_space_towrite
 *
 *  - dd_tempreserved[], which is the sum of in-flight transactions'
 *    holds' txh_space_towrite (i.e. those transactions that have called
 *    dmu_tx_assign() but not yet called dmu_tx_commit()).
 *
 *  - dd_space_towrite[], which is the amount of dirtied dbufs.
 *
 * Note that all of these values are inflated by spa_get_worst_case_asize(),
 * which means that we may get ERESTART well before we are actually in danger
 * of running out of space, but this also mitigates any small inaccuracies
 * in the rough estimate (e.g. txh_space_towrite doesn't take into account
 * indirect blocks, and dd_space_towrite[] doesn't take into account changes
 * to the MOS).
 *
 * Note that due to this algorithm, it is possible to exceed the allowed
 * usage by one transaction.  Also, as we approach the allowed usage,
 * we will allow a very limited amount of changes into each TXG, thus
 * decreasing performance.
 */
static int
dmu_tx_try_assign(dmu_tx_t *tx, uint64_t txg_how)
{
	spa_t *spa = tx->tx_pool->dp_spa;

	ASSERT0(tx->tx_txg);

	if (tx->tx_err)
		return (tx->tx_err);

	if (spa_suspended(spa)) {
		/*
		 * If the user has indicated a blocking failure mode
		 * then return ERESTART which will block in dmu_tx_wait().
		 * Otherwise, return EIO so that an error can get
		 * propagated back to the VOP calls.
		 *
		 * Note that we always honor the txg_how flag regardless
		 * of the failuremode setting.
		 */
		if (spa_get_failmode(spa) == ZIO_FAILURE_MODE_CONTINUE &&
		    !(txg_how & TXG_WAIT))
			return (SET_ERROR(EIO));

		return (SET_ERROR(ERESTART));
	}

	if (!tx->tx_dirty_delayed &&
	    dsl_pool_need_dirty_delay(tx->tx_pool)) {
		tx->tx_wait_dirty = B_TRUE;
		return (SET_ERROR(ERESTART));
	}

	tx->tx_txg = txg_hold_open(tx->tx_pool, &tx->tx_txgh);
	tx->tx_needassign_txh = NULL;

	/*
	 * NB: No error returns are allowed after txg_hold_open, but
	 * before processing the dnode holds, due to the
	 * dmu_tx_unassign() logic.
	 */

	uint64_t towrite = 0;
	uint64_t tohold = 0;
	for (dmu_tx_hold_t *txh = list_head(&tx->tx_holds); txh != NULL;
	    txh = list_next(&tx->tx_holds, txh)) {
		dnode_t *dn = txh->txh_dnode;
		if (dn != NULL) {
			mutex_enter(&dn->dn_mtx);
			if (dn->dn_assigned_txg == tx->tx_txg - 1) {
				mutex_exit(&dn->dn_mtx);
				tx->tx_needassign_txh = txh;
				return (SET_ERROR(ERESTART));
			}
			if (dn->dn_assigned_txg == 0)
				dn->dn_assigned_txg = tx->tx_txg;
			ASSERT3U(dn->dn_assigned_txg, ==, tx->tx_txg);
			(void) refcount_add(&dn->dn_tx_holds, tx);
			mutex_exit(&dn->dn_mtx);
		}
		towrite += refcount_count(&txh->txh_space_towrite);
		tohold += refcount_count(&txh->txh_memory_tohold);
	}

	/* needed allocation: worst-case estimate of write space */
	uint64_t asize = spa_get_worst_case_asize(tx->tx_pool->dp_spa, towrite);
	/* calculate memory footprint estimate */
	uint64_t memory = towrite + tohold;

	if (tx->tx_dir != NULL && asize != 0) {
		int err = dsl_dir_tempreserve_space(tx->tx_dir, memory,
		    asize, tx->tx_netfree, &tx->tx_tempreserve_cookie, tx);
		if (err != 0)
			return (err);
	}

	return (0);
}

static void
dmu_tx_unassign(dmu_tx_t *tx)
{
	if (tx->tx_txg == 0)
		return;

	txg_rele_to_quiesce(&tx->tx_txgh);

	/*
	 * Walk the transaction's hold list, removing the hold on the
	 * associated dnode, and notifying waiters if the refcount drops to 0.
	 */
	for (dmu_tx_hold_t *txh = list_head(&tx->tx_holds);
	    txh != tx->tx_needassign_txh;
	    txh = list_next(&tx->tx_holds, txh)) {
		dnode_t *dn = txh->txh_dnode;

		if (dn == NULL)
			continue;
		mutex_enter(&dn->dn_mtx);
		ASSERT3U(dn->dn_assigned_txg, ==, tx->tx_txg);

		if (refcount_remove(&dn->dn_tx_holds, tx) == 0) {
			dn->dn_assigned_txg = 0;
			cv_broadcast(&dn->dn_notxholds);
		}
		mutex_exit(&dn->dn_mtx);
	}

	txg_rele_to_sync(&tx->tx_txgh);

	tx->tx_lasttried_txg = tx->tx_txg;
	tx->tx_txg = 0;
}

/*
 * Assign tx to a transaction group; txg_how is a bitmask:
 *
 * If TXG_WAIT is set and the currently open txg is full, this function
 * will wait until there's a new txg. This should be used when no locks
 * are being held. With this bit set, this function will only fail if
 * we're truly out of space (or over quota).
 *
 * If TXG_WAIT is *not* set and we can't assign into the currently open
 * txg without blocking, this function will return immediately with
 * ERESTART. This should be used whenever locks are being held.  On an
 * ERESTART error, the caller should drop all locks, call dmu_tx_wait(),
 * and try again.
 *
 * If TXG_NOTHROTTLE is set, this indicates that this tx should not be
 * delayed due on the ZFS Write Throttle (see comments in dsl_pool.c for
 * details on the throttle). This is used by the VFS operations, after
 * they have already called dmu_tx_wait() (though most likely on a
 * different tx).
 */
int
dmu_tx_assign(dmu_tx_t *tx, uint64_t txg_how)
{
	int err;

	ASSERT(tx->tx_txg == 0);
	ASSERT0(txg_how & ~(TXG_WAIT | TXG_NOTHROTTLE));
	ASSERT(!dsl_pool_sync_context(tx->tx_pool));

	/* If we might wait, we must not hold the config lock. */
	IMPLY((txg_how & TXG_WAIT), !dsl_pool_config_held(tx->tx_pool));

	if ((txg_how & TXG_NOTHROTTLE))
		tx->tx_dirty_delayed = B_TRUE;

	while ((err = dmu_tx_try_assign(tx, txg_how)) != 0) {
		dmu_tx_unassign(tx);

		if (err != ERESTART || !(txg_how & TXG_WAIT))
			return (err);

		dmu_tx_wait(tx);
	}

	txg_rele_to_quiesce(&tx->tx_txgh);

	return (0);
}

void
dmu_tx_wait(dmu_tx_t *tx)
{
	spa_t *spa = tx->tx_pool->dp_spa;
	dsl_pool_t *dp = tx->tx_pool;

	ASSERT(tx->tx_txg == 0);
	ASSERT(!dsl_pool_config_held(tx->tx_pool));

	if (tx->tx_wait_dirty) {
		/*
		 * dmu_tx_try_assign() has determined that we need to wait
		 * because we've consumed much or all of the dirty buffer
		 * space.
		 */
		mutex_enter(&dp->dp_lock);
		while (dp->dp_dirty_total >= zfs_dirty_data_max)
			cv_wait(&dp->dp_spaceavail_cv, &dp->dp_lock);
		uint64_t dirty = dp->dp_dirty_total;
		mutex_exit(&dp->dp_lock);

		dmu_tx_delay(tx, dirty);

		tx->tx_wait_dirty = B_FALSE;

		/*
		 * Note: setting tx_dirty_delayed only has effect if the
		 * caller used TX_WAIT.  Otherwise they are going to
		 * destroy this tx and try again.  The common case,
		 * zfs_write(), uses TX_WAIT.
		 */
		tx->tx_dirty_delayed = B_TRUE;
	} else if (spa_suspended(spa) || tx->tx_lasttried_txg == 0) {
		/*
		 * If the pool is suspended we need to wait until it
		 * is resumed.  Note that it's possible that the pool
		 * has become active after this thread has tried to
		 * obtain a tx.  If that's the case then tx_lasttried_txg
		 * would not have been set.
		 */
		txg_wait_synced(dp, spa_last_synced_txg(spa) + 1);
	} else if (tx->tx_needassign_txh) {
		/*
		 * A dnode is assigned to the quiescing txg.  Wait for its
		 * transaction to complete.
		 */
		dnode_t *dn = tx->tx_needassign_txh->txh_dnode;

		mutex_enter(&dn->dn_mtx);
		while (dn->dn_assigned_txg == tx->tx_lasttried_txg - 1)
			cv_wait(&dn->dn_notxholds, &dn->dn_mtx);
		mutex_exit(&dn->dn_mtx);
		tx->tx_needassign_txh = NULL;
	} else {
		/*
		 * If we have a lot of dirty data just wait until we sync
		 * out a TXG at which point we'll hopefully have synced
		 * a portion of the changes.
		 */
		txg_wait_synced(dp, spa_last_synced_txg(spa) + 1);
	}
}

static void
dmu_tx_destroy(dmu_tx_t *tx)
{
	dmu_tx_hold_t *txh;

	while ((txh = list_head(&tx->tx_holds)) != NULL) {
		dnode_t *dn = txh->txh_dnode;

		list_remove(&tx->tx_holds, txh);
		refcount_destroy_many(&txh->txh_space_towrite,
		    refcount_count(&txh->txh_space_towrite));
		refcount_destroy_many(&txh->txh_memory_tohold,
		    refcount_count(&txh->txh_memory_tohold));
		kmem_free(txh, sizeof (dmu_tx_hold_t));
		if (dn != NULL)
			dnode_rele(dn, tx);
	}

	list_destroy(&tx->tx_callbacks);
	list_destroy(&tx->tx_holds);
	kmem_free(tx, sizeof (dmu_tx_t));
}

void
dmu_tx_commit(dmu_tx_t *tx)
{
	ASSERT(tx->tx_txg != 0);

	/*
	 * Go through the transaction's hold list and remove holds on
	 * associated dnodes, notifying waiters if no holds remain.
	 */
	for (dmu_tx_hold_t *txh = list_head(&tx->tx_holds); txh != NULL;
	    txh = list_next(&tx->tx_holds, txh)) {
		dnode_t *dn = txh->txh_dnode;

		if (dn == NULL)
			continue;

		mutex_enter(&dn->dn_mtx);
		ASSERT3U(dn->dn_assigned_txg, ==, tx->tx_txg);

		if (refcount_remove(&dn->dn_tx_holds, tx) == 0) {
			dn->dn_assigned_txg = 0;
			cv_broadcast(&dn->dn_notxholds);
		}
		mutex_exit(&dn->dn_mtx);
	}

	if (tx->tx_tempreserve_cookie)
		dsl_dir_tempreserve_clear(tx->tx_tempreserve_cookie, tx);

	if (!list_is_empty(&tx->tx_callbacks))
		txg_register_callbacks(&tx->tx_txgh, &tx->tx_callbacks);

	if (tx->tx_anyobj == FALSE)
		txg_rele_to_sync(&tx->tx_txgh);

	dmu_tx_destroy(tx);
}

void
dmu_tx_abort(dmu_tx_t *tx)
{
	ASSERT(tx->tx_txg == 0);

	/*
	 * Call any registered callbacks with an error code.
	 */
	if (!list_is_empty(&tx->tx_callbacks))
		dmu_tx_do_callbacks(&tx->tx_callbacks, ECANCELED);

	dmu_tx_destroy(tx);
}

uint64_t
dmu_tx_get_txg(dmu_tx_t *tx)
{
	ASSERT(tx->tx_txg != 0);
	return (tx->tx_txg);
}

dsl_pool_t *
dmu_tx_pool(dmu_tx_t *tx)
{
	ASSERT(tx->tx_pool != NULL);
	return (tx->tx_pool);
}

void
dmu_tx_callback_register(dmu_tx_t *tx, dmu_tx_callback_func_t *func, void *data)
{
	dmu_tx_callback_t *dcb;

	dcb = kmem_alloc(sizeof (dmu_tx_callback_t), KM_SLEEP);

	dcb->dcb_func = func;
	dcb->dcb_data = data;

	list_insert_tail(&tx->tx_callbacks, dcb);
}

/*
 * Call all the commit callbacks on a list, with a given error code.
 */
void
dmu_tx_do_callbacks(list_t *cb_list, int error)
{
	dmu_tx_callback_t *dcb;

	while ((dcb = list_head(cb_list)) != NULL) {
		list_remove(cb_list, dcb);
		dcb->dcb_func(dcb->dcb_data, error);
		kmem_free(dcb, sizeof (dmu_tx_callback_t));
	}
}

/*
 * Interface to hold a bunch of attributes.
 * used for creating new files.
 * attrsize is the total size of all attributes
 * to be added during object creation
 *
 * For updating/adding a single attribute dmu_tx_hold_sa() should be used.
 */

/*
 * hold necessary attribute name for attribute registration.
 * should be a very rare case where this is needed.  If it does
 * happen it would only happen on the first write to the file system.
 */
static void
dmu_tx_sa_registration_hold(sa_os_t *sa, dmu_tx_t *tx)
{
	if (!sa->sa_need_attr_registration)
		return;

	for (int i = 0; i != sa->sa_num_attrs; i++) {
		if (!sa->sa_attr_table[i].sa_registered) {
			if (sa->sa_reg_attr_obj)
				dmu_tx_hold_zap(tx, sa->sa_reg_attr_obj,
				    B_TRUE, sa->sa_attr_table[i].sa_name);
			else
				dmu_tx_hold_zap(tx, DMU_NEW_OBJECT,
				    B_TRUE, sa->sa_attr_table[i].sa_name);
		}
	}
}

void
dmu_tx_hold_spill(dmu_tx_t *tx, uint64_t object)
{
	dmu_tx_hold_t *txh = dmu_tx_hold_object_impl(tx,
	    tx->tx_objset, object, THT_SPILL, 0, 0);

	(void) refcount_add_many(&txh->txh_space_towrite,
	    SPA_OLD_MAXBLOCKSIZE, FTAG);
}

void
dmu_tx_hold_sa_create(dmu_tx_t *tx, int attrsize)
{
	sa_os_t *sa = tx->tx_objset->os_sa;

	dmu_tx_hold_bonus(tx, DMU_NEW_OBJECT);

	if (tx->tx_objset->os_sa->sa_master_obj == 0)
		return;

	if (tx->tx_objset->os_sa->sa_layout_attr_obj) {
		dmu_tx_hold_zap(tx, sa->sa_layout_attr_obj, B_TRUE, NULL);
	} else {
		dmu_tx_hold_zap(tx, sa->sa_master_obj, B_TRUE, SA_LAYOUTS);
		dmu_tx_hold_zap(tx, sa->sa_master_obj, B_TRUE, SA_REGISTRY);
		dmu_tx_hold_zap(tx, DMU_NEW_OBJECT, B_TRUE, NULL);
		dmu_tx_hold_zap(tx, DMU_NEW_OBJECT, B_TRUE, NULL);
	}

	dmu_tx_sa_registration_hold(sa, tx);

	if (attrsize <= DN_OLD_MAX_BONUSLEN && !sa->sa_force_spill)
		return;

	(void) dmu_tx_hold_object_impl(tx, tx->tx_objset, DMU_NEW_OBJECT,
	    THT_SPILL, 0, 0);
}

/*
 * Hold SA attribute
 *
 * dmu_tx_hold_sa(dmu_tx_t *tx, sa_handle_t *, attribute, add, size)
 *
 * variable_size is the total size of all variable sized attributes
 * passed to this function.  It is not the total size of all
 * variable size attributes that *may* exist on this object.
 */
void
dmu_tx_hold_sa(dmu_tx_t *tx, sa_handle_t *hdl, boolean_t may_grow)
{
	uint64_t object;
	sa_os_t *sa = tx->tx_objset->os_sa;

	ASSERT(hdl != NULL);

	object = sa_handle_object(hdl);

	dmu_tx_hold_bonus(tx, object);

	if (tx->tx_objset->os_sa->sa_master_obj == 0)
		return;

	if (tx->tx_objset->os_sa->sa_reg_attr_obj == 0 ||
	    tx->tx_objset->os_sa->sa_layout_attr_obj == 0) {
		dmu_tx_hold_zap(tx, sa->sa_master_obj, B_TRUE, SA_LAYOUTS);
		dmu_tx_hold_zap(tx, sa->sa_master_obj, B_TRUE, SA_REGISTRY);
		dmu_tx_hold_zap(tx, DMU_NEW_OBJECT, B_TRUE, NULL);
		dmu_tx_hold_zap(tx, DMU_NEW_OBJECT, B_TRUE, NULL);
	}

	dmu_tx_sa_registration_hold(sa, tx);

	if (may_grow && tx->tx_objset->os_sa->sa_layout_attr_obj)
		dmu_tx_hold_zap(tx, sa->sa_layout_attr_obj, B_TRUE, NULL);

	if (sa->sa_force_spill || may_grow || hdl->sa_spill) {
		ASSERT(tx->tx_txg == 0);
		dmu_tx_hold_spill(tx, object);
	} else {
		dmu_buf_impl_t *db = (dmu_buf_impl_t *)hdl->sa_bonus;
		dnode_t *dn;

		DB_DNODE_ENTER(db);
		dn = DB_DNODE(db);
		if (dn->dn_have_spill) {
			ASSERT(tx->tx_txg == 0);
			dmu_tx_hold_spill(tx, object);
		}
		DB_DNODE_EXIT(db);
	}
}
