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
 * Copyright (c) 2013 Steven Hartland. All rights reserved.
 * Copyright (c) 2014 Spectra Logic Corporation, All rights reserved.
 * Copyright (c) 2014 Integros [integros.com]
 * Copyright 2016 Nexenta Systems, Inc.  All rights reserved.
 */

#include <sys/dsl_pool.h>
#include <sys/dsl_dataset.h>
#include <sys/dsl_prop.h>
#include <sys/dsl_dir.h>
#include <sys/dsl_synctask.h>
#include <sys/dsl_scan.h>
#include <sys/dnode.h>
#include <sys/dmu_tx.h>
#include <sys/dmu_objset.h>
#include <sys/arc.h>
#include <sys/zap.h>
#include <sys/zio.h>
#include <sys/zfs_context.h>
#include <sys/fs/zfs.h>
#include <sys/zfs_znode.h>
#include <sys/spa_impl.h>
#include <sys/dsl_deadlist.h>
#include <sys/vdev_impl.h>
#include <sys/metaslab_impl.h>
#include <sys/bptree.h>
#include <sys/zfeature.h>
#include <sys/zil_impl.h>
#include <sys/dsl_userhold.h>

#if defined(__FreeBSD__) && defined(_KERNEL)
#include <sys/types.h>
#include <sys/sysctl.h>
#endif

/*
 * ZFS Write Throttle
 * ------------------
 *
 * ZFS must limit the rate of incoming writes to the rate at which it is able
 * to sync data modifications to the backend storage. Throttling by too much
 * creates an artificial limit; throttling by too little can only be sustained
 * for short periods and would lead to highly lumpy performance. On a per-pool
 * basis, ZFS tracks the amount of modified (dirty) data. As operations change
 * data, the amount of dirty data increases; as ZFS syncs out data, the amount
 * of dirty data decreases. When the amount of dirty data exceeds a
 * predetermined threshold further modifications are blocked until the amount
 * of dirty data decreases (as data is synced out).
 *
 * The limit on dirty data is tunable, and should be adjusted according to
 * both the IO capacity and available memory of the system. The larger the
 * window, the more ZFS is able to aggregate and amortize metadata (and data)
 * changes. However, memory is a limited resource, and allowing for more dirty
 * data comes at the cost of keeping other useful data in memory (for example
 * ZFS data cached by the ARC).
 *
 * Implementation
 *
 * As buffers are modified dsl_pool_willuse_space() increments both the per-
 * txg (dp_dirty_pertxg[]) and poolwide (dp_dirty_total) accounting of
 * dirty space used; dsl_pool_dirty_space() decrements those values as data
 * is synced out from dsl_pool_sync(). While only the poolwide value is
 * relevant, the per-txg value is useful for debugging. The tunable
 * zfs_dirty_data_max determines the dirty space limit. Once that value is
 * exceeded, new writes are halted until space frees up.
 *
 * The zfs_dirty_data_sync tunable dictates the threshold at which we
 * ensure that there is a txg syncing (see the comment in txg.c for a full
 * description of transaction group stages).
 *
 * The IO scheduler uses both the dirty space limit and current amount of
 * dirty data as inputs. Those values affect the number of concurrent IOs ZFS
 * issues. See the comment in vdev_queue.c for details of the IO scheduler.
 *
 * The delay is also calculated based on the amount of dirty data.  See the
 * comment above dmu_tx_delay() for details.
 */

/*
 * zfs_dirty_data_max will be set to zfs_dirty_data_max_percent% of all memory,
 * capped at zfs_dirty_data_max_max.  It can also be overridden in /etc/system.
 */
uint64_t zfs_dirty_data_max;
uint64_t zfs_dirty_data_max_max = 4ULL * 1024 * 1024 * 1024;
int zfs_dirty_data_max_percent = 10;

/*
 * If there is at least this much dirty data, push out a txg.
 */
uint64_t zfs_dirty_data_sync = 64 * 1024 * 1024;

/*
 * Once there is this amount of dirty data, the dmu_tx_delay() will kick in
 * and delay each transaction.
 * This value should be >= zfs_vdev_async_write_active_max_dirty_percent.
 */
int zfs_delay_min_dirty_percent = 60;

/*
 * This controls how quickly the delay approaches infinity.
 * Larger values cause it to delay more for a given amount of dirty data.
 * Therefore larger values will cause there to be less dirty data for a
 * given throughput.
 *
 * For the smoothest delay, this value should be about 1 billion divided
 * by the maximum number of operations per second.  This will smoothly
 * handle between 10x and 1/10th this number.
 *
 * Note: zfs_delay_scale * zfs_dirty_data_max must be < 2^64, due to the
 * multiply in dmu_tx_delay().
 */
uint64_t zfs_delay_scale = 1000 * 1000 * 1000 / 2000;

/*
 * This determines the number of threads used by the dp_sync_taskq.
 */
int zfs_sync_taskq_batch_pct = 75;

/*
 * These tunables determine the behavior of how zil_itxg_clean() is
 * called via zil_clean() in the context of spa_sync(). When an itxg
 * list needs to be cleaned, TQ_NOSLEEP will be used when dispatching.
 * If the dispatch fails, the call to zil_itxg_clean() will occur
 * synchronously in the context of spa_sync(), which can negatively
 * impact the performance of spa_sync() (e.g. in the case of the itxg
 * list having a large number of itxs that needs to be cleaned).
 *
 * Thus, these tunables can be used to manipulate the behavior of the
 * taskq used by zil_clean(); they determine the number of taskq entries
 * that are pre-populated when the taskq is first created (via the
 * "zfs_zil_clean_taskq_minalloc" tunable) and the maximum number of
 * taskq entries that are cached after an on-demand allocation (via the
 * "zfs_zil_clean_taskq_maxalloc").
 *
 * The idea being, we want to try reasonably hard to ensure there will
 * already be a taskq entry pre-allocated by the time that it is needed
 * by zil_clean(). This way, we can avoid the possibility of an
 * on-demand allocation of a new taskq entry from failing, which would
 * result in zil_itxg_clean() being called synchronously from zil_clean()
 * (which can adversely affect performance of spa_sync()).
 *
 * Additionally, the number of threads used by the taskq can be
 * configured via the "zfs_zil_clean_taskq_nthr_pct" tunable.
 */
int zfs_zil_clean_taskq_nthr_pct = 100;
int zfs_zil_clean_taskq_minalloc = 1024;
int zfs_zil_clean_taskq_maxalloc = 1024 * 1024;

#if defined(__FreeBSD__) && defined(_KERNEL)

extern int zfs_vdev_async_write_active_max_dirty_percent;

SYSCTL_DECL(_vfs_zfs);

SYSCTL_UQUAD(_vfs_zfs, OID_AUTO, dirty_data_max, CTLFLAG_RWTUN,
    &zfs_dirty_data_max, 0,
    "The maximum amount of dirty data in bytes after which new writes are "
    "halted until space becomes available");

SYSCTL_UQUAD(_vfs_zfs, OID_AUTO, dirty_data_max_max, CTLFLAG_RDTUN,
    &zfs_dirty_data_max_max, 0,
    "The absolute cap on dirty_data_max when auto calculating");

static int sysctl_zfs_dirty_data_max_percent(SYSCTL_HANDLER_ARGS);
SYSCTL_PROC(_vfs_zfs, OID_AUTO, dirty_data_max_percent,
    CTLTYPE_INT | CTLFLAG_MPSAFE | CTLFLAG_RWTUN, 0, sizeof(int),
    sysctl_zfs_dirty_data_max_percent, "I",
    "The percent of physical memory used to auto calculate dirty_data_max");

SYSCTL_UQUAD(_vfs_zfs, OID_AUTO, dirty_data_sync, CTLFLAG_RWTUN,
    &zfs_dirty_data_sync, 0,
    "Force a txg if the number of dirty buffer bytes exceed this value");

static int sysctl_zfs_delay_min_dirty_percent(SYSCTL_HANDLER_ARGS);
/* No zfs_delay_min_dirty_percent tunable due to limit requirements */
SYSCTL_PROC(_vfs_zfs, OID_AUTO, delay_min_dirty_percent,
    CTLTYPE_INT | CTLFLAG_MPSAFE | CTLFLAG_RW, 0, sizeof(int),
    sysctl_zfs_delay_min_dirty_percent, "I",
    "The limit of outstanding dirty data before transactions are delayed");

static int sysctl_zfs_delay_scale(SYSCTL_HANDLER_ARGS);
/* No zfs_delay_scale tunable due to limit requirements */
SYSCTL_PROC(_vfs_zfs, OID_AUTO, delay_scale,
    CTLTYPE_U64 | CTLFLAG_MPSAFE | CTLFLAG_RW, 0, sizeof(uint64_t),
    sysctl_zfs_delay_scale, "QU",
    "Controls how quickly the delay approaches infinity");

static int
sysctl_zfs_dirty_data_max_percent(SYSCTL_HANDLER_ARGS)
{
	int val, err;

	val = zfs_dirty_data_max_percent;
	err = sysctl_handle_int(oidp, &val, 0, req);
	if (err != 0 || req->newptr == NULL)
		return (err);

	if (val < 0 || val > 100)
		return (EINVAL);

	zfs_dirty_data_max_percent = val;

	return (0);
}

static int
sysctl_zfs_delay_min_dirty_percent(SYSCTL_HANDLER_ARGS)
{
	int val, err;

	val = zfs_delay_min_dirty_percent;
	err = sysctl_handle_int(oidp, &val, 0, req);
	if (err != 0 || req->newptr == NULL)
		return (err);

	if (val < zfs_vdev_async_write_active_max_dirty_percent)
		return (EINVAL);

	zfs_delay_min_dirty_percent = val;

	return (0);
}

static int
sysctl_zfs_delay_scale(SYSCTL_HANDLER_ARGS)
{
	uint64_t val;
	int err;

	val = zfs_delay_scale;
	err = sysctl_handle_64(oidp, &val, 0, req);
	if (err != 0 || req->newptr == NULL)
		return (err);

	if (val > UINT64_MAX / zfs_dirty_data_max)
		return (EINVAL);

	zfs_delay_scale = val;

	return (0);
}
#endif

int
dsl_pool_open_special_dir(dsl_pool_t *dp, const char *name, dsl_dir_t **ddp)
{
	uint64_t obj;
	int err;

	err = zap_lookup(dp->dp_meta_objset,
	    dsl_dir_phys(dp->dp_root_dir)->dd_child_dir_zapobj,
	    name, sizeof (obj), 1, &obj);
	if (err)
		return (err);

	return (dsl_dir_hold_obj(dp, obj, name, dp, ddp));
}

static dsl_pool_t *
dsl_pool_open_impl(spa_t *spa, uint64_t txg)
{
	dsl_pool_t *dp;
	blkptr_t *bp = spa_get_rootblkptr(spa);

	dp = kmem_zalloc(sizeof (dsl_pool_t), KM_SLEEP);
	dp->dp_spa = spa;
	dp->dp_meta_rootbp = *bp;
	rrw_init(&dp->dp_config_rwlock, B_TRUE);
	txg_init(dp, txg);

	txg_list_create(&dp->dp_dirty_datasets, spa,
	    offsetof(dsl_dataset_t, ds_dirty_link));
	txg_list_create(&dp->dp_dirty_zilogs, spa,
	    offsetof(zilog_t, zl_dirty_link));
	txg_list_create(&dp->dp_dirty_dirs, spa,
	    offsetof(dsl_dir_t, dd_dirty_link));
	txg_list_create(&dp->dp_sync_tasks, spa,
	    offsetof(dsl_sync_task_t, dst_node));
	txg_list_create(&dp->dp_early_sync_tasks, spa,
	    offsetof(dsl_sync_task_t, dst_node));

	dp->dp_sync_taskq = taskq_create("dp_sync_taskq",
	    zfs_sync_taskq_batch_pct, minclsyspri, 1, INT_MAX,
	    TASKQ_THREADS_CPU_PCT);

	dp->dp_zil_clean_taskq = taskq_create("dp_zil_clean_taskq",
	    zfs_zil_clean_taskq_nthr_pct, minclsyspri,
	    zfs_zil_clean_taskq_minalloc,
	    zfs_zil_clean_taskq_maxalloc,
	    TASKQ_PREPOPULATE | TASKQ_THREADS_CPU_PCT);

	mutex_init(&dp->dp_lock, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&dp->dp_spaceavail_cv, NULL, CV_DEFAULT, NULL);

	dp->dp_vnrele_taskq = taskq_create("zfs_vn_rele_taskq", 1, minclsyspri,
	    1, 4, 0);

	return (dp);
}

int
dsl_pool_init(spa_t *spa, uint64_t txg, dsl_pool_t **dpp)
{
	int err;
	dsl_pool_t *dp = dsl_pool_open_impl(spa, txg);

	err = dmu_objset_open_impl(spa, NULL, &dp->dp_meta_rootbp,
	    &dp->dp_meta_objset);
	if (err != 0)
		dsl_pool_close(dp);
	else
		*dpp = dp;

	return (err);
}

int
dsl_pool_open(dsl_pool_t *dp)
{
	int err;
	dsl_dir_t *dd;
	dsl_dataset_t *ds;
	uint64_t obj;

	rrw_enter(&dp->dp_config_rwlock, RW_WRITER, FTAG);
	err = zap_lookup(dp->dp_meta_objset, DMU_POOL_DIRECTORY_OBJECT,
	    DMU_POOL_ROOT_DATASET, sizeof (uint64_t), 1,
	    &dp->dp_root_dir_obj);
	if (err)
		goto out;

	err = dsl_dir_hold_obj(dp, dp->dp_root_dir_obj,
	    NULL, dp, &dp->dp_root_dir);
	if (err)
		goto out;

	err = dsl_pool_open_special_dir(dp, MOS_DIR_NAME, &dp->dp_mos_dir);
	if (err)
		goto out;

	if (spa_version(dp->dp_spa) >= SPA_VERSION_ORIGIN) {
		err = dsl_pool_open_special_dir(dp, ORIGIN_DIR_NAME, &dd);
		if (err)
			goto out;
		err = dsl_dataset_hold_obj(dp,
		    dsl_dir_phys(dd)->dd_head_dataset_obj, FTAG, &ds);
		if (err == 0) {
			err = dsl_dataset_hold_obj(dp,
			    dsl_dataset_phys(ds)->ds_prev_snap_obj, dp,
			    &dp->dp_origin_snap);
			dsl_dataset_rele(ds, FTAG);
		}
		dsl_dir_rele(dd, dp);
		if (err)
			goto out;
	}

	if (spa_version(dp->dp_spa) >= SPA_VERSION_DEADLISTS) {
		err = dsl_pool_open_special_dir(dp, FREE_DIR_NAME,
		    &dp->dp_free_dir);
		if (err)
			goto out;

		err = zap_lookup(dp->dp_meta_objset, DMU_POOL_DIRECTORY_OBJECT,
		    DMU_POOL_FREE_BPOBJ, sizeof (uint64_t), 1, &obj);
		if (err)
			goto out;
		VERIFY0(bpobj_open(&dp->dp_free_bpobj,
		    dp->dp_meta_objset, obj));
	}

	if (spa_feature_is_active(dp->dp_spa, SPA_FEATURE_OBSOLETE_COUNTS)) {
		err = zap_lookup(dp->dp_meta_objset, DMU_POOL_DIRECTORY_OBJECT,
		    DMU_POOL_OBSOLETE_BPOBJ, sizeof (uint64_t), 1, &obj);
		if (err == 0) {
			VERIFY0(bpobj_open(&dp->dp_obsolete_bpobj,
			    dp->dp_meta_objset, obj));
		} else if (err == ENOENT) {
			/*
			 * We might not have created the remap bpobj yet.
			 */
			err = 0;
		} else {
			goto out;
		}
	}

	/*
	 * Note: errors ignored, because the these special dirs, used for
	 * space accounting, are only created on demand.
	 */
	(void) dsl_pool_open_special_dir(dp, LEAK_DIR_NAME,
	    &dp->dp_leak_dir);

	if (spa_feature_is_active(dp->dp_spa, SPA_FEATURE_ASYNC_DESTROY)) {
		err = zap_lookup(dp->dp_meta_objset, DMU_POOL_DIRECTORY_OBJECT,
		    DMU_POOL_BPTREE_OBJ, sizeof (uint64_t), 1,
		    &dp->dp_bptree_obj);
		if (err != 0)
			goto out;
	}

	if (spa_feature_is_active(dp->dp_spa, SPA_FEATURE_EMPTY_BPOBJ)) {
		err = zap_lookup(dp->dp_meta_objset, DMU_POOL_DIRECTORY_OBJECT,
		    DMU_POOL_EMPTY_BPOBJ, sizeof (uint64_t), 1,
		    &dp->dp_empty_bpobj);
		if (err != 0)
			goto out;
	}

	err = zap_lookup(dp->dp_meta_objset, DMU_POOL_DIRECTORY_OBJECT,
	    DMU_POOL_TMP_USERREFS, sizeof (uint64_t), 1,
	    &dp->dp_tmp_userrefs_obj);
	if (err == ENOENT)
		err = 0;
	if (err)
		goto out;

	err = dsl_scan_init(dp, dp->dp_tx.tx_open_txg);

out:
	rrw_exit(&dp->dp_config_rwlock, FTAG);
	return (err);
}

void
dsl_pool_close(dsl_pool_t *dp)
{
	/*
	 * Drop our references from dsl_pool_open().
	 *
	 * Since we held the origin_snap from "syncing" context (which
	 * includes pool-opening context), it actually only got a "ref"
	 * and not a hold, so just drop that here.
	 */
	if (dp->dp_origin_snap != NULL)
		dsl_dataset_rele(dp->dp_origin_snap, dp);
	if (dp->dp_mos_dir != NULL)
		dsl_dir_rele(dp->dp_mos_dir, dp);
	if (dp->dp_free_dir != NULL)
		dsl_dir_rele(dp->dp_free_dir, dp);
	if (dp->dp_leak_dir != NULL)
		dsl_dir_rele(dp->dp_leak_dir, dp);
	if (dp->dp_root_dir != NULL)
		dsl_dir_rele(dp->dp_root_dir, dp);

	bpobj_close(&dp->dp_free_bpobj);
	bpobj_close(&dp->dp_obsolete_bpobj);

	/* undo the dmu_objset_open_impl(mos) from dsl_pool_open() */
	if (dp->dp_meta_objset != NULL)
		dmu_objset_evict(dp->dp_meta_objset);

	txg_list_destroy(&dp->dp_dirty_datasets);
	txg_list_destroy(&dp->dp_dirty_zilogs);
	txg_list_destroy(&dp->dp_sync_tasks);
	txg_list_destroy(&dp->dp_early_sync_tasks);
	txg_list_destroy(&dp->dp_dirty_dirs);

	taskq_destroy(dp->dp_zil_clean_taskq);
	taskq_destroy(dp->dp_sync_taskq);

	/*
	 * We can't set retry to TRUE since we're explicitly specifying
	 * a spa to flush. This is good enough; any missed buffers for
	 * this spa won't cause trouble, and they'll eventually fall
	 * out of the ARC just like any other unused buffer.
	 */
	arc_flush(dp->dp_spa, FALSE);

	txg_fini(dp);
	dsl_scan_fini(dp);
	dmu_buf_user_evict_wait();

	rrw_destroy(&dp->dp_config_rwlock);
	mutex_destroy(&dp->dp_lock);
	taskq_destroy(dp->dp_vnrele_taskq);
	if (dp->dp_blkstats != NULL)
		kmem_free(dp->dp_blkstats, sizeof (zfs_all_blkstats_t));
	kmem_free(dp, sizeof (dsl_pool_t));
}

void
dsl_pool_create_obsolete_bpobj(dsl_pool_t *dp, dmu_tx_t *tx)
{
	uint64_t obj;
	/*
	 * Currently, we only create the obsolete_bpobj where there are
	 * indirect vdevs with referenced mappings.
	 */
	ASSERT(spa_feature_is_active(dp->dp_spa, SPA_FEATURE_DEVICE_REMOVAL));
	/* create and open the obsolete_bpobj */
	obj = bpobj_alloc(dp->dp_meta_objset, SPA_OLD_MAXBLOCKSIZE, tx);
	VERIFY0(bpobj_open(&dp->dp_obsolete_bpobj, dp->dp_meta_objset, obj));
	VERIFY0(zap_add(dp->dp_meta_objset, DMU_POOL_DIRECTORY_OBJECT,
	    DMU_POOL_OBSOLETE_BPOBJ, sizeof (uint64_t), 1, &obj, tx));
	spa_feature_incr(dp->dp_spa, SPA_FEATURE_OBSOLETE_COUNTS, tx);
}

void
dsl_pool_destroy_obsolete_bpobj(dsl_pool_t *dp, dmu_tx_t *tx)
{
	spa_feature_decr(dp->dp_spa, SPA_FEATURE_OBSOLETE_COUNTS, tx);
	VERIFY0(zap_remove(dp->dp_meta_objset,
	    DMU_POOL_DIRECTORY_OBJECT,
	    DMU_POOL_OBSOLETE_BPOBJ, tx));
	bpobj_free(dp->dp_meta_objset,
	    dp->dp_obsolete_bpobj.bpo_object, tx);
	bpobj_close(&dp->dp_obsolete_bpobj);
}

dsl_pool_t *
dsl_pool_create(spa_t *spa, nvlist_t *zplprops, uint64_t txg)
{
	int err;
	dsl_pool_t *dp = dsl_pool_open_impl(spa, txg);
	dmu_tx_t *tx = dmu_tx_create_assigned(dp, txg);
	dsl_dataset_t *ds;
	uint64_t obj;

	rrw_enter(&dp->dp_config_rwlock, RW_WRITER, FTAG);

	/* create and open the MOS (meta-objset) */
	dp->dp_meta_objset = dmu_objset_create_impl(spa,
	    NULL, &dp->dp_meta_rootbp, DMU_OST_META, tx);

	/* create the pool directory */
	err = zap_create_claim(dp->dp_meta_objset, DMU_POOL_DIRECTORY_OBJECT,
	    DMU_OT_OBJECT_DIRECTORY, DMU_OT_NONE, 0, tx);
	ASSERT0(err);

	/* Initialize scan structures */
	VERIFY0(dsl_scan_init(dp, txg));

	/* create and open the root dir */
	dp->dp_root_dir_obj = dsl_dir_create_sync(dp, NULL, NULL, tx);
	VERIFY0(dsl_dir_hold_obj(dp, dp->dp_root_dir_obj,
	    NULL, dp, &dp->dp_root_dir));

	/* create and open the meta-objset dir */
	(void) dsl_dir_create_sync(dp, dp->dp_root_dir, MOS_DIR_NAME, tx);
	VERIFY0(dsl_pool_open_special_dir(dp,
	    MOS_DIR_NAME, &dp->dp_mos_dir));

	if (spa_version(spa) >= SPA_VERSION_DEADLISTS) {
		/* create and open the free dir */
		(void) dsl_dir_create_sync(dp, dp->dp_root_dir,
		    FREE_DIR_NAME, tx);
		VERIFY0(dsl_pool_open_special_dir(dp,
		    FREE_DIR_NAME, &dp->dp_free_dir));

		/* create and open the free_bplist */
		obj = bpobj_alloc(dp->dp_meta_objset, SPA_OLD_MAXBLOCKSIZE, tx);
		VERIFY(zap_add(dp->dp_meta_objset, DMU_POOL_DIRECTORY_OBJECT,
		    DMU_POOL_FREE_BPOBJ, sizeof (uint64_t), 1, &obj, tx) == 0);
		VERIFY0(bpobj_open(&dp->dp_free_bpobj,
		    dp->dp_meta_objset, obj));
	}

	if (spa_version(spa) >= SPA_VERSION_DSL_SCRUB)
		dsl_pool_create_origin(dp, tx);

	/* create the root dataset */
	obj = dsl_dataset_create_sync_dd(dp->dp_root_dir, NULL, 0, tx);

	/* create the root objset */
	VERIFY0(dsl_dataset_hold_obj(dp, obj, FTAG, &ds));
#ifdef _KERNEL
	{
		objset_t *os;
		rrw_enter(&ds->ds_bp_rwlock, RW_READER, FTAG);
		os = dmu_objset_create_impl(dp->dp_spa, ds,
		    dsl_dataset_get_blkptr(ds), DMU_OST_ZFS, tx);
		rrw_exit(&ds->ds_bp_rwlock, FTAG);
		zfs_create_fs(os, kcred, zplprops, tx);
	}
#endif
	dsl_dataset_rele(ds, FTAG);

	dmu_tx_commit(tx);

	rrw_exit(&dp->dp_config_rwlock, FTAG);

	return (dp);
}

/*
 * Account for the meta-objset space in its placeholder dsl_dir.
 */
void
dsl_pool_mos_diduse_space(dsl_pool_t *dp,
    int64_t used, int64_t comp, int64_t uncomp)
{
	ASSERT3U(comp, ==, uncomp); /* it's all metadata */
	mutex_enter(&dp->dp_lock);
	dp->dp_mos_used_delta += used;
	dp->dp_mos_compressed_delta += comp;
	dp->dp_mos_uncompressed_delta += uncomp;
	mutex_exit(&dp->dp_lock);
}

static void
dsl_pool_sync_mos(dsl_pool_t *dp, dmu_tx_t *tx)
{
	zio_t *zio = zio_root(dp->dp_spa, NULL, NULL, ZIO_FLAG_MUSTSUCCEED);
	dmu_objset_sync(dp->dp_meta_objset, zio, tx);
	VERIFY0(zio_wait(zio));
	dprintf_bp(&dp->dp_meta_rootbp, "meta objset rootbp is %s", "");
	spa_set_rootblkptr(dp->dp_spa, &dp->dp_meta_rootbp);
}

static void
dsl_pool_dirty_delta(dsl_pool_t *dp, int64_t delta)
{
	ASSERT(MUTEX_HELD(&dp->dp_lock));

	if (delta < 0)
		ASSERT3U(-delta, <=, dp->dp_dirty_total);

	dp->dp_dirty_total += delta;

	/*
	 * Note: we signal even when increasing dp_dirty_total.
	 * This ensures forward progress -- each thread wakes the next waiter.
	 */
	if (dp->dp_dirty_total < zfs_dirty_data_max)
		cv_signal(&dp->dp_spaceavail_cv);
}

static boolean_t
dsl_early_sync_task_verify(dsl_pool_t *dp, uint64_t txg)
{
	spa_t *spa = dp->dp_spa;
	vdev_t *rvd = spa->spa_root_vdev;

	for (uint64_t c = 0; c < rvd->vdev_children; c++) {
		vdev_t *vd = rvd->vdev_child[c];
		txg_list_t *tl = &vd->vdev_ms_list;
		metaslab_t *ms;

		for (ms = txg_list_head(tl, TXG_CLEAN(txg)); ms;
		    ms = txg_list_next(tl, ms, TXG_CLEAN(txg))) {
			VERIFY(range_tree_is_empty(ms->ms_freeing));
			VERIFY(range_tree_is_empty(ms->ms_checkpointing));
		}
	}

	return (B_TRUE);
}

void
dsl_pool_sync(dsl_pool_t *dp, uint64_t txg)
{
	zio_t *zio;
	dmu_tx_t *tx;
	dsl_dir_t *dd;
	dsl_dataset_t *ds;
	objset_t *mos = dp->dp_meta_objset;
	list_t synced_datasets;

	list_create(&synced_datasets, sizeof (dsl_dataset_t),
	    offsetof(dsl_dataset_t, ds_synced_link));

	tx = dmu_tx_create_assigned(dp, txg);

	/*
	 * Run all early sync tasks before writing out any dirty blocks.
	 * For more info on early sync tasks see block comment in
	 * dsl_early_sync_task().
	 */
	if (!txg_list_empty(&dp->dp_early_sync_tasks, txg)) {
		dsl_sync_task_t *dst;

		ASSERT3U(spa_sync_pass(dp->dp_spa), ==, 1);
		while ((dst =
		    txg_list_remove(&dp->dp_early_sync_tasks, txg)) != NULL) {
			ASSERT(dsl_early_sync_task_verify(dp, txg));
			dsl_sync_task_sync(dst, tx);
		}
		ASSERT(dsl_early_sync_task_verify(dp, txg));
	}

	/*
	 * Write out all dirty blocks of dirty datasets.
	 */
	zio = zio_root(dp->dp_spa, NULL, NULL, ZIO_FLAG_MUSTSUCCEED);
	while ((ds = txg_list_remove(&dp->dp_dirty_datasets, txg)) != NULL) {
		/*
		 * We must not sync any non-MOS datasets twice, because
		 * we may have taken a snapshot of them.  However, we
		 * may sync newly-created datasets on pass 2.
		 */
		ASSERT(!list_link_active(&ds->ds_synced_link));
		list_insert_tail(&synced_datasets, ds);
		dsl_dataset_sync(ds, zio, tx);
	}
	VERIFY0(zio_wait(zio));

	/*
	 * We have written all of the accounted dirty data, so our
	 * dp_space_towrite should now be zero.  However, some seldom-used
	 * code paths do not adhere to this (e.g. dbuf_undirty(), also
	 * rounding error in dbuf_write_physdone).
	 * Shore up the accounting of any dirtied space now.
	 */
	dsl_pool_undirty_space(dp, dp->dp_dirty_pertxg[txg & TXG_MASK], txg);

	/*
	 * Update the long range free counter after
	 * we're done syncing user data
	 */
	mutex_enter(&dp->dp_lock);
	ASSERT(spa_sync_pass(dp->dp_spa) == 1 ||
	    dp->dp_long_free_dirty_pertxg[txg & TXG_MASK] == 0);
	dp->dp_long_free_dirty_pertxg[txg & TXG_MASK] = 0;
	mutex_exit(&dp->dp_lock);

	/*
	 * After the data blocks have been written (ensured by the zio_wait()
	 * above), update the user/group space accounting.  This happens
	 * in tasks dispatched to dp_sync_taskq, so wait for them before
	 * continuing.
	 */
	for (ds = list_head(&synced_datasets); ds != NULL;
	    ds = list_next(&synced_datasets, ds)) {
		dmu_objset_do_userquota_updates(ds->ds_objset, tx);
	}
	taskq_wait(dp->dp_sync_taskq);

	/*
	 * Sync the datasets again to push out the changes due to
	 * userspace updates.  This must be done before we process the
	 * sync tasks, so that any snapshots will have the correct
	 * user accounting information (and we won't get confused
	 * about which blocks are part of the snapshot).
	 */
	zio = zio_root(dp->dp_spa, NULL, NULL, ZIO_FLAG_MUSTSUCCEED);
	while ((ds = txg_list_remove(&dp->dp_dirty_datasets, txg)) != NULL) {
		ASSERT(list_link_active(&ds->ds_synced_link));
		dmu_buf_rele(ds->ds_dbuf, ds);
		dsl_dataset_sync(ds, zio, tx);
	}
	VERIFY0(zio_wait(zio));

	/*
	 * Now that the datasets have been completely synced, we can
	 * clean up our in-memory structures accumulated while syncing:
	 *
	 *  - move dead blocks from the pending deadlist to the on-disk deadlist
	 *  - release hold from dsl_dataset_dirty()
	 */
	while ((ds = list_remove_head(&synced_datasets)) != NULL) {
		dsl_dataset_sync_done(ds, tx);
	}
	while ((dd = txg_list_remove(&dp->dp_dirty_dirs, txg)) != NULL) {
		dsl_dir_sync(dd, tx);
	}

	/*
	 * The MOS's space is accounted for in the pool/$MOS
	 * (dp_mos_dir).  We can't modify the mos while we're syncing
	 * it, so we remember the deltas and apply them here.
	 */
	if (dp->dp_mos_used_delta != 0 || dp->dp_mos_compressed_delta != 0 ||
	    dp->dp_mos_uncompressed_delta != 0) {
		dsl_dir_diduse_space(dp->dp_mos_dir, DD_USED_HEAD,
		    dp->dp_mos_used_delta,
		    dp->dp_mos_compressed_delta,
		    dp->dp_mos_uncompressed_delta, tx);
		dp->dp_mos_used_delta = 0;
		dp->dp_mos_compressed_delta = 0;
		dp->dp_mos_uncompressed_delta = 0;
	}

	if (!multilist_is_empty(mos->os_dirty_dnodes[txg & TXG_MASK])) {
		dsl_pool_sync_mos(dp, tx);
	}

	/*
	 * If we modify a dataset in the same txg that we want to destroy it,
	 * its dsl_dir's dd_dbuf will be dirty, and thus have a hold on it.
	 * dsl_dir_destroy_check() will fail if there are unexpected holds.
	 * Therefore, we want to sync the MOS (thus syncing the dd_dbuf
	 * and clearing the hold on it) before we process the sync_tasks.
	 * The MOS data dirtied by the sync_tasks will be synced on the next
	 * pass.
	 */
	if (!txg_list_empty(&dp->dp_sync_tasks, txg)) {
		dsl_sync_task_t *dst;
		/*
		 * No more sync tasks should have been added while we
		 * were syncing.
		 */
		ASSERT3U(spa_sync_pass(dp->dp_spa), ==, 1);
		while ((dst = txg_list_remove(&dp->dp_sync_tasks, txg)) != NULL)
			dsl_sync_task_sync(dst, tx);
	}

	dmu_tx_commit(tx);

	DTRACE_PROBE2(dsl_pool_sync__done, dsl_pool_t *dp, dp, uint64_t, txg);
}

void
dsl_pool_sync_done(dsl_pool_t *dp, uint64_t txg)
{
	zilog_t *zilog;

	while (zilog = txg_list_head(&dp->dp_dirty_zilogs, txg)) {
		dsl_dataset_t *ds = dmu_objset_ds(zilog->zl_os);
		/*
		 * We don't remove the zilog from the dp_dirty_zilogs
		 * list until after we've cleaned it. This ensures that
		 * callers of zilog_is_dirty() receive an accurate
		 * answer when they are racing with the spa sync thread.
		 */
		zil_clean(zilog, txg);
		(void) txg_list_remove_this(&dp->dp_dirty_zilogs, zilog, txg);
		ASSERT(!dmu_objset_is_dirty(zilog->zl_os, txg));
		dmu_buf_rele(ds->ds_dbuf, zilog);
	}
	ASSERT(!dmu_objset_is_dirty(dp->dp_meta_objset, txg));
}

/*
 * TRUE if the current thread is the tx_sync_thread or if we
 * are being called from SPA context during pool initialization.
 */
int
dsl_pool_sync_context(dsl_pool_t *dp)
{
	return (curthread == dp->dp_tx.tx_sync_thread ||
	    spa_is_initializing(dp->dp_spa) ||
	    taskq_member(dp->dp_sync_taskq, curthread));
}

/*
 * This function returns the amount of allocatable space in the pool
 * minus whatever space is currently reserved by ZFS for specific
 * purposes. Specifically:
 *
 * 1] Any reserved SLOP space
 * 2] Any space used by the checkpoint
 * 3] Any space used for deferred frees
 *
 * The latter 2 are especially important because they are needed to
 * rectify the SPA's and DMU's different understanding of how much space
 * is used. Now the DMU is aware of that extra space tracked by the SPA
 * without having to maintain a separate special dir (e.g similar to
 * $MOS, $FREEING, and $LEAKED).
 *
 * Note: By deferred frees here, we mean the frees that were deferred
 * in spa_sync() after sync pass 1 (spa_deferred_bpobj), and not the
 * segments placed in ms_defer trees during metaslab_sync_done().
 */
uint64_t
dsl_pool_adjustedsize(dsl_pool_t *dp, zfs_space_check_t slop_policy)
{
	spa_t *spa = dp->dp_spa;
	uint64_t space, resv, adjustedsize;
	uint64_t spa_deferred_frees =
	    spa->spa_deferred_bpobj.bpo_phys->bpo_bytes;

	space = spa_get_dspace(spa)
	    - spa_get_checkpoint_space(spa) - spa_deferred_frees;
	resv = spa_get_slop_space(spa);

	switch (slop_policy) {
	case ZFS_SPACE_CHECK_NORMAL:
		break;
	case ZFS_SPACE_CHECK_RESERVED:
		resv >>= 1;
		break;
	case ZFS_SPACE_CHECK_EXTRA_RESERVED:
		resv >>= 2;
		break;
	case ZFS_SPACE_CHECK_NONE:
		resv = 0;
		break;
	default:
		panic("invalid slop policy value: %d", slop_policy);
		break;
	}
	adjustedsize = (space >= resv) ? (space - resv) : 0;

	return (adjustedsize);
}

uint64_t
dsl_pool_unreserved_space(dsl_pool_t *dp, zfs_space_check_t slop_policy)
{
	uint64_t poolsize = dsl_pool_adjustedsize(dp, slop_policy);
	uint64_t deferred =
	    metaslab_class_get_deferred(spa_normal_class(dp->dp_spa));
	uint64_t quota = (poolsize >= deferred) ? (poolsize - deferred) : 0;
	return (quota);
}

boolean_t
dsl_pool_need_dirty_delay(dsl_pool_t *dp)
{
	uint64_t delay_min_bytes =
	    zfs_dirty_data_max * zfs_delay_min_dirty_percent / 100;
	boolean_t rv;

	mutex_enter(&dp->dp_lock);
	if (dp->dp_dirty_total > zfs_dirty_data_sync)
		txg_kick(dp);
	rv = (dp->dp_dirty_total > delay_min_bytes);
	mutex_exit(&dp->dp_lock);
	return (rv);
}

void
dsl_pool_dirty_space(dsl_pool_t *dp, int64_t space, dmu_tx_t *tx)
{
	if (space > 0) {
		mutex_enter(&dp->dp_lock);
		dp->dp_dirty_pertxg[tx->tx_txg & TXG_MASK] += space;
		dsl_pool_dirty_delta(dp, space);
		mutex_exit(&dp->dp_lock);
	}
}

void
dsl_pool_undirty_space(dsl_pool_t *dp, int64_t space, uint64_t txg)
{
	ASSERT3S(space, >=, 0);
	if (space == 0)
		return;
	mutex_enter(&dp->dp_lock);
	if (dp->dp_dirty_pertxg[txg & TXG_MASK] < space) {
		/* XXX writing something we didn't dirty? */
		space = dp->dp_dirty_pertxg[txg & TXG_MASK];
	}
	ASSERT3U(dp->dp_dirty_pertxg[txg & TXG_MASK], >=, space);
	dp->dp_dirty_pertxg[txg & TXG_MASK] -= space;
	ASSERT3U(dp->dp_dirty_total, >=, space);
	dsl_pool_dirty_delta(dp, -space);
	mutex_exit(&dp->dp_lock);
}

/* ARGSUSED */
static int
upgrade_clones_cb(dsl_pool_t *dp, dsl_dataset_t *hds, void *arg)
{
	dmu_tx_t *tx = arg;
	dsl_dataset_t *ds, *prev = NULL;
	int err;

	err = dsl_dataset_hold_obj(dp, hds->ds_object, FTAG, &ds);
	if (err)
		return (err);

	while (dsl_dataset_phys(ds)->ds_prev_snap_obj != 0) {
		err = dsl_dataset_hold_obj(dp,
		    dsl_dataset_phys(ds)->ds_prev_snap_obj, FTAG, &prev);
		if (err) {
			dsl_dataset_rele(ds, FTAG);
			return (err);
		}

		if (dsl_dataset_phys(prev)->ds_next_snap_obj != ds->ds_object)
			break;
		dsl_dataset_rele(ds, FTAG);
		ds = prev;
		prev = NULL;
	}

	if (prev == NULL) {
		prev = dp->dp_origin_snap;

		/*
		 * The $ORIGIN can't have any data, or the accounting
		 * will be wrong.
		 */
		rrw_enter(&ds->ds_bp_rwlock, RW_READER, FTAG);
		ASSERT0(dsl_dataset_phys(prev)->ds_bp.blk_birth);
		rrw_exit(&ds->ds_bp_rwlock, FTAG);

		/* The origin doesn't get attached to itself */
		if (ds->ds_object == prev->ds_object) {
			dsl_dataset_rele(ds, FTAG);
			return (0);
		}

		dmu_buf_will_dirty(ds->ds_dbuf, tx);
		dsl_dataset_phys(ds)->ds_prev_snap_obj = prev->ds_object;
		dsl_dataset_phys(ds)->ds_prev_snap_txg =
		    dsl_dataset_phys(prev)->ds_creation_txg;

		dmu_buf_will_dirty(ds->ds_dir->dd_dbuf, tx);
		dsl_dir_phys(ds->ds_dir)->dd_origin_obj = prev->ds_object;

		dmu_buf_will_dirty(prev->ds_dbuf, tx);
		dsl_dataset_phys(prev)->ds_num_children++;

		if (dsl_dataset_phys(ds)->ds_next_snap_obj == 0) {
			ASSERT(ds->ds_prev == NULL);
			VERIFY0(dsl_dataset_hold_obj(dp,
			    dsl_dataset_phys(ds)->ds_prev_snap_obj,
			    ds, &ds->ds_prev));
		}
	}

	ASSERT3U(dsl_dir_phys(ds->ds_dir)->dd_origin_obj, ==, prev->ds_object);
	ASSERT3U(dsl_dataset_phys(ds)->ds_prev_snap_obj, ==, prev->ds_object);

	if (dsl_dataset_phys(prev)->ds_next_clones_obj == 0) {
		dmu_buf_will_dirty(prev->ds_dbuf, tx);
		dsl_dataset_phys(prev)->ds_next_clones_obj =
		    zap_create(dp->dp_meta_objset,
		    DMU_OT_NEXT_CLONES, DMU_OT_NONE, 0, tx);
	}
	VERIFY0(zap_add_int(dp->dp_meta_objset,
	    dsl_dataset_phys(prev)->ds_next_clones_obj, ds->ds_object, tx));

	dsl_dataset_rele(ds, FTAG);
	if (prev != dp->dp_origin_snap)
		dsl_dataset_rele(prev, FTAG);
	return (0);
}

void
dsl_pool_upgrade_clones(dsl_pool_t *dp, dmu_tx_t *tx)
{
	ASSERT(dmu_tx_is_syncing(tx));
	ASSERT(dp->dp_origin_snap != NULL);

	VERIFY0(dmu_objset_find_dp(dp, dp->dp_root_dir_obj, upgrade_clones_cb,
	    tx, DS_FIND_CHILDREN | DS_FIND_SERIALIZE));
}

/* ARGSUSED */
static int
upgrade_dir_clones_cb(dsl_pool_t *dp, dsl_dataset_t *ds, void *arg)
{
	dmu_tx_t *tx = arg;
	objset_t *mos = dp->dp_meta_objset;

	if (dsl_dir_phys(ds->ds_dir)->dd_origin_obj != 0) {
		dsl_dataset_t *origin;

		VERIFY0(dsl_dataset_hold_obj(dp,
		    dsl_dir_phys(ds->ds_dir)->dd_origin_obj, FTAG, &origin));

		if (dsl_dir_phys(origin->ds_dir)->dd_clones == 0) {
			dmu_buf_will_dirty(origin->ds_dir->dd_dbuf, tx);
			dsl_dir_phys(origin->ds_dir)->dd_clones =
			    zap_create(mos, DMU_OT_DSL_CLONES, DMU_OT_NONE,
			    0, tx);
		}

		VERIFY0(zap_add_int(dp->dp_meta_objset,
		    dsl_dir_phys(origin->ds_dir)->dd_clones,
		    ds->ds_object, tx));

		dsl_dataset_rele(origin, FTAG);
	}
	return (0);
}

void
dsl_pool_upgrade_dir_clones(dsl_pool_t *dp, dmu_tx_t *tx)
{
	ASSERT(dmu_tx_is_syncing(tx));
	uint64_t obj;

	(void) dsl_dir_create_sync(dp, dp->dp_root_dir, FREE_DIR_NAME, tx);
	VERIFY0(dsl_pool_open_special_dir(dp,
	    FREE_DIR_NAME, &dp->dp_free_dir));

	/*
	 * We can't use bpobj_alloc(), because spa_version() still
	 * returns the old version, and we need a new-version bpobj with
	 * subobj support.  So call dmu_object_alloc() directly.
	 */
	obj = dmu_object_alloc(dp->dp_meta_objset, DMU_OT_BPOBJ,
	    SPA_OLD_MAXBLOCKSIZE, DMU_OT_BPOBJ_HDR, sizeof (bpobj_phys_t), tx);
	VERIFY0(zap_add(dp->dp_meta_objset, DMU_POOL_DIRECTORY_OBJECT,
	    DMU_POOL_FREE_BPOBJ, sizeof (uint64_t), 1, &obj, tx));
	VERIFY0(bpobj_open(&dp->dp_free_bpobj, dp->dp_meta_objset, obj));

	VERIFY0(dmu_objset_find_dp(dp, dp->dp_root_dir_obj,
	    upgrade_dir_clones_cb, tx, DS_FIND_CHILDREN | DS_FIND_SERIALIZE));
}

void
dsl_pool_create_origin(dsl_pool_t *dp, dmu_tx_t *tx)
{
	uint64_t dsobj;
	dsl_dataset_t *ds;

	ASSERT(dmu_tx_is_syncing(tx));
	ASSERT(dp->dp_origin_snap == NULL);
	ASSERT(rrw_held(&dp->dp_config_rwlock, RW_WRITER));

	/* create the origin dir, ds, & snap-ds */
	dsobj = dsl_dataset_create_sync(dp->dp_root_dir, ORIGIN_DIR_NAME,
	    NULL, 0, kcred, tx);
	VERIFY0(dsl_dataset_hold_obj(dp, dsobj, FTAG, &ds));
	dsl_dataset_snapshot_sync_impl(ds, ORIGIN_DIR_NAME, tx);
	VERIFY0(dsl_dataset_hold_obj(dp, dsl_dataset_phys(ds)->ds_prev_snap_obj,
	    dp, &dp->dp_origin_snap));
	dsl_dataset_rele(ds, FTAG);
}

taskq_t *
dsl_pool_vnrele_taskq(dsl_pool_t *dp)
{
	return (dp->dp_vnrele_taskq);
}

/*
 * Walk through the pool-wide zap object of temporary snapshot user holds
 * and release them.
 */
void
dsl_pool_clean_tmp_userrefs(dsl_pool_t *dp)
{
	zap_attribute_t za;
	zap_cursor_t zc;
	objset_t *mos = dp->dp_meta_objset;
	uint64_t zapobj = dp->dp_tmp_userrefs_obj;
	nvlist_t *holds;

	if (zapobj == 0)
		return;
	ASSERT(spa_version(dp->dp_spa) >= SPA_VERSION_USERREFS);

	holds = fnvlist_alloc();

	for (zap_cursor_init(&zc, mos, zapobj);
	    zap_cursor_retrieve(&zc, &za) == 0;
	    zap_cursor_advance(&zc)) {
		char *htag;
		nvlist_t *tags;

		htag = strchr(za.za_name, '-');
		*htag = '\0';
		++htag;
		if (nvlist_lookup_nvlist(holds, za.za_name, &tags) != 0) {
			tags = fnvlist_alloc();
			fnvlist_add_boolean(tags, htag);
			fnvlist_add_nvlist(holds, za.za_name, tags);
			fnvlist_free(tags);
		} else {
			fnvlist_add_boolean(tags, htag);
		}
	}
	dsl_dataset_user_release_tmp(dp, holds);
	fnvlist_free(holds);
	zap_cursor_fini(&zc);
}

/*
 * Create the pool-wide zap object for storing temporary snapshot holds.
 */
void
dsl_pool_user_hold_create_obj(dsl_pool_t *dp, dmu_tx_t *tx)
{
	objset_t *mos = dp->dp_meta_objset;

	ASSERT(dp->dp_tmp_userrefs_obj == 0);
	ASSERT(dmu_tx_is_syncing(tx));

	dp->dp_tmp_userrefs_obj = zap_create_link(mos, DMU_OT_USERREFS,
	    DMU_POOL_DIRECTORY_OBJECT, DMU_POOL_TMP_USERREFS, tx);
}

static int
dsl_pool_user_hold_rele_impl(dsl_pool_t *dp, uint64_t dsobj,
    const char *tag, uint64_t now, dmu_tx_t *tx, boolean_t holding)
{
	objset_t *mos = dp->dp_meta_objset;
	uint64_t zapobj = dp->dp_tmp_userrefs_obj;
	char *name;
	int error;

	ASSERT(spa_version(dp->dp_spa) >= SPA_VERSION_USERREFS);
	ASSERT(dmu_tx_is_syncing(tx));

	/*
	 * If the pool was created prior to SPA_VERSION_USERREFS, the
	 * zap object for temporary holds might not exist yet.
	 */
	if (zapobj == 0) {
		if (holding) {
			dsl_pool_user_hold_create_obj(dp, tx);
			zapobj = dp->dp_tmp_userrefs_obj;
		} else {
			return (SET_ERROR(ENOENT));
		}
	}

	name = kmem_asprintf("%llx-%s", (u_longlong_t)dsobj, tag);
	if (holding)
		error = zap_add(mos, zapobj, name, 8, 1, &now, tx);
	else
		error = zap_remove(mos, zapobj, name, tx);
	strfree(name);

	return (error);
}

/*
 * Add a temporary hold for the given dataset object and tag.
 */
int
dsl_pool_user_hold(dsl_pool_t *dp, uint64_t dsobj, const char *tag,
    uint64_t now, dmu_tx_t *tx)
{
	return (dsl_pool_user_hold_rele_impl(dp, dsobj, tag, now, tx, B_TRUE));
}

/*
 * Release a temporary hold for the given dataset object and tag.
 */
int
dsl_pool_user_release(dsl_pool_t *dp, uint64_t dsobj, const char *tag,
    dmu_tx_t *tx)
{
	return (dsl_pool_user_hold_rele_impl(dp, dsobj, tag, 0,
	    tx, B_FALSE));
}

/*
 * DSL Pool Configuration Lock
 *
 * The dp_config_rwlock protects against changes to DSL state (e.g. dataset
 * creation / destruction / rename / property setting).  It must be held for
 * read to hold a dataset or dsl_dir.  I.e. you must call
 * dsl_pool_config_enter() or dsl_pool_hold() before calling
 * dsl_{dataset,dir}_hold{_obj}.  In most circumstances, the dp_config_rwlock
 * must be held continuously until all datasets and dsl_dirs are released.
 *
 * The only exception to this rule is that if a "long hold" is placed on
 * a dataset, then the dp_config_rwlock may be dropped while the dataset
 * is still held.  The long hold will prevent the dataset from being
 * destroyed -- the destroy will fail with EBUSY.  A long hold can be
 * obtained by calling dsl_dataset_long_hold(), or by "owning" a dataset
 * (by calling dsl_{dataset,objset}_{try}own{_obj}).
 *
 * Legitimate long-holders (including owners) should be long-running, cancelable
 * tasks that should cause "zfs destroy" to fail.  This includes DMU
 * consumers (i.e. a ZPL filesystem being mounted or ZVOL being open),
 * "zfs send", and "zfs diff".  There are several other long-holders whose
 * uses are suboptimal (e.g. "zfs promote", and zil_suspend()).
 *
 * The usual formula for long-holding would be:
 * dsl_pool_hold()
 * dsl_dataset_hold()
 * ... perform checks ...
 * dsl_dataset_long_hold()
 * dsl_pool_rele()
 * ... perform long-running task ...
 * dsl_dataset_long_rele()
 * dsl_dataset_rele()
 *
 * Note that when the long hold is released, the dataset is still held but
 * the pool is not held.  The dataset may change arbitrarily during this time
 * (e.g. it could be destroyed).  Therefore you shouldn't do anything to the
 * dataset except release it.
 *
 * User-initiated operations (e.g. ioctls, zfs_ioc_*()) are either read-only
 * or modifying operations.
 *
 * Modifying operations should generally use dsl_sync_task().  The synctask
 * infrastructure enforces proper locking strategy with respect to the
 * dp_config_rwlock.  See the comment above dsl_sync_task() for details.
 *
 * Read-only operations will manually hold the pool, then the dataset, obtain
 * information from the dataset, then release the pool and dataset.
 * dmu_objset_{hold,rele}() are convenience routines that also do the pool
 * hold/rele.
 */

int
dsl_pool_hold(const char *name, void *tag, dsl_pool_t **dp)
{
	spa_t *spa;
	int error;

	error = spa_open(name, &spa, tag);
	if (error == 0) {
		*dp = spa_get_dsl(spa);
		dsl_pool_config_enter(*dp, tag);
	}
	return (error);
}

void
dsl_pool_rele(dsl_pool_t *dp, void *tag)
{
	dsl_pool_config_exit(dp, tag);
	spa_close(dp->dp_spa, tag);
}

void
dsl_pool_config_enter(dsl_pool_t *dp, void *tag)
{
	/*
	 * We use a "reentrant" reader-writer lock, but not reentrantly.
	 *
	 * The rrwlock can (with the track_all flag) track all reading threads,
	 * which is very useful for debugging which code path failed to release
	 * the lock, and for verifying that the *current* thread does hold
	 * the lock.
	 *
	 * (Unlike a rwlock, which knows that N threads hold it for
	 * read, but not *which* threads, so rw_held(RW_READER) returns TRUE
	 * if any thread holds it for read, even if this thread doesn't).
	 */
	ASSERT(!rrw_held(&dp->dp_config_rwlock, RW_READER));
	rrw_enter(&dp->dp_config_rwlock, RW_READER, tag);
}

void
dsl_pool_config_enter_prio(dsl_pool_t *dp, void *tag)
{
	ASSERT(!rrw_held(&dp->dp_config_rwlock, RW_READER));
	rrw_enter_read_prio(&dp->dp_config_rwlock, tag);
}

void
dsl_pool_config_exit(dsl_pool_t *dp, void *tag)
{
	rrw_exit(&dp->dp_config_rwlock, tag);
}

boolean_t
dsl_pool_config_held(dsl_pool_t *dp)
{
	return (RRW_LOCK_HELD(&dp->dp_config_rwlock));
}

boolean_t
dsl_pool_config_held_writer(dsl_pool_t *dp)
{
	return (RRW_WRITE_HELD(&dp->dp_config_rwlock));
}
