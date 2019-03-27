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
 * Copyright (c) 2016 by Delphix. All rights reserved.
 */

#include <sys/spa.h>
#include <sys/spa_impl.h>
#include <sys/txg.h>
#include <sys/vdev_impl.h>
#include <sys/refcount.h>
#include <sys/metaslab_impl.h>
#include <sys/dsl_synctask.h>
#include <sys/zap.h>
#include <sys/dmu_tx.h>

/*
 * Maximum number of metaslabs per group that can be initialized
 * simultaneously.
 */
int max_initialize_ms = 3;

/*
 * Value that is written to disk during initialization.
 */
uint64_t zfs_initialize_value = 0xdeadbeefdeadbeefULL;

/* maximum number of I/Os outstanding per leaf vdev */
int zfs_initialize_limit = 1;

/* size of initializing writes; default 1MiB, see zfs_remove_max_segment */
uint64_t zfs_initialize_chunk_size = 1024 * 1024;

static boolean_t
vdev_initialize_should_stop(vdev_t *vd)
{
	return (vd->vdev_initialize_exit_wanted || !vdev_writeable(vd) ||
	    vd->vdev_detached || vd->vdev_top->vdev_removing);
}

static void
vdev_initialize_zap_update_sync(void *arg, dmu_tx_t *tx)
{
	/*
	 * We pass in the guid instead of the vdev_t since the vdev may
	 * have been freed prior to the sync task being processed. This
	 * happens when a vdev is detached as we call spa_config_vdev_exit(),
	 * stop the intializing thread, schedule the sync task, and free
	 * the vdev. Later when the scheduled sync task is invoked, it would
	 * find that the vdev has been freed.
	 */
	uint64_t guid = *(uint64_t *)arg;
	uint64_t txg = dmu_tx_get_txg(tx);
	kmem_free(arg, sizeof (uint64_t));

	vdev_t *vd = spa_lookup_by_guid(tx->tx_pool->dp_spa, guid, B_FALSE);
	if (vd == NULL || vd->vdev_top->vdev_removing || !vdev_is_concrete(vd))
		return;

	uint64_t last_offset = vd->vdev_initialize_offset[txg & TXG_MASK];
	vd->vdev_initialize_offset[txg & TXG_MASK] = 0;

	VERIFY(vd->vdev_leaf_zap != 0);

	objset_t *mos = vd->vdev_spa->spa_meta_objset;

	if (last_offset > 0) {
		vd->vdev_initialize_last_offset = last_offset;
		VERIFY0(zap_update(mos, vd->vdev_leaf_zap,
		    VDEV_LEAF_ZAP_INITIALIZE_LAST_OFFSET,
		    sizeof (last_offset), 1, &last_offset, tx));
	}
	if (vd->vdev_initialize_action_time > 0) {
		uint64_t val = (uint64_t)vd->vdev_initialize_action_time;
		VERIFY0(zap_update(mos, vd->vdev_leaf_zap,
		    VDEV_LEAF_ZAP_INITIALIZE_ACTION_TIME, sizeof (val),
		    1, &val, tx));
	}

	uint64_t initialize_state = vd->vdev_initialize_state;
	VERIFY0(zap_update(mos, vd->vdev_leaf_zap,
	    VDEV_LEAF_ZAP_INITIALIZE_STATE, sizeof (initialize_state), 1,
	    &initialize_state, tx));
}

static void
vdev_initialize_change_state(vdev_t *vd, vdev_initializing_state_t new_state)
{
	ASSERT(MUTEX_HELD(&vd->vdev_initialize_lock));
	spa_t *spa = vd->vdev_spa;

	if (new_state == vd->vdev_initialize_state)
		return;

	/*
	 * Copy the vd's guid, this will be freed by the sync task.
	 */
	uint64_t *guid = kmem_zalloc(sizeof (uint64_t), KM_SLEEP);
	*guid = vd->vdev_guid;

	/*
	 * If we're suspending, then preserving the original start time.
	 */
	if (vd->vdev_initialize_state != VDEV_INITIALIZE_SUSPENDED) {
		vd->vdev_initialize_action_time = gethrestime_sec();
	}
	vd->vdev_initialize_state = new_state;

	dmu_tx_t *tx = dmu_tx_create_dd(spa_get_dsl(spa)->dp_mos_dir);
	VERIFY0(dmu_tx_assign(tx, TXG_WAIT));
	dsl_sync_task_nowait(spa_get_dsl(spa), vdev_initialize_zap_update_sync,
	    guid, 2, ZFS_SPACE_CHECK_RESERVED, tx);

	switch (new_state) {
	case VDEV_INITIALIZE_ACTIVE:
		spa_history_log_internal(spa, "initialize", tx,
		    "vdev=%s activated", vd->vdev_path);
		break;
	case VDEV_INITIALIZE_SUSPENDED:
		spa_history_log_internal(spa, "initialize", tx,
		    "vdev=%s suspended", vd->vdev_path);
		break;
	case VDEV_INITIALIZE_CANCELED:
		spa_history_log_internal(spa, "initialize", tx,
		    "vdev=%s canceled", vd->vdev_path);
		break;
	case VDEV_INITIALIZE_COMPLETE:
		spa_history_log_internal(spa, "initialize", tx,
		    "vdev=%s complete", vd->vdev_path);
		break;
	default:
		panic("invalid state %llu", (unsigned long long)new_state);
	}

	dmu_tx_commit(tx);
}

static void
vdev_initialize_cb(zio_t *zio)
{
	vdev_t *vd = zio->io_vd;
	mutex_enter(&vd->vdev_initialize_io_lock);
	if (zio->io_error == ENXIO && !vdev_writeable(vd)) {
		/*
		 * The I/O failed because the vdev was unavailable; roll the
		 * last offset back. (This works because spa_sync waits on
		 * spa_txg_zio before it runs sync tasks.)
		 */
		uint64_t *off =
		    &vd->vdev_initialize_offset[zio->io_txg & TXG_MASK];
		*off = MIN(*off, zio->io_offset);
	} else {
		/*
		 * Since initializing is best-effort, we ignore I/O errors and
		 * rely on vdev_probe to determine if the errors are more
		 * critical.
		 */
		if (zio->io_error != 0)
			vd->vdev_stat.vs_initialize_errors++;

		vd->vdev_initialize_bytes_done += zio->io_orig_size;
	}
	ASSERT3U(vd->vdev_initialize_inflight, >, 0);
	vd->vdev_initialize_inflight--;
	cv_broadcast(&vd->vdev_initialize_io_cv);
	mutex_exit(&vd->vdev_initialize_io_lock);

	spa_config_exit(vd->vdev_spa, SCL_STATE_ALL, vd);
}

/* Takes care of physical writing and limiting # of concurrent ZIOs. */
static int
vdev_initialize_write(vdev_t *vd, uint64_t start, uint64_t size, abd_t *data)
{
	spa_t *spa = vd->vdev_spa;

	/* Limit inflight initializing I/Os */
	mutex_enter(&vd->vdev_initialize_io_lock);
	while (vd->vdev_initialize_inflight >= zfs_initialize_limit) {
		cv_wait(&vd->vdev_initialize_io_cv,
		    &vd->vdev_initialize_io_lock);
	}
	vd->vdev_initialize_inflight++;
	mutex_exit(&vd->vdev_initialize_io_lock);

	dmu_tx_t *tx = dmu_tx_create_dd(spa_get_dsl(spa)->dp_mos_dir);
	VERIFY0(dmu_tx_assign(tx, TXG_WAIT));
	uint64_t txg = dmu_tx_get_txg(tx);

	spa_config_enter(spa, SCL_STATE_ALL, vd, RW_READER);
	mutex_enter(&vd->vdev_initialize_lock);

	if (vd->vdev_initialize_offset[txg & TXG_MASK] == 0) {
		uint64_t *guid = kmem_zalloc(sizeof (uint64_t), KM_SLEEP);
		*guid = vd->vdev_guid;

		/* This is the first write of this txg. */
		dsl_sync_task_nowait(spa_get_dsl(spa),
		    vdev_initialize_zap_update_sync, guid, 2,
		    ZFS_SPACE_CHECK_RESERVED, tx);
	}

	/*
	 * We know the vdev struct will still be around since all
	 * consumers of vdev_free must stop the initialization first.
	 */
	if (vdev_initialize_should_stop(vd)) {
		mutex_enter(&vd->vdev_initialize_io_lock);
		ASSERT3U(vd->vdev_initialize_inflight, >, 0);
		vd->vdev_initialize_inflight--;
		mutex_exit(&vd->vdev_initialize_io_lock);
		spa_config_exit(vd->vdev_spa, SCL_STATE_ALL, vd);
		mutex_exit(&vd->vdev_initialize_lock);
		dmu_tx_commit(tx);
		return (SET_ERROR(EINTR));
	}
	mutex_exit(&vd->vdev_initialize_lock);

	vd->vdev_initialize_offset[txg & TXG_MASK] = start + size;
	zio_nowait(zio_write_phys(spa->spa_txg_zio[txg & TXG_MASK], vd, start,
	    size, data, ZIO_CHECKSUM_OFF, vdev_initialize_cb, NULL,
	    ZIO_PRIORITY_INITIALIZING, ZIO_FLAG_CANFAIL, B_FALSE));
	/* vdev_initialize_cb releases SCL_STATE_ALL */

	dmu_tx_commit(tx);

	return (0);
}

/*
 * Translate a logical range to the physical range for the specified vdev_t.
 * This function is initially called with a leaf vdev and will walk each
 * parent vdev until it reaches a top-level vdev. Once the top-level is
 * reached the physical range is initialized and the recursive function
 * begins to unwind. As it unwinds it calls the parent's vdev specific
 * translation function to do the real conversion.
 */
void
vdev_xlate(vdev_t *vd, const range_seg_t *logical_rs, range_seg_t *physical_rs)
{
	/*
	 * Walk up the vdev tree
	 */
	if (vd != vd->vdev_top) {
		vdev_xlate(vd->vdev_parent, logical_rs, physical_rs);
	} else {
		/*
		 * We've reached the top-level vdev, initialize the
		 * physical range to the logical range and start to
		 * unwind.
		 */
		physical_rs->rs_start = logical_rs->rs_start;
		physical_rs->rs_end = logical_rs->rs_end;
		return;
	}

	vdev_t *pvd = vd->vdev_parent;
	ASSERT3P(pvd, !=, NULL);
	ASSERT3P(pvd->vdev_ops->vdev_op_xlate, !=, NULL);

	/*
	 * As this recursive function unwinds, translate the logical
	 * range into its physical components by calling the
	 * vdev specific translate function.
	 */
	range_seg_t intermediate = { 0 };
	pvd->vdev_ops->vdev_op_xlate(vd, physical_rs, &intermediate);

	physical_rs->rs_start = intermediate.rs_start;
	physical_rs->rs_end = intermediate.rs_end;
}

/*
 * Callback to fill each ABD chunk with zfs_initialize_value. len must be
 * divisible by sizeof (uint64_t), and buf must be 8-byte aligned. The ABD
 * allocation will guarantee these for us.
 */
/* ARGSUSED */
static int
vdev_initialize_block_fill(void *buf, size_t len, void *unused)
{
	ASSERT0(len % sizeof (uint64_t));
	for (uint64_t i = 0; i < len; i += sizeof (uint64_t)) {
		*(uint64_t *)((char *)(buf) + i) = zfs_initialize_value;
	}
	return (0);
}

static abd_t *
vdev_initialize_block_alloc()
{
	/* Allocate ABD for filler data */
	abd_t *data = abd_alloc_for_io(zfs_initialize_chunk_size, B_FALSE);

	ASSERT0(zfs_initialize_chunk_size % sizeof (uint64_t));
	(void) abd_iterate_func(data, 0, zfs_initialize_chunk_size,
	    vdev_initialize_block_fill, NULL);

	return (data);
}

static void
vdev_initialize_block_free(abd_t *data)
{
	abd_free(data);
}

static int
vdev_initialize_ranges(vdev_t *vd, abd_t *data)
{
	avl_tree_t *rt = &vd->vdev_initialize_tree->rt_root;

	for (range_seg_t *rs = avl_first(rt); rs != NULL;
	    rs = AVL_NEXT(rt, rs)) {
		uint64_t size = rs->rs_end - rs->rs_start;

		/* Split range into legally-sized physical chunks */
		uint64_t writes_required =
		    ((size - 1) / zfs_initialize_chunk_size) + 1;

		for (uint64_t w = 0; w < writes_required; w++) {
			int error;

			error = vdev_initialize_write(vd,
			    VDEV_LABEL_START_SIZE + rs->rs_start +
			    (w * zfs_initialize_chunk_size),
			    MIN(size - (w * zfs_initialize_chunk_size),
			    zfs_initialize_chunk_size), data);
			if (error != 0)
				return (error);
		}
	}
	return (0);
}

static void
vdev_initialize_ms_load(metaslab_t *msp)
{
	ASSERT(MUTEX_HELD(&msp->ms_lock));

	metaslab_load_wait(msp);
	if (!msp->ms_loaded)
		VERIFY0(metaslab_load(msp));
}

static void
vdev_initialize_mg_wait(metaslab_group_t *mg)
{
	ASSERT(MUTEX_HELD(&mg->mg_ms_initialize_lock));
	while (mg->mg_initialize_updating) {
		cv_wait(&mg->mg_ms_initialize_cv, &mg->mg_ms_initialize_lock);
	}
}

static void
vdev_initialize_mg_mark(metaslab_group_t *mg)
{
	ASSERT(MUTEX_HELD(&mg->mg_ms_initialize_lock));
	ASSERT(mg->mg_initialize_updating);

	while (mg->mg_ms_initializing >= max_initialize_ms) {
		cv_wait(&mg->mg_ms_initialize_cv, &mg->mg_ms_initialize_lock);
	}
	mg->mg_ms_initializing++;
	ASSERT3U(mg->mg_ms_initializing, <=, max_initialize_ms);
}

/*
 * Mark the metaslab as being initialized to prevent any allocations
 * on this metaslab. We must also track how many metaslabs are currently
 * being initialized within a metaslab group and limit them to prevent
 * allocation failures from occurring because all metaslabs are being
 * initialized.
 */
static void
vdev_initialize_ms_mark(metaslab_t *msp)
{
	ASSERT(!MUTEX_HELD(&msp->ms_lock));
	metaslab_group_t *mg = msp->ms_group;

	mutex_enter(&mg->mg_ms_initialize_lock);

	/*
	 * To keep an accurate count of how many threads are initializing
	 * a specific metaslab group, we only allow one thread to mark
	 * the metaslab group at a time. This ensures that the value of
	 * ms_initializing will be accurate when we decide to mark a metaslab
	 * group as being initialized. To do this we force all other threads
	 * to wait till the metaslab's mg_initialize_updating flag is no
	 * longer set.
	 */
	vdev_initialize_mg_wait(mg);
	mg->mg_initialize_updating = B_TRUE;
	if (msp->ms_initializing == 0) {
		vdev_initialize_mg_mark(mg);
	}
	mutex_enter(&msp->ms_lock);
	msp->ms_initializing++;
	mutex_exit(&msp->ms_lock);

	mg->mg_initialize_updating = B_FALSE;
	cv_broadcast(&mg->mg_ms_initialize_cv);
	mutex_exit(&mg->mg_ms_initialize_lock);
}

static void
vdev_initialize_ms_unmark(metaslab_t *msp)
{
	ASSERT(!MUTEX_HELD(&msp->ms_lock));
	metaslab_group_t *mg = msp->ms_group;
	mutex_enter(&mg->mg_ms_initialize_lock);
	mutex_enter(&msp->ms_lock);
	if (--msp->ms_initializing == 0) {
		mg->mg_ms_initializing--;
		cv_broadcast(&mg->mg_ms_initialize_cv);
	}
	mutex_exit(&msp->ms_lock);
	mutex_exit(&mg->mg_ms_initialize_lock);
}

static void
vdev_initialize_calculate_progress(vdev_t *vd)
{
	ASSERT(spa_config_held(vd->vdev_spa, SCL_CONFIG, RW_READER) ||
	    spa_config_held(vd->vdev_spa, SCL_CONFIG, RW_WRITER));
	ASSERT(vd->vdev_leaf_zap != 0);

	vd->vdev_initialize_bytes_est = 0;
	vd->vdev_initialize_bytes_done = 0;

	for (uint64_t i = 0; i < vd->vdev_top->vdev_ms_count; i++) {
		metaslab_t *msp = vd->vdev_top->vdev_ms[i];
		mutex_enter(&msp->ms_lock);

		uint64_t ms_free = msp->ms_size -
		    space_map_allocated(msp->ms_sm);

		if (vd->vdev_top->vdev_ops == &vdev_raidz_ops)
			ms_free /= vd->vdev_top->vdev_children;

		/*
		 * Convert the metaslab range to a physical range
		 * on our vdev. We use this to determine if we are
		 * in the middle of this metaslab range.
		 */
		range_seg_t logical_rs, physical_rs;
		logical_rs.rs_start = msp->ms_start;
		logical_rs.rs_end = msp->ms_start + msp->ms_size;
		vdev_xlate(vd, &logical_rs, &physical_rs);

		if (vd->vdev_initialize_last_offset <= physical_rs.rs_start) {
			vd->vdev_initialize_bytes_est += ms_free;
			mutex_exit(&msp->ms_lock);
			continue;
		} else if (vd->vdev_initialize_last_offset >
		    physical_rs.rs_end) {
			vd->vdev_initialize_bytes_done += ms_free;
			vd->vdev_initialize_bytes_est += ms_free;
			mutex_exit(&msp->ms_lock);
			continue;
		}

		/*
		 * If we get here, we're in the middle of initializing this
		 * metaslab. Load it and walk the free tree for more accurate
		 * progress estimation.
		 */
		vdev_initialize_ms_load(msp);

		for (range_seg_t *rs = avl_first(&msp->ms_allocatable->rt_root); rs;
		    rs = AVL_NEXT(&msp->ms_allocatable->rt_root, rs)) {
			logical_rs.rs_start = rs->rs_start;
			logical_rs.rs_end = rs->rs_end;
			vdev_xlate(vd, &logical_rs, &physical_rs);

			uint64_t size = physical_rs.rs_end -
			    physical_rs.rs_start;
			vd->vdev_initialize_bytes_est += size;
			if (vd->vdev_initialize_last_offset >
			    physical_rs.rs_end) {
				vd->vdev_initialize_bytes_done += size;
			} else if (vd->vdev_initialize_last_offset >
			    physical_rs.rs_start &&
			    vd->vdev_initialize_last_offset <
			    physical_rs.rs_end) {
				vd->vdev_initialize_bytes_done +=
				    vd->vdev_initialize_last_offset -
				    physical_rs.rs_start;
			}
		}
		mutex_exit(&msp->ms_lock);
	}
}

static void
vdev_initialize_load(vdev_t *vd)
{
	ASSERT(spa_config_held(vd->vdev_spa, SCL_CONFIG, RW_READER) ||
	    spa_config_held(vd->vdev_spa, SCL_CONFIG, RW_WRITER));
	ASSERT(vd->vdev_leaf_zap != 0);

	if (vd->vdev_initialize_state == VDEV_INITIALIZE_ACTIVE ||
	    vd->vdev_initialize_state == VDEV_INITIALIZE_SUSPENDED) {
		int err = zap_lookup(vd->vdev_spa->spa_meta_objset,
		    vd->vdev_leaf_zap, VDEV_LEAF_ZAP_INITIALIZE_LAST_OFFSET,
		    sizeof (vd->vdev_initialize_last_offset), 1,
		    &vd->vdev_initialize_last_offset);
		ASSERT(err == 0 || err == ENOENT);
	}

	vdev_initialize_calculate_progress(vd);
}


/*
 * Convert the logical range into a physcial range and add it to our
 * avl tree.
 */
void
vdev_initialize_range_add(void *arg, uint64_t start, uint64_t size)
{
	vdev_t *vd = arg;
	range_seg_t logical_rs, physical_rs;
	logical_rs.rs_start = start;
	logical_rs.rs_end = start + size;

	ASSERT(vd->vdev_ops->vdev_op_leaf);
	vdev_xlate(vd, &logical_rs, &physical_rs);

	IMPLY(vd->vdev_top == vd,
	    logical_rs.rs_start == physical_rs.rs_start);
	IMPLY(vd->vdev_top == vd,
	    logical_rs.rs_end == physical_rs.rs_end);

	/* Only add segments that we have not visited yet */
	if (physical_rs.rs_end <= vd->vdev_initialize_last_offset)
		return;

	/* Pick up where we left off mid-range. */
	if (vd->vdev_initialize_last_offset > physical_rs.rs_start) {
		zfs_dbgmsg("range write: vd %s changed (%llu, %llu) to "
		    "(%llu, %llu)", vd->vdev_path,
		    (u_longlong_t)physical_rs.rs_start,
		    (u_longlong_t)physical_rs.rs_end,
		    (u_longlong_t)vd->vdev_initialize_last_offset,
		    (u_longlong_t)physical_rs.rs_end);
		ASSERT3U(physical_rs.rs_end, >,
		    vd->vdev_initialize_last_offset);
		physical_rs.rs_start = vd->vdev_initialize_last_offset;
	}
	ASSERT3U(physical_rs.rs_end, >=, physical_rs.rs_start);

	/*
	 * With raidz, it's possible that the logical range does not live on
	 * this leaf vdev. We only add the physical range to this vdev's if it
	 * has a length greater than 0.
	 */
	if (physical_rs.rs_end > physical_rs.rs_start) {
		range_tree_add(vd->vdev_initialize_tree, physical_rs.rs_start,
		    physical_rs.rs_end - physical_rs.rs_start);
	} else {
		ASSERT3U(physical_rs.rs_end, ==, physical_rs.rs_start);
	}
}

static void
vdev_initialize_thread(void *arg)
{
	vdev_t *vd = arg;
	spa_t *spa = vd->vdev_spa;
	int error = 0;
	uint64_t ms_count = 0;

	ASSERT(vdev_is_concrete(vd));
	spa_config_enter(spa, SCL_CONFIG, FTAG, RW_READER);

	vd->vdev_initialize_last_offset = 0;
	vdev_initialize_load(vd);

	abd_t *deadbeef = vdev_initialize_block_alloc();

	vd->vdev_initialize_tree = range_tree_create(NULL, NULL);

	for (uint64_t i = 0; !vd->vdev_detached &&
	    i < vd->vdev_top->vdev_ms_count; i++) {
		metaslab_t *msp = vd->vdev_top->vdev_ms[i];

		/*
		 * If we've expanded the top-level vdev or it's our
		 * first pass, calculate our progress.
		 */
		if (vd->vdev_top->vdev_ms_count != ms_count) {
			vdev_initialize_calculate_progress(vd);
			ms_count = vd->vdev_top->vdev_ms_count;
		}

		vdev_initialize_ms_mark(msp);
		mutex_enter(&msp->ms_lock);
		vdev_initialize_ms_load(msp);

		range_tree_walk(msp->ms_allocatable, vdev_initialize_range_add,
		    vd);
		mutex_exit(&msp->ms_lock);

		spa_config_exit(spa, SCL_CONFIG, FTAG);
		error = vdev_initialize_ranges(vd, deadbeef);
		vdev_initialize_ms_unmark(msp);
		spa_config_enter(spa, SCL_CONFIG, FTAG, RW_READER);

		range_tree_vacate(vd->vdev_initialize_tree, NULL, NULL);
		if (error != 0)
			break;
	}

	spa_config_exit(spa, SCL_CONFIG, FTAG);
	mutex_enter(&vd->vdev_initialize_io_lock);
	while (vd->vdev_initialize_inflight > 0) {
		cv_wait(&vd->vdev_initialize_io_cv,
		    &vd->vdev_initialize_io_lock);
	}
	mutex_exit(&vd->vdev_initialize_io_lock);

	range_tree_destroy(vd->vdev_initialize_tree);
	vdev_initialize_block_free(deadbeef);
	vd->vdev_initialize_tree = NULL;

	mutex_enter(&vd->vdev_initialize_lock);
	if (!vd->vdev_initialize_exit_wanted && vdev_writeable(vd)) {
		vdev_initialize_change_state(vd, VDEV_INITIALIZE_COMPLETE);
	}
	ASSERT(vd->vdev_initialize_thread != NULL ||
	    vd->vdev_initialize_inflight == 0);

	/*
	 * Drop the vdev_initialize_lock while we sync out the
	 * txg since it's possible that a device might be trying to
	 * come online and must check to see if it needs to restart an
	 * initialization. That thread will be holding the spa_config_lock
	 * which would prevent the txg_wait_synced from completing.
	 */
	mutex_exit(&vd->vdev_initialize_lock);
	txg_wait_synced(spa_get_dsl(spa), 0);
	mutex_enter(&vd->vdev_initialize_lock);

	vd->vdev_initialize_thread = NULL;
	cv_broadcast(&vd->vdev_initialize_cv);
	mutex_exit(&vd->vdev_initialize_lock);
	thread_exit();
}

/*
 * Initiates a device. Caller must hold vdev_initialize_lock.
 * Device must be a leaf and not already be initializing.
 */
void
vdev_initialize(vdev_t *vd)
{
	ASSERT(MUTEX_HELD(&vd->vdev_initialize_lock));
	ASSERT(vd->vdev_ops->vdev_op_leaf);
	ASSERT(vdev_is_concrete(vd));
	ASSERT3P(vd->vdev_initialize_thread, ==, NULL);
	ASSERT(!vd->vdev_detached);
	ASSERT(!vd->vdev_initialize_exit_wanted);
	ASSERT(!vd->vdev_top->vdev_removing);

	vdev_initialize_change_state(vd, VDEV_INITIALIZE_ACTIVE);
	vd->vdev_initialize_thread = thread_create(NULL, 0,
	    vdev_initialize_thread, vd, 0, &p0, TS_RUN, maxclsyspri);
}

/*
 * Stop initializng a device, with the resultant initialing state being
 * tgt_state. Blocks until the initializing thread has exited.
 * Caller must hold vdev_initialize_lock and must not be writing to the spa
 * config, as the initializing thread may try to enter the config as a reader
 * before exiting.
 */
void
vdev_initialize_stop(vdev_t *vd, vdev_initializing_state_t tgt_state)
{
	spa_t *spa = vd->vdev_spa;
	ASSERT(!spa_config_held(spa, SCL_CONFIG | SCL_STATE, RW_WRITER));

	ASSERT(MUTEX_HELD(&vd->vdev_initialize_lock));
	ASSERT(vd->vdev_ops->vdev_op_leaf);
	ASSERT(vdev_is_concrete(vd));

	/*
	 * Allow cancel requests to proceed even if the initialize thread
	 * has stopped.
	 */
	if (vd->vdev_initialize_thread == NULL &&
	    tgt_state != VDEV_INITIALIZE_CANCELED) {
		return;
	}

	vdev_initialize_change_state(vd, tgt_state);
	vd->vdev_initialize_exit_wanted = B_TRUE;
	while (vd->vdev_initialize_thread != NULL)
		cv_wait(&vd->vdev_initialize_cv, &vd->vdev_initialize_lock);

	ASSERT3P(vd->vdev_initialize_thread, ==, NULL);
	vd->vdev_initialize_exit_wanted = B_FALSE;
}

static void
vdev_initialize_stop_all_impl(vdev_t *vd, vdev_initializing_state_t tgt_state)
{
	if (vd->vdev_ops->vdev_op_leaf && vdev_is_concrete(vd)) {
		mutex_enter(&vd->vdev_initialize_lock);
		vdev_initialize_stop(vd, tgt_state);
		mutex_exit(&vd->vdev_initialize_lock);
		return;
	}

	for (uint64_t i = 0; i < vd->vdev_children; i++) {
		vdev_initialize_stop_all_impl(vd->vdev_child[i], tgt_state);
	}
}

/*
 * Convenience function to stop initializing of a vdev tree and set all
 * initialize thread pointers to NULL.
 */
void
vdev_initialize_stop_all(vdev_t *vd, vdev_initializing_state_t tgt_state)
{
	vdev_initialize_stop_all_impl(vd, tgt_state);

	if (vd->vdev_spa->spa_sync_on) {
		/* Make sure that our state has been synced to disk */
		txg_wait_synced(spa_get_dsl(vd->vdev_spa), 0);
	}
}

void
vdev_initialize_restart(vdev_t *vd)
{
	ASSERT(MUTEX_HELD(&spa_namespace_lock));
	ASSERT(!spa_config_held(vd->vdev_spa, SCL_ALL, RW_WRITER));

	if (vd->vdev_leaf_zap != 0) {
		mutex_enter(&vd->vdev_initialize_lock);
		uint64_t initialize_state = VDEV_INITIALIZE_NONE;
		int err = zap_lookup(vd->vdev_spa->spa_meta_objset,
		    vd->vdev_leaf_zap, VDEV_LEAF_ZAP_INITIALIZE_STATE,
		    sizeof (initialize_state), 1, &initialize_state);
		ASSERT(err == 0 || err == ENOENT);
		vd->vdev_initialize_state = initialize_state;

		uint64_t timestamp = 0;
		err = zap_lookup(vd->vdev_spa->spa_meta_objset,
		    vd->vdev_leaf_zap, VDEV_LEAF_ZAP_INITIALIZE_ACTION_TIME,
		    sizeof (timestamp), 1, &timestamp);
		ASSERT(err == 0 || err == ENOENT);
		vd->vdev_initialize_action_time = (time_t)timestamp;

		if (vd->vdev_initialize_state == VDEV_INITIALIZE_SUSPENDED ||
		    vd->vdev_offline) {
			/* load progress for reporting, but don't resume */
			vdev_initialize_load(vd);
		} else if (vd->vdev_initialize_state ==
		    VDEV_INITIALIZE_ACTIVE && vdev_writeable(vd)) {
			vdev_initialize(vd);
		}

		mutex_exit(&vd->vdev_initialize_lock);
	}

	for (uint64_t i = 0; i < vd->vdev_children; i++) {
		vdev_initialize_restart(vd->vdev_child[i]);
	}
}
