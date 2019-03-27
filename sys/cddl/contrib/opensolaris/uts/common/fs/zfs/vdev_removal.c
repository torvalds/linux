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
 * Copyright (c) 2011, 2018 by Delphix. All rights reserved.
 */

#include <sys/zfs_context.h>
#include <sys/spa_impl.h>
#include <sys/dmu.h>
#include <sys/dmu_tx.h>
#include <sys/zap.h>
#include <sys/vdev_impl.h>
#include <sys/metaslab.h>
#include <sys/metaslab_impl.h>
#include <sys/uberblock_impl.h>
#include <sys/txg.h>
#include <sys/avl.h>
#include <sys/bpobj.h>
#include <sys/dsl_pool.h>
#include <sys/dsl_synctask.h>
#include <sys/dsl_dir.h>
#include <sys/arc.h>
#include <sys/zfeature.h>
#include <sys/vdev_indirect_births.h>
#include <sys/vdev_indirect_mapping.h>
#include <sys/abd.h>
#include <sys/vdev_initialize.h>

/*
 * This file contains the necessary logic to remove vdevs from a
 * storage pool.  Currently, the only devices that can be removed
 * are log, cache, and spare devices; and top level vdevs from a pool
 * w/o raidz.  (Note that members of a mirror can also be removed
 * by the detach operation.)
 *
 * Log vdevs are removed by evacuating them and then turning the vdev
 * into a hole vdev while holding spa config locks.
 *
 * Top level vdevs are removed and converted into an indirect vdev via
 * a multi-step process:
 *
 *  - Disable allocations from this device (spa_vdev_remove_top).
 *
 *  - From a new thread (spa_vdev_remove_thread), copy data from
 *    the removing vdev to a different vdev.  The copy happens in open
 *    context (spa_vdev_copy_impl) and issues a sync task
 *    (vdev_mapping_sync) so the sync thread can update the partial
 *    indirect mappings in core and on disk.
 *
 *  - If a free happens during a removal, it is freed from the
 *    removing vdev, and if it has already been copied, from the new
 *    location as well (free_from_removing_vdev).
 *
 *  - After the removal is completed, the copy thread converts the vdev
 *    into an indirect vdev (vdev_remove_complete) before instructing
 *    the sync thread to destroy the space maps and finish the removal
 *    (spa_finish_removal).
 */

typedef struct vdev_copy_arg {
	metaslab_t	*vca_msp;
	uint64_t	vca_outstanding_bytes;
	kcondvar_t	vca_cv;
	kmutex_t	vca_lock;
} vdev_copy_arg_t;

/*
 * The maximum amount of memory we can use for outstanding i/o while
 * doing a device removal.  This determines how much i/o we can have
 * in flight concurrently.
 */
int zfs_remove_max_copy_bytes = 64 * 1024 * 1024;

/*
 * The largest contiguous segment that we will attempt to allocate when
 * removing a device.  This can be no larger than SPA_MAXBLOCKSIZE.  If
 * there is a performance problem with attempting to allocate large blocks,
 * consider decreasing this.
 *
 * Note: we will issue I/Os of up to this size.  The mpt driver does not
 * respond well to I/Os larger than 1MB, so we set this to 1MB.  (When
 * mpt processes an I/O larger than 1MB, it needs to do an allocation of
 * 2 physically contiguous pages; if this allocation fails, mpt will drop
 * the I/O and hang the device.)
 */
int zfs_remove_max_segment = 1024 * 1024;

/*
 * Allow a remap segment to span free chunks of at most this size. The main
 * impact of a larger span is that we will read and write larger, more
 * contiguous chunks, with more "unnecessary" data -- trading off bandwidth
 * for iops.  The value here was chosen to align with
 * zfs_vdev_read_gap_limit, which is a similar concept when doing regular
 * reads (but there's no reason it has to be the same).
 *
 * Additionally, a higher span will have the following relatively minor
 * effects:
 *  - the mapping will be smaller, since one entry can cover more allocated
 *    segments
 *  - more of the fragmentation in the removing device will be preserved
 *  - we'll do larger allocations, which may fail and fall back on smaller
 *    allocations
 */
int vdev_removal_max_span = 32 * 1024;

/*
 * This is used by the test suite so that it can ensure that certain
 * actions happen while in the middle of a removal.
 */
uint64_t zfs_remove_max_bytes_pause = UINT64_MAX;

#define	VDEV_REMOVAL_ZAP_OBJS	"lzap"

static void spa_vdev_remove_thread(void *arg);

static void
spa_sync_removing_state(spa_t *spa, dmu_tx_t *tx)
{
	VERIFY0(zap_update(spa->spa_dsl_pool->dp_meta_objset,
	    DMU_POOL_DIRECTORY_OBJECT,
	    DMU_POOL_REMOVING, sizeof (uint64_t),
	    sizeof (spa->spa_removing_phys) / sizeof (uint64_t),
	    &spa->spa_removing_phys, tx));
}

static nvlist_t *
spa_nvlist_lookup_by_guid(nvlist_t **nvpp, int count, uint64_t target_guid)
{
	for (int i = 0; i < count; i++) {
		uint64_t guid =
		    fnvlist_lookup_uint64(nvpp[i], ZPOOL_CONFIG_GUID);

		if (guid == target_guid)
			return (nvpp[i]);
	}

	return (NULL);
}

static void
spa_vdev_remove_aux(nvlist_t *config, char *name, nvlist_t **dev, int count,
    nvlist_t *dev_to_remove)
{
	nvlist_t **newdev = NULL;

	if (count > 1)
		newdev = kmem_alloc((count - 1) * sizeof (void *), KM_SLEEP);

	for (int i = 0, j = 0; i < count; i++) {
		if (dev[i] == dev_to_remove)
			continue;
		VERIFY(nvlist_dup(dev[i], &newdev[j++], KM_SLEEP) == 0);
	}

	VERIFY(nvlist_remove(config, name, DATA_TYPE_NVLIST_ARRAY) == 0);
	VERIFY(nvlist_add_nvlist_array(config, name, newdev, count - 1) == 0);

	for (int i = 0; i < count - 1; i++)
		nvlist_free(newdev[i]);

	if (count > 1)
		kmem_free(newdev, (count - 1) * sizeof (void *));
}

static spa_vdev_removal_t *
spa_vdev_removal_create(vdev_t *vd)
{
	spa_vdev_removal_t *svr = kmem_zalloc(sizeof (*svr), KM_SLEEP);
	mutex_init(&svr->svr_lock, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&svr->svr_cv, NULL, CV_DEFAULT, NULL);
	svr->svr_allocd_segs = range_tree_create(NULL, NULL);
	svr->svr_vdev_id = vd->vdev_id;

	for (int i = 0; i < TXG_SIZE; i++) {
		svr->svr_frees[i] = range_tree_create(NULL, NULL);
		list_create(&svr->svr_new_segments[i],
		    sizeof (vdev_indirect_mapping_entry_t),
		    offsetof(vdev_indirect_mapping_entry_t, vime_node));
	}

	return (svr);
}

void
spa_vdev_removal_destroy(spa_vdev_removal_t *svr)
{
	for (int i = 0; i < TXG_SIZE; i++) {
		ASSERT0(svr->svr_bytes_done[i]);
		ASSERT0(svr->svr_max_offset_to_sync[i]);
		range_tree_destroy(svr->svr_frees[i]);
		list_destroy(&svr->svr_new_segments[i]);
	}

	range_tree_destroy(svr->svr_allocd_segs);
	mutex_destroy(&svr->svr_lock);
	cv_destroy(&svr->svr_cv);
	kmem_free(svr, sizeof (*svr));
}

/*
 * This is called as a synctask in the txg in which we will mark this vdev
 * as removing (in the config stored in the MOS).
 *
 * It begins the evacuation of a toplevel vdev by:
 * - initializing the spa_removing_phys which tracks this removal
 * - computing the amount of space to remove for accounting purposes
 * - dirtying all dbufs in the spa_config_object
 * - creating the spa_vdev_removal
 * - starting the spa_vdev_remove_thread
 */
static void
vdev_remove_initiate_sync(void *arg, dmu_tx_t *tx)
{
	int vdev_id = (uintptr_t)arg;
	spa_t *spa = dmu_tx_pool(tx)->dp_spa;
	vdev_t *vd = vdev_lookup_top(spa, vdev_id);
	vdev_indirect_config_t *vic = &vd->vdev_indirect_config;
	objset_t *mos = spa->spa_dsl_pool->dp_meta_objset;
	spa_vdev_removal_t *svr = NULL;
	uint64_t txg = dmu_tx_get_txg(tx);

	ASSERT3P(vd->vdev_ops, !=, &vdev_raidz_ops);
	svr = spa_vdev_removal_create(vd);

	ASSERT(vd->vdev_removing);
	ASSERT3P(vd->vdev_indirect_mapping, ==, NULL);

	spa_feature_incr(spa, SPA_FEATURE_DEVICE_REMOVAL, tx);
	if (spa_feature_is_enabled(spa, SPA_FEATURE_OBSOLETE_COUNTS)) {
		/*
		 * By activating the OBSOLETE_COUNTS feature, we prevent
		 * the pool from being downgraded and ensure that the
		 * refcounts are precise.
		 */
		spa_feature_incr(spa, SPA_FEATURE_OBSOLETE_COUNTS, tx);
		uint64_t one = 1;
		VERIFY0(zap_add(spa->spa_meta_objset, vd->vdev_top_zap,
		    VDEV_TOP_ZAP_OBSOLETE_COUNTS_ARE_PRECISE, sizeof (one), 1,
		    &one, tx));
		ASSERT3U(vdev_obsolete_counts_are_precise(vd), !=, 0);
	}

	vic->vic_mapping_object = vdev_indirect_mapping_alloc(mos, tx);
	vd->vdev_indirect_mapping =
	    vdev_indirect_mapping_open(mos, vic->vic_mapping_object);
	vic->vic_births_object = vdev_indirect_births_alloc(mos, tx);
	vd->vdev_indirect_births =
	    vdev_indirect_births_open(mos, vic->vic_births_object);
	spa->spa_removing_phys.sr_removing_vdev = vd->vdev_id;
	spa->spa_removing_phys.sr_start_time = gethrestime_sec();
	spa->spa_removing_phys.sr_end_time = 0;
	spa->spa_removing_phys.sr_state = DSS_SCANNING;
	spa->spa_removing_phys.sr_to_copy = 0;
	spa->spa_removing_phys.sr_copied = 0;

	/*
	 * Note: We can't use vdev_stat's vs_alloc for sr_to_copy, because
	 * there may be space in the defer tree, which is free, but still
	 * counted in vs_alloc.
	 */
	for (uint64_t i = 0; i < vd->vdev_ms_count; i++) {
		metaslab_t *ms = vd->vdev_ms[i];
		if (ms->ms_sm == NULL)
			continue;

		/*
		 * Sync tasks happen before metaslab_sync(), therefore
		 * smp_alloc and sm_alloc must be the same.
		 */
		ASSERT3U(space_map_allocated(ms->ms_sm), ==,
		    ms->ms_sm->sm_phys->smp_alloc);

		spa->spa_removing_phys.sr_to_copy +=
		    space_map_allocated(ms->ms_sm);

		/*
		 * Space which we are freeing this txg does not need to
		 * be copied.
		 */
		spa->spa_removing_phys.sr_to_copy -=
		    range_tree_space(ms->ms_freeing);

		ASSERT0(range_tree_space(ms->ms_freed));
		for (int t = 0; t < TXG_SIZE; t++)
			ASSERT0(range_tree_space(ms->ms_allocating[t]));
	}

	/*
	 * Sync tasks are called before metaslab_sync(), so there should
	 * be no already-synced metaslabs in the TXG_CLEAN list.
	 */
	ASSERT3P(txg_list_head(&vd->vdev_ms_list, TXG_CLEAN(txg)), ==, NULL);

	spa_sync_removing_state(spa, tx);

	/*
	 * All blocks that we need to read the most recent mapping must be
	 * stored on concrete vdevs.  Therefore, we must dirty anything that
	 * is read before spa_remove_init().  Specifically, the
	 * spa_config_object.  (Note that although we already modified the
	 * spa_config_object in spa_sync_removing_state, that may not have
	 * modified all blocks of the object.)
	 */
	dmu_object_info_t doi;
	VERIFY0(dmu_object_info(mos, DMU_POOL_DIRECTORY_OBJECT, &doi));
	for (uint64_t offset = 0; offset < doi.doi_max_offset; ) {
		dmu_buf_t *dbuf;
		VERIFY0(dmu_buf_hold(mos, DMU_POOL_DIRECTORY_OBJECT,
		    offset, FTAG, &dbuf, 0));
		dmu_buf_will_dirty(dbuf, tx);
		offset += dbuf->db_size;
		dmu_buf_rele(dbuf, FTAG);
	}

	/*
	 * Now that we've allocated the im_object, dirty the vdev to ensure
	 * that the object gets written to the config on disk.
	 */
	vdev_config_dirty(vd);

	zfs_dbgmsg("starting removal thread for vdev %llu (%p) in txg %llu "
	    "im_obj=%llu", vd->vdev_id, vd, dmu_tx_get_txg(tx),
	    vic->vic_mapping_object);

	spa_history_log_internal(spa, "vdev remove started", tx,
	    "%s vdev %llu %s", spa_name(spa), vd->vdev_id,
	    (vd->vdev_path != NULL) ? vd->vdev_path : "-");
	/*
	 * Setting spa_vdev_removal causes subsequent frees to call
	 * free_from_removing_vdev().  Note that we don't need any locking
	 * because we are the sync thread, and metaslab_free_impl() is only
	 * called from syncing context (potentially from a zio taskq thread,
	 * but in any case only when there are outstanding free i/os, which
	 * there are not).
	 */
	ASSERT3P(spa->spa_vdev_removal, ==, NULL);
	spa->spa_vdev_removal = svr;
	svr->svr_thread = thread_create(NULL, 0,
	    spa_vdev_remove_thread, spa, 0, &p0, TS_RUN, minclsyspri);
}

/*
 * When we are opening a pool, we must read the mapping for each
 * indirect vdev in order from most recently removed to least
 * recently removed.  We do this because the blocks for the mapping
 * of older indirect vdevs may be stored on more recently removed vdevs.
 * In order to read each indirect mapping object, we must have
 * initialized all more recently removed vdevs.
 */
int
spa_remove_init(spa_t *spa)
{
	int error;

	error = zap_lookup(spa->spa_dsl_pool->dp_meta_objset,
	    DMU_POOL_DIRECTORY_OBJECT,
	    DMU_POOL_REMOVING, sizeof (uint64_t),
	    sizeof (spa->spa_removing_phys) / sizeof (uint64_t),
	    &spa->spa_removing_phys);

	if (error == ENOENT) {
		spa->spa_removing_phys.sr_state = DSS_NONE;
		spa->spa_removing_phys.sr_removing_vdev = -1;
		spa->spa_removing_phys.sr_prev_indirect_vdev = -1;
		spa->spa_indirect_vdevs_loaded = B_TRUE;
		return (0);
	} else if (error != 0) {
		return (error);
	}

	if (spa->spa_removing_phys.sr_state == DSS_SCANNING) {
		/*
		 * We are currently removing a vdev.  Create and
		 * initialize a spa_vdev_removal_t from the bonus
		 * buffer of the removing vdevs vdev_im_object, and
		 * initialize its partial mapping.
		 */
		spa_config_enter(spa, SCL_STATE, FTAG, RW_READER);
		vdev_t *vd = vdev_lookup_top(spa,
		    spa->spa_removing_phys.sr_removing_vdev);

		if (vd == NULL) {
			spa_config_exit(spa, SCL_STATE, FTAG);
			return (EINVAL);
		}

		vdev_indirect_config_t *vic = &vd->vdev_indirect_config;

		ASSERT(vdev_is_concrete(vd));
		spa_vdev_removal_t *svr = spa_vdev_removal_create(vd);
		ASSERT3U(svr->svr_vdev_id, ==, vd->vdev_id);
		ASSERT(vd->vdev_removing);

		vd->vdev_indirect_mapping = vdev_indirect_mapping_open(
		    spa->spa_meta_objset, vic->vic_mapping_object);
		vd->vdev_indirect_births = vdev_indirect_births_open(
		    spa->spa_meta_objset, vic->vic_births_object);
		spa_config_exit(spa, SCL_STATE, FTAG);

		spa->spa_vdev_removal = svr;
	}

	spa_config_enter(spa, SCL_STATE, FTAG, RW_READER);
	uint64_t indirect_vdev_id =
	    spa->spa_removing_phys.sr_prev_indirect_vdev;
	while (indirect_vdev_id != UINT64_MAX) {
		vdev_t *vd = vdev_lookup_top(spa, indirect_vdev_id);
		vdev_indirect_config_t *vic = &vd->vdev_indirect_config;

		ASSERT3P(vd->vdev_ops, ==, &vdev_indirect_ops);
		vd->vdev_indirect_mapping = vdev_indirect_mapping_open(
		    spa->spa_meta_objset, vic->vic_mapping_object);
		vd->vdev_indirect_births = vdev_indirect_births_open(
		    spa->spa_meta_objset, vic->vic_births_object);

		indirect_vdev_id = vic->vic_prev_indirect_vdev;
	}
	spa_config_exit(spa, SCL_STATE, FTAG);

	/*
	 * Now that we've loaded all the indirect mappings, we can allow
	 * reads from other blocks (e.g. via predictive prefetch).
	 */
	spa->spa_indirect_vdevs_loaded = B_TRUE;
	return (0);
}

void
spa_restart_removal(spa_t *spa)
{
	spa_vdev_removal_t *svr = spa->spa_vdev_removal;

	if (svr == NULL)
		return;

	/*
	 * In general when this function is called there is no
	 * removal thread running. The only scenario where this
	 * is not true is during spa_import() where this function
	 * is called twice [once from spa_import_impl() and
	 * spa_async_resume()]. Thus, in the scenario where we
	 * import a pool that has an ongoing removal we don't
	 * want to spawn a second thread.
	 */
	if (svr->svr_thread != NULL)
		return;

	if (!spa_writeable(spa))
		return;

	zfs_dbgmsg("restarting removal of %llu", svr->svr_vdev_id);
	svr->svr_thread = thread_create(NULL, 0, spa_vdev_remove_thread, spa,
	    0, &p0, TS_RUN, minclsyspri);
}

/*
 * Process freeing from a device which is in the middle of being removed.
 * We must handle this carefully so that we attempt to copy freed data,
 * and we correctly free already-copied data.
 */
void
free_from_removing_vdev(vdev_t *vd, uint64_t offset, uint64_t size)
{
	spa_t *spa = vd->vdev_spa;
	spa_vdev_removal_t *svr = spa->spa_vdev_removal;
	vdev_indirect_mapping_t *vim = vd->vdev_indirect_mapping;
	uint64_t txg = spa_syncing_txg(spa);
	uint64_t max_offset_yet = 0;

	ASSERT(vd->vdev_indirect_config.vic_mapping_object != 0);
	ASSERT3U(vd->vdev_indirect_config.vic_mapping_object, ==,
	    vdev_indirect_mapping_object(vim));
	ASSERT3U(vd->vdev_id, ==, svr->svr_vdev_id);

	mutex_enter(&svr->svr_lock);

	/*
	 * Remove the segment from the removing vdev's spacemap.  This
	 * ensures that we will not attempt to copy this space (if the
	 * removal thread has not yet visited it), and also ensures
	 * that we know what is actually allocated on the new vdevs
	 * (needed if we cancel the removal).
	 *
	 * Note: we must do the metaslab_free_concrete() with the svr_lock
	 * held, so that the remove_thread can not load this metaslab and then
	 * visit this offset between the time that we metaslab_free_concrete()
	 * and when we check to see if it has been visited.
	 *
	 * Note: The checkpoint flag is set to false as having/taking
	 * a checkpoint and removing a device can't happen at the same
	 * time.
	 */
	ASSERT(!spa_has_checkpoint(spa));
	metaslab_free_concrete(vd, offset, size, B_FALSE);

	uint64_t synced_size = 0;
	uint64_t synced_offset = 0;
	uint64_t max_offset_synced = vdev_indirect_mapping_max_offset(vim);
	if (offset < max_offset_synced) {
		/*
		 * The mapping for this offset is already on disk.
		 * Free from the new location.
		 *
		 * Note that we use svr_max_synced_offset because it is
		 * updated atomically with respect to the in-core mapping.
		 * By contrast, vim_max_offset is not.
		 *
		 * This block may be split between a synced entry and an
		 * in-flight or unvisited entry.  Only process the synced
		 * portion of it here.
		 */
		synced_size = MIN(size, max_offset_synced - offset);
		synced_offset = offset;

		ASSERT3U(max_offset_yet, <=, max_offset_synced);
		max_offset_yet = max_offset_synced;

		DTRACE_PROBE3(remove__free__synced,
		    spa_t *, spa,
		    uint64_t, offset,
		    uint64_t, synced_size);

		size -= synced_size;
		offset += synced_size;
	}

	/*
	 * Look at all in-flight txgs starting from the currently syncing one
	 * and see if a section of this free is being copied. By starting from
	 * this txg and iterating forward, we might find that this region
	 * was copied in two different txgs and handle it appropriately.
	 */
	for (int i = 0; i < TXG_CONCURRENT_STATES; i++) {
		int txgoff = (txg + i) & TXG_MASK;
		if (size > 0 && offset < svr->svr_max_offset_to_sync[txgoff]) {
			/*
			 * The mapping for this offset is in flight, and
			 * will be synced in txg+i.
			 */
			uint64_t inflight_size = MIN(size,
			    svr->svr_max_offset_to_sync[txgoff] - offset);

			DTRACE_PROBE4(remove__free__inflight,
			    spa_t *, spa,
			    uint64_t, offset,
			    uint64_t, inflight_size,
			    uint64_t, txg + i);

			/*
			 * We copy data in order of increasing offset.
			 * Therefore the max_offset_to_sync[] must increase
			 * (or be zero, indicating that nothing is being
			 * copied in that txg).
			 */
			if (svr->svr_max_offset_to_sync[txgoff] != 0) {
				ASSERT3U(svr->svr_max_offset_to_sync[txgoff],
				    >=, max_offset_yet);
				max_offset_yet =
				    svr->svr_max_offset_to_sync[txgoff];
			}

			/*
			 * We've already committed to copying this segment:
			 * we have allocated space elsewhere in the pool for
			 * it and have an IO outstanding to copy the data. We
			 * cannot free the space before the copy has
			 * completed, or else the copy IO might overwrite any
			 * new data. To free that space, we record the
			 * segment in the appropriate svr_frees tree and free
			 * the mapped space later, in the txg where we have
			 * completed the copy and synced the mapping (see
			 * vdev_mapping_sync).
			 */
			range_tree_add(svr->svr_frees[txgoff],
			    offset, inflight_size);
			size -= inflight_size;
			offset += inflight_size;

			/*
			 * This space is already accounted for as being
			 * done, because it is being copied in txg+i.
			 * However, if i!=0, then it is being copied in
			 * a future txg.  If we crash after this txg
			 * syncs but before txg+i syncs, then the space
			 * will be free.  Therefore we must account
			 * for the space being done in *this* txg
			 * (when it is freed) rather than the future txg
			 * (when it will be copied).
			 */
			ASSERT3U(svr->svr_bytes_done[txgoff], >=,
			    inflight_size);
			svr->svr_bytes_done[txgoff] -= inflight_size;
			svr->svr_bytes_done[txg & TXG_MASK] += inflight_size;
		}
	}
	ASSERT0(svr->svr_max_offset_to_sync[TXG_CLEAN(txg) & TXG_MASK]);

	if (size > 0) {
		/*
		 * The copy thread has not yet visited this offset.  Ensure
		 * that it doesn't.
		 */

		DTRACE_PROBE3(remove__free__unvisited,
		    spa_t *, spa,
		    uint64_t, offset,
		    uint64_t, size);

		if (svr->svr_allocd_segs != NULL)
			range_tree_clear(svr->svr_allocd_segs, offset, size);

		/*
		 * Since we now do not need to copy this data, for
		 * accounting purposes we have done our job and can count
		 * it as completed.
		 */
		svr->svr_bytes_done[txg & TXG_MASK] += size;
	}
	mutex_exit(&svr->svr_lock);

	/*
	 * Now that we have dropped svr_lock, process the synced portion
	 * of this free.
	 */
	if (synced_size > 0) {
		vdev_indirect_mark_obsolete(vd, synced_offset, synced_size);

		/*
		 * Note: this can only be called from syncing context,
		 * and the vdev_indirect_mapping is only changed from the
		 * sync thread, so we don't need svr_lock while doing
		 * metaslab_free_impl_cb.
		 */
		boolean_t checkpoint = B_FALSE;
		vdev_indirect_ops.vdev_op_remap(vd, synced_offset, synced_size,
		    metaslab_free_impl_cb, &checkpoint);
	}
}

/*
 * Stop an active removal and update the spa_removing phys.
 */
static void
spa_finish_removal(spa_t *spa, dsl_scan_state_t state, dmu_tx_t *tx)
{
	spa_vdev_removal_t *svr = spa->spa_vdev_removal;
	ASSERT3U(dmu_tx_get_txg(tx), ==, spa_syncing_txg(spa));

	/* Ensure the removal thread has completed before we free the svr. */
	spa_vdev_remove_suspend(spa);

	ASSERT(state == DSS_FINISHED || state == DSS_CANCELED);

	if (state == DSS_FINISHED) {
		spa_removing_phys_t *srp = &spa->spa_removing_phys;
		vdev_t *vd = vdev_lookup_top(spa, svr->svr_vdev_id);
		vdev_indirect_config_t *vic = &vd->vdev_indirect_config;

		if (srp->sr_prev_indirect_vdev != UINT64_MAX) {
			vdev_t *pvd = vdev_lookup_top(spa,
			    srp->sr_prev_indirect_vdev);
			ASSERT3P(pvd->vdev_ops, ==, &vdev_indirect_ops);
		}

		vic->vic_prev_indirect_vdev = srp->sr_prev_indirect_vdev;
		srp->sr_prev_indirect_vdev = vd->vdev_id;
	}
	spa->spa_removing_phys.sr_state = state;
	spa->spa_removing_phys.sr_end_time = gethrestime_sec();

	spa->spa_vdev_removal = NULL;
	spa_vdev_removal_destroy(svr);

	spa_sync_removing_state(spa, tx);

	vdev_config_dirty(spa->spa_root_vdev);
}

static void
free_mapped_segment_cb(void *arg, uint64_t offset, uint64_t size)
{
	vdev_t *vd = arg;
	vdev_indirect_mark_obsolete(vd, offset, size);
	boolean_t checkpoint = B_FALSE;
	vdev_indirect_ops.vdev_op_remap(vd, offset, size,
	    metaslab_free_impl_cb, &checkpoint);
}

/*
 * On behalf of the removal thread, syncs an incremental bit more of
 * the indirect mapping to disk and updates the in-memory mapping.
 * Called as a sync task in every txg that the removal thread makes progress.
 */
static void
vdev_mapping_sync(void *arg, dmu_tx_t *tx)
{
	spa_vdev_removal_t *svr = arg;
	spa_t *spa = dmu_tx_pool(tx)->dp_spa;
	vdev_t *vd = vdev_lookup_top(spa, svr->svr_vdev_id);
	vdev_indirect_config_t *vic = &vd->vdev_indirect_config;
	uint64_t txg = dmu_tx_get_txg(tx);
	vdev_indirect_mapping_t *vim = vd->vdev_indirect_mapping;

	ASSERT(vic->vic_mapping_object != 0);
	ASSERT3U(txg, ==, spa_syncing_txg(spa));

	vdev_indirect_mapping_add_entries(vim,
	    &svr->svr_new_segments[txg & TXG_MASK], tx);
	vdev_indirect_births_add_entry(vd->vdev_indirect_births,
	    vdev_indirect_mapping_max_offset(vim), dmu_tx_get_txg(tx), tx);

	/*
	 * Free the copied data for anything that was freed while the
	 * mapping entries were in flight.
	 */
	mutex_enter(&svr->svr_lock);
	range_tree_vacate(svr->svr_frees[txg & TXG_MASK],
	    free_mapped_segment_cb, vd);
	ASSERT3U(svr->svr_max_offset_to_sync[txg & TXG_MASK], >=,
	    vdev_indirect_mapping_max_offset(vim));
	svr->svr_max_offset_to_sync[txg & TXG_MASK] = 0;
	mutex_exit(&svr->svr_lock);

	spa_sync_removing_state(spa, tx);
}

typedef struct vdev_copy_segment_arg {
	spa_t *vcsa_spa;
	dva_t *vcsa_dest_dva;
	uint64_t vcsa_txg;
	range_tree_t *vcsa_obsolete_segs;
} vdev_copy_segment_arg_t;

static void
unalloc_seg(void *arg, uint64_t start, uint64_t size)
{
	vdev_copy_segment_arg_t *vcsa = arg;
	spa_t *spa = vcsa->vcsa_spa;
	blkptr_t bp = { 0 };

	BP_SET_BIRTH(&bp, TXG_INITIAL, TXG_INITIAL);
	BP_SET_LSIZE(&bp, size);
	BP_SET_PSIZE(&bp, size);
	BP_SET_COMPRESS(&bp, ZIO_COMPRESS_OFF);
	BP_SET_CHECKSUM(&bp, ZIO_CHECKSUM_OFF);
	BP_SET_TYPE(&bp, DMU_OT_NONE);
	BP_SET_LEVEL(&bp, 0);
	BP_SET_DEDUP(&bp, 0);
	BP_SET_BYTEORDER(&bp, ZFS_HOST_BYTEORDER);

	DVA_SET_VDEV(&bp.blk_dva[0], DVA_GET_VDEV(vcsa->vcsa_dest_dva));
	DVA_SET_OFFSET(&bp.blk_dva[0],
	    DVA_GET_OFFSET(vcsa->vcsa_dest_dva) + start);
	DVA_SET_ASIZE(&bp.blk_dva[0], size);

	zio_free(spa, vcsa->vcsa_txg, &bp);
}

/*
 * All reads and writes associated with a call to spa_vdev_copy_segment()
 * are done.
 */
static void
spa_vdev_copy_segment_done(zio_t *zio)
{
	vdev_copy_segment_arg_t *vcsa = zio->io_private;

	range_tree_vacate(vcsa->vcsa_obsolete_segs,
	    unalloc_seg, vcsa);
	range_tree_destroy(vcsa->vcsa_obsolete_segs);
	kmem_free(vcsa, sizeof (*vcsa));

	spa_config_exit(zio->io_spa, SCL_STATE, zio->io_spa);
}

/*
 * The write of the new location is done.
 */
static void
spa_vdev_copy_segment_write_done(zio_t *zio)
{
	vdev_copy_arg_t *vca = zio->io_private;

	abd_free(zio->io_abd);

	mutex_enter(&vca->vca_lock);
	vca->vca_outstanding_bytes -= zio->io_size;
	cv_signal(&vca->vca_cv);
	mutex_exit(&vca->vca_lock);
}

/*
 * The read of the old location is done.  The parent zio is the write to
 * the new location.  Allow it to start.
 */
static void
spa_vdev_copy_segment_read_done(zio_t *zio)
{
	zio_nowait(zio_unique_parent(zio));
}

/*
 * If the old and new vdevs are mirrors, we will read both sides of the old
 * mirror, and write each copy to the corresponding side of the new mirror.
 * If the old and new vdevs have a different number of children, we will do
 * this as best as possible.  Since we aren't verifying checksums, this
 * ensures that as long as there's a good copy of the data, we'll have a
 * good copy after the removal, even if there's silent damage to one side
 * of the mirror. If we're removing a mirror that has some silent damage,
 * we'll have exactly the same damage in the new location (assuming that
 * the new location is also a mirror).
 *
 * We accomplish this by creating a tree of zio_t's, with as many writes as
 * there are "children" of the new vdev (a non-redundant vdev counts as one
 * child, a 2-way mirror has 2 children, etc). Each write has an associated
 * read from a child of the old vdev. Typically there will be the same
 * number of children of the old and new vdevs.  However, if there are more
 * children of the new vdev, some child(ren) of the old vdev will be issued
 * multiple reads.  If there are more children of the old vdev, some copies
 * will be dropped.
 *
 * For example, the tree of zio_t's for a 2-way mirror is:
 *
 *                            null
 *                           /    \
 *    write(new vdev, child 0)      write(new vdev, child 1)
 *      |                             |
 *    read(old vdev, child 0)       read(old vdev, child 1)
 *
 * Child zio's complete before their parents complete.  However, zio's
 * created with zio_vdev_child_io() may be issued before their children
 * complete.  In this case we need to make sure that the children (reads)
 * complete before the parents (writes) are *issued*.  We do this by not
 * calling zio_nowait() on each write until its corresponding read has
 * completed.
 *
 * The spa_config_lock must be held while zio's created by
 * zio_vdev_child_io() are in progress, to ensure that the vdev tree does
 * not change (e.g. due to a concurrent "zpool attach/detach"). The "null"
 * zio is needed to release the spa_config_lock after all the reads and
 * writes complete. (Note that we can't grab the config lock for each read,
 * because it is not reentrant - we could deadlock with a thread waiting
 * for a write lock.)
 */
static void
spa_vdev_copy_one_child(vdev_copy_arg_t *vca, zio_t *nzio,
    vdev_t *source_vd, uint64_t source_offset,
    vdev_t *dest_child_vd, uint64_t dest_offset, int dest_id, uint64_t size)
{
	ASSERT3U(spa_config_held(nzio->io_spa, SCL_ALL, RW_READER), !=, 0);

	mutex_enter(&vca->vca_lock);
	vca->vca_outstanding_bytes += size;
	mutex_exit(&vca->vca_lock);

	abd_t *abd = abd_alloc_for_io(size, B_FALSE);

	vdev_t *source_child_vd;
	if (source_vd->vdev_ops == &vdev_mirror_ops && dest_id != -1) {
		/*
		 * Source and dest are both mirrors.  Copy from the same
		 * child id as we are copying to (wrapping around if there
		 * are more dest children than source children).
		 */
		source_child_vd =
		    source_vd->vdev_child[dest_id % source_vd->vdev_children];
	} else {
		source_child_vd = source_vd;
	}

	zio_t *write_zio = zio_vdev_child_io(nzio, NULL,
	    dest_child_vd, dest_offset, abd, size,
	    ZIO_TYPE_WRITE, ZIO_PRIORITY_REMOVAL,
	    ZIO_FLAG_CANFAIL,
	    spa_vdev_copy_segment_write_done, vca);

	zio_nowait(zio_vdev_child_io(write_zio, NULL,
	    source_child_vd, source_offset, abd, size,
	    ZIO_TYPE_READ, ZIO_PRIORITY_REMOVAL,
	    ZIO_FLAG_CANFAIL,
	    spa_vdev_copy_segment_read_done, vca));
}

/*
 * Allocate a new location for this segment, and create the zio_t's to
 * read from the old location and write to the new location.
 */
static int
spa_vdev_copy_segment(vdev_t *vd, range_tree_t *segs,
    uint64_t maxalloc, uint64_t txg,
    vdev_copy_arg_t *vca, zio_alloc_list_t *zal)
{
	metaslab_group_t *mg = vd->vdev_mg;
	spa_t *spa = vd->vdev_spa;
	spa_vdev_removal_t *svr = spa->spa_vdev_removal;
	vdev_indirect_mapping_entry_t *entry;
	dva_t dst = { 0 };
	uint64_t start = range_tree_min(segs);

	ASSERT3U(maxalloc, <=, SPA_MAXBLOCKSIZE);

	uint64_t size = range_tree_span(segs);
	if (range_tree_span(segs) > maxalloc) {
		/*
		 * We can't allocate all the segments.  Prefer to end
		 * the allocation at the end of a segment, thus avoiding
		 * additional split blocks.
		 */
		range_seg_t search;
		avl_index_t where;
		search.rs_start = start + maxalloc;
		search.rs_end = search.rs_start;
		range_seg_t *rs = avl_find(&segs->rt_root, &search, &where);
		if (rs == NULL) {
			rs = avl_nearest(&segs->rt_root, where, AVL_BEFORE);
		} else {
			rs = AVL_PREV(&segs->rt_root, rs);
		}
		if (rs != NULL) {
			size = rs->rs_end - start;
		} else {
			/*
			 * There are no segments that end before maxalloc.
			 * I.e. the first segment is larger than maxalloc,
			 * so we must split it.
			 */
			size = maxalloc;
		}
	}
	ASSERT3U(size, <=, maxalloc);

	/*
	 * We use allocator 0 for this I/O because we don't expect device remap
	 * to be the steady state of the system, so parallelizing is not as
	 * critical as it is for other allocation types. We also want to ensure
	 * that the IOs are allocated together as much as possible, to reduce
	 * mapping sizes.
	 */
	int error = metaslab_alloc_dva(spa, mg->mg_class, size,
	    &dst, 0, NULL, txg, 0, zal, 0);
	if (error != 0)
		return (error);

	/*
	 * Determine the ranges that are not actually needed.  Offsets are
	 * relative to the start of the range to be copied (i.e. relative to the
	 * local variable "start").
	 */
	range_tree_t *obsolete_segs = range_tree_create(NULL, NULL);

	range_seg_t *rs = avl_first(&segs->rt_root);
	ASSERT3U(rs->rs_start, ==, start);
	uint64_t prev_seg_end = rs->rs_end;
	while ((rs = AVL_NEXT(&segs->rt_root, rs)) != NULL) {
		if (rs->rs_start >= start + size) {
			break;
		} else {
			range_tree_add(obsolete_segs,
			    prev_seg_end - start,
			    rs->rs_start - prev_seg_end);
		}
		prev_seg_end = rs->rs_end;
	}
	/* We don't end in the middle of an obsolete range */
	ASSERT3U(start + size, <=, prev_seg_end);

	range_tree_clear(segs, start, size);

	/*
	 * We can't have any padding of the allocated size, otherwise we will
	 * misunderstand what's allocated, and the size of the mapping.
	 * The caller ensures this will be true by passing in a size that is
	 * aligned to the worst (highest) ashift in the pool.
	 */
	ASSERT3U(DVA_GET_ASIZE(&dst), ==, size);

	entry = kmem_zalloc(sizeof (vdev_indirect_mapping_entry_t), KM_SLEEP);
	DVA_MAPPING_SET_SRC_OFFSET(&entry->vime_mapping, start);
	entry->vime_mapping.vimep_dst = dst;
	if (spa_feature_is_enabled(spa, SPA_FEATURE_OBSOLETE_COUNTS)) {
		entry->vime_obsolete_count = range_tree_space(obsolete_segs);
	}

	vdev_copy_segment_arg_t *vcsa = kmem_zalloc(sizeof (*vcsa), KM_SLEEP);
	vcsa->vcsa_dest_dva = &entry->vime_mapping.vimep_dst;
	vcsa->vcsa_obsolete_segs = obsolete_segs;
	vcsa->vcsa_spa = spa;
	vcsa->vcsa_txg = txg;

	/*
	 * See comment before spa_vdev_copy_one_child().
	 */
	spa_config_enter(spa, SCL_STATE, spa, RW_READER);
	zio_t *nzio = zio_null(spa->spa_txg_zio[txg & TXG_MASK], spa, NULL,
	    spa_vdev_copy_segment_done, vcsa, 0);
	vdev_t *dest_vd = vdev_lookup_top(spa, DVA_GET_VDEV(&dst));
	if (dest_vd->vdev_ops == &vdev_mirror_ops) {
		for (int i = 0; i < dest_vd->vdev_children; i++) {
			vdev_t *child = dest_vd->vdev_child[i];
			spa_vdev_copy_one_child(vca, nzio, vd, start,
			    child, DVA_GET_OFFSET(&dst), i, size);
		}
	} else {
		spa_vdev_copy_one_child(vca, nzio, vd, start,
		    dest_vd, DVA_GET_OFFSET(&dst), -1, size);
	}
	zio_nowait(nzio);

	list_insert_tail(&svr->svr_new_segments[txg & TXG_MASK], entry);
	ASSERT3U(start + size, <=, vd->vdev_ms_count << vd->vdev_ms_shift);
	vdev_dirty(vd, 0, NULL, txg);

	return (0);
}

/*
 * Complete the removal of a toplevel vdev. This is called as a
 * synctask in the same txg that we will sync out the new config (to the
 * MOS object) which indicates that this vdev is indirect.
 */
static void
vdev_remove_complete_sync(void *arg, dmu_tx_t *tx)
{
	spa_vdev_removal_t *svr = arg;
	spa_t *spa = dmu_tx_pool(tx)->dp_spa;
	vdev_t *vd = vdev_lookup_top(spa, svr->svr_vdev_id);

	ASSERT3P(vd->vdev_ops, ==, &vdev_indirect_ops);

	for (int i = 0; i < TXG_SIZE; i++) {
		ASSERT0(svr->svr_bytes_done[i]);
	}

	ASSERT3U(spa->spa_removing_phys.sr_copied, ==,
	    spa->spa_removing_phys.sr_to_copy);

	vdev_destroy_spacemaps(vd, tx);

	/* destroy leaf zaps, if any */
	ASSERT3P(svr->svr_zaplist, !=, NULL);
	for (nvpair_t *pair = nvlist_next_nvpair(svr->svr_zaplist, NULL);
	    pair != NULL;
	    pair = nvlist_next_nvpair(svr->svr_zaplist, pair)) {
		vdev_destroy_unlink_zap(vd, fnvpair_value_uint64(pair), tx);
	}
	fnvlist_free(svr->svr_zaplist);

	spa_finish_removal(dmu_tx_pool(tx)->dp_spa, DSS_FINISHED, tx);
	/* vd->vdev_path is not available here */
	spa_history_log_internal(spa, "vdev remove completed",  tx,
	    "%s vdev %llu", spa_name(spa), vd->vdev_id);
}

static void
vdev_remove_enlist_zaps(vdev_t *vd, nvlist_t *zlist)
{
	ASSERT3P(zlist, !=, NULL);
	ASSERT3P(vd->vdev_ops, !=, &vdev_raidz_ops);

	if (vd->vdev_leaf_zap != 0) {
		char zkey[32];
		(void) snprintf(zkey, sizeof (zkey), "%s-%ju",
		    VDEV_REMOVAL_ZAP_OBJS, (uintmax_t)vd->vdev_leaf_zap);
		fnvlist_add_uint64(zlist, zkey, vd->vdev_leaf_zap);
	}

	for (uint64_t id = 0; id < vd->vdev_children; id++) {
		vdev_remove_enlist_zaps(vd->vdev_child[id], zlist);
	}
}

static void
vdev_remove_replace_with_indirect(vdev_t *vd, uint64_t txg)
{
	vdev_t *ivd;
	dmu_tx_t *tx;
	spa_t *spa = vd->vdev_spa;
	spa_vdev_removal_t *svr = spa->spa_vdev_removal;

	/*
	 * First, build a list of leaf zaps to be destroyed.
	 * This is passed to the sync context thread,
	 * which does the actual unlinking.
	 */
	svr->svr_zaplist = fnvlist_alloc();
	vdev_remove_enlist_zaps(vd, svr->svr_zaplist);

	ivd = vdev_add_parent(vd, &vdev_indirect_ops);
	ivd->vdev_removing = 0;

	vd->vdev_leaf_zap = 0;

	vdev_remove_child(ivd, vd);
	vdev_compact_children(ivd);

	ASSERT(!list_link_active(&vd->vdev_state_dirty_node));

	tx = dmu_tx_create_assigned(spa->spa_dsl_pool, txg);
	dsl_sync_task_nowait(spa->spa_dsl_pool, vdev_remove_complete_sync, svr,
	    0, ZFS_SPACE_CHECK_NONE, tx);
	dmu_tx_commit(tx);

	/*
	 * Indicate that this thread has exited.
	 * After this, we can not use svr.
	 */
	mutex_enter(&svr->svr_lock);
	svr->svr_thread = NULL;
	cv_broadcast(&svr->svr_cv);
	mutex_exit(&svr->svr_lock);
}

/*
 * Complete the removal of a toplevel vdev. This is called in open
 * context by the removal thread after we have copied all vdev's data.
 */
static void
vdev_remove_complete(spa_t *spa)
{
	uint64_t txg;

	/*
	 * Wait for any deferred frees to be synced before we call
	 * vdev_metaslab_fini()
	 */
	txg_wait_synced(spa->spa_dsl_pool, 0);
	txg = spa_vdev_enter(spa);
	vdev_t *vd = vdev_lookup_top(spa, spa->spa_vdev_removal->svr_vdev_id);
	ASSERT3P(vd->vdev_initialize_thread, ==, NULL);

	sysevent_t *ev = spa_event_create(spa, vd, NULL,
	    ESC_ZFS_VDEV_REMOVE_DEV);

	zfs_dbgmsg("finishing device removal for vdev %llu in txg %llu",
	    vd->vdev_id, txg);

	/*
	 * Discard allocation state.
	 */
	if (vd->vdev_mg != NULL) {
		vdev_metaslab_fini(vd);
		metaslab_group_destroy(vd->vdev_mg);
		vd->vdev_mg = NULL;
	}
	ASSERT0(vd->vdev_stat.vs_space);
	ASSERT0(vd->vdev_stat.vs_dspace);

	vdev_remove_replace_with_indirect(vd, txg);

	/*
	 * We now release the locks, allowing spa_sync to run and finish the
	 * removal via vdev_remove_complete_sync in syncing context.
	 *
	 * Note that we hold on to the vdev_t that has been replaced.  Since
	 * it isn't part of the vdev tree any longer, it can't be concurrently
	 * manipulated, even while we don't have the config lock.
	 */
	(void) spa_vdev_exit(spa, NULL, txg, 0);

	/*
	 * Top ZAP should have been transferred to the indirect vdev in
	 * vdev_remove_replace_with_indirect.
	 */
	ASSERT0(vd->vdev_top_zap);

	/*
	 * Leaf ZAP should have been moved in vdev_remove_replace_with_indirect.
	 */
	ASSERT0(vd->vdev_leaf_zap);

	txg = spa_vdev_enter(spa);
	(void) vdev_label_init(vd, 0, VDEV_LABEL_REMOVE);
	/*
	 * Request to update the config and the config cachefile.
	 */
	vdev_config_dirty(spa->spa_root_vdev);
	(void) spa_vdev_exit(spa, vd, txg, 0);

	spa_event_post(ev);
}

/*
 * Evacuates a segment of size at most max_alloc from the vdev
 * via repeated calls to spa_vdev_copy_segment. If an allocation
 * fails, the pool is probably too fragmented to handle such a
 * large size, so decrease max_alloc so that the caller will not try
 * this size again this txg.
 */
static void
spa_vdev_copy_impl(vdev_t *vd, spa_vdev_removal_t *svr, vdev_copy_arg_t *vca,
    uint64_t *max_alloc, dmu_tx_t *tx)
{
	uint64_t txg = dmu_tx_get_txg(tx);
	spa_t *spa = dmu_tx_pool(tx)->dp_spa;

	mutex_enter(&svr->svr_lock);

	/*
	 * Determine how big of a chunk to copy.  We can allocate up
	 * to max_alloc bytes, and we can span up to vdev_removal_max_span
	 * bytes of unallocated space at a time.  "segs" will track the
	 * allocated segments that we are copying.  We may also be copying
	 * free segments (of up to vdev_removal_max_span bytes).
	 */
	range_tree_t *segs = range_tree_create(NULL, NULL);
	for (;;) {
		range_seg_t *rs = avl_first(&svr->svr_allocd_segs->rt_root);
		if (rs == NULL)
			break;

		uint64_t seg_length;

		if (range_tree_is_empty(segs)) {
			/* need to truncate the first seg based on max_alloc */
			seg_length =
			    MIN(rs->rs_end - rs->rs_start, *max_alloc);
		} else {
			if (rs->rs_start - range_tree_max(segs) >
			    vdev_removal_max_span) {
				/*
				 * Including this segment would cause us to
				 * copy a larger unneeded chunk than is allowed.
				 */
				break;
			} else if (rs->rs_end - range_tree_min(segs) >
			    *max_alloc) {
				/*
				 * This additional segment would extend past
				 * max_alloc. Rather than splitting this
				 * segment, leave it for the next mapping.
				 */
				break;
			} else {
				seg_length = rs->rs_end - rs->rs_start;
			}
		}

		range_tree_add(segs, rs->rs_start, seg_length);
		range_tree_remove(svr->svr_allocd_segs,
		    rs->rs_start, seg_length);
	}

	if (range_tree_is_empty(segs)) {
		mutex_exit(&svr->svr_lock);
		range_tree_destroy(segs);
		return;
	}

	if (svr->svr_max_offset_to_sync[txg & TXG_MASK] == 0) {
		dsl_sync_task_nowait(dmu_tx_pool(tx), vdev_mapping_sync,
		    svr, 0, ZFS_SPACE_CHECK_NONE, tx);
	}

	svr->svr_max_offset_to_sync[txg & TXG_MASK] = range_tree_max(segs);

	/*
	 * Note: this is the amount of *allocated* space
	 * that we are taking care of each txg.
	 */
	svr->svr_bytes_done[txg & TXG_MASK] += range_tree_space(segs);

	mutex_exit(&svr->svr_lock);

	zio_alloc_list_t zal;
	metaslab_trace_init(&zal);
	uint64_t thismax = SPA_MAXBLOCKSIZE;
	while (!range_tree_is_empty(segs)) {
		int error = spa_vdev_copy_segment(vd,
		    segs, thismax, txg, vca, &zal);

		if (error == ENOSPC) {
			/*
			 * Cut our segment in half, and don't try this
			 * segment size again this txg.  Note that the
			 * allocation size must be aligned to the highest
			 * ashift in the pool, so that the allocation will
			 * not be padded out to a multiple of the ashift,
			 * which could cause us to think that this mapping
			 * is larger than we intended.
			 */
			ASSERT3U(spa->spa_max_ashift, >=, SPA_MINBLOCKSHIFT);
			ASSERT3U(spa->spa_max_ashift, ==, spa->spa_min_ashift);
			uint64_t attempted =
			    MIN(range_tree_span(segs), thismax);
			thismax = P2ROUNDUP(attempted / 2,
			    1 << spa->spa_max_ashift);
			/*
			 * The minimum-size allocation can not fail.
			 */
			ASSERT3U(attempted, >, 1 << spa->spa_max_ashift);
			*max_alloc = attempted - (1 << spa->spa_max_ashift);
		} else {
			ASSERT0(error);

			/*
			 * We've performed an allocation, so reset the
			 * alloc trace list.
			 */
			metaslab_trace_fini(&zal);
			metaslab_trace_init(&zal);
		}
	}
	metaslab_trace_fini(&zal);
	range_tree_destroy(segs);
}

/*
 * The removal thread operates in open context.  It iterates over all
 * allocated space in the vdev, by loading each metaslab's spacemap.
 * For each contiguous segment of allocated space (capping the segment
 * size at SPA_MAXBLOCKSIZE), we:
 *    - Allocate space for it on another vdev.
 *    - Create a new mapping from the old location to the new location
 *      (as a record in svr_new_segments).
 *    - Initiate a logical read zio to get the data off the removing disk.
 *    - In the read zio's done callback, initiate a logical write zio to
 *      write it to the new vdev.
 * Note that all of this will take effect when a particular TXG syncs.
 * The sync thread ensures that all the phys reads and writes for the syncing
 * TXG have completed (see spa_txg_zio) and writes the new mappings to disk
 * (see vdev_mapping_sync()).
 */
static void
spa_vdev_remove_thread(void *arg)
{
	spa_t *spa = arg;
	spa_vdev_removal_t *svr = spa->spa_vdev_removal;
	vdev_copy_arg_t vca;
	uint64_t max_alloc = zfs_remove_max_segment;
	uint64_t last_txg = 0;

	spa_config_enter(spa, SCL_CONFIG, FTAG, RW_READER);
	vdev_t *vd = vdev_lookup_top(spa, svr->svr_vdev_id);
	vdev_indirect_mapping_t *vim = vd->vdev_indirect_mapping;
	uint64_t start_offset = vdev_indirect_mapping_max_offset(vim);

	ASSERT3P(vd->vdev_ops, !=, &vdev_indirect_ops);
	ASSERT(vdev_is_concrete(vd));
	ASSERT(vd->vdev_removing);
	ASSERT(vd->vdev_indirect_config.vic_mapping_object != 0);
	ASSERT(vim != NULL);

	mutex_init(&vca.vca_lock, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&vca.vca_cv, NULL, CV_DEFAULT, NULL);
	vca.vca_outstanding_bytes = 0;

	mutex_enter(&svr->svr_lock);

	/*
	 * Start from vim_max_offset so we pick up where we left off
	 * if we are restarting the removal after opening the pool.
	 */
	uint64_t msi;
	for (msi = start_offset >> vd->vdev_ms_shift;
	    msi < vd->vdev_ms_count && !svr->svr_thread_exit; msi++) {
		metaslab_t *msp = vd->vdev_ms[msi];
		ASSERT3U(msi, <=, vd->vdev_ms_count);

		ASSERT0(range_tree_space(svr->svr_allocd_segs));

		mutex_enter(&msp->ms_sync_lock);
		mutex_enter(&msp->ms_lock);

		/*
		 * Assert nothing in flight -- ms_*tree is empty.
		 */
		for (int i = 0; i < TXG_SIZE; i++) {
			ASSERT0(range_tree_space(msp->ms_allocating[i]));
		}

		/*
		 * If the metaslab has ever been allocated from (ms_sm!=NULL),
		 * read the allocated segments from the space map object
		 * into svr_allocd_segs. Since we do this while holding
		 * svr_lock and ms_sync_lock, concurrent frees (which
		 * would have modified the space map) will wait for us
		 * to finish loading the spacemap, and then take the
		 * appropriate action (see free_from_removing_vdev()).
		 */
		if (msp->ms_sm != NULL) {
			space_map_t *sm = NULL;

			/*
			 * We have to open a new space map here, because
			 * ms_sm's sm_length and sm_alloc may not reflect
			 * what's in the object contents, if we are in between
			 * metaslab_sync() and metaslab_sync_done().
			 */
			VERIFY0(space_map_open(&sm,
			    spa->spa_dsl_pool->dp_meta_objset,
			    msp->ms_sm->sm_object, msp->ms_sm->sm_start,
			    msp->ms_sm->sm_size, msp->ms_sm->sm_shift));
			space_map_update(sm);
			VERIFY0(space_map_load(sm, svr->svr_allocd_segs,
			    SM_ALLOC));
			space_map_close(sm);

			range_tree_walk(msp->ms_freeing,
			    range_tree_remove, svr->svr_allocd_segs);

			/*
			 * When we are resuming from a paused removal (i.e.
			 * when importing a pool with a removal in progress),
			 * discard any state that we have already processed.
			 */
			range_tree_clear(svr->svr_allocd_segs, 0, start_offset);
		}
		mutex_exit(&msp->ms_lock);
		mutex_exit(&msp->ms_sync_lock);

		vca.vca_msp = msp;
		zfs_dbgmsg("copying %llu segments for metaslab %llu",
		    avl_numnodes(&svr->svr_allocd_segs->rt_root),
		    msp->ms_id);

		while (!svr->svr_thread_exit &&
		    !range_tree_is_empty(svr->svr_allocd_segs)) {

			mutex_exit(&svr->svr_lock);

			/*
			 * We need to periodically drop the config lock so that
			 * writers can get in.  Additionally, we can't wait
			 * for a txg to sync while holding a config lock
			 * (since a waiting writer could cause a 3-way deadlock
			 * with the sync thread, which also gets a config
			 * lock for reader).  So we can't hold the config lock
			 * while calling dmu_tx_assign().
			 */
			spa_config_exit(spa, SCL_CONFIG, FTAG);

			/*
			 * This delay will pause the removal around the point
			 * specified by zfs_remove_max_bytes_pause. We do this
			 * solely from the test suite or during debugging.
			 */
			uint64_t bytes_copied =
			    spa->spa_removing_phys.sr_copied;
			for (int i = 0; i < TXG_SIZE; i++)
				bytes_copied += svr->svr_bytes_done[i];
			while (zfs_remove_max_bytes_pause <= bytes_copied &&
			    !svr->svr_thread_exit)
				delay(hz);

			mutex_enter(&vca.vca_lock);
			while (vca.vca_outstanding_bytes >
			    zfs_remove_max_copy_bytes) {
				cv_wait(&vca.vca_cv, &vca.vca_lock);
			}
			mutex_exit(&vca.vca_lock);

			dmu_tx_t *tx =
			    dmu_tx_create_dd(spa_get_dsl(spa)->dp_mos_dir);

			VERIFY0(dmu_tx_assign(tx, TXG_WAIT));
			uint64_t txg = dmu_tx_get_txg(tx);

			/*
			 * Reacquire the vdev_config lock.  The vdev_t
			 * that we're removing may have changed, e.g. due
			 * to a vdev_attach or vdev_detach.
			 */
			spa_config_enter(spa, SCL_CONFIG, FTAG, RW_READER);
			vd = vdev_lookup_top(spa, svr->svr_vdev_id);

			if (txg != last_txg)
				max_alloc = zfs_remove_max_segment;
			last_txg = txg;

			spa_vdev_copy_impl(vd, svr, &vca, &max_alloc, tx);

			dmu_tx_commit(tx);
			mutex_enter(&svr->svr_lock);
		}
	}

	mutex_exit(&svr->svr_lock);

	spa_config_exit(spa, SCL_CONFIG, FTAG);

	/*
	 * Wait for all copies to finish before cleaning up the vca.
	 */
	txg_wait_synced(spa->spa_dsl_pool, 0);
	ASSERT0(vca.vca_outstanding_bytes);

	mutex_destroy(&vca.vca_lock);
	cv_destroy(&vca.vca_cv);

	if (svr->svr_thread_exit) {
		mutex_enter(&svr->svr_lock);
		range_tree_vacate(svr->svr_allocd_segs, NULL, NULL);
		svr->svr_thread = NULL;
		cv_broadcast(&svr->svr_cv);
		mutex_exit(&svr->svr_lock);
	} else {
		ASSERT0(range_tree_space(svr->svr_allocd_segs));
		vdev_remove_complete(spa);
	}
	thread_exit();
}

void
spa_vdev_remove_suspend(spa_t *spa)
{
	spa_vdev_removal_t *svr = spa->spa_vdev_removal;

	if (svr == NULL)
		return;

	mutex_enter(&svr->svr_lock);
	svr->svr_thread_exit = B_TRUE;
	while (svr->svr_thread != NULL)
		cv_wait(&svr->svr_cv, &svr->svr_lock);
	svr->svr_thread_exit = B_FALSE;
	mutex_exit(&svr->svr_lock);
}

/* ARGSUSED */
static int
spa_vdev_remove_cancel_check(void *arg, dmu_tx_t *tx)
{
	spa_t *spa = dmu_tx_pool(tx)->dp_spa;

	if (spa->spa_vdev_removal == NULL)
		return (ESRCH);
	return (0);
}

/*
 * Cancel a removal by freeing all entries from the partial mapping
 * and marking the vdev as no longer being removing.
 */
/* ARGSUSED */
static void
spa_vdev_remove_cancel_sync(void *arg, dmu_tx_t *tx)
{
	spa_t *spa = dmu_tx_pool(tx)->dp_spa;
	spa_vdev_removal_t *svr = spa->spa_vdev_removal;
	vdev_t *vd = vdev_lookup_top(spa, svr->svr_vdev_id);
	vdev_indirect_config_t *vic = &vd->vdev_indirect_config;
	vdev_indirect_mapping_t *vim = vd->vdev_indirect_mapping;
	objset_t *mos = spa->spa_meta_objset;

	ASSERT3P(svr->svr_thread, ==, NULL);

	spa_feature_decr(spa, SPA_FEATURE_DEVICE_REMOVAL, tx);
	if (vdev_obsolete_counts_are_precise(vd)) {
		spa_feature_decr(spa, SPA_FEATURE_OBSOLETE_COUNTS, tx);
		VERIFY0(zap_remove(spa->spa_meta_objset, vd->vdev_top_zap,
		    VDEV_TOP_ZAP_OBSOLETE_COUNTS_ARE_PRECISE, tx));
	}

	if (vdev_obsolete_sm_object(vd) != 0) {
		ASSERT(vd->vdev_obsolete_sm != NULL);
		ASSERT3U(vdev_obsolete_sm_object(vd), ==,
		    space_map_object(vd->vdev_obsolete_sm));

		space_map_free(vd->vdev_obsolete_sm, tx);
		VERIFY0(zap_remove(spa->spa_meta_objset, vd->vdev_top_zap,
		    VDEV_TOP_ZAP_INDIRECT_OBSOLETE_SM, tx));
		space_map_close(vd->vdev_obsolete_sm);
		vd->vdev_obsolete_sm = NULL;
		spa_feature_decr(spa, SPA_FEATURE_OBSOLETE_COUNTS, tx);
	}
	for (int i = 0; i < TXG_SIZE; i++) {
		ASSERT(list_is_empty(&svr->svr_new_segments[i]));
		ASSERT3U(svr->svr_max_offset_to_sync[i], <=,
		    vdev_indirect_mapping_max_offset(vim));
	}

	for (uint64_t msi = 0; msi < vd->vdev_ms_count; msi++) {
		metaslab_t *msp = vd->vdev_ms[msi];

		if (msp->ms_start >= vdev_indirect_mapping_max_offset(vim))
			break;

		ASSERT0(range_tree_space(svr->svr_allocd_segs));

		mutex_enter(&msp->ms_lock);

		/*
		 * Assert nothing in flight -- ms_*tree is empty.
		 */
		for (int i = 0; i < TXG_SIZE; i++)
			ASSERT0(range_tree_space(msp->ms_allocating[i]));
		for (int i = 0; i < TXG_DEFER_SIZE; i++)
			ASSERT0(range_tree_space(msp->ms_defer[i]));
		ASSERT0(range_tree_space(msp->ms_freed));

		if (msp->ms_sm != NULL) {
			/*
			 * Assert that the in-core spacemap has the same
			 * length as the on-disk one, so we can use the
			 * existing in-core spacemap to load it from disk.
			 */
			ASSERT3U(msp->ms_sm->sm_alloc, ==,
			    msp->ms_sm->sm_phys->smp_alloc);
			ASSERT3U(msp->ms_sm->sm_length, ==,
			    msp->ms_sm->sm_phys->smp_objsize);

			mutex_enter(&svr->svr_lock);
			VERIFY0(space_map_load(msp->ms_sm,
			    svr->svr_allocd_segs, SM_ALLOC));
			range_tree_walk(msp->ms_freeing,
			    range_tree_remove, svr->svr_allocd_segs);

			/*
			 * Clear everything past what has been synced,
			 * because we have not allocated mappings for it yet.
			 */
			uint64_t syncd = vdev_indirect_mapping_max_offset(vim);
			uint64_t sm_end = msp->ms_sm->sm_start +
			    msp->ms_sm->sm_size;
			if (sm_end > syncd)
				range_tree_clear(svr->svr_allocd_segs,
				    syncd, sm_end - syncd);

			mutex_exit(&svr->svr_lock);
		}
		mutex_exit(&msp->ms_lock);

		mutex_enter(&svr->svr_lock);
		range_tree_vacate(svr->svr_allocd_segs,
		    free_mapped_segment_cb, vd);
		mutex_exit(&svr->svr_lock);
	}

	/*
	 * Note: this must happen after we invoke free_mapped_segment_cb,
	 * because it adds to the obsolete_segments.
	 */
	range_tree_vacate(vd->vdev_obsolete_segments, NULL, NULL);

	ASSERT3U(vic->vic_mapping_object, ==,
	    vdev_indirect_mapping_object(vd->vdev_indirect_mapping));
	vdev_indirect_mapping_close(vd->vdev_indirect_mapping);
	vd->vdev_indirect_mapping = NULL;
	vdev_indirect_mapping_free(mos, vic->vic_mapping_object, tx);
	vic->vic_mapping_object = 0;

	ASSERT3U(vic->vic_births_object, ==,
	    vdev_indirect_births_object(vd->vdev_indirect_births));
	vdev_indirect_births_close(vd->vdev_indirect_births);
	vd->vdev_indirect_births = NULL;
	vdev_indirect_births_free(mos, vic->vic_births_object, tx);
	vic->vic_births_object = 0;

	/*
	 * We may have processed some frees from the removing vdev in this
	 * txg, thus increasing svr_bytes_done; discard that here to
	 * satisfy the assertions in spa_vdev_removal_destroy().
	 * Note that future txg's can not have any bytes_done, because
	 * future TXG's are only modified from open context, and we have
	 * already shut down the copying thread.
	 */
	svr->svr_bytes_done[dmu_tx_get_txg(tx) & TXG_MASK] = 0;
	spa_finish_removal(spa, DSS_CANCELED, tx);

	vd->vdev_removing = B_FALSE;
	vdev_config_dirty(vd);

	zfs_dbgmsg("canceled device removal for vdev %llu in %llu",
	    vd->vdev_id, dmu_tx_get_txg(tx));
	spa_history_log_internal(spa, "vdev remove canceled", tx,
	    "%s vdev %llu %s", spa_name(spa),
	    vd->vdev_id, (vd->vdev_path != NULL) ? vd->vdev_path : "-");
}

int
spa_vdev_remove_cancel(spa_t *spa)
{
	spa_vdev_remove_suspend(spa);

	if (spa->spa_vdev_removal == NULL)
		return (ESRCH);

	uint64_t vdid = spa->spa_vdev_removal->svr_vdev_id;

	int error = dsl_sync_task(spa->spa_name, spa_vdev_remove_cancel_check,
	    spa_vdev_remove_cancel_sync, NULL, 0,
	    ZFS_SPACE_CHECK_EXTRA_RESERVED);

	if (error == 0) {
		spa_config_enter(spa, SCL_ALLOC | SCL_VDEV, FTAG, RW_WRITER);
		vdev_t *vd = vdev_lookup_top(spa, vdid);
		metaslab_group_activate(vd->vdev_mg);
		spa_config_exit(spa, SCL_ALLOC | SCL_VDEV, FTAG);
	}

	return (error);
}

/*
 * Called every sync pass of every txg if there's a svr.
 */
void
svr_sync(spa_t *spa, dmu_tx_t *tx)
{
	spa_vdev_removal_t *svr = spa->spa_vdev_removal;
	int txgoff = dmu_tx_get_txg(tx) & TXG_MASK;

	/*
	 * This check is necessary so that we do not dirty the
	 * DIRECTORY_OBJECT via spa_sync_removing_state() when there
	 * is nothing to do.  Dirtying it every time would prevent us
	 * from syncing-to-convergence.
	 */
	if (svr->svr_bytes_done[txgoff] == 0)
		return;

	/*
	 * Update progress accounting.
	 */
	spa->spa_removing_phys.sr_copied += svr->svr_bytes_done[txgoff];
	svr->svr_bytes_done[txgoff] = 0;

	spa_sync_removing_state(spa, tx);
}

static void
vdev_remove_make_hole_and_free(vdev_t *vd)
{
	uint64_t id = vd->vdev_id;
	spa_t *spa = vd->vdev_spa;
	vdev_t *rvd = spa->spa_root_vdev;
	boolean_t last_vdev = (id == (rvd->vdev_children - 1));

	ASSERT(MUTEX_HELD(&spa_namespace_lock));
	ASSERT(spa_config_held(spa, SCL_ALL, RW_WRITER) == SCL_ALL);

	vdev_free(vd);

	if (last_vdev) {
		vdev_compact_children(rvd);
	} else {
		vd = vdev_alloc_common(spa, id, 0, &vdev_hole_ops);
		vdev_add_child(rvd, vd);
	}
	vdev_config_dirty(rvd);

	/*
	 * Reassess the health of our root vdev.
	 */
	vdev_reopen(rvd);
}

/*
 * Remove a log device.  The config lock is held for the specified TXG.
 */
static int
spa_vdev_remove_log(vdev_t *vd, uint64_t *txg)
{
	metaslab_group_t *mg = vd->vdev_mg;
	spa_t *spa = vd->vdev_spa;
	int error = 0;

	ASSERT(vd->vdev_islog);
	ASSERT(vd == vd->vdev_top);

	/*
	 * Stop allocating from this vdev.
	 */
	metaslab_group_passivate(mg);

	/*
	 * Wait for the youngest allocations and frees to sync,
	 * and then wait for the deferral of those frees to finish.
	 */
	spa_vdev_config_exit(spa, NULL,
	    *txg + TXG_CONCURRENT_STATES + TXG_DEFER_SIZE, 0, FTAG);

	/*
	 * Evacuate the device.  We don't hold the config lock as writer
	 * since we need to do I/O but we do keep the
	 * spa_namespace_lock held.  Once this completes the device
	 * should no longer have any blocks allocated on it.
	 */
	if (vd->vdev_islog) {
		if (vd->vdev_stat.vs_alloc != 0)
			error = spa_reset_logs(spa);
	}

	*txg = spa_vdev_config_enter(spa);

	if (error != 0) {
		metaslab_group_activate(mg);
		return (error);
	}
	ASSERT0(vd->vdev_stat.vs_alloc);

	/*
	 * The evacuation succeeded.  Remove any remaining MOS metadata
	 * associated with this vdev, and wait for these changes to sync.
	 */
	vd->vdev_removing = B_TRUE;

	vdev_dirty_leaves(vd, VDD_DTL, *txg);
	vdev_config_dirty(vd);

	spa_history_log_internal(spa, "vdev remove", NULL,
	    "%s vdev %llu (log) %s", spa_name(spa), vd->vdev_id,
	    (vd->vdev_path != NULL) ? vd->vdev_path : "-");

	/* Make sure these changes are sync'ed */
	spa_vdev_config_exit(spa, NULL, *txg, 0, FTAG);

	/* Stop initializing */
	(void) vdev_initialize_stop_all(vd, VDEV_INITIALIZE_CANCELED);

	*txg = spa_vdev_config_enter(spa);

	sysevent_t *ev = spa_event_create(spa, vd, NULL,
	    ESC_ZFS_VDEV_REMOVE_DEV);
	ASSERT(MUTEX_HELD(&spa_namespace_lock));
	ASSERT(spa_config_held(spa, SCL_ALL, RW_WRITER) == SCL_ALL);

	/* The top ZAP should have been destroyed by vdev_remove_empty. */
	ASSERT0(vd->vdev_top_zap);
	/* The leaf ZAP should have been destroyed by vdev_dtl_sync. */
	ASSERT0(vd->vdev_leaf_zap);

	(void) vdev_label_init(vd, 0, VDEV_LABEL_REMOVE);

	if (list_link_active(&vd->vdev_state_dirty_node))
		vdev_state_clean(vd);
	if (list_link_active(&vd->vdev_config_dirty_node))
		vdev_config_clean(vd);

	/*
	 * Clean up the vdev namespace.
	 */
	vdev_remove_make_hole_and_free(vd);

	if (ev != NULL)
		spa_event_post(ev);

	return (0);
}

static int
spa_vdev_remove_top_check(vdev_t *vd)
{
	spa_t *spa = vd->vdev_spa;

	if (vd != vd->vdev_top)
		return (SET_ERROR(ENOTSUP));

	if (!spa_feature_is_enabled(spa, SPA_FEATURE_DEVICE_REMOVAL))
		return (SET_ERROR(ENOTSUP));

	/*
	 * There has to be enough free space to remove the
	 * device and leave double the "slop" space (i.e. we
	 * must leave at least 3% of the pool free, in addition to
	 * the normal slop space).
	 */
	if (dsl_dir_space_available(spa->spa_dsl_pool->dp_root_dir,
	    NULL, 0, B_TRUE) <
	    vd->vdev_stat.vs_dspace + spa_get_slop_space(spa)) {
		return (SET_ERROR(ENOSPC));
	}

	/*
	 * There can not be a removal in progress.
	 */
	if (spa->spa_removing_phys.sr_state == DSS_SCANNING)
		return (SET_ERROR(EBUSY));

	/*
	 * The device must have all its data.
	 */
	if (!vdev_dtl_empty(vd, DTL_MISSING) ||
	    !vdev_dtl_empty(vd, DTL_OUTAGE))
		return (SET_ERROR(EBUSY));

	/*
	 * The device must be healthy.
	 */
	if (!vdev_readable(vd))
		return (SET_ERROR(EIO));

	/*
	 * All vdevs in normal class must have the same ashift.
	 */
	if (spa->spa_max_ashift != spa->spa_min_ashift) {
		return (SET_ERROR(EINVAL));
	}

	/*
	 * All vdevs in normal class must have the same ashift
	 * and not be raidz.
	 */
	vdev_t *rvd = spa->spa_root_vdev;
	int num_indirect = 0;
	for (uint64_t id = 0; id < rvd->vdev_children; id++) {
		vdev_t *cvd = rvd->vdev_child[id];
		if (cvd->vdev_ashift != 0 && !cvd->vdev_islog)
			ASSERT3U(cvd->vdev_ashift, ==, spa->spa_max_ashift);
		if (cvd->vdev_ops == &vdev_indirect_ops)
			num_indirect++;
		if (!vdev_is_concrete(cvd))
			continue;
		if (cvd->vdev_ops == &vdev_raidz_ops)
			return (SET_ERROR(EINVAL));
		/*
		 * Need the mirror to be mirror of leaf vdevs only
		 */
		if (cvd->vdev_ops == &vdev_mirror_ops) {
			for (uint64_t cid = 0;
			    cid < cvd->vdev_children; cid++) {
				vdev_t *tmp = cvd->vdev_child[cid];
				if (!tmp->vdev_ops->vdev_op_leaf)
					return (SET_ERROR(EINVAL));
			}
		}
	}

	return (0);
}

/*
 * Initiate removal of a top-level vdev, reducing the total space in the pool.
 * The config lock is held for the specified TXG.  Once initiated,
 * evacuation of all allocated space (copying it to other vdevs) happens
 * in the background (see spa_vdev_remove_thread()), and can be canceled
 * (see spa_vdev_remove_cancel()).  If successful, the vdev will
 * be transformed to an indirect vdev (see spa_vdev_remove_complete()).
 */
static int
spa_vdev_remove_top(vdev_t *vd, uint64_t *txg)
{
	spa_t *spa = vd->vdev_spa;
	int error;

	/*
	 * Check for errors up-front, so that we don't waste time
	 * passivating the metaslab group and clearing the ZIL if there
	 * are errors.
	 */
	error = spa_vdev_remove_top_check(vd);
	if (error != 0)
		return (error);

	/*
	 * Stop allocating from this vdev.  Note that we must check
	 * that this is not the only device in the pool before
	 * passivating, otherwise we will not be able to make
	 * progress because we can't allocate from any vdevs.
	 * The above check for sufficient free space serves this
	 * purpose.
	 */
	metaslab_group_t *mg = vd->vdev_mg;
	metaslab_group_passivate(mg);

	/*
	 * Wait for the youngest allocations and frees to sync,
	 * and then wait for the deferral of those frees to finish.
	 */
	spa_vdev_config_exit(spa, NULL,
	    *txg + TXG_CONCURRENT_STATES + TXG_DEFER_SIZE, 0, FTAG);

	/*
	 * We must ensure that no "stubby" log blocks are allocated
	 * on the device to be removed.  These blocks could be
	 * written at any time, including while we are in the middle
	 * of copying them.
	 */
	error = spa_reset_logs(spa);

	/*
	 * We stop any initializing that is currently in progress but leave
	 * the state as "active". This will allow the initializing to resume
	 * if the removal is canceled sometime later.
	 */
	vdev_initialize_stop_all(vd, VDEV_INITIALIZE_ACTIVE);

	*txg = spa_vdev_config_enter(spa);

	/*
	 * Things might have changed while the config lock was dropped
	 * (e.g. space usage).  Check for errors again.
	 */
	if (error == 0)
		error = spa_vdev_remove_top_check(vd);

	if (error != 0) {
		metaslab_group_activate(mg);
		spa_async_request(spa, SPA_ASYNC_INITIALIZE_RESTART);
		return (error);
	}

	vd->vdev_removing = B_TRUE;

	vdev_dirty_leaves(vd, VDD_DTL, *txg);
	vdev_config_dirty(vd);
	dmu_tx_t *tx = dmu_tx_create_assigned(spa->spa_dsl_pool, *txg);
	dsl_sync_task_nowait(spa->spa_dsl_pool,
	    vdev_remove_initiate_sync,
	    (void *)(uintptr_t)vd->vdev_id, 0, ZFS_SPACE_CHECK_NONE, tx);
	dmu_tx_commit(tx);

	return (0);
}

/*
 * Remove a device from the pool.
 *
 * Removing a device from the vdev namespace requires several steps
 * and can take a significant amount of time.  As a result we use
 * the spa_vdev_config_[enter/exit] functions which allow us to
 * grab and release the spa_config_lock while still holding the namespace
 * lock.  During each step the configuration is synced out.
 */
int
spa_vdev_remove(spa_t *spa, uint64_t guid, boolean_t unspare)
{
	vdev_t *vd;
	nvlist_t **spares, **l2cache, *nv;
	uint64_t txg = 0;
	uint_t nspares, nl2cache;
	int error = 0;
	boolean_t locked = MUTEX_HELD(&spa_namespace_lock);
	sysevent_t *ev = NULL;

	ASSERT(spa_writeable(spa));

	if (!locked)
		txg = spa_vdev_enter(spa);

	ASSERT(MUTEX_HELD(&spa_namespace_lock));
	if (spa_feature_is_active(spa, SPA_FEATURE_POOL_CHECKPOINT)) {
		error = (spa_has_checkpoint(spa)) ?
		    ZFS_ERR_CHECKPOINT_EXISTS : ZFS_ERR_DISCARDING_CHECKPOINT;

		if (!locked)
			return (spa_vdev_exit(spa, NULL, txg, error));

		return (error);
	}

	vd = spa_lookup_by_guid(spa, guid, B_FALSE);

	if (spa->spa_spares.sav_vdevs != NULL &&
	    nvlist_lookup_nvlist_array(spa->spa_spares.sav_config,
	    ZPOOL_CONFIG_SPARES, &spares, &nspares) == 0 &&
	    (nv = spa_nvlist_lookup_by_guid(spares, nspares, guid)) != NULL) {
		/*
		 * Only remove the hot spare if it's not currently in use
		 * in this pool.
		 */
		if (vd == NULL || unspare) {
			char *nvstr = fnvlist_lookup_string(nv,
			    ZPOOL_CONFIG_PATH);
			spa_history_log_internal(spa, "vdev remove", NULL,
			    "%s vdev (%s) %s", spa_name(spa),
			    VDEV_TYPE_SPARE, nvstr);
			if (vd == NULL)
				vd = spa_lookup_by_guid(spa, guid, B_TRUE);
			ev = spa_event_create(spa, vd, NULL,
			    ESC_ZFS_VDEV_REMOVE_AUX);
			spa_vdev_remove_aux(spa->spa_spares.sav_config,
			    ZPOOL_CONFIG_SPARES, spares, nspares, nv);
			spa_load_spares(spa);
			spa->spa_spares.sav_sync = B_TRUE;
		} else {
			error = SET_ERROR(EBUSY);
		}
	} else if (spa->spa_l2cache.sav_vdevs != NULL &&
	    nvlist_lookup_nvlist_array(spa->spa_l2cache.sav_config,
	    ZPOOL_CONFIG_L2CACHE, &l2cache, &nl2cache) == 0 &&
	    (nv = spa_nvlist_lookup_by_guid(l2cache, nl2cache, guid)) != NULL) {
		char *nvstr = fnvlist_lookup_string(nv, ZPOOL_CONFIG_PATH);
		spa_history_log_internal(spa, "vdev remove", NULL,
		    "%s vdev (%s) %s", spa_name(spa), VDEV_TYPE_L2CACHE, nvstr);
		/*
		 * Cache devices can always be removed.
		 */
		vd = spa_lookup_by_guid(spa, guid, B_TRUE);
		ev = spa_event_create(spa, vd, NULL, ESC_ZFS_VDEV_REMOVE_AUX);
		spa_vdev_remove_aux(spa->spa_l2cache.sav_config,
		    ZPOOL_CONFIG_L2CACHE, l2cache, nl2cache, nv);
		spa_load_l2cache(spa);
		spa->spa_l2cache.sav_sync = B_TRUE;
	} else if (vd != NULL && vd->vdev_islog) {
		ASSERT(!locked);
		error = spa_vdev_remove_log(vd, &txg);
	} else if (vd != NULL) {
		ASSERT(!locked);
		error = spa_vdev_remove_top(vd, &txg);
	} else {
		/*
		 * There is no vdev of any kind with the specified guid.
		 */
		error = SET_ERROR(ENOENT);
	}

	if (!locked)
		error = spa_vdev_exit(spa, NULL, txg, error);

	if (ev != NULL) {
		if (error != 0) {
			spa_event_discard(ev);
		} else {
			spa_event_post(ev);
		}
	}

	return (error);
}

int
spa_removal_get_stats(spa_t *spa, pool_removal_stat_t *prs)
{
	prs->prs_state = spa->spa_removing_phys.sr_state;

	if (prs->prs_state == DSS_NONE)
		return (SET_ERROR(ENOENT));

	prs->prs_removing_vdev = spa->spa_removing_phys.sr_removing_vdev;
	prs->prs_start_time = spa->spa_removing_phys.sr_start_time;
	prs->prs_end_time = spa->spa_removing_phys.sr_end_time;
	prs->prs_to_copy = spa->spa_removing_phys.sr_to_copy;
	prs->prs_copied = spa->spa_removing_phys.sr_copied;

	if (spa->spa_vdev_removal != NULL) {
		for (int i = 0; i < TXG_SIZE; i++) {
			prs->prs_copied +=
			    spa->spa_vdev_removal->svr_bytes_done[i];
		}
	}

	prs->prs_mapping_memory = 0;
	uint64_t indirect_vdev_id =
	    spa->spa_removing_phys.sr_prev_indirect_vdev;
	while (indirect_vdev_id != -1) {
		vdev_t *vd = spa->spa_root_vdev->vdev_child[indirect_vdev_id];
		vdev_indirect_config_t *vic = &vd->vdev_indirect_config;
		vdev_indirect_mapping_t *vim = vd->vdev_indirect_mapping;

		ASSERT3P(vd->vdev_ops, ==, &vdev_indirect_ops);
		prs->prs_mapping_memory += vdev_indirect_mapping_size(vim);
		indirect_vdev_id = vic->vic_prev_indirect_vdev;
	}

	return (0);
}
