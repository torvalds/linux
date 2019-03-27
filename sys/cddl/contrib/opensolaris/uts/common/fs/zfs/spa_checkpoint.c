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
 * Copyright (c) 2017 by Delphix. All rights reserved.
 */

/*
 * Storage Pool Checkpoint
 *
 * A storage pool checkpoint can be thought of as a pool-wide snapshot or
 * a stable version of extreme rewind that guarantees no blocks from the
 * checkpointed state will have been overwritten. It remembers the entire
 * state of the storage pool (e.g. snapshots, dataset names, etc..) from the
 * point that it was taken and the user can rewind back to that point even if
 * they applied destructive operations on their datasets or even enabled new
 * zpool on-disk features. If a pool has a checkpoint that is no longer
 * needed, the user can discard it.
 *
 * == On disk data structures used ==
 *
 * - The pool has a new feature flag and a new entry in the MOS. The feature
 *   flag is set to active when we create the checkpoint and remains active
 *   until the checkpoint is fully discarded. The entry in the MOS config
 *   (DMU_POOL_ZPOOL_CHECKPOINT) is populated with the uberblock that
 *   references the state of the pool when we take the checkpoint. The entry
 *   remains populated until we start discarding the checkpoint or we rewind
 *   back to it.
 *
 * - Each vdev contains a vdev-wide space map while the pool has a checkpoint,
 *   which persists until the checkpoint is fully discarded. The space map
 *   contains entries that have been freed in the current state of the pool
 *   but we want to keep around in case we decide to rewind to the checkpoint.
 *   [see vdev_checkpoint_sm]
 *
 * - Each metaslab's ms_sm space map behaves the same as without the
 *   checkpoint, with the only exception being the scenario when we free
 *   blocks that belong to the checkpoint. In this case, these blocks remain
 *   ALLOCATED in the metaslab's space map and they are added as FREE in the
 *   vdev's checkpoint space map.
 *
 * - Each uberblock has a field (ub_checkpoint_txg) which holds the txg that
 *   the uberblock was checkpointed. For normal uberblocks this field is 0.
 *
 * == Overview of operations ==
 *
 * - To create a checkpoint, we first wait for the current TXG to be synced,
 *   so we can use the most recently synced uberblock (spa_ubsync) as the
 *   checkpointed uberblock. Then we use an early synctask to place that
 *   uberblock in MOS config, increment the feature flag for the checkpoint
 *   (marking it active), and setting spa_checkpoint_txg (see its use below)
 *   to the TXG of the checkpointed uberblock. We use an early synctask for
 *   the aforementioned operations to ensure that no blocks were dirtied
 *   between the current TXG and the TXG of the checkpointed uberblock
 *   (e.g the previous txg).
 *
 * - When a checkpoint exists, we need to ensure that the blocks that
 *   belong to the checkpoint are freed but never reused. This means that
 *   these blocks should never end up in the ms_allocatable or the ms_freeing
 *   trees of a metaslab. Therefore, whenever there is a checkpoint the new
 *   ms_checkpointing tree is used in addition to the aforementioned ones.
 *
 *   Whenever a block is freed and we find out that it is referenced by the
 *   checkpoint (we find out by comparing its birth to spa_checkpoint_txg),
 *   we place it in the ms_checkpointing tree instead of the ms_freeingtree.
 *   This way, we divide the blocks that are being freed into checkpointed
 *   and not-checkpointed blocks.
 *
 *   In order to persist these frees, we write the extents from the
 *   ms_freeingtree to the ms_sm as usual, and the extents from the
 *   ms_checkpointing tree to the vdev_checkpoint_sm. This way, these
 *   checkpointed extents will remain allocated in the metaslab's ms_sm space
 *   map, and therefore won't be reused [see metaslab_sync()]. In addition,
 *   when we discard the checkpoint, we can find the entries that have
 *   actually been freed in vdev_checkpoint_sm.
 *   [see spa_checkpoint_discard_thread_sync()]
 *
 * - To discard the checkpoint we use an early synctask to delete the
 *   checkpointed uberblock from the MOS config, set spa_checkpoint_txg to 0,
 *   and wakeup the discarding zthr thread (an open-context async thread).
 *   We use an early synctask to ensure that the operation happens before any
 *   new data end up in the checkpoint's data structures.
 *
 *   Once the synctask is done and the discarding zthr is awake, we discard
 *   the checkpointed data over multiple TXGs by having the zthr prefetching
 *   entries from vdev_checkpoint_sm and then starting a synctask that places
 *   them as free blocks in to their respective ms_allocatable and ms_sm
 *   structures.
 *   [see spa_checkpoint_discard_thread()]
 *
 *   When there are no entries left in the vdev_checkpoint_sm of all
 *   top-level vdevs, a final synctask runs that decrements the feature flag.
 *
 * - To rewind to the checkpoint, we first use the current uberblock and
 *   open the MOS so we can access the checkpointed uberblock from the MOS
 *   config. After we retrieve the checkpointed uberblock, we use it as the
 *   current uberblock for the pool by writing it to disk with an updated
 *   TXG, opening its version of the MOS, and moving on as usual from there.
 *   [see spa_ld_checkpoint_rewind()]
 *
 *   An important note on rewinding to the checkpoint has to do with how we
 *   handle ZIL blocks. In the scenario of a rewind, we clear out any ZIL
 *   blocks that have not been claimed by the time we took the checkpoint
 *   as they should no longer be valid.
 *   [see comment in zil_claim()]
 *
 * == Miscellaneous information ==
 *
 * - In the hypothetical event that we take a checkpoint, remove a vdev,
 *   and attempt to rewind, the rewind would fail as the checkpointed
 *   uberblock would reference data in the removed device. For this reason
 *   and others of similar nature, we disallow the following operations that
 *   can change the config:
 *   	vdev removal and attach/detach, mirror splitting, and pool reguid.
 *
 * - As most of the checkpoint logic is implemented in the SPA and doesn't
 *   distinguish datasets when it comes to space accounting, having a
 *   checkpoint can potentially break the boundaries set by dataset
 *   reservations.
 */

#include <sys/dmu_tx.h>
#include <sys/dsl_dir.h>
#include <sys/dsl_synctask.h>
#include <sys/metaslab_impl.h>
#include <sys/spa.h>
#include <sys/spa_impl.h>
#include <sys/spa_checkpoint.h>
#include <sys/vdev_impl.h>
#include <sys/zap.h>
#include <sys/zfeature.h>

/*
 * The following parameter limits the amount of memory to be used for the
 * prefetching of the checkpoint space map done on each vdev while
 * discarding the checkpoint.
 *
 * The reason it exists is because top-level vdevs with long checkpoint
 * space maps can potentially take up a lot of memory depending on the
 * amount of checkpointed data that has been freed within them while
 * the pool had a checkpoint.
 */
uint64_t	zfs_spa_discard_memory_limit = 16 * 1024 * 1024;

int
spa_checkpoint_get_stats(spa_t *spa, pool_checkpoint_stat_t *pcs)
{
	if (!spa_feature_is_active(spa, SPA_FEATURE_POOL_CHECKPOINT))
		return (SET_ERROR(ZFS_ERR_NO_CHECKPOINT));

	bzero(pcs, sizeof (pool_checkpoint_stat_t));

	int error = zap_contains(spa_meta_objset(spa),
	    DMU_POOL_DIRECTORY_OBJECT, DMU_POOL_ZPOOL_CHECKPOINT);
	ASSERT(error == 0 || error == ENOENT);

	if (error == ENOENT)
		pcs->pcs_state = CS_CHECKPOINT_DISCARDING;
	else
		pcs->pcs_state = CS_CHECKPOINT_EXISTS;

	pcs->pcs_space = spa->spa_checkpoint_info.sci_dspace;
	pcs->pcs_start_time = spa->spa_checkpoint_info.sci_timestamp;

	return (0);
}

static void
spa_checkpoint_discard_complete_sync(void *arg, dmu_tx_t *tx)
{
	spa_t *spa = arg;

	spa->spa_checkpoint_info.sci_timestamp = 0;

	spa_feature_decr(spa, SPA_FEATURE_POOL_CHECKPOINT, tx);

	spa_history_log_internal(spa, "spa discard checkpoint", tx,
	    "finished discarding checkpointed state from the pool");
}

typedef struct spa_checkpoint_discard_sync_callback_arg {
	vdev_t *sdc_vd;
	uint64_t sdc_txg;
	uint64_t sdc_entry_limit;
} spa_checkpoint_discard_sync_callback_arg_t;

static int
spa_checkpoint_discard_sync_callback(space_map_entry_t *sme, void *arg)
{
	spa_checkpoint_discard_sync_callback_arg_t *sdc = arg;
	vdev_t *vd = sdc->sdc_vd;
	metaslab_t *ms = vd->vdev_ms[sme->sme_offset >> vd->vdev_ms_shift];
	uint64_t end = sme->sme_offset + sme->sme_run;

	if (sdc->sdc_entry_limit == 0)
		return (EINTR);

	/*
	 * Since the space map is not condensed, we know that
	 * none of its entries is crossing the boundaries of
	 * its respective metaslab.
	 *
	 * That said, there is no fundamental requirement that
	 * the checkpoint's space map entries should not cross
	 * metaslab boundaries. So if needed we could add code
	 * that handles metaslab-crossing segments in the future.
	 */
	VERIFY3U(sme->sme_type, ==, SM_FREE);
	VERIFY3U(sme->sme_offset, >=, ms->ms_start);
	VERIFY3U(end, <=, ms->ms_start + ms->ms_size);

	/*
	 * At this point we should not be processing any
	 * other frees concurrently, so the lock is technically
	 * unnecessary. We use the lock anyway though to
	 * potentially save ourselves from future headaches.
	 */
	mutex_enter(&ms->ms_lock);
	if (range_tree_is_empty(ms->ms_freeing))
		vdev_dirty(vd, VDD_METASLAB, ms, sdc->sdc_txg);
	range_tree_add(ms->ms_freeing, sme->sme_offset, sme->sme_run);
	mutex_exit(&ms->ms_lock);

	ASSERT3U(vd->vdev_spa->spa_checkpoint_info.sci_dspace, >=,
	    sme->sme_run);
	ASSERT3U(vd->vdev_stat.vs_checkpoint_space, >=, sme->sme_run);

	vd->vdev_spa->spa_checkpoint_info.sci_dspace -= sme->sme_run;
	vd->vdev_stat.vs_checkpoint_space -= sme->sme_run;
	sdc->sdc_entry_limit--;

	return (0);
}

static void
spa_checkpoint_accounting_verify(spa_t *spa)
{
	vdev_t *rvd = spa->spa_root_vdev;
	uint64_t ckpoint_sm_space_sum = 0;
	uint64_t vs_ckpoint_space_sum = 0;

	for (uint64_t c = 0; c < rvd->vdev_children; c++) {
		vdev_t *vd = rvd->vdev_child[c];

		if (vd->vdev_checkpoint_sm != NULL) {
			ckpoint_sm_space_sum +=
			    -vd->vdev_checkpoint_sm->sm_alloc;
			vs_ckpoint_space_sum +=
			    vd->vdev_stat.vs_checkpoint_space;
			ASSERT3U(ckpoint_sm_space_sum, ==,
			    vs_ckpoint_space_sum);
		} else {
			ASSERT0(vd->vdev_stat.vs_checkpoint_space);
		}
	}
	ASSERT3U(spa->spa_checkpoint_info.sci_dspace, ==, ckpoint_sm_space_sum);
}

static void
spa_checkpoint_discard_thread_sync(void *arg, dmu_tx_t *tx)
{
	vdev_t *vd = arg;
	int error;

	/*
	 * The space map callback is applied only to non-debug entries.
	 * Because the number of debug entries is less or equal to the
	 * number of non-debug entries, we want to ensure that we only
	 * read what we prefetched from open-context.
	 *
	 * Thus, we set the maximum entries that the space map callback
	 * will be applied to be half the entries that could fit in the
	 * imposed memory limit.
	 *
	 * Note that since this is a conservative estimate we also
	 * assume the worst case scenario in our computation where each
	 * entry is two-word.
	 */
	uint64_t max_entry_limit =
	    (zfs_spa_discard_memory_limit / (2 * sizeof (uint64_t))) >> 1;

	/*
	 * Iterate from the end of the space map towards the beginning,
	 * placing its entries on ms_freeing and removing them from the
	 * space map. The iteration stops if one of the following
	 * conditions is true:
	 *
	 * 1] We reached the beginning of the space map. At this point
	 *    the space map should be completely empty and
	 *    space_map_incremental_destroy should have returned 0.
	 *    The next step would be to free and close the space map
	 *    and remove its entry from its vdev's top zap. This allows
	 *    spa_checkpoint_discard_thread() to move on to the next vdev.
	 *
	 * 2] We reached the memory limit (amount of memory used to hold
	 *    space map entries in memory) and space_map_incremental_destroy
	 *    returned EINTR. This means that there are entries remaining
	 *    in the space map that will be cleared in a future invocation
	 *    of this function by spa_checkpoint_discard_thread().
	 */
	spa_checkpoint_discard_sync_callback_arg_t sdc;
	sdc.sdc_vd = vd;
	sdc.sdc_txg = tx->tx_txg;
	sdc.sdc_entry_limit = max_entry_limit;

	uint64_t words_before =
	    space_map_length(vd->vdev_checkpoint_sm) / sizeof (uint64_t);

	error = space_map_incremental_destroy(vd->vdev_checkpoint_sm,
	    spa_checkpoint_discard_sync_callback, &sdc, tx);

	uint64_t words_after =
	    space_map_length(vd->vdev_checkpoint_sm) / sizeof (uint64_t);

#ifdef DEBUG
	spa_checkpoint_accounting_verify(vd->vdev_spa);
#endif

	zfs_dbgmsg("discarding checkpoint: txg %llu, vdev id %d, "
	    "deleted %llu words - %llu words are left",
	    tx->tx_txg, vd->vdev_id, (words_before - words_after),
	    words_after);

	if (error != EINTR) {
		if (error != 0) {
			zfs_panic_recover("zfs: error %d was returned "
			    "while incrementally destroying the checkpoint "
			    "space map of vdev %llu\n",
			    error, vd->vdev_id);
		}
		ASSERT0(words_after);
		ASSERT0(vd->vdev_checkpoint_sm->sm_alloc);
		ASSERT0(space_map_length(vd->vdev_checkpoint_sm));

		space_map_free(vd->vdev_checkpoint_sm, tx);
		space_map_close(vd->vdev_checkpoint_sm);
		vd->vdev_checkpoint_sm = NULL;

		VERIFY0(zap_remove(spa_meta_objset(vd->vdev_spa),
		    vd->vdev_top_zap, VDEV_TOP_ZAP_POOL_CHECKPOINT_SM, tx));
	}
}

static boolean_t
spa_checkpoint_discard_is_done(spa_t *spa)
{
	vdev_t *rvd = spa->spa_root_vdev;

	ASSERT(!spa_has_checkpoint(spa));
	ASSERT(spa_feature_is_active(spa, SPA_FEATURE_POOL_CHECKPOINT));

	for (uint64_t c = 0; c < rvd->vdev_children; c++) {
		if (rvd->vdev_child[c]->vdev_checkpoint_sm != NULL)
			return (B_FALSE);
		ASSERT0(rvd->vdev_child[c]->vdev_stat.vs_checkpoint_space);
	}

	return (B_TRUE);
}

/* ARGSUSED */
boolean_t
spa_checkpoint_discard_thread_check(void *arg, zthr_t *zthr)
{
	spa_t *spa = arg;

	if (!spa_feature_is_active(spa, SPA_FEATURE_POOL_CHECKPOINT))
		return (B_FALSE);

	if (spa_has_checkpoint(spa))
		return (B_FALSE);

	return (B_TRUE);
}

int
spa_checkpoint_discard_thread(void *arg, zthr_t *zthr)
{
	spa_t *spa = arg;
	vdev_t *rvd = spa->spa_root_vdev;

	for (uint64_t c = 0; c < rvd->vdev_children; c++) {
		vdev_t *vd = rvd->vdev_child[c];

		while (vd->vdev_checkpoint_sm != NULL) {
			space_map_t *checkpoint_sm = vd->vdev_checkpoint_sm;
			int numbufs;
			dmu_buf_t **dbp;

			if (zthr_iscancelled(zthr))
				return (0);

			ASSERT3P(vd->vdev_ops, !=, &vdev_indirect_ops);

			uint64_t size = MIN(space_map_length(checkpoint_sm),
			    zfs_spa_discard_memory_limit);
			uint64_t offset =
			    space_map_length(checkpoint_sm) - size;

			/*
			 * Ensure that the part of the space map that will
			 * be destroyed by the synctask, is prefetched in
			 * memory before the synctask runs.
			 */
			int error = dmu_buf_hold_array_by_bonus(
			    checkpoint_sm->sm_dbuf, offset, size,
			    B_TRUE, FTAG, &numbufs, &dbp);
			if (error != 0) {
				zfs_panic_recover("zfs: error %d was returned "
				    "while prefetching checkpoint space map "
				    "entries of vdev %llu\n",
				    error, vd->vdev_id);
			}

			VERIFY0(dsl_sync_task(spa->spa_name, NULL,
			    spa_checkpoint_discard_thread_sync, vd,
			    0, ZFS_SPACE_CHECK_NONE));

			dmu_buf_rele_array(dbp, numbufs, FTAG);
		}
	}

	VERIFY(spa_checkpoint_discard_is_done(spa));
	VERIFY0(spa->spa_checkpoint_info.sci_dspace);
	VERIFY0(dsl_sync_task(spa->spa_name, NULL,
	    spa_checkpoint_discard_complete_sync, spa,
	    0, ZFS_SPACE_CHECK_NONE));

	return (0);
}


/* ARGSUSED */
static int
spa_checkpoint_check(void *arg, dmu_tx_t *tx)
{
	spa_t *spa = dmu_tx_pool(tx)->dp_spa;

	if (!spa_feature_is_enabled(spa, SPA_FEATURE_POOL_CHECKPOINT))
		return (SET_ERROR(ENOTSUP));

	if (!spa_top_vdevs_spacemap_addressable(spa))
		return (SET_ERROR(ZFS_ERR_VDEV_TOO_BIG));

	if (spa->spa_vdev_removal != NULL)
		return (SET_ERROR(ZFS_ERR_DEVRM_IN_PROGRESS));

	if (spa->spa_checkpoint_txg != 0)
		return (SET_ERROR(ZFS_ERR_CHECKPOINT_EXISTS));

	if (spa_feature_is_active(spa, SPA_FEATURE_POOL_CHECKPOINT))
		return (SET_ERROR(ZFS_ERR_DISCARDING_CHECKPOINT));

	return (0);
}

/* ARGSUSED */
static void
spa_checkpoint_sync(void *arg, dmu_tx_t *tx)
{
	dsl_pool_t *dp = dmu_tx_pool(tx);
	spa_t *spa = dp->dp_spa;
	uberblock_t checkpoint = spa->spa_ubsync;

	/*
	 * At this point, there should not be a checkpoint in the MOS.
	 */
	ASSERT3U(zap_contains(spa_meta_objset(spa), DMU_POOL_DIRECTORY_OBJECT,
	    DMU_POOL_ZPOOL_CHECKPOINT), ==, ENOENT);

	ASSERT0(spa->spa_checkpoint_info.sci_timestamp);
	ASSERT0(spa->spa_checkpoint_info.sci_dspace);

	/*
	 * Since the checkpointed uberblock is the one that just got synced
	 * (we use spa_ubsync), its txg must be equal to the txg number of
	 * the txg we are syncing, minus 1.
	 */
	ASSERT3U(checkpoint.ub_txg, ==, spa->spa_syncing_txg - 1);

	/*
	 * Once the checkpoint is in place, we need to ensure that none of
	 * its blocks will be marked for reuse after it has been freed.
	 * When there is a checkpoint and a block is freed, we compare its
	 * birth txg to the txg of the checkpointed uberblock to see if the
	 * block is part of the checkpoint or not. Therefore, we have to set
	 * spa_checkpoint_txg before any frees happen in this txg (which is
	 * why this is done as an early_synctask as explained in the comment
	 * in spa_checkpoint()).
	 */
	spa->spa_checkpoint_txg = checkpoint.ub_txg;
	spa->spa_checkpoint_info.sci_timestamp = checkpoint.ub_timestamp;

	checkpoint.ub_checkpoint_txg = checkpoint.ub_txg;
	VERIFY0(zap_add(spa->spa_dsl_pool->dp_meta_objset,
	    DMU_POOL_DIRECTORY_OBJECT, DMU_POOL_ZPOOL_CHECKPOINT,
	    sizeof (uint64_t), sizeof (uberblock_t) / sizeof (uint64_t),
	    &checkpoint, tx));

	/*
	 * Increment the feature refcount and thus activate the feature.
	 * Note that the feature will be deactivated when we've
	 * completely discarded all checkpointed state (both vdev
	 * space maps and uberblock).
	 */
	spa_feature_incr(spa, SPA_FEATURE_POOL_CHECKPOINT, tx);

	spa_history_log_internal(spa, "spa checkpoint", tx,
	    "checkpointed uberblock txg=%llu", checkpoint.ub_txg);
}

/*
 * Create a checkpoint for the pool.
 */
int
spa_checkpoint(const char *pool)
{
	int error;
	spa_t *spa;

	error = spa_open(pool, &spa, FTAG);
	if (error != 0)
		return (error);

	mutex_enter(&spa->spa_vdev_top_lock);

	/*
	 * Wait for current syncing txg to finish so the latest synced
	 * uberblock (spa_ubsync) has all the changes that we expect
	 * to see if we were to revert later to the checkpoint. In other
	 * words we want the checkpointed uberblock to include/reference
	 * all the changes that were pending at the time that we issued
	 * the checkpoint command.
	 */
	txg_wait_synced(spa_get_dsl(spa), 0);

	/*
	 * As the checkpointed uberblock references blocks from the previous
	 * txg (spa_ubsync) we want to ensure that are not freeing any of
	 * these blocks in the same txg that the following synctask will
	 * run. Thus, we run it as an early synctask, so the dirty changes
	 * that are synced to disk afterwards during zios and other synctasks
	 * do not reuse checkpointed blocks.
	 */
	error = dsl_early_sync_task(pool, spa_checkpoint_check,
	    spa_checkpoint_sync, NULL, 0, ZFS_SPACE_CHECK_NORMAL);

	mutex_exit(&spa->spa_vdev_top_lock);

	spa_close(spa, FTAG);
	return (error);
}

/* ARGSUSED */
static int
spa_checkpoint_discard_check(void *arg, dmu_tx_t *tx)
{
	spa_t *spa = dmu_tx_pool(tx)->dp_spa;

	if (!spa_feature_is_active(spa, SPA_FEATURE_POOL_CHECKPOINT))
		return (SET_ERROR(ZFS_ERR_NO_CHECKPOINT));

	if (spa->spa_checkpoint_txg == 0)
		return (SET_ERROR(ZFS_ERR_DISCARDING_CHECKPOINT));

	VERIFY0(zap_contains(spa_meta_objset(spa),
	    DMU_POOL_DIRECTORY_OBJECT, DMU_POOL_ZPOOL_CHECKPOINT));

	return (0);
}

/* ARGSUSED */
static void
spa_checkpoint_discard_sync(void *arg, dmu_tx_t *tx)
{
	spa_t *spa = dmu_tx_pool(tx)->dp_spa;

	VERIFY0(zap_remove(spa_meta_objset(spa), DMU_POOL_DIRECTORY_OBJECT,
	    DMU_POOL_ZPOOL_CHECKPOINT, tx));

	spa->spa_checkpoint_txg = 0;

	zthr_wakeup(spa->spa_checkpoint_discard_zthr);

	spa_history_log_internal(spa, "spa discard checkpoint", tx,
	    "started discarding checkpointed state from the pool");
}

/*
 * Discard the checkpoint from a pool.
 */
int
spa_checkpoint_discard(const char *pool)
{
	/*
	 * Similarly to spa_checkpoint(), we want our synctask to run
	 * before any pending dirty data are written to disk so they
	 * won't end up in the checkpoint's data structures (e.g.
	 * ms_checkpointing and vdev_checkpoint_sm) and re-create any
	 * space maps that the discarding open-context thread has
	 * deleted.
	 * [see spa_discard_checkpoint_sync and spa_discard_checkpoint_thread]
	 */
	return (dsl_early_sync_task(pool, spa_checkpoint_discard_check,
	    spa_checkpoint_discard_sync, NULL, 0,
	    ZFS_SPACE_CHECK_DISCARD_CHECKPOINT));
}
