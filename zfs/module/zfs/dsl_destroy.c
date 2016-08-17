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
 * Copyright (c) 2012, 2014 by Delphix. All rights reserved.
 * Copyright (c) 2013 Steven Hartland. All rights reserved.
 * Copyright (c) 2013 by Joyent, Inc. All rights reserved.
 * Copyright (c) 2016 Actifio, Inc. All rights reserved.
 */

#include <sys/zfs_context.h>
#include <sys/dsl_userhold.h>
#include <sys/dsl_dataset.h>
#include <sys/dsl_synctask.h>
#include <sys/dmu_tx.h>
#include <sys/dsl_pool.h>
#include <sys/dsl_dir.h>
#include <sys/dmu_traverse.h>
#include <sys/dsl_scan.h>
#include <sys/dmu_objset.h>
#include <sys/zap.h>
#include <sys/zfeature.h>
#include <sys/zfs_ioctl.h>
#include <sys/dsl_deleg.h>
#include <sys/dmu_impl.h>
#include <sys/zvol.h>

typedef struct dmu_snapshots_destroy_arg {
	nvlist_t *dsda_snaps;
	nvlist_t *dsda_successful_snaps;
	boolean_t dsda_defer;
	nvlist_t *dsda_errlist;
} dmu_snapshots_destroy_arg_t;

int
dsl_destroy_snapshot_check_impl(dsl_dataset_t *ds, boolean_t defer)
{
	if (!ds->ds_is_snapshot)
		return (SET_ERROR(EINVAL));

	if (dsl_dataset_long_held(ds))
		return (SET_ERROR(EBUSY));

	/*
	 * Only allow deferred destroy on pools that support it.
	 * NOTE: deferred destroy is only supported on snapshots.
	 */
	if (defer) {
		if (spa_version(ds->ds_dir->dd_pool->dp_spa) <
		    SPA_VERSION_USERREFS)
			return (SET_ERROR(ENOTSUP));
		return (0);
	}

	/*
	 * If this snapshot has an elevated user reference count,
	 * we can't destroy it yet.
	 */
	if (ds->ds_userrefs > 0)
		return (SET_ERROR(EBUSY));

	/*
	 * Can't delete a branch point.
	 */
	if (dsl_dataset_phys(ds)->ds_num_children > 1)
		return (SET_ERROR(EEXIST));

	return (0);
}

static int
dsl_destroy_snapshot_check(void *arg, dmu_tx_t *tx)
{
	dmu_snapshots_destroy_arg_t *dsda = arg;
	dsl_pool_t *dp = dmu_tx_pool(tx);
	nvpair_t *pair;
	int error = 0;

	if (!dmu_tx_is_syncing(tx))
		return (0);

	for (pair = nvlist_next_nvpair(dsda->dsda_snaps, NULL);
	    pair != NULL; pair = nvlist_next_nvpair(dsda->dsda_snaps, pair)) {
		dsl_dataset_t *ds;

		error = dsl_dataset_hold(dp, nvpair_name(pair),
		    FTAG, &ds);

		/*
		 * If the snapshot does not exist, silently ignore it
		 * (it's "already destroyed").
		 */
		if (error == ENOENT)
			continue;

		if (error == 0) {
			error = dsl_destroy_snapshot_check_impl(ds,
			    dsda->dsda_defer);
			dsl_dataset_rele(ds, FTAG);
		}

		if (error == 0) {
			fnvlist_add_boolean(dsda->dsda_successful_snaps,
			    nvpair_name(pair));
		} else {
			fnvlist_add_int32(dsda->dsda_errlist,
			    nvpair_name(pair), error);
		}
	}

	pair = nvlist_next_nvpair(dsda->dsda_errlist, NULL);
	if (pair != NULL)
		return (fnvpair_value_int32(pair));

	return (0);
}

struct process_old_arg {
	dsl_dataset_t *ds;
	dsl_dataset_t *ds_prev;
	boolean_t after_branch_point;
	zio_t *pio;
	uint64_t used, comp, uncomp;
};

static int
process_old_cb(void *arg, const blkptr_t *bp, dmu_tx_t *tx)
{
	struct process_old_arg *poa = arg;
	dsl_pool_t *dp = poa->ds->ds_dir->dd_pool;

	ASSERT(!BP_IS_HOLE(bp));

	if (bp->blk_birth <= dsl_dataset_phys(poa->ds)->ds_prev_snap_txg) {
		dsl_deadlist_insert(&poa->ds->ds_deadlist, bp, tx);
		if (poa->ds_prev && !poa->after_branch_point &&
		    bp->blk_birth >
		    dsl_dataset_phys(poa->ds_prev)->ds_prev_snap_txg) {
			dsl_dataset_phys(poa->ds_prev)->ds_unique_bytes +=
			    bp_get_dsize_sync(dp->dp_spa, bp);
		}
	} else {
		poa->used += bp_get_dsize_sync(dp->dp_spa, bp);
		poa->comp += BP_GET_PSIZE(bp);
		poa->uncomp += BP_GET_UCSIZE(bp);
		dsl_free_sync(poa->pio, dp, tx->tx_txg, bp);
	}
	return (0);
}

static void
process_old_deadlist(dsl_dataset_t *ds, dsl_dataset_t *ds_prev,
    dsl_dataset_t *ds_next, boolean_t after_branch_point, dmu_tx_t *tx)
{
	struct process_old_arg poa = { 0 };
	dsl_pool_t *dp = ds->ds_dir->dd_pool;
	objset_t *mos = dp->dp_meta_objset;
	uint64_t deadlist_obj;

	ASSERT(ds->ds_deadlist.dl_oldfmt);
	ASSERT(ds_next->ds_deadlist.dl_oldfmt);

	poa.ds = ds;
	poa.ds_prev = ds_prev;
	poa.after_branch_point = after_branch_point;
	poa.pio = zio_root(dp->dp_spa, NULL, NULL, ZIO_FLAG_MUSTSUCCEED);
	VERIFY0(bpobj_iterate(&ds_next->ds_deadlist.dl_bpobj,
	    process_old_cb, &poa, tx));
	VERIFY0(zio_wait(poa.pio));
	ASSERT3U(poa.used, ==, dsl_dataset_phys(ds)->ds_unique_bytes);

	/* change snapused */
	dsl_dir_diduse_space(ds->ds_dir, DD_USED_SNAP,
	    -poa.used, -poa.comp, -poa.uncomp, tx);

	/* swap next's deadlist to our deadlist */
	dsl_deadlist_close(&ds->ds_deadlist);
	dsl_deadlist_close(&ds_next->ds_deadlist);
	deadlist_obj = dsl_dataset_phys(ds)->ds_deadlist_obj;
	dsl_dataset_phys(ds)->ds_deadlist_obj =
	    dsl_dataset_phys(ds_next)->ds_deadlist_obj;
	dsl_dataset_phys(ds_next)->ds_deadlist_obj = deadlist_obj;
	dsl_deadlist_open(&ds->ds_deadlist, mos,
	    dsl_dataset_phys(ds)->ds_deadlist_obj);
	dsl_deadlist_open(&ds_next->ds_deadlist, mos,
	    dsl_dataset_phys(ds_next)->ds_deadlist_obj);
}

static void
dsl_dataset_remove_clones_key(dsl_dataset_t *ds, uint64_t mintxg, dmu_tx_t *tx)
{
	objset_t *mos = ds->ds_dir->dd_pool->dp_meta_objset;
	zap_cursor_t *zc;
	zap_attribute_t *za;

	/*
	 * If it is the old version, dd_clones doesn't exist so we can't
	 * find the clones, but dsl_deadlist_remove_key() is a no-op so it
	 * doesn't matter.
	 */
	if (dsl_dir_phys(ds->ds_dir)->dd_clones == 0)
		return;

	zc = kmem_alloc(sizeof (zap_cursor_t), KM_SLEEP);
	za = kmem_alloc(sizeof (zap_attribute_t), KM_SLEEP);

	for (zap_cursor_init(zc, mos, dsl_dir_phys(ds->ds_dir)->dd_clones);
	    zap_cursor_retrieve(zc, za) == 0;
	    zap_cursor_advance(zc)) {
		dsl_dataset_t *clone;

		VERIFY0(dsl_dataset_hold_obj(ds->ds_dir->dd_pool,
		    za->za_first_integer, FTAG, &clone));
		if (clone->ds_dir->dd_origin_txg > mintxg) {
			dsl_deadlist_remove_key(&clone->ds_deadlist,
			    mintxg, tx);
			dsl_dataset_remove_clones_key(clone, mintxg, tx);
		}
		dsl_dataset_rele(clone, FTAG);
	}
	zap_cursor_fini(zc);

	kmem_free(za, sizeof (zap_attribute_t));
	kmem_free(zc, sizeof (zap_cursor_t));
}

void
dsl_destroy_snapshot_sync_impl(dsl_dataset_t *ds, boolean_t defer, dmu_tx_t *tx)
{
	int after_branch_point = FALSE;
	dsl_pool_t *dp = ds->ds_dir->dd_pool;
	objset_t *mos = dp->dp_meta_objset;
	dsl_dataset_t *ds_prev = NULL;
	uint64_t obj, old_unique, used = 0, comp = 0, uncomp = 0;
	dsl_dataset_t *ds_next, *ds_head, *hds;


	ASSERT(RRW_WRITE_HELD(&dp->dp_config_rwlock));
	ASSERT3U(dsl_dataset_phys(ds)->ds_bp.blk_birth, <=, tx->tx_txg);
	ASSERT(refcount_is_zero(&ds->ds_longholds));

	if (defer &&
	    (ds->ds_userrefs > 0 ||
	    dsl_dataset_phys(ds)->ds_num_children > 1)) {
		ASSERT(spa_version(dp->dp_spa) >= SPA_VERSION_USERREFS);
		dmu_buf_will_dirty(ds->ds_dbuf, tx);
		dsl_dataset_phys(ds)->ds_flags |= DS_FLAG_DEFER_DESTROY;
		spa_history_log_internal_ds(ds, "defer_destroy", tx, "");
		return;
	}

	ASSERT3U(dsl_dataset_phys(ds)->ds_num_children, <=, 1);

	/* We need to log before removing it from the namespace. */
	spa_history_log_internal_ds(ds, "destroy", tx, "");

	dsl_scan_ds_destroyed(ds, tx);

	obj = ds->ds_object;

	if (ds->ds_large_blocks) {
		ASSERT0(zap_contains(mos, obj, DS_FIELD_LARGE_BLOCKS));
		spa_feature_decr(dp->dp_spa, SPA_FEATURE_LARGE_BLOCKS, tx);
	}
	if (dsl_dataset_phys(ds)->ds_prev_snap_obj != 0) {
		ASSERT3P(ds->ds_prev, ==, NULL);
		VERIFY0(dsl_dataset_hold_obj(dp,
		    dsl_dataset_phys(ds)->ds_prev_snap_obj, FTAG, &ds_prev));
		after_branch_point =
		    (dsl_dataset_phys(ds_prev)->ds_next_snap_obj != obj);

		dmu_buf_will_dirty(ds_prev->ds_dbuf, tx);
		if (after_branch_point &&
		    dsl_dataset_phys(ds_prev)->ds_next_clones_obj != 0) {
			dsl_dataset_remove_from_next_clones(ds_prev, obj, tx);
			if (dsl_dataset_phys(ds)->ds_next_snap_obj != 0) {
				VERIFY0(zap_add_int(mos,
				    dsl_dataset_phys(ds_prev)->
				    ds_next_clones_obj,
				    dsl_dataset_phys(ds)->ds_next_snap_obj,
				    tx));
			}
		}
		if (!after_branch_point) {
			dsl_dataset_phys(ds_prev)->ds_next_snap_obj =
			    dsl_dataset_phys(ds)->ds_next_snap_obj;
		}
	}

	VERIFY0(dsl_dataset_hold_obj(dp,
	    dsl_dataset_phys(ds)->ds_next_snap_obj, FTAG, &ds_next));
	ASSERT3U(dsl_dataset_phys(ds_next)->ds_prev_snap_obj, ==, obj);

	old_unique = dsl_dataset_phys(ds_next)->ds_unique_bytes;

	dmu_buf_will_dirty(ds_next->ds_dbuf, tx);
	dsl_dataset_phys(ds_next)->ds_prev_snap_obj =
	    dsl_dataset_phys(ds)->ds_prev_snap_obj;
	dsl_dataset_phys(ds_next)->ds_prev_snap_txg =
	    dsl_dataset_phys(ds)->ds_prev_snap_txg;
	ASSERT3U(dsl_dataset_phys(ds)->ds_prev_snap_txg, ==,
	    ds_prev ? dsl_dataset_phys(ds_prev)->ds_creation_txg : 0);

	if (ds_next->ds_deadlist.dl_oldfmt) {
		process_old_deadlist(ds, ds_prev, ds_next,
		    after_branch_point, tx);
	} else {
		/* Adjust prev's unique space. */
		if (ds_prev && !after_branch_point) {
			dsl_deadlist_space_range(&ds_next->ds_deadlist,
			    dsl_dataset_phys(ds_prev)->ds_prev_snap_txg,
			    dsl_dataset_phys(ds)->ds_prev_snap_txg,
			    &used, &comp, &uncomp);
			dsl_dataset_phys(ds_prev)->ds_unique_bytes += used;
		}

		/* Adjust snapused. */
		dsl_deadlist_space_range(&ds_next->ds_deadlist,
		    dsl_dataset_phys(ds)->ds_prev_snap_txg, UINT64_MAX,
		    &used, &comp, &uncomp);
		dsl_dir_diduse_space(ds->ds_dir, DD_USED_SNAP,
		    -used, -comp, -uncomp, tx);

		/* Move blocks to be freed to pool's free list. */
		dsl_deadlist_move_bpobj(&ds_next->ds_deadlist,
		    &dp->dp_free_bpobj, dsl_dataset_phys(ds)->ds_prev_snap_txg,
		    tx);
		dsl_dir_diduse_space(tx->tx_pool->dp_free_dir,
		    DD_USED_HEAD, used, comp, uncomp, tx);

		/* Merge our deadlist into next's and free it. */
		dsl_deadlist_merge(&ds_next->ds_deadlist,
		    dsl_dataset_phys(ds)->ds_deadlist_obj, tx);
	}
	dsl_deadlist_close(&ds->ds_deadlist);
	dsl_deadlist_free(mos, dsl_dataset_phys(ds)->ds_deadlist_obj, tx);
	dmu_buf_will_dirty(ds->ds_dbuf, tx);
	dsl_dataset_phys(ds)->ds_deadlist_obj = 0;

	/* Collapse range in clone heads */
	dsl_dataset_remove_clones_key(ds,
	    dsl_dataset_phys(ds)->ds_creation_txg, tx);

	if (ds_next->ds_is_snapshot) {
		dsl_dataset_t *ds_nextnext;

		/*
		 * Update next's unique to include blocks which
		 * were previously shared by only this snapshot
		 * and it.  Those blocks will be born after the
		 * prev snap and before this snap, and will have
		 * died after the next snap and before the one
		 * after that (ie. be on the snap after next's
		 * deadlist).
		 */
		VERIFY0(dsl_dataset_hold_obj(dp,
		    dsl_dataset_phys(ds_next)->ds_next_snap_obj,
		    FTAG, &ds_nextnext));
		dsl_deadlist_space_range(&ds_nextnext->ds_deadlist,
		    dsl_dataset_phys(ds)->ds_prev_snap_txg,
		    dsl_dataset_phys(ds)->ds_creation_txg,
		    &used, &comp, &uncomp);
		dsl_dataset_phys(ds_next)->ds_unique_bytes += used;
		dsl_dataset_rele(ds_nextnext, FTAG);
		ASSERT3P(ds_next->ds_prev, ==, NULL);

		/* Collapse range in this head. */
		VERIFY0(dsl_dataset_hold_obj(dp,
		    dsl_dir_phys(ds->ds_dir)->dd_head_dataset_obj, FTAG, &hds));
		dsl_deadlist_remove_key(&hds->ds_deadlist,
		    dsl_dataset_phys(ds)->ds_creation_txg, tx);
		dsl_dataset_rele(hds, FTAG);

	} else {
		ASSERT3P(ds_next->ds_prev, ==, ds);
		dsl_dataset_rele(ds_next->ds_prev, ds_next);
		ds_next->ds_prev = NULL;
		if (ds_prev) {
			VERIFY0(dsl_dataset_hold_obj(dp,
			    dsl_dataset_phys(ds)->ds_prev_snap_obj,
			    ds_next, &ds_next->ds_prev));
		}

		dsl_dataset_recalc_head_uniq(ds_next);

		/*
		 * Reduce the amount of our unconsumed refreservation
		 * being charged to our parent by the amount of
		 * new unique data we have gained.
		 */
		if (old_unique < ds_next->ds_reserved) {
			int64_t mrsdelta;
			uint64_t new_unique =
			    dsl_dataset_phys(ds_next)->ds_unique_bytes;

			ASSERT(old_unique <= new_unique);
			mrsdelta = MIN(new_unique - old_unique,
			    ds_next->ds_reserved - old_unique);
			dsl_dir_diduse_space(ds->ds_dir,
			    DD_USED_REFRSRV, -mrsdelta, 0, 0, tx);
		}
	}
	dsl_dataset_rele(ds_next, FTAG);

	/*
	 * This must be done after the dsl_traverse(), because it will
	 * re-open the objset.
	 */
	if (ds->ds_objset) {
		dmu_objset_evict(ds->ds_objset);
		ds->ds_objset = NULL;
	}

	/* remove from snapshot namespace */
	ASSERT(dsl_dataset_phys(ds)->ds_snapnames_zapobj == 0);
	VERIFY0(dsl_dataset_hold_obj(dp,
	    dsl_dir_phys(ds->ds_dir)->dd_head_dataset_obj, FTAG, &ds_head));
	VERIFY0(dsl_dataset_get_snapname(ds));
#ifdef ZFS_DEBUG
	{
		uint64_t val;
		int err;

		err = dsl_dataset_snap_lookup(ds_head,
		    ds->ds_snapname, &val);
		ASSERT0(err);
		ASSERT3U(val, ==, obj);
	}
#endif
	VERIFY0(dsl_dataset_snap_remove(ds_head, ds->ds_snapname, tx, B_TRUE));
	dsl_dataset_rele(ds_head, FTAG);

	if (ds_prev != NULL)
		dsl_dataset_rele(ds_prev, FTAG);

	spa_prop_clear_bootfs(dp->dp_spa, ds->ds_object, tx);

	if (dsl_dataset_phys(ds)->ds_next_clones_obj != 0) {
		ASSERTV(uint64_t count);
		ASSERT0(zap_count(mos,
		    dsl_dataset_phys(ds)->ds_next_clones_obj, &count) &&
		    count == 0);
		VERIFY0(dmu_object_free(mos,
		    dsl_dataset_phys(ds)->ds_next_clones_obj, tx));
	}
	if (dsl_dataset_phys(ds)->ds_props_obj != 0)
		VERIFY0(zap_destroy(mos, dsl_dataset_phys(ds)->ds_props_obj,
		    tx));
	if (dsl_dataset_phys(ds)->ds_userrefs_obj != 0)
		VERIFY0(zap_destroy(mos, dsl_dataset_phys(ds)->ds_userrefs_obj,
		    tx));
	dsl_dir_rele(ds->ds_dir, ds);
	ds->ds_dir = NULL;
	dmu_object_free_zapified(mos, obj, tx);
}

static void
dsl_destroy_snapshot_sync(void *arg, dmu_tx_t *tx)
{
	dmu_snapshots_destroy_arg_t *dsda = arg;
	dsl_pool_t *dp = dmu_tx_pool(tx);
	nvpair_t *pair;

	for (pair = nvlist_next_nvpair(dsda->dsda_successful_snaps, NULL);
	    pair != NULL;
	    pair = nvlist_next_nvpair(dsda->dsda_successful_snaps, pair)) {
		dsl_dataset_t *ds;

		VERIFY0(dsl_dataset_hold(dp, nvpair_name(pair), FTAG, &ds));

		dsl_destroy_snapshot_sync_impl(ds, dsda->dsda_defer, tx);
		zvol_remove_minors(dp->dp_spa, nvpair_name(pair), B_TRUE);
		dsl_dataset_rele(ds, FTAG);
	}
}

/*
 * The semantics of this function are described in the comment above
 * lzc_destroy_snaps().  To summarize:
 *
 * The snapshots must all be in the same pool.
 *
 * Snapshots that don't exist will be silently ignored (considered to be
 * "already deleted").
 *
 * On success, all snaps will be destroyed and this will return 0.
 * On failure, no snaps will be destroyed, the errlist will be filled in,
 * and this will return an errno.
 */
int
dsl_destroy_snapshots_nvl(nvlist_t *snaps, boolean_t defer,
    nvlist_t *errlist)
{
	dmu_snapshots_destroy_arg_t dsda;
	int error;
	nvpair_t *pair;

	pair = nvlist_next_nvpair(snaps, NULL);
	if (pair == NULL)
		return (0);

	dsda.dsda_snaps = snaps;
	VERIFY0(nvlist_alloc(&dsda.dsda_successful_snaps,
	    NV_UNIQUE_NAME, KM_SLEEP));
	dsda.dsda_defer = defer;
	dsda.dsda_errlist = errlist;

	error = dsl_sync_task(nvpair_name(pair),
	    dsl_destroy_snapshot_check, dsl_destroy_snapshot_sync,
	    &dsda, 0, ZFS_SPACE_CHECK_NONE);
	fnvlist_free(dsda.dsda_successful_snaps);

	return (error);
}

int
dsl_destroy_snapshot(const char *name, boolean_t defer)
{
	int error;
	nvlist_t *nvl = fnvlist_alloc();
	nvlist_t *errlist = fnvlist_alloc();

	fnvlist_add_boolean(nvl, name);
	error = dsl_destroy_snapshots_nvl(nvl, defer, errlist);
	fnvlist_free(errlist);
	fnvlist_free(nvl);
	return (error);
}

struct killarg {
	dsl_dataset_t *ds;
	dmu_tx_t *tx;
};

/* ARGSUSED */
static int
kill_blkptr(spa_t *spa, zilog_t *zilog, const blkptr_t *bp,
    const zbookmark_phys_t *zb, const dnode_phys_t *dnp, void *arg)
{
	struct killarg *ka = arg;
	dmu_tx_t *tx = ka->tx;

	if (BP_IS_HOLE(bp) || BP_IS_EMBEDDED(bp))
		return (0);

	if (zb->zb_level == ZB_ZIL_LEVEL) {
		ASSERT(zilog != NULL);
		/*
		 * It's a block in the intent log.  It has no
		 * accounting, so just free it.
		 */
		dsl_free(ka->tx->tx_pool, ka->tx->tx_txg, bp);
	} else {
		ASSERT(zilog == NULL);
		ASSERT3U(bp->blk_birth, >,
		    dsl_dataset_phys(ka->ds)->ds_prev_snap_txg);
		(void) dsl_dataset_block_kill(ka->ds, bp, tx, B_FALSE);
	}

	return (0);
}

static void
old_synchronous_dataset_destroy(dsl_dataset_t *ds, dmu_tx_t *tx)
{
	struct killarg ka;

	/*
	 * Free everything that we point to (that's born after
	 * the previous snapshot, if we are a clone)
	 *
	 * NB: this should be very quick, because we already
	 * freed all the objects in open context.
	 */
	ka.ds = ds;
	ka.tx = tx;
	VERIFY0(traverse_dataset(ds,
	    dsl_dataset_phys(ds)->ds_prev_snap_txg, TRAVERSE_POST,
	    kill_blkptr, &ka));
	ASSERT(!DS_UNIQUE_IS_ACCURATE(ds) ||
	    dsl_dataset_phys(ds)->ds_unique_bytes == 0);
}

typedef struct dsl_destroy_head_arg {
	const char *ddha_name;
} dsl_destroy_head_arg_t;

int
dsl_destroy_head_check_impl(dsl_dataset_t *ds, int expected_holds)
{
	int error;
	uint64_t count;
	objset_t *mos;

	ASSERT(!ds->ds_is_snapshot);
	if (ds->ds_is_snapshot)
		return (SET_ERROR(EINVAL));

	if (refcount_count(&ds->ds_longholds) != expected_holds)
		return (SET_ERROR(EBUSY));

	mos = ds->ds_dir->dd_pool->dp_meta_objset;

	/*
	 * Can't delete a head dataset if there are snapshots of it.
	 * (Except if the only snapshots are from the branch we cloned
	 * from.)
	 */
	if (ds->ds_prev != NULL &&
	    dsl_dataset_phys(ds->ds_prev)->ds_next_snap_obj == ds->ds_object)
		return (SET_ERROR(EBUSY));

	/*
	 * Can't delete if there are children of this fs.
	 */
	error = zap_count(mos,
	    dsl_dir_phys(ds->ds_dir)->dd_child_dir_zapobj, &count);
	if (error != 0)
		return (error);
	if (count != 0)
		return (SET_ERROR(EEXIST));

	if (dsl_dir_is_clone(ds->ds_dir) && DS_IS_DEFER_DESTROY(ds->ds_prev) &&
	    dsl_dataset_phys(ds->ds_prev)->ds_num_children == 2 &&
	    ds->ds_prev->ds_userrefs == 0) {
		/* We need to remove the origin snapshot as well. */
		if (!refcount_is_zero(&ds->ds_prev->ds_longholds))
			return (SET_ERROR(EBUSY));
	}
	return (0);
}

static int
dsl_destroy_head_check(void *arg, dmu_tx_t *tx)
{
	dsl_destroy_head_arg_t *ddha = arg;
	dsl_pool_t *dp = dmu_tx_pool(tx);
	dsl_dataset_t *ds;
	int error;

	error = dsl_dataset_hold(dp, ddha->ddha_name, FTAG, &ds);
	if (error != 0)
		return (error);

	error = dsl_destroy_head_check_impl(ds, 0);
	dsl_dataset_rele(ds, FTAG);
	return (error);
}

static void
dsl_dir_destroy_sync(uint64_t ddobj, dmu_tx_t *tx)
{
	dsl_dir_t *dd;
	dsl_pool_t *dp = dmu_tx_pool(tx);
	objset_t *mos = dp->dp_meta_objset;
	dd_used_t t;

	ASSERT(RRW_WRITE_HELD(&dmu_tx_pool(tx)->dp_config_rwlock));

	VERIFY0(dsl_dir_hold_obj(dp, ddobj, NULL, FTAG, &dd));

	ASSERT0(dsl_dir_phys(dd)->dd_head_dataset_obj);

	/*
	 * Decrement the filesystem count for all parent filesystems.
	 *
	 * When we receive an incremental stream into a filesystem that already
	 * exists, a temporary clone is created.  We never count this temporary
	 * clone, whose name begins with a '%'.
	 */
	if (dd->dd_myname[0] != '%' && dd->dd_parent != NULL)
		dsl_fs_ss_count_adjust(dd->dd_parent, -1,
		    DD_FIELD_FILESYSTEM_COUNT, tx);

	/*
	 * Remove our reservation. The impl() routine avoids setting the
	 * actual property, which would require the (already destroyed) ds.
	 */
	dsl_dir_set_reservation_sync_impl(dd, 0, tx);

	ASSERT0(dsl_dir_phys(dd)->dd_used_bytes);
	ASSERT0(dsl_dir_phys(dd)->dd_reserved);
	for (t = 0; t < DD_USED_NUM; t++)
		ASSERT0(dsl_dir_phys(dd)->dd_used_breakdown[t]);

	VERIFY0(zap_destroy(mos, dsl_dir_phys(dd)->dd_child_dir_zapobj, tx));
	VERIFY0(zap_destroy(mos, dsl_dir_phys(dd)->dd_props_zapobj, tx));
	VERIFY0(dsl_deleg_destroy(mos, dsl_dir_phys(dd)->dd_deleg_zapobj, tx));
	VERIFY0(zap_remove(mos,
	    dsl_dir_phys(dd->dd_parent)->dd_child_dir_zapobj,
	    dd->dd_myname, tx));

	dsl_dir_rele(dd, FTAG);
	dmu_object_free_zapified(mos, ddobj, tx);
}

void
dsl_destroy_head_sync_impl(dsl_dataset_t *ds, dmu_tx_t *tx)
{
	dsl_pool_t *dp = dmu_tx_pool(tx);
	objset_t *mos = dp->dp_meta_objset;
	uint64_t obj, ddobj, prevobj = 0;
	boolean_t rmorigin;
	objset_t *os;

	ASSERT3U(dsl_dataset_phys(ds)->ds_num_children, <=, 1);
	ASSERT(ds->ds_prev == NULL ||
	    dsl_dataset_phys(ds->ds_prev)->ds_next_snap_obj != ds->ds_object);
	ASSERT3U(dsl_dataset_phys(ds)->ds_bp.blk_birth, <=, tx->tx_txg);
	ASSERT(RRW_WRITE_HELD(&dp->dp_config_rwlock));

	/* We need to log before removing it from the namespace. */
	spa_history_log_internal_ds(ds, "destroy", tx, "");

	rmorigin = (dsl_dir_is_clone(ds->ds_dir) &&
	    DS_IS_DEFER_DESTROY(ds->ds_prev) &&
	    dsl_dataset_phys(ds->ds_prev)->ds_num_children == 2 &&
	    ds->ds_prev->ds_userrefs == 0);

	/* Remove our reservation. */
	if (ds->ds_reserved != 0) {
		dsl_dataset_set_refreservation_sync_impl(ds,
		    (ZPROP_SRC_NONE | ZPROP_SRC_LOCAL | ZPROP_SRC_RECEIVED),
		    0, tx);
		ASSERT0(ds->ds_reserved);
	}

	if (ds->ds_large_blocks)
		spa_feature_decr(dp->dp_spa, SPA_FEATURE_LARGE_BLOCKS, tx);

	dsl_scan_ds_destroyed(ds, tx);

	obj = ds->ds_object;

	if (dsl_dataset_phys(ds)->ds_prev_snap_obj != 0) {
		/* This is a clone */
		ASSERT(ds->ds_prev != NULL);
		ASSERT3U(dsl_dataset_phys(ds->ds_prev)->ds_next_snap_obj, !=,
		    obj);
		ASSERT0(dsl_dataset_phys(ds)->ds_next_snap_obj);

		dmu_buf_will_dirty(ds->ds_prev->ds_dbuf, tx);
		if (dsl_dataset_phys(ds->ds_prev)->ds_next_clones_obj != 0) {
			dsl_dataset_remove_from_next_clones(ds->ds_prev,
			    obj, tx);
		}

		ASSERT3U(dsl_dataset_phys(ds->ds_prev)->ds_num_children, >, 1);
		dsl_dataset_phys(ds->ds_prev)->ds_num_children--;
	}

	/*
	 * Destroy the deadlist.  Unless it's a clone, the
	 * deadlist should be empty.  (If it's a clone, it's
	 * safe to ignore the deadlist contents.)
	 */
	dsl_deadlist_close(&ds->ds_deadlist);
	dsl_deadlist_free(mos, dsl_dataset_phys(ds)->ds_deadlist_obj, tx);
	dmu_buf_will_dirty(ds->ds_dbuf, tx);
	dsl_dataset_phys(ds)->ds_deadlist_obj = 0;

	VERIFY0(dmu_objset_from_ds(ds, &os));

	if (!spa_feature_is_enabled(dp->dp_spa, SPA_FEATURE_ASYNC_DESTROY)) {
		old_synchronous_dataset_destroy(ds, tx);
	} else {
		/*
		 * Move the bptree into the pool's list of trees to
		 * clean up and update space accounting information.
		 */
		uint64_t used, comp, uncomp;

		zil_destroy_sync(dmu_objset_zil(os), tx);

		if (!spa_feature_is_active(dp->dp_spa,
		    SPA_FEATURE_ASYNC_DESTROY)) {
			dsl_scan_t *scn = dp->dp_scan;
			spa_feature_incr(dp->dp_spa, SPA_FEATURE_ASYNC_DESTROY,
			    tx);
			dp->dp_bptree_obj = bptree_alloc(mos, tx);
			VERIFY0(zap_add(mos,
			    DMU_POOL_DIRECTORY_OBJECT,
			    DMU_POOL_BPTREE_OBJ, sizeof (uint64_t), 1,
			    &dp->dp_bptree_obj, tx));
			ASSERT(!scn->scn_async_destroying);
			scn->scn_async_destroying = B_TRUE;
		}

		used = dsl_dir_phys(ds->ds_dir)->dd_used_bytes;
		comp = dsl_dir_phys(ds->ds_dir)->dd_compressed_bytes;
		uncomp = dsl_dir_phys(ds->ds_dir)->dd_uncompressed_bytes;

		ASSERT(!DS_UNIQUE_IS_ACCURATE(ds) ||
		    dsl_dataset_phys(ds)->ds_unique_bytes == used);

		bptree_add(mos, dp->dp_bptree_obj,
		    &dsl_dataset_phys(ds)->ds_bp,
		    dsl_dataset_phys(ds)->ds_prev_snap_txg,
		    used, comp, uncomp, tx);
		dsl_dir_diduse_space(ds->ds_dir, DD_USED_HEAD,
		    -used, -comp, -uncomp, tx);
		dsl_dir_diduse_space(dp->dp_free_dir, DD_USED_HEAD,
		    used, comp, uncomp, tx);
	}

	if (ds->ds_prev != NULL) {
		if (spa_version(dp->dp_spa) >= SPA_VERSION_DIR_CLONES) {
			VERIFY0(zap_remove_int(mos,
			    dsl_dir_phys(ds->ds_prev->ds_dir)->dd_clones,
			    ds->ds_object, tx));
		}
		prevobj = ds->ds_prev->ds_object;
		dsl_dataset_rele(ds->ds_prev, ds);
		ds->ds_prev = NULL;
	}

	/*
	 * This must be done after the dsl_traverse(), because it will
	 * re-open the objset.
	 */
	if (ds->ds_objset) {
		dmu_objset_evict(ds->ds_objset);
		ds->ds_objset = NULL;
	}

	/* Erase the link in the dir */
	dmu_buf_will_dirty(ds->ds_dir->dd_dbuf, tx);
	dsl_dir_phys(ds->ds_dir)->dd_head_dataset_obj = 0;
	ddobj = ds->ds_dir->dd_object;
	ASSERT(dsl_dataset_phys(ds)->ds_snapnames_zapobj != 0);
	VERIFY0(zap_destroy(mos,
	    dsl_dataset_phys(ds)->ds_snapnames_zapobj, tx));

	if (ds->ds_bookmarks != 0) {
		VERIFY0(zap_destroy(mos, ds->ds_bookmarks, tx));
		spa_feature_decr(dp->dp_spa, SPA_FEATURE_BOOKMARKS, tx);
	}

	spa_prop_clear_bootfs(dp->dp_spa, ds->ds_object, tx);

	ASSERT0(dsl_dataset_phys(ds)->ds_next_clones_obj);
	ASSERT0(dsl_dataset_phys(ds)->ds_props_obj);
	ASSERT0(dsl_dataset_phys(ds)->ds_userrefs_obj);
	dsl_dir_rele(ds->ds_dir, ds);
	ds->ds_dir = NULL;
	dmu_object_free_zapified(mos, obj, tx);

	dsl_dir_destroy_sync(ddobj, tx);

	if (rmorigin) {
		dsl_dataset_t *prev;
		VERIFY0(dsl_dataset_hold_obj(dp, prevobj, FTAG, &prev));
		dsl_destroy_snapshot_sync_impl(prev, B_FALSE, tx);
		dsl_dataset_rele(prev, FTAG);
	}
}

static void
dsl_destroy_head_sync(void *arg, dmu_tx_t *tx)
{
	dsl_destroy_head_arg_t *ddha = arg;
	dsl_pool_t *dp = dmu_tx_pool(tx);
	dsl_dataset_t *ds;

	VERIFY0(dsl_dataset_hold(dp, ddha->ddha_name, FTAG, &ds));
	dsl_destroy_head_sync_impl(ds, tx);
	zvol_remove_minors(dp->dp_spa, ddha->ddha_name, B_TRUE);
	dsl_dataset_rele(ds, FTAG);
}

static void
dsl_destroy_head_begin_sync(void *arg, dmu_tx_t *tx)
{
	dsl_destroy_head_arg_t *ddha = arg;
	dsl_pool_t *dp = dmu_tx_pool(tx);
	dsl_dataset_t *ds;

	VERIFY0(dsl_dataset_hold(dp, ddha->ddha_name, FTAG, &ds));

	/* Mark it as inconsistent on-disk, in case we crash */
	dmu_buf_will_dirty(ds->ds_dbuf, tx);
	dsl_dataset_phys(ds)->ds_flags |= DS_FLAG_INCONSISTENT;

	spa_history_log_internal_ds(ds, "destroy begin", tx, "");
	dsl_dataset_rele(ds, FTAG);
}

int
dsl_destroy_head(const char *name)
{
	dsl_destroy_head_arg_t ddha;
	int error;
	spa_t *spa;
	boolean_t isenabled;

#ifdef _KERNEL
	zfs_destroy_unmount_origin(name);
#endif

	error = spa_open(name, &spa, FTAG);
	if (error != 0)
		return (error);
	isenabled = spa_feature_is_enabled(spa, SPA_FEATURE_ASYNC_DESTROY);
	spa_close(spa, FTAG);

	ddha.ddha_name = name;

	if (!isenabled) {
		objset_t *os;

		error = dsl_sync_task(name, dsl_destroy_head_check,
		    dsl_destroy_head_begin_sync, &ddha,
		    0, ZFS_SPACE_CHECK_NONE);
		if (error != 0)
			return (error);

		/*
		 * Head deletion is processed in one txg on old pools;
		 * remove the objects from open context so that the txg sync
		 * is not too long.
		 */
		error = dmu_objset_own(name, DMU_OST_ANY, B_FALSE, FTAG, &os);
		if (error == 0) {
			uint64_t obj;
			uint64_t prev_snap_txg =
			    dsl_dataset_phys(dmu_objset_ds(os))->
			    ds_prev_snap_txg;
			for (obj = 0; error == 0;
			    error = dmu_object_next(os, &obj, FALSE,
			    prev_snap_txg))
				(void) dmu_free_long_object(os, obj);
			/* sync out all frees */
			txg_wait_synced(dmu_objset_pool(os), 0);
			dmu_objset_disown(os, FTAG);
		}
	}

	return (dsl_sync_task(name, dsl_destroy_head_check,
	    dsl_destroy_head_sync, &ddha, 0, ZFS_SPACE_CHECK_NONE));
}

/*
 * Note, this function is used as the callback for dmu_objset_find().  We
 * always return 0 so that we will continue to find and process
 * inconsistent datasets, even if we encounter an error trying to
 * process one of them.
 */
/* ARGSUSED */
int
dsl_destroy_inconsistent(const char *dsname, void *arg)
{
	objset_t *os;

	if (dmu_objset_hold(dsname, FTAG, &os) == 0) {
		boolean_t inconsistent = DS_IS_INCONSISTENT(dmu_objset_ds(os));
		dmu_objset_rele(os, FTAG);
		if (inconsistent)
			(void) dsl_destroy_head(dsname);
	}
	return (0);
}


#if defined(_KERNEL) && defined(HAVE_SPL)
EXPORT_SYMBOL(dsl_destroy_head);
EXPORT_SYMBOL(dsl_destroy_head_sync_impl);
EXPORT_SYMBOL(dsl_dataset_user_hold_check_one);
EXPORT_SYMBOL(dsl_destroy_snapshot_sync_impl);
EXPORT_SYMBOL(dsl_destroy_inconsistent);
EXPORT_SYMBOL(dsl_dataset_user_release_tmp);
EXPORT_SYMBOL(dsl_destroy_head_check_impl);
#endif
