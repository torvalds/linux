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
 * Copyright (c) 2011, 2015 by Delphix. All rights reserved.
 * Copyright (c) 2014 Integros [integros.com]
 */

#include <sys/arc.h>
#include <sys/bptree.h>
#include <sys/dmu.h>
#include <sys/dmu_objset.h>
#include <sys/dmu_tx.h>
#include <sys/dmu_traverse.h>
#include <sys/dsl_dataset.h>
#include <sys/dsl_dir.h>
#include <sys/dsl_pool.h>
#include <sys/dnode.h>
#include <sys/refcount.h>
#include <sys/spa.h>

/*
 * A bptree is a queue of root block pointers from destroyed datasets. When a
 * dataset is destroyed its root block pointer is put on the end of the pool's
 * bptree queue so the dataset's blocks can be freed asynchronously by
 * dsl_scan_sync. This allows the delete operation to finish without traversing
 * all the dataset's blocks.
 *
 * Note that while bt_begin and bt_end are only ever incremented in this code,
 * they are effectively reset to 0 every time the entire bptree is freed because
 * the bptree's object is destroyed and re-created.
 */

struct bptree_args {
	bptree_phys_t *ba_phys;	/* data in bonus buffer, dirtied if freeing */
	boolean_t ba_free;	/* true if freeing during traversal */

	bptree_itor_t *ba_func;	/* function to call for each blockpointer */
	void *ba_arg;		/* caller supplied argument to ba_func */
	dmu_tx_t *ba_tx;	/* caller supplied tx, NULL if not freeing */
} bptree_args_t;

uint64_t
bptree_alloc(objset_t *os, dmu_tx_t *tx)
{
	uint64_t obj;
	dmu_buf_t *db;
	bptree_phys_t *bt;

	obj = dmu_object_alloc(os, DMU_OTN_UINT64_METADATA,
	    SPA_OLD_MAXBLOCKSIZE, DMU_OTN_UINT64_METADATA,
	    sizeof (bptree_phys_t), tx);

	/*
	 * Bonus buffer contents are already initialized to 0, but for
	 * readability we make it explicit.
	 */
	VERIFY3U(0, ==, dmu_bonus_hold(os, obj, FTAG, &db));
	dmu_buf_will_dirty(db, tx);
	bt = db->db_data;
	bt->bt_begin = 0;
	bt->bt_end = 0;
	bt->bt_bytes = 0;
	bt->bt_comp = 0;
	bt->bt_uncomp = 0;
	dmu_buf_rele(db, FTAG);

	return (obj);
}

int
bptree_free(objset_t *os, uint64_t obj, dmu_tx_t *tx)
{
	dmu_buf_t *db;
	bptree_phys_t *bt;

	VERIFY3U(0, ==, dmu_bonus_hold(os, obj, FTAG, &db));
	bt = db->db_data;
	ASSERT3U(bt->bt_begin, ==, bt->bt_end);
	ASSERT0(bt->bt_bytes);
	ASSERT0(bt->bt_comp);
	ASSERT0(bt->bt_uncomp);
	dmu_buf_rele(db, FTAG);

	return (dmu_object_free(os, obj, tx));
}

boolean_t
bptree_is_empty(objset_t *os, uint64_t obj)
{
	dmu_buf_t *db;
	bptree_phys_t *bt;
	boolean_t rv;

	VERIFY0(dmu_bonus_hold(os, obj, FTAG, &db));
	bt = db->db_data;
	rv = (bt->bt_begin == bt->bt_end);
	dmu_buf_rele(db, FTAG);
	return (rv);
}

void
bptree_add(objset_t *os, uint64_t obj, blkptr_t *bp, uint64_t birth_txg,
    uint64_t bytes, uint64_t comp, uint64_t uncomp, dmu_tx_t *tx)
{
	dmu_buf_t *db;
	bptree_phys_t *bt;
	bptree_entry_phys_t bte = { 0 };

	/*
	 * bptree objects are in the pool mos, therefore they can only be
	 * modified in syncing context. Furthermore, this is only modified
	 * by the sync thread, so no locking is necessary.
	 */
	ASSERT(dmu_tx_is_syncing(tx));

	VERIFY3U(0, ==, dmu_bonus_hold(os, obj, FTAG, &db));
	bt = db->db_data;

	bte.be_birth_txg = birth_txg;
	bte.be_bp = *bp;
	dmu_write(os, obj, bt->bt_end * sizeof (bte), sizeof (bte), &bte, tx);

	dmu_buf_will_dirty(db, tx);
	bt->bt_end++;
	bt->bt_bytes += bytes;
	bt->bt_comp += comp;
	bt->bt_uncomp += uncomp;
	dmu_buf_rele(db, FTAG);
}

/* ARGSUSED */
static int
bptree_visit_cb(spa_t *spa, zilog_t *zilog, const blkptr_t *bp,
    const zbookmark_phys_t *zb, const dnode_phys_t *dnp, void *arg)
{
	int err;
	struct bptree_args *ba = arg;

	if (bp == NULL || BP_IS_HOLE(bp))
		return (0);

	err = ba->ba_func(ba->ba_arg, bp, ba->ba_tx);
	if (err == 0 && ba->ba_free) {
		ba->ba_phys->bt_bytes -= bp_get_dsize_sync(spa, bp);
		ba->ba_phys->bt_comp -= BP_GET_PSIZE(bp);
		ba->ba_phys->bt_uncomp -= BP_GET_UCSIZE(bp);
	}
	return (err);
}

/*
 * If "free" is set:
 *  - It is assumed that "func" will be freeing the block pointers.
 *  - If "func" returns nonzero, the bookmark will be remembered and
 *    iteration will be restarted from this point on next invocation.
 *  - If an i/o error is encountered (e.g. "func" returns EIO or ECKSUM),
 *    bptree_iterate will remember the bookmark, continue traversing
 *    any additional entries, and return 0.
 *
 * If "free" is not set, traversal will stop and return an error if
 * an i/o error is encountered.
 *
 * In either case, if zfs_free_leak_on_eio is set, i/o errors will be
 * ignored and traversal will continue (i.e. TRAVERSE_HARD will be passed to
 * traverse_dataset_destroyed()).
 */
int
bptree_iterate(objset_t *os, uint64_t obj, boolean_t free, bptree_itor_t func,
    void *arg, dmu_tx_t *tx)
{
	boolean_t ioerr = B_FALSE;
	int err;
	uint64_t i;
	dmu_buf_t *db;
	struct bptree_args ba;

	ASSERT(!free || dmu_tx_is_syncing(tx));

	err = dmu_bonus_hold(os, obj, FTAG, &db);
	if (err != 0)
		return (err);

	if (free)
		dmu_buf_will_dirty(db, tx);

	ba.ba_phys = db->db_data;
	ba.ba_free = free;
	ba.ba_func = func;
	ba.ba_arg = arg;
	ba.ba_tx = tx;

	err = 0;
	for (i = ba.ba_phys->bt_begin; i < ba.ba_phys->bt_end; i++) {
		bptree_entry_phys_t bte;
		int flags = TRAVERSE_PREFETCH_METADATA | TRAVERSE_POST;

		err = dmu_read(os, obj, i * sizeof (bte), sizeof (bte),
		    &bte, DMU_READ_NO_PREFETCH);
		if (err != 0)
			break;

		if (zfs_free_leak_on_eio)
			flags |= TRAVERSE_HARD;
		zfs_dbgmsg("bptree index %lld: traversing from min_txg=%lld "
		    "bookmark %lld/%lld/%lld/%lld",
		    (longlong_t)i,
		    (longlong_t)bte.be_birth_txg,
		    (longlong_t)bte.be_zb.zb_objset,
		    (longlong_t)bte.be_zb.zb_object,
		    (longlong_t)bte.be_zb.zb_level,
		    (longlong_t)bte.be_zb.zb_blkid);
		err = traverse_dataset_destroyed(os->os_spa, &bte.be_bp,
		    bte.be_birth_txg, &bte.be_zb, flags,
		    bptree_visit_cb, &ba);
		if (free) {
			/*
			 * The callback has freed the visited block pointers.
			 * Record our traversal progress on disk, either by
			 * updating this record's bookmark, or by logically
			 * removing this record by advancing bt_begin.
			 */
			if (err != 0) {
				/* save bookmark for future resume */
				ASSERT3U(bte.be_zb.zb_objset, ==,
				    ZB_DESTROYED_OBJSET);
				ASSERT0(bte.be_zb.zb_level);
				dmu_write(os, obj, i * sizeof (bte),
				    sizeof (bte), &bte, tx);
				if (err == EIO || err == ECKSUM ||
				    err == ENXIO) {
					/*
					 * Skip the rest of this tree and
					 * continue on to the next entry.
					 */
					err = 0;
					ioerr = B_TRUE;
				} else {
					break;
				}
			} else if (ioerr) {
				/*
				 * This entry is finished, but there were
				 * i/o errors on previous entries, so we
				 * can't adjust bt_begin.  Set this entry's
				 * be_birth_txg such that it will be
				 * treated as a no-op in future traversals.
				 */
				bte.be_birth_txg = UINT64_MAX;
				dmu_write(os, obj, i * sizeof (bte),
				    sizeof (bte), &bte, tx);
			}

			if (!ioerr) {
				ba.ba_phys->bt_begin++;
				(void) dmu_free_range(os, obj,
				    i * sizeof (bte), sizeof (bte), tx);
			}
		} else if (err != 0) {
			break;
		}
	}

	ASSERT(!free || err != 0 || ioerr ||
	    ba.ba_phys->bt_begin == ba.ba_phys->bt_end);

	/* if all blocks are free there should be no used space */
	if (ba.ba_phys->bt_begin == ba.ba_phys->bt_end) {
		if (zfs_free_leak_on_eio) {
			ba.ba_phys->bt_bytes = 0;
			ba.ba_phys->bt_comp = 0;
			ba.ba_phys->bt_uncomp = 0;
		}

		ASSERT0(ba.ba_phys->bt_bytes);
		ASSERT0(ba.ba_phys->bt_comp);
		ASSERT0(ba.ba_phys->bt_uncomp);
	}

	dmu_buf_rele(db, FTAG);

	return (err);
}
