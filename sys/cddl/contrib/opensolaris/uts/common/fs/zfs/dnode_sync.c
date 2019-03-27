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
 * Copyright (c) 2012, 2018 by Delphix. All rights reserved.
 * Copyright (c) 2014 Spectra Logic Corporation, All rights reserved.
 */

#include <sys/zfs_context.h>
#include <sys/dbuf.h>
#include <sys/dnode.h>
#include <sys/dmu.h>
#include <sys/dmu_tx.h>
#include <sys/dmu_objset.h>
#include <sys/dsl_dataset.h>
#include <sys/spa.h>
#include <sys/range_tree.h>
#include <sys/zfeature.h>

static void
dnode_increase_indirection(dnode_t *dn, dmu_tx_t *tx)
{
	dmu_buf_impl_t *db;
	int txgoff = tx->tx_txg & TXG_MASK;
	int nblkptr = dn->dn_phys->dn_nblkptr;
	int old_toplvl = dn->dn_phys->dn_nlevels - 1;
	int new_level = dn->dn_next_nlevels[txgoff];
	int i;

	rw_enter(&dn->dn_struct_rwlock, RW_WRITER);

	/* this dnode can't be paged out because it's dirty */
	ASSERT(dn->dn_phys->dn_type != DMU_OT_NONE);
	ASSERT(RW_WRITE_HELD(&dn->dn_struct_rwlock));
	ASSERT(new_level > 1 && dn->dn_phys->dn_nlevels > 0);

	db = dbuf_hold_level(dn, dn->dn_phys->dn_nlevels, 0, FTAG);
	ASSERT(db != NULL);

	dn->dn_phys->dn_nlevels = new_level;
	dprintf("os=%p obj=%llu, increase to %d\n", dn->dn_objset,
	    dn->dn_object, dn->dn_phys->dn_nlevels);

	/* transfer dnode's block pointers to new indirect block */
	(void) dbuf_read(db, NULL, DB_RF_MUST_SUCCEED|DB_RF_HAVESTRUCT);
	ASSERT(db->db.db_data);
	ASSERT(arc_released(db->db_buf));
	ASSERT3U(sizeof (blkptr_t) * nblkptr, <=, db->db.db_size);
	bcopy(dn->dn_phys->dn_blkptr, db->db.db_data,
	    sizeof (blkptr_t) * nblkptr);
	arc_buf_freeze(db->db_buf);

	/* set dbuf's parent pointers to new indirect buf */
	for (i = 0; i < nblkptr; i++) {
		dmu_buf_impl_t *child =
		    dbuf_find(dn->dn_objset, dn->dn_object, old_toplvl, i);

		if (child == NULL)
			continue;
#ifdef	DEBUG
		DB_DNODE_ENTER(child);
		ASSERT3P(DB_DNODE(child), ==, dn);
		DB_DNODE_EXIT(child);
#endif	/* DEBUG */
		if (child->db_parent && child->db_parent != dn->dn_dbuf) {
			ASSERT(child->db_parent->db_level == db->db_level);
			ASSERT(child->db_blkptr !=
			    &dn->dn_phys->dn_blkptr[child->db_blkid]);
			mutex_exit(&child->db_mtx);
			continue;
		}
		ASSERT(child->db_parent == NULL ||
		    child->db_parent == dn->dn_dbuf);

		child->db_parent = db;
		dbuf_add_ref(db, child);
		if (db->db.db_data)
			child->db_blkptr = (blkptr_t *)db->db.db_data + i;
		else
			child->db_blkptr = NULL;
		dprintf_dbuf_bp(child, child->db_blkptr,
		    "changed db_blkptr to new indirect %s", "");

		mutex_exit(&child->db_mtx);
	}

	bzero(dn->dn_phys->dn_blkptr, sizeof (blkptr_t) * nblkptr);

	dbuf_rele(db, FTAG);

	rw_exit(&dn->dn_struct_rwlock);
}

static void
free_blocks(dnode_t *dn, blkptr_t *bp, int num, dmu_tx_t *tx)
{
	dsl_dataset_t *ds = dn->dn_objset->os_dsl_dataset;
	uint64_t bytesfreed = 0;

	dprintf("ds=%p obj=%llx num=%d\n", ds, dn->dn_object, num);

	for (int i = 0; i < num; i++, bp++) {
		if (BP_IS_HOLE(bp))
			continue;

		bytesfreed += dsl_dataset_block_kill(ds, bp, tx, B_FALSE);
		ASSERT3U(bytesfreed, <=, DN_USED_BYTES(dn->dn_phys));

		/*
		 * Save some useful information on the holes being
		 * punched, including logical size, type, and indirection
		 * level. Retaining birth time enables detection of when
		 * holes are punched for reducing the number of free
		 * records transmitted during a zfs send.
		 */

		uint64_t lsize = BP_GET_LSIZE(bp);
		dmu_object_type_t type = BP_GET_TYPE(bp);
		uint64_t lvl = BP_GET_LEVEL(bp);

		bzero(bp, sizeof (blkptr_t));

		if (spa_feature_is_active(dn->dn_objset->os_spa,
		    SPA_FEATURE_HOLE_BIRTH)) {
			BP_SET_LSIZE(bp, lsize);
			BP_SET_TYPE(bp, type);
			BP_SET_LEVEL(bp, lvl);
			BP_SET_BIRTH(bp, dmu_tx_get_txg(tx), 0);
		}
	}
	dnode_diduse_space(dn, -bytesfreed);
}

#ifdef ZFS_DEBUG
static void
free_verify(dmu_buf_impl_t *db, uint64_t start, uint64_t end, dmu_tx_t *tx)
{
	int off, num;
	int i, err, epbs;
	uint64_t txg = tx->tx_txg;
	dnode_t *dn;

	DB_DNODE_ENTER(db);
	dn = DB_DNODE(db);
	epbs = dn->dn_phys->dn_indblkshift - SPA_BLKPTRSHIFT;
	off = start - (db->db_blkid * 1<<epbs);
	num = end - start + 1;

	ASSERT3U(off, >=, 0);
	ASSERT3U(num, >=, 0);
	ASSERT3U(db->db_level, >, 0);
	ASSERT3U(db->db.db_size, ==, 1 << dn->dn_phys->dn_indblkshift);
	ASSERT3U(off+num, <=, db->db.db_size >> SPA_BLKPTRSHIFT);
	ASSERT(db->db_blkptr != NULL);

	for (i = off; i < off+num; i++) {
		uint64_t *buf;
		dmu_buf_impl_t *child;
		dbuf_dirty_record_t *dr;
		int j;

		ASSERT(db->db_level == 1);

		rw_enter(&dn->dn_struct_rwlock, RW_READER);
		err = dbuf_hold_impl(dn, db->db_level-1,
		    (db->db_blkid << epbs) + i, TRUE, FALSE, FTAG, &child);
		rw_exit(&dn->dn_struct_rwlock);
		if (err == ENOENT)
			continue;
		ASSERT(err == 0);
		ASSERT(child->db_level == 0);
		dr = child->db_last_dirty;
		while (dr && dr->dr_txg > txg)
			dr = dr->dr_next;
		ASSERT(dr == NULL || dr->dr_txg == txg);

		/* data_old better be zeroed */
		if (dr) {
			buf = dr->dt.dl.dr_data->b_data;
			for (j = 0; j < child->db.db_size >> 3; j++) {
				if (buf[j] != 0) {
					panic("freed data not zero: "
					    "child=%p i=%d off=%d num=%d\n",
					    (void *)child, i, off, num);
				}
			}
		}

		/*
		 * db_data better be zeroed unless it's dirty in a
		 * future txg.
		 */
		mutex_enter(&child->db_mtx);
		buf = child->db.db_data;
		if (buf != NULL && child->db_state != DB_FILL &&
		    child->db_last_dirty == NULL) {
			for (j = 0; j < child->db.db_size >> 3; j++) {
				if (buf[j] != 0) {
					panic("freed data not zero: "
					    "child=%p i=%d off=%d num=%d\n",
					    (void *)child, i, off, num);
				}
			}
		}
		mutex_exit(&child->db_mtx);

		dbuf_rele(child, FTAG);
	}
	DB_DNODE_EXIT(db);
}
#endif

/*
 * We don't usually free the indirect blocks here.  If in one txg we have a
 * free_range and a write to the same indirect block, it's important that we
 * preserve the hole's birth times. Therefore, we don't free any any indirect
 * blocks in free_children().  If an indirect block happens to turn into all
 * holes, it will be freed by dbuf_write_children_ready, which happens at a
 * point in the syncing process where we know for certain the contents of the
 * indirect block.
 *
 * However, if we're freeing a dnode, its space accounting must go to zero
 * before we actually try to free the dnode, or we will trip an assertion. In
 * addition, we know the case described above cannot occur, because the dnode is
 * being freed.  Therefore, we free the indirect blocks immediately in that
 * case.
 */
static void
free_children(dmu_buf_impl_t *db, uint64_t blkid, uint64_t nblks,
    boolean_t free_indirects, dmu_tx_t *tx)
{
	dnode_t *dn;
	blkptr_t *bp;
	dmu_buf_impl_t *subdb;
	uint64_t start, end, dbstart, dbend;
	unsigned int epbs, shift, i;

	/*
	 * There is a small possibility that this block will not be cached:
	 *   1 - if level > 1 and there are no children with level <= 1
	 *   2 - if this block was evicted since we read it from
	 *	 dmu_tx_hold_free().
	 */
	if (db->db_state != DB_CACHED)
		(void) dbuf_read(db, NULL, DB_RF_MUST_SUCCEED);

	/*
	 * If we modify this indirect block, and we are not freeing the
	 * dnode (!free_indirects), then this indirect block needs to get
	 * written to disk by dbuf_write().  If it is dirty, we know it will
	 * be written (otherwise, we would have incorrect on-disk state
	 * because the space would be freed but still referenced by the BP
	 * in this indirect block).  Therefore we VERIFY that it is
	 * dirty.
	 *
	 * Our VERIFY covers some cases that do not actually have to be
	 * dirty, but the open-context code happens to dirty.  E.g. if the
	 * blocks we are freeing are all holes, because in that case, we
	 * are only freeing part of this indirect block, so it is an
	 * ancestor of the first or last block to be freed.  The first and
	 * last L1 indirect blocks are always dirtied by dnode_free_range().
	 */
	VERIFY(BP_GET_FILL(db->db_blkptr) == 0 || db->db_dirtycnt > 0);

	dbuf_release_bp(db);
	bp = db->db.db_data;

	DB_DNODE_ENTER(db);
	dn = DB_DNODE(db);
	epbs = dn->dn_phys->dn_indblkshift - SPA_BLKPTRSHIFT;
	ASSERT3U(epbs, <, 31);
	shift = (db->db_level - 1) * epbs;
	dbstart = db->db_blkid << epbs;
	start = blkid >> shift;
	if (dbstart < start) {
		bp += start - dbstart;
	} else {
		start = dbstart;
	}
	dbend = ((db->db_blkid + 1) << epbs) - 1;
	end = (blkid + nblks - 1) >> shift;
	if (dbend <= end)
		end = dbend;

	ASSERT3U(start, <=, end);

	if (db->db_level == 1) {
		FREE_VERIFY(db, start, end, tx);
		free_blocks(dn, bp, end-start+1, tx);
	} else {
		for (uint64_t id = start; id <= end; id++, bp++) {
			if (BP_IS_HOLE(bp))
				continue;
			rw_enter(&dn->dn_struct_rwlock, RW_READER);
			VERIFY0(dbuf_hold_impl(dn, db->db_level - 1,
			    id, TRUE, FALSE, FTAG, &subdb));
			rw_exit(&dn->dn_struct_rwlock);
			ASSERT3P(bp, ==, subdb->db_blkptr);

			free_children(subdb, blkid, nblks, free_indirects, tx);
			dbuf_rele(subdb, FTAG);
		}
	}

	if (free_indirects) {
		for (i = 0, bp = db->db.db_data; i < 1 << epbs; i++, bp++)
			ASSERT(BP_IS_HOLE(bp));
		bzero(db->db.db_data, db->db.db_size);
		free_blocks(dn, db->db_blkptr, 1, tx);
	}

	DB_DNODE_EXIT(db);
	arc_buf_freeze(db->db_buf);
}

/*
 * Traverse the indicated range of the provided file
 * and "free" all the blocks contained there.
 */
static void
dnode_sync_free_range_impl(dnode_t *dn, uint64_t blkid, uint64_t nblks,
    boolean_t free_indirects, dmu_tx_t *tx)
{
	blkptr_t *bp = dn->dn_phys->dn_blkptr;
	int dnlevel = dn->dn_phys->dn_nlevels;
	boolean_t trunc = B_FALSE;

	if (blkid > dn->dn_phys->dn_maxblkid)
		return;

	ASSERT(dn->dn_phys->dn_maxblkid < UINT64_MAX);
	if (blkid + nblks > dn->dn_phys->dn_maxblkid) {
		nblks = dn->dn_phys->dn_maxblkid - blkid + 1;
		trunc = B_TRUE;
	}

	/* There are no indirect blocks in the object */
	if (dnlevel == 1) {
		if (blkid >= dn->dn_phys->dn_nblkptr) {
			/* this range was never made persistent */
			return;
		}
		ASSERT3U(blkid + nblks, <=, dn->dn_phys->dn_nblkptr);
		free_blocks(dn, bp + blkid, nblks, tx);
	} else {
		int shift = (dnlevel - 1) *
		    (dn->dn_phys->dn_indblkshift - SPA_BLKPTRSHIFT);
		int start = blkid >> shift;
		int end = (blkid + nblks - 1) >> shift;
		dmu_buf_impl_t *db;

		ASSERT(start < dn->dn_phys->dn_nblkptr);
		bp += start;
		for (int i = start; i <= end; i++, bp++) {
			if (BP_IS_HOLE(bp))
				continue;
			rw_enter(&dn->dn_struct_rwlock, RW_READER);
			VERIFY0(dbuf_hold_impl(dn, dnlevel - 1, i,
			    TRUE, FALSE, FTAG, &db));
			rw_exit(&dn->dn_struct_rwlock);

			free_children(db, blkid, nblks, free_indirects, tx);
			dbuf_rele(db, FTAG);
		}
	}

	if (trunc) {
		dn->dn_phys->dn_maxblkid = blkid == 0 ? 0 : blkid - 1;

		uint64_t off = (dn->dn_phys->dn_maxblkid + 1) *
		    (dn->dn_phys->dn_datablkszsec << SPA_MINBLOCKSHIFT);
		ASSERT(off < dn->dn_phys->dn_maxblkid ||
		    dn->dn_phys->dn_maxblkid == 0 ||
		    dnode_next_offset(dn, 0, &off, 1, 1, 0) != 0);
	}
}

typedef struct dnode_sync_free_range_arg {
	dnode_t *dsfra_dnode;
	dmu_tx_t *dsfra_tx;
	boolean_t dsfra_free_indirects;
} dnode_sync_free_range_arg_t;

static void
dnode_sync_free_range(void *arg, uint64_t blkid, uint64_t nblks)
{
	dnode_sync_free_range_arg_t *dsfra = arg;
	dnode_t *dn = dsfra->dsfra_dnode;

	mutex_exit(&dn->dn_mtx);
	dnode_sync_free_range_impl(dn, blkid, nblks,
	    dsfra->dsfra_free_indirects, dsfra->dsfra_tx);
	mutex_enter(&dn->dn_mtx);
}

/*
 * Try to kick all the dnode's dbufs out of the cache...
 */
void
dnode_evict_dbufs(dnode_t *dn)
{
	dmu_buf_impl_t db_marker;
	dmu_buf_impl_t *db, *db_next;

	mutex_enter(&dn->dn_dbufs_mtx);
	for (db = avl_first(&dn->dn_dbufs); db != NULL; db = db_next) {

#ifdef	DEBUG
		DB_DNODE_ENTER(db);
		ASSERT3P(DB_DNODE(db), ==, dn);
		DB_DNODE_EXIT(db);
#endif	/* DEBUG */

		mutex_enter(&db->db_mtx);
		if (db->db_state != DB_EVICTING &&
		    refcount_is_zero(&db->db_holds)) {
			db_marker.db_level = db->db_level;
			db_marker.db_blkid = db->db_blkid;
			db_marker.db_state = DB_SEARCH;
			avl_insert_here(&dn->dn_dbufs, &db_marker, db,
			    AVL_BEFORE);

			/*
			 * We need to use the "marker" dbuf rather than
			 * simply getting the next dbuf, because
			 * dbuf_destroy() may actually remove multiple dbufs.
			 * It can call itself recursively on the parent dbuf,
			 * which may also be removed from dn_dbufs.  The code
			 * flow would look like:
			 *
			 * dbuf_destroy():
			 *   dnode_rele_and_unlock(parent_dbuf, evicting=TRUE):
			 *	if (!cacheable || pending_evict)
			 *	  dbuf_destroy()
			 */
			dbuf_destroy(db);

			db_next = AVL_NEXT(&dn->dn_dbufs, &db_marker);
			avl_remove(&dn->dn_dbufs, &db_marker);
		} else {
			db->db_pending_evict = TRUE;
			mutex_exit(&db->db_mtx);
			db_next = AVL_NEXT(&dn->dn_dbufs, db);
		}
	}
	mutex_exit(&dn->dn_dbufs_mtx);

	dnode_evict_bonus(dn);
}

void
dnode_evict_bonus(dnode_t *dn)
{
	rw_enter(&dn->dn_struct_rwlock, RW_WRITER);
	if (dn->dn_bonus != NULL) {
		if (refcount_is_zero(&dn->dn_bonus->db_holds)) {
			mutex_enter(&dn->dn_bonus->db_mtx);
			dbuf_destroy(dn->dn_bonus);
			dn->dn_bonus = NULL;
		} else {
			dn->dn_bonus->db_pending_evict = TRUE;
		}
	}
	rw_exit(&dn->dn_struct_rwlock);
}

static void
dnode_undirty_dbufs(list_t *list)
{
	dbuf_dirty_record_t *dr;

	while (dr = list_head(list)) {
		dmu_buf_impl_t *db = dr->dr_dbuf;
		uint64_t txg = dr->dr_txg;

		if (db->db_level != 0)
			dnode_undirty_dbufs(&dr->dt.di.dr_children);

		mutex_enter(&db->db_mtx);
		/* XXX - use dbuf_undirty()? */
		list_remove(list, dr);
		ASSERT(db->db_last_dirty == dr);
		db->db_last_dirty = NULL;
		db->db_dirtycnt -= 1;
		if (db->db_level == 0) {
			ASSERT(db->db_blkid == DMU_BONUS_BLKID ||
			    dr->dt.dl.dr_data == db->db_buf);
			dbuf_unoverride(dr);
		} else {
			mutex_destroy(&dr->dt.di.dr_mtx);
			list_destroy(&dr->dt.di.dr_children);
		}
		kmem_free(dr, sizeof (dbuf_dirty_record_t));
		dbuf_rele_and_unlock(db, (void *)(uintptr_t)txg, B_FALSE);
	}
}

static void
dnode_sync_free(dnode_t *dn, dmu_tx_t *tx)
{
	int txgoff = tx->tx_txg & TXG_MASK;

	ASSERT(dmu_tx_is_syncing(tx));

	/*
	 * Our contents should have been freed in dnode_sync() by the
	 * free range record inserted by the caller of dnode_free().
	 */
	ASSERT0(DN_USED_BYTES(dn->dn_phys));
	ASSERT(BP_IS_HOLE(dn->dn_phys->dn_blkptr));

	dnode_undirty_dbufs(&dn->dn_dirty_records[txgoff]);
	dnode_evict_dbufs(dn);

	/*
	 * XXX - It would be nice to assert this, but we may still
	 * have residual holds from async evictions from the arc...
	 *
	 * zfs_obj_to_path() also depends on this being
	 * commented out.
	 *
	 * ASSERT3U(refcount_count(&dn->dn_holds), ==, 1);
	 */

	/* Undirty next bits */
	dn->dn_next_nlevels[txgoff] = 0;
	dn->dn_next_indblkshift[txgoff] = 0;
	dn->dn_next_blksz[txgoff] = 0;

	/* ASSERT(blkptrs are zero); */
	ASSERT(dn->dn_phys->dn_type != DMU_OT_NONE);
	ASSERT(dn->dn_type != DMU_OT_NONE);

	ASSERT(dn->dn_free_txg > 0);
	if (dn->dn_allocated_txg != dn->dn_free_txg)
		dmu_buf_will_dirty(&dn->dn_dbuf->db, tx);
	bzero(dn->dn_phys, sizeof (dnode_phys_t) * dn->dn_num_slots);

	mutex_enter(&dn->dn_mtx);
	dn->dn_type = DMU_OT_NONE;
	dn->dn_maxblkid = 0;
	dn->dn_allocated_txg = 0;
	dn->dn_free_txg = 0;
	dn->dn_have_spill = B_FALSE;
	mutex_exit(&dn->dn_mtx);

	ASSERT(dn->dn_object != DMU_META_DNODE_OBJECT);

	dnode_rele(dn, (void *)(uintptr_t)tx->tx_txg);
	/*
	 * Now that we've released our hold, the dnode may
	 * be evicted, so we musn't access it.
	 */
}

/*
 * Write out the dnode's dirty buffers.
 */
void
dnode_sync(dnode_t *dn, dmu_tx_t *tx)
{
	dnode_phys_t *dnp = dn->dn_phys;
	int txgoff = tx->tx_txg & TXG_MASK;
	list_t *list = &dn->dn_dirty_records[txgoff];
	static const dnode_phys_t zerodn = { 0 };
	boolean_t kill_spill = B_FALSE;

	ASSERT(dmu_tx_is_syncing(tx));
	ASSERT(dnp->dn_type != DMU_OT_NONE || dn->dn_allocated_txg);
	ASSERT(dnp->dn_type != DMU_OT_NONE ||
	    bcmp(dnp, &zerodn, DNODE_MIN_SIZE) == 0);
	DNODE_VERIFY(dn);

	ASSERT(dn->dn_dbuf == NULL || arc_released(dn->dn_dbuf->db_buf));

	if (dmu_objset_userused_enabled(dn->dn_objset) &&
	    !DMU_OBJECT_IS_SPECIAL(dn->dn_object)) {
		mutex_enter(&dn->dn_mtx);
		dn->dn_oldused = DN_USED_BYTES(dn->dn_phys);
		dn->dn_oldflags = dn->dn_phys->dn_flags;
		dn->dn_phys->dn_flags |= DNODE_FLAG_USERUSED_ACCOUNTED;
		mutex_exit(&dn->dn_mtx);
		dmu_objset_userquota_get_ids(dn, B_FALSE, tx);
	} else {
		/* Once we account for it, we should always account for it. */
		ASSERT(!(dn->dn_phys->dn_flags &
		    DNODE_FLAG_USERUSED_ACCOUNTED));
	}

	mutex_enter(&dn->dn_mtx);
	if (dn->dn_allocated_txg == tx->tx_txg) {
		/* The dnode is newly allocated or reallocated */
		if (dnp->dn_type == DMU_OT_NONE) {
			/* this is a first alloc, not a realloc */
			dnp->dn_nlevels = 1;
			dnp->dn_nblkptr = dn->dn_nblkptr;
		}

		dnp->dn_type = dn->dn_type;
		dnp->dn_bonustype = dn->dn_bonustype;
		dnp->dn_bonuslen = dn->dn_bonuslen;
	}

	dnp->dn_extra_slots = dn->dn_num_slots - 1;

	ASSERT(dnp->dn_nlevels > 1 ||
	    BP_IS_HOLE(&dnp->dn_blkptr[0]) ||
	    BP_IS_EMBEDDED(&dnp->dn_blkptr[0]) ||
	    BP_GET_LSIZE(&dnp->dn_blkptr[0]) ==
	    dnp->dn_datablkszsec << SPA_MINBLOCKSHIFT);
	ASSERT(dnp->dn_nlevels < 2 ||
	    BP_IS_HOLE(&dnp->dn_blkptr[0]) ||
	    BP_GET_LSIZE(&dnp->dn_blkptr[0]) == 1 << dnp->dn_indblkshift);

	if (dn->dn_next_type[txgoff] != 0) {
		dnp->dn_type = dn->dn_type;
		dn->dn_next_type[txgoff] = 0;
	}

	if (dn->dn_next_blksz[txgoff] != 0) {
		ASSERT(P2PHASE(dn->dn_next_blksz[txgoff],
		    SPA_MINBLOCKSIZE) == 0);
		ASSERT(BP_IS_HOLE(&dnp->dn_blkptr[0]) ||
		    dn->dn_maxblkid == 0 || list_head(list) != NULL ||
		    dn->dn_next_blksz[txgoff] >> SPA_MINBLOCKSHIFT ==
		    dnp->dn_datablkszsec ||
		    !range_tree_is_empty(dn->dn_free_ranges[txgoff]));
		dnp->dn_datablkszsec =
		    dn->dn_next_blksz[txgoff] >> SPA_MINBLOCKSHIFT;
		dn->dn_next_blksz[txgoff] = 0;
	}

	if (dn->dn_next_bonuslen[txgoff] != 0) {
		if (dn->dn_next_bonuslen[txgoff] == DN_ZERO_BONUSLEN)
			dnp->dn_bonuslen = 0;
		else
			dnp->dn_bonuslen = dn->dn_next_bonuslen[txgoff];
		ASSERT(dnp->dn_bonuslen <=
		    DN_SLOTS_TO_BONUSLEN(dnp->dn_extra_slots + 1));
		dn->dn_next_bonuslen[txgoff] = 0;
	}

	if (dn->dn_next_bonustype[txgoff] != 0) {
		ASSERT(DMU_OT_IS_VALID(dn->dn_next_bonustype[txgoff]));
		dnp->dn_bonustype = dn->dn_next_bonustype[txgoff];
		dn->dn_next_bonustype[txgoff] = 0;
	}

	boolean_t freeing_dnode = dn->dn_free_txg > 0 &&
	    dn->dn_free_txg <= tx->tx_txg;

	/*
	 * Remove the spill block if we have been explicitly asked to
	 * remove it, or if the object is being removed.
	 */
	if (dn->dn_rm_spillblk[txgoff] || freeing_dnode) {
		if (dnp->dn_flags & DNODE_FLAG_SPILL_BLKPTR)
			kill_spill = B_TRUE;
		dn->dn_rm_spillblk[txgoff] = 0;
	}

	if (dn->dn_next_indblkshift[txgoff] != 0) {
		ASSERT(dnp->dn_nlevels == 1);
		dnp->dn_indblkshift = dn->dn_next_indblkshift[txgoff];
		dn->dn_next_indblkshift[txgoff] = 0;
	}

	/*
	 * Just take the live (open-context) values for checksum and compress.
	 * Strictly speaking it's a future leak, but nothing bad happens if we
	 * start using the new checksum or compress algorithm a little early.
	 */
	dnp->dn_checksum = dn->dn_checksum;
	dnp->dn_compress = dn->dn_compress;

	mutex_exit(&dn->dn_mtx);

	if (kill_spill) {
		free_blocks(dn, DN_SPILL_BLKPTR(dn->dn_phys), 1, tx);
		mutex_enter(&dn->dn_mtx);
		dnp->dn_flags &= ~DNODE_FLAG_SPILL_BLKPTR;
		mutex_exit(&dn->dn_mtx);
	}

	/* process all the "freed" ranges in the file */
	if (dn->dn_free_ranges[txgoff] != NULL) {
		dnode_sync_free_range_arg_t dsfra;
		dsfra.dsfra_dnode = dn;
		dsfra.dsfra_tx = tx;
		dsfra.dsfra_free_indirects = freeing_dnode;
		if (freeing_dnode) {
			ASSERT(range_tree_contains(dn->dn_free_ranges[txgoff],
			    0, dn->dn_maxblkid + 1));
		}
		mutex_enter(&dn->dn_mtx);
		range_tree_vacate(dn->dn_free_ranges[txgoff],
		    dnode_sync_free_range, &dsfra);
		range_tree_destroy(dn->dn_free_ranges[txgoff]);
		dn->dn_free_ranges[txgoff] = NULL;
		mutex_exit(&dn->dn_mtx);
	}

	if (freeing_dnode) {
		dn->dn_objset->os_freed_dnodes++;
		dnode_sync_free(dn, tx);
		return;
	}

	if (dn->dn_num_slots > DNODE_MIN_SLOTS) {
		dsl_dataset_t *ds = dn->dn_objset->os_dsl_dataset;
		mutex_enter(&ds->ds_lock);
		ds->ds_feature_activation_needed[SPA_FEATURE_LARGE_DNODE] =
		    B_TRUE;
		mutex_exit(&ds->ds_lock);
	}

	if (dn->dn_next_nlevels[txgoff]) {
		dnode_increase_indirection(dn, tx);
		dn->dn_next_nlevels[txgoff] = 0;
	}

	if (dn->dn_next_nblkptr[txgoff]) {
		/* this should only happen on a realloc */
		ASSERT(dn->dn_allocated_txg == tx->tx_txg);
		if (dn->dn_next_nblkptr[txgoff] > dnp->dn_nblkptr) {
			/* zero the new blkptrs we are gaining */
			bzero(dnp->dn_blkptr + dnp->dn_nblkptr,
			    sizeof (blkptr_t) *
			    (dn->dn_next_nblkptr[txgoff] - dnp->dn_nblkptr));
#ifdef ZFS_DEBUG
		} else {
			int i;
			ASSERT(dn->dn_next_nblkptr[txgoff] < dnp->dn_nblkptr);
			/* the blkptrs we are losing better be unallocated */
			for (i = dn->dn_next_nblkptr[txgoff];
			    i < dnp->dn_nblkptr; i++)
				ASSERT(BP_IS_HOLE(&dnp->dn_blkptr[i]));
#endif
		}
		mutex_enter(&dn->dn_mtx);
		dnp->dn_nblkptr = dn->dn_next_nblkptr[txgoff];
		dn->dn_next_nblkptr[txgoff] = 0;
		mutex_exit(&dn->dn_mtx);
	}

	dbuf_sync_list(list, dn->dn_phys->dn_nlevels - 1, tx);

	if (!DMU_OBJECT_IS_SPECIAL(dn->dn_object)) {
		ASSERT3P(list_head(list), ==, NULL);
		dnode_rele(dn, (void *)(uintptr_t)tx->tx_txg);
	}

	/*
	 * Although we have dropped our reference to the dnode, it
	 * can't be evicted until its written, and we haven't yet
	 * initiated the IO for the dnode's dbuf.
	 */
}
