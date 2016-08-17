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
 * Copyright (c) 2008, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2011, 2016 by Delphix. All rights reserved.
 * Copyright 2016 Gary Mills
 * Copyright (c) 2017 Datto Inc.
 * Copyright 2017 Joyent, Inc.
 */

#include <sys/dsl_scan.h>
#include <sys/dsl_pool.h>
#include <sys/dsl_dataset.h>
#include <sys/dsl_prop.h>
#include <sys/dsl_dir.h>
#include <sys/dsl_synctask.h>
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
#include <sys/vdev_impl.h>
#include <sys/zil_impl.h>
#include <sys/zio_checksum.h>
#include <sys/ddt.h>
#include <sys/sa.h>
#include <sys/sa_impl.h>
#include <sys/zfeature.h>
#include <sys/abd.h>
#ifdef _KERNEL
#include <sys/zfs_vfsops.h>
#endif

typedef int (scan_cb_t)(dsl_pool_t *, const blkptr_t *,
    const zbookmark_phys_t *);

static scan_cb_t dsl_scan_scrub_cb;
static void dsl_scan_cancel_sync(void *, dmu_tx_t *);
static void dsl_scan_sync_state(dsl_scan_t *, dmu_tx_t *);
static boolean_t dsl_scan_restarting(dsl_scan_t *, dmu_tx_t *);

int zfs_top_maxinflight = 32;		/* maximum I/Os per top-level */
int zfs_resilver_delay = 2;		/* number of ticks to delay resilver */
int zfs_scrub_delay = 4;		/* number of ticks to delay scrub */
int zfs_scan_idle = 50;			/* idle window in clock ticks */

int zfs_scan_min_time_ms = 1000; /* min millisecs to scrub per txg */
int zfs_free_min_time_ms = 1000; /* min millisecs to free per txg */
int zfs_resilver_min_time_ms = 3000; /* min millisecs to resilver per txg */
int zfs_no_scrub_io = B_FALSE; /* set to disable scrub i/o */
int zfs_no_scrub_prefetch = B_FALSE; /* set to disable scrub prefetch */
enum ddt_class zfs_scrub_ddt_class_max = DDT_CLASS_DUPLICATE;
int dsl_scan_delay_completion = B_FALSE; /* set to delay scan completion */
/* max number of blocks to free in a single TXG */
unsigned long zfs_free_max_blocks = 100000;

#define	DSL_SCAN_IS_SCRUB_RESILVER(scn) \
	((scn)->scn_phys.scn_func == POOL_SCAN_SCRUB || \
	(scn)->scn_phys.scn_func == POOL_SCAN_RESILVER)

/*
 * Enable/disable the processing of the free_bpobj object.
 */
int zfs_free_bpobj_enabled = 1;

/* the order has to match pool_scan_type */
static scan_cb_t *scan_funcs[POOL_SCAN_FUNCS] = {
	NULL,
	dsl_scan_scrub_cb,	/* POOL_SCAN_SCRUB */
	dsl_scan_scrub_cb,	/* POOL_SCAN_RESILVER */
};

int
dsl_scan_init(dsl_pool_t *dp, uint64_t txg)
{
	int err;
	dsl_scan_t *scn;
	spa_t *spa = dp->dp_spa;
	uint64_t f;

	scn = dp->dp_scan = kmem_zalloc(sizeof (dsl_scan_t), KM_SLEEP);
	scn->scn_dp = dp;

	/*
	 * It's possible that we're resuming a scan after a reboot so
	 * make sure that the scan_async_destroying flag is initialized
	 * appropriately.
	 */
	ASSERT(!scn->scn_async_destroying);
	scn->scn_async_destroying = spa_feature_is_active(dp->dp_spa,
	    SPA_FEATURE_ASYNC_DESTROY);

	err = zap_lookup(dp->dp_meta_objset, DMU_POOL_DIRECTORY_OBJECT,
	    "scrub_func", sizeof (uint64_t), 1, &f);
	if (err == 0) {
		/*
		 * There was an old-style scrub in progress.  Restart a
		 * new-style scrub from the beginning.
		 */
		scn->scn_restart_txg = txg;
		zfs_dbgmsg("old-style scrub was in progress; "
		    "restarting new-style scrub in txg %llu",
		    scn->scn_restart_txg);

		/*
		 * Load the queue obj from the old location so that it
		 * can be freed by dsl_scan_done().
		 */
		(void) zap_lookup(dp->dp_meta_objset, DMU_POOL_DIRECTORY_OBJECT,
		    "scrub_queue", sizeof (uint64_t), 1,
		    &scn->scn_phys.scn_queue_obj);
	} else {
		err = zap_lookup(dp->dp_meta_objset, DMU_POOL_DIRECTORY_OBJECT,
		    DMU_POOL_SCAN, sizeof (uint64_t), SCAN_PHYS_NUMINTS,
		    &scn->scn_phys);
		/*
		 * Detect if the pool contains the signature of #2094.  If it
		 * does properly update the scn->scn_phys structure and notify
		 * the administrator by setting an errata for the pool.
		 */
		if (err == EOVERFLOW) {
			uint64_t zaptmp[SCAN_PHYS_NUMINTS + 1];
			VERIFY3S(SCAN_PHYS_NUMINTS, ==, 24);
			VERIFY3S(offsetof(dsl_scan_phys_t, scn_flags), ==,
			    (23 * sizeof (uint64_t)));

			err = zap_lookup(dp->dp_meta_objset,
			    DMU_POOL_DIRECTORY_OBJECT, DMU_POOL_SCAN,
			    sizeof (uint64_t), SCAN_PHYS_NUMINTS + 1, &zaptmp);
			if (err == 0) {
				uint64_t overflow = zaptmp[SCAN_PHYS_NUMINTS];

				if (overflow & ~DSL_SCAN_FLAGS_MASK ||
				    scn->scn_async_destroying) {
					spa->spa_errata =
					    ZPOOL_ERRATA_ZOL_2094_ASYNC_DESTROY;
					return (EOVERFLOW);
				}

				bcopy(zaptmp, &scn->scn_phys,
				    SCAN_PHYS_NUMINTS * sizeof (uint64_t));
				scn->scn_phys.scn_flags = overflow;

				/* Required scrub already in progress. */
				if (scn->scn_phys.scn_state == DSS_FINISHED ||
				    scn->scn_phys.scn_state == DSS_CANCELED)
					spa->spa_errata =
					    ZPOOL_ERRATA_ZOL_2094_SCRUB;
			}
		}

		if (err == ENOENT)
			return (0);
		else if (err)
			return (err);

		if (scn->scn_phys.scn_state == DSS_SCANNING &&
		    spa_prev_software_version(dp->dp_spa) < SPA_VERSION_SCAN) {
			/*
			 * A new-type scrub was in progress on an old
			 * pool, and the pool was accessed by old
			 * software.  Restart from the beginning, since
			 * the old software may have changed the pool in
			 * the meantime.
			 */
			scn->scn_restart_txg = txg;
			zfs_dbgmsg("new-style scrub was modified "
			    "by old software; restarting in txg %llu",
			    scn->scn_restart_txg);
		}
	}

	spa_scan_stat_init(spa);
	return (0);
}

void
dsl_scan_fini(dsl_pool_t *dp)
{
	if (dp->dp_scan) {
		kmem_free(dp->dp_scan, sizeof (dsl_scan_t));
		dp->dp_scan = NULL;
	}
}

/* ARGSUSED */
static int
dsl_scan_setup_check(void *arg, dmu_tx_t *tx)
{
	dsl_scan_t *scn = dmu_tx_pool(tx)->dp_scan;

	if (scn->scn_phys.scn_state == DSS_SCANNING)
		return (SET_ERROR(EBUSY));

	return (0);
}

static void
dsl_scan_setup_sync(void *arg, dmu_tx_t *tx)
{
	dsl_scan_t *scn = dmu_tx_pool(tx)->dp_scan;
	pool_scan_func_t *funcp = arg;
	dmu_object_type_t ot = 0;
	dsl_pool_t *dp = scn->scn_dp;
	spa_t *spa = dp->dp_spa;

	ASSERT(scn->scn_phys.scn_state != DSS_SCANNING);
	ASSERT(*funcp > POOL_SCAN_NONE && *funcp < POOL_SCAN_FUNCS);
	bzero(&scn->scn_phys, sizeof (scn->scn_phys));
	scn->scn_phys.scn_func = *funcp;
	scn->scn_phys.scn_state = DSS_SCANNING;
	scn->scn_phys.scn_min_txg = 0;
	scn->scn_phys.scn_max_txg = tx->tx_txg;
	scn->scn_phys.scn_ddt_class_max = DDT_CLASSES - 1; /* the entire DDT */
	scn->scn_phys.scn_start_time = gethrestime_sec();
	scn->scn_phys.scn_errors = 0;
	scn->scn_phys.scn_to_examine = spa->spa_root_vdev->vdev_stat.vs_alloc;
	scn->scn_restart_txg = 0;
	scn->scn_done_txg = 0;
	spa_scan_stat_init(spa);

	if (DSL_SCAN_IS_SCRUB_RESILVER(scn)) {
		scn->scn_phys.scn_ddt_class_max = zfs_scrub_ddt_class_max;

		/* rewrite all disk labels */
		vdev_config_dirty(spa->spa_root_vdev);

		if (vdev_resilver_needed(spa->spa_root_vdev,
		    &scn->scn_phys.scn_min_txg, &scn->scn_phys.scn_max_txg)) {
			spa_event_notify(spa, NULL, NULL,
			    ESC_ZFS_RESILVER_START);
		} else {
			spa_event_notify(spa, NULL, NULL, ESC_ZFS_SCRUB_START);
		}

		spa->spa_scrub_started = B_TRUE;
		/*
		 * If this is an incremental scrub, limit the DDT scrub phase
		 * to just the auto-ditto class (for correctness); the rest
		 * of the scrub should go faster using top-down pruning.
		 */
		if (scn->scn_phys.scn_min_txg > TXG_INITIAL)
			scn->scn_phys.scn_ddt_class_max = DDT_CLASS_DITTO;

	}

	/* back to the generic stuff */

	if (dp->dp_blkstats == NULL) {
		dp->dp_blkstats =
		    vmem_alloc(sizeof (zfs_all_blkstats_t), KM_SLEEP);
	}
	bzero(dp->dp_blkstats, sizeof (zfs_all_blkstats_t));

	if (spa_version(spa) < SPA_VERSION_DSL_SCRUB)
		ot = DMU_OT_ZAP_OTHER;

	scn->scn_phys.scn_queue_obj = zap_create(dp->dp_meta_objset,
	    ot ? ot : DMU_OT_SCAN_QUEUE, DMU_OT_NONE, 0, tx);

	dsl_scan_sync_state(scn, tx);

	spa_history_log_internal(spa, "scan setup", tx,
	    "func=%u mintxg=%llu maxtxg=%llu",
	    *funcp, scn->scn_phys.scn_min_txg, scn->scn_phys.scn_max_txg);
}

/* ARGSUSED */
static void
dsl_scan_done(dsl_scan_t *scn, boolean_t complete, dmu_tx_t *tx)
{
	static const char *old_names[] = {
		"scrub_bookmark",
		"scrub_ddt_bookmark",
		"scrub_ddt_class_max",
		"scrub_queue",
		"scrub_min_txg",
		"scrub_max_txg",
		"scrub_func",
		"scrub_errors",
		NULL
	};

	dsl_pool_t *dp = scn->scn_dp;
	spa_t *spa = dp->dp_spa;
	int i;

	/* Remove any remnants of an old-style scrub. */
	for (i = 0; old_names[i]; i++) {
		(void) zap_remove(dp->dp_meta_objset,
		    DMU_POOL_DIRECTORY_OBJECT, old_names[i], tx);
	}

	if (scn->scn_phys.scn_queue_obj != 0) {
		VERIFY(0 == dmu_object_free(dp->dp_meta_objset,
		    scn->scn_phys.scn_queue_obj, tx));
		scn->scn_phys.scn_queue_obj = 0;
	}

	scn->scn_phys.scn_flags &= ~DSF_SCRUB_PAUSED;

	/*
	 * If we were "restarted" from a stopped state, don't bother
	 * with anything else.
	 */
	if (scn->scn_phys.scn_state != DSS_SCANNING)
		return;

	if (complete)
		scn->scn_phys.scn_state = DSS_FINISHED;
	else
		scn->scn_phys.scn_state = DSS_CANCELED;

	if (dsl_scan_restarting(scn, tx))
		spa_history_log_internal(spa, "scan aborted, restarting", tx,
		    "errors=%llu", spa_get_errlog_size(spa));
	else if (!complete)
		spa_history_log_internal(spa, "scan cancelled", tx,
		    "errors=%llu", spa_get_errlog_size(spa));
	else
		spa_history_log_internal(spa, "scan done", tx,
		    "errors=%llu", spa_get_errlog_size(spa));

	if (DSL_SCAN_IS_SCRUB_RESILVER(scn)) {
		mutex_enter(&spa->spa_scrub_lock);
		while (spa->spa_scrub_inflight > 0) {
			cv_wait(&spa->spa_scrub_io_cv,
			    &spa->spa_scrub_lock);
		}
		mutex_exit(&spa->spa_scrub_lock);
		spa->spa_scrub_started = B_FALSE;
		spa->spa_scrub_active = B_FALSE;

		/*
		 * If the scrub/resilver completed, update all DTLs to
		 * reflect this.  Whether it succeeded or not, vacate
		 * all temporary scrub DTLs.
		 */
		vdev_dtl_reassess(spa->spa_root_vdev, tx->tx_txg,
		    complete ? scn->scn_phys.scn_max_txg : 0, B_TRUE);
		if (complete) {
			spa_event_notify(spa, NULL, NULL,
			    scn->scn_phys.scn_min_txg ?
			    ESC_ZFS_RESILVER_FINISH : ESC_ZFS_SCRUB_FINISH);
		}
		spa_errlog_rotate(spa);

		/*
		 * We may have finished replacing a device.
		 * Let the async thread assess this and handle the detach.
		 */
		spa_async_request(spa, SPA_ASYNC_RESILVER_DONE);
	}

	scn->scn_phys.scn_end_time = gethrestime_sec();

	if (spa->spa_errata == ZPOOL_ERRATA_ZOL_2094_SCRUB)
		spa->spa_errata = 0;
}

/* ARGSUSED */
static int
dsl_scan_cancel_check(void *arg, dmu_tx_t *tx)
{
	dsl_scan_t *scn = dmu_tx_pool(tx)->dp_scan;

	if (scn->scn_phys.scn_state != DSS_SCANNING)
		return (SET_ERROR(ENOENT));
	return (0);
}

/* ARGSUSED */
static void
dsl_scan_cancel_sync(void *arg, dmu_tx_t *tx)
{
	dsl_scan_t *scn = dmu_tx_pool(tx)->dp_scan;

	dsl_scan_done(scn, B_FALSE, tx);
	dsl_scan_sync_state(scn, tx);
}

int
dsl_scan_cancel(dsl_pool_t *dp)
{
	return (dsl_sync_task(spa_name(dp->dp_spa), dsl_scan_cancel_check,
	    dsl_scan_cancel_sync, NULL, 3, ZFS_SPACE_CHECK_RESERVED));
}

boolean_t
dsl_scan_is_paused_scrub(const dsl_scan_t *scn)
{
	if (dsl_scan_scrubbing(scn->scn_dp) &&
	    scn->scn_phys.scn_flags & DSF_SCRUB_PAUSED)
		return (B_TRUE);

	return (B_FALSE);
}

static int
dsl_scrub_pause_resume_check(void *arg, dmu_tx_t *tx)
{
	pool_scrub_cmd_t *cmd = arg;
	dsl_pool_t *dp = dmu_tx_pool(tx);
	dsl_scan_t *scn = dp->dp_scan;

	if (*cmd == POOL_SCRUB_PAUSE) {
		/* can't pause a scrub when there is no in-progress scrub */
		if (!dsl_scan_scrubbing(dp))
			return (SET_ERROR(ENOENT));

		/* can't pause a paused scrub */
		if (dsl_scan_is_paused_scrub(scn))
			return (SET_ERROR(EBUSY));
	} else if (*cmd != POOL_SCRUB_NORMAL) {
		return (SET_ERROR(ENOTSUP));
	}

	return (0);
}

static void
dsl_scrub_pause_resume_sync(void *arg, dmu_tx_t *tx)
{
	pool_scrub_cmd_t *cmd = arg;
	dsl_pool_t *dp = dmu_tx_pool(tx);
	spa_t *spa = dp->dp_spa;
	dsl_scan_t *scn = dp->dp_scan;


	if (*cmd == POOL_SCRUB_PAUSE) {
		/* can't pause a scrub when there is no in-progress scrub */
		spa->spa_scan_pass_scrub_pause = gethrestime_sec();
		scn->scn_phys.scn_flags |= DSF_SCRUB_PAUSED;
		dsl_scan_sync_state(scn, tx);
	} else {
		ASSERT3U(*cmd, ==, POOL_SCRUB_NORMAL);
		if (dsl_scan_is_paused_scrub(scn)) {
			/*
			 * We need to keep track of how much time we spend
			 * paused per pass so that we can adjust the scrub rate
			 * shown in the output of 'zpool status'
			 */
			spa->spa_scan_pass_scrub_spent_paused +=
			    gethrestime_sec() - spa->spa_scan_pass_scrub_pause;
			spa->spa_scan_pass_scrub_pause = 0;
			scn->scn_phys.scn_flags &= ~DSF_SCRUB_PAUSED;
			dsl_scan_sync_state(scn, tx);
		}
	}
}

/*
 * Set scrub pause/resume state if it makes sense to do so
 */
int
dsl_scrub_set_pause_resume(const dsl_pool_t *dp, pool_scrub_cmd_t cmd)
{
	return (dsl_sync_task(spa_name(dp->dp_spa),
	    dsl_scrub_pause_resume_check, dsl_scrub_pause_resume_sync, &cmd, 3,
	    ZFS_SPACE_CHECK_RESERVED));
}

boolean_t
dsl_scan_scrubbing(const dsl_pool_t *dp)
{
	dsl_scan_t *scn = dp->dp_scan;

	if (scn->scn_phys.scn_state == DSS_SCANNING &&
	    scn->scn_phys.scn_func == POOL_SCAN_SCRUB)
		return (B_TRUE);

	return (B_FALSE);
}

static void dsl_scan_visitbp(blkptr_t *bp, const zbookmark_phys_t *zb,
    dnode_phys_t *dnp, dsl_dataset_t *ds, dsl_scan_t *scn,
    dmu_objset_type_t ostype, dmu_tx_t *tx);
inline __attribute__((always_inline)) static void dsl_scan_visitdnode(
    dsl_scan_t *, dsl_dataset_t *ds, dmu_objset_type_t ostype,
    dnode_phys_t *dnp, uint64_t object, dmu_tx_t *tx);

void
dsl_free(dsl_pool_t *dp, uint64_t txg, const blkptr_t *bp)
{
	zio_free(dp->dp_spa, txg, bp);
}

void
dsl_free_sync(zio_t *pio, dsl_pool_t *dp, uint64_t txg, const blkptr_t *bpp)
{
	ASSERT(dsl_pool_sync_context(dp));
	zio_nowait(zio_free_sync(pio, dp->dp_spa, txg, bpp, pio->io_flags));
}

static uint64_t
dsl_scan_ds_maxtxg(dsl_dataset_t *ds)
{
	uint64_t smt = ds->ds_dir->dd_pool->dp_scan->scn_phys.scn_max_txg;
	if (ds->ds_is_snapshot)
		return (MIN(smt, dsl_dataset_phys(ds)->ds_creation_txg));
	return (smt);
}

static void
dsl_scan_sync_state(dsl_scan_t *scn, dmu_tx_t *tx)
{
	VERIFY0(zap_update(scn->scn_dp->dp_meta_objset,
	    DMU_POOL_DIRECTORY_OBJECT,
	    DMU_POOL_SCAN, sizeof (uint64_t), SCAN_PHYS_NUMINTS,
	    &scn->scn_phys, tx));
}

extern int zfs_vdev_async_write_active_min_dirty_percent;

static boolean_t
dsl_scan_check_suspend(dsl_scan_t *scn, const zbookmark_phys_t *zb)
{
	uint64_t elapsed_nanosecs;
	int mintime;
	int dirty_pct;

	/* we never skip user/group accounting objects */
	if (zb && (int64_t)zb->zb_object < 0)
		return (B_FALSE);

	if (scn->scn_suspending)
		return (B_TRUE); /* we're already suspending */

	if (!ZB_IS_ZERO(&scn->scn_phys.scn_bookmark))
		return (B_FALSE); /* we're resuming */

	/* We only know how to resume from level-0 blocks. */
	if (zb && zb->zb_level != 0)
		return (B_FALSE);

	/*
	 * We suspend if:
	 *  - we have scanned for the maximum time: an entire txg
	 *    timeout (default 5 sec)
	 *  or
	 *  - we have scanned for at least the minimum time (default 1 sec
	 *    for scrub, 3 sec for resilver), and either we have sufficient
	 *    dirty data that we are starting to write more quickly
	 *    (default 30%), or someone is explicitly waiting for this txg
	 *    to complete.
	 *  or
	 *  - the spa is shutting down because this pool is being exported
	 *    or the machine is rebooting.
	 */
	mintime = (scn->scn_phys.scn_func == POOL_SCAN_RESILVER) ?
	    zfs_resilver_min_time_ms : zfs_scan_min_time_ms;
	elapsed_nanosecs = gethrtime() - scn->scn_sync_start_time;
	dirty_pct = scn->scn_dp->dp_dirty_total * 100 / zfs_dirty_data_max;
	if (elapsed_nanosecs / NANOSEC >= zfs_txg_timeout ||
	    (NSEC2MSEC(elapsed_nanosecs) > mintime &&
	    (txg_sync_waiting(scn->scn_dp) ||
	    dirty_pct >= zfs_vdev_async_write_active_min_dirty_percent)) ||
	    spa_shutting_down(scn->scn_dp->dp_spa)) {
		if (zb) {
			dprintf("suspending at bookmark %llx/%llx/%llx/%llx\n",
			    (longlong_t)zb->zb_objset,
			    (longlong_t)zb->zb_object,
			    (longlong_t)zb->zb_level,
			    (longlong_t)zb->zb_blkid);
			scn->scn_phys.scn_bookmark = *zb;
		}
		dprintf("suspending at DDT bookmark %llx/%llx/%llx/%llx\n",
		    (longlong_t)scn->scn_phys.scn_ddt_bookmark.ddb_class,
		    (longlong_t)scn->scn_phys.scn_ddt_bookmark.ddb_type,
		    (longlong_t)scn->scn_phys.scn_ddt_bookmark.ddb_checksum,
		    (longlong_t)scn->scn_phys.scn_ddt_bookmark.ddb_cursor);
		scn->scn_suspending = B_TRUE;
		return (B_TRUE);
	}
	return (B_FALSE);
}

typedef struct zil_scan_arg {
	dsl_pool_t	*zsa_dp;
	zil_header_t	*zsa_zh;
} zil_scan_arg_t;

/* ARGSUSED */
static int
dsl_scan_zil_block(zilog_t *zilog, blkptr_t *bp, void *arg, uint64_t claim_txg)
{
	zil_scan_arg_t *zsa = arg;
	dsl_pool_t *dp = zsa->zsa_dp;
	dsl_scan_t *scn = dp->dp_scan;
	zil_header_t *zh = zsa->zsa_zh;
	zbookmark_phys_t zb;

	if (BP_IS_HOLE(bp) || bp->blk_birth <= scn->scn_phys.scn_cur_min_txg)
		return (0);

	/*
	 * One block ("stubby") can be allocated a long time ago; we
	 * want to visit that one because it has been allocated
	 * (on-disk) even if it hasn't been claimed (even though for
	 * scrub there's nothing to do to it).
	 */
	if (claim_txg == 0 && bp->blk_birth >= spa_first_txg(dp->dp_spa))
		return (0);

	SET_BOOKMARK(&zb, zh->zh_log.blk_cksum.zc_word[ZIL_ZC_OBJSET],
	    ZB_ZIL_OBJECT, ZB_ZIL_LEVEL, bp->blk_cksum.zc_word[ZIL_ZC_SEQ]);

	VERIFY(0 == scan_funcs[scn->scn_phys.scn_func](dp, bp, &zb));
	return (0);
}

/* ARGSUSED */
static int
dsl_scan_zil_record(zilog_t *zilog, lr_t *lrc, void *arg, uint64_t claim_txg)
{
	if (lrc->lrc_txtype == TX_WRITE) {
		zil_scan_arg_t *zsa = arg;
		dsl_pool_t *dp = zsa->zsa_dp;
		dsl_scan_t *scn = dp->dp_scan;
		zil_header_t *zh = zsa->zsa_zh;
		lr_write_t *lr = (lr_write_t *)lrc;
		blkptr_t *bp = &lr->lr_blkptr;
		zbookmark_phys_t zb;

		if (BP_IS_HOLE(bp) ||
		    bp->blk_birth <= scn->scn_phys.scn_cur_min_txg)
			return (0);

		/*
		 * birth can be < claim_txg if this record's txg is
		 * already txg sync'ed (but this log block contains
		 * other records that are not synced)
		 */
		if (claim_txg == 0 || bp->blk_birth < claim_txg)
			return (0);

		SET_BOOKMARK(&zb, zh->zh_log.blk_cksum.zc_word[ZIL_ZC_OBJSET],
		    lr->lr_foid, ZB_ZIL_LEVEL,
		    lr->lr_offset / BP_GET_LSIZE(bp));

		VERIFY(0 == scan_funcs[scn->scn_phys.scn_func](dp, bp, &zb));
	}
	return (0);
}

static void
dsl_scan_zil(dsl_pool_t *dp, zil_header_t *zh)
{
	uint64_t claim_txg = zh->zh_claim_txg;
	zil_scan_arg_t zsa = { dp, zh };
	zilog_t *zilog;

	/*
	 * We only want to visit blocks that have been claimed but not yet
	 * replayed (or, in read-only mode, blocks that *would* be claimed).
	 */
	if (claim_txg == 0 && spa_writeable(dp->dp_spa))
		return;

	zilog = zil_alloc(dp->dp_meta_objset, zh);

	(void) zil_parse(zilog, dsl_scan_zil_block, dsl_scan_zil_record, &zsa,
	    claim_txg);

	zil_free(zilog);
}

/* ARGSUSED */
static void
dsl_scan_prefetch(dsl_scan_t *scn, arc_buf_t *buf, blkptr_t *bp,
    uint64_t objset, uint64_t object, uint64_t blkid)
{
	zbookmark_phys_t czb;
	arc_flags_t flags = ARC_FLAG_NOWAIT | ARC_FLAG_PREFETCH;

	if (zfs_no_scrub_prefetch)
		return;

	if (BP_IS_HOLE(bp) || bp->blk_birth <= scn->scn_phys.scn_min_txg ||
	    (BP_GET_LEVEL(bp) == 0 && BP_GET_TYPE(bp) != DMU_OT_DNODE))
		return;

	SET_BOOKMARK(&czb, objset, object, BP_GET_LEVEL(bp), blkid);

	(void) arc_read(scn->scn_zio_root, scn->scn_dp->dp_spa, bp,
	    NULL, NULL, ZIO_PRIORITY_ASYNC_READ,
	    ZIO_FLAG_CANFAIL | ZIO_FLAG_SCAN_THREAD, &flags, &czb);
}

static boolean_t
dsl_scan_check_resume(dsl_scan_t *scn, const dnode_phys_t *dnp,
    const zbookmark_phys_t *zb)
{
	/*
	 * We never skip over user/group accounting objects (obj<0)
	 */
	if (!ZB_IS_ZERO(&scn->scn_phys.scn_bookmark) &&
	    (int64_t)zb->zb_object >= 0) {
		/*
		 * If we already visited this bp & everything below (in
		 * a prior txg sync), don't bother doing it again.
		 */
		if (zbookmark_subtree_completed(dnp, zb,
		    &scn->scn_phys.scn_bookmark))
			return (B_TRUE);

		/*
		 * If we found the block we're trying to resume from, or
		 * we went past it to a different object, zero it out to
		 * indicate that it's OK to start checking for suspending
		 * again.
		 */
		if (bcmp(zb, &scn->scn_phys.scn_bookmark, sizeof (*zb)) == 0 ||
		    zb->zb_object > scn->scn_phys.scn_bookmark.zb_object) {
			dprintf("resuming at %llx/%llx/%llx/%llx\n",
			    (longlong_t)zb->zb_objset,
			    (longlong_t)zb->zb_object,
			    (longlong_t)zb->zb_level,
			    (longlong_t)zb->zb_blkid);
			bzero(&scn->scn_phys.scn_bookmark, sizeof (*zb));
		}
	}
	return (B_FALSE);
}

/*
 * Return nonzero on i/o error.
 * Return new buf to write out in *bufp.
 */
inline __attribute__((always_inline)) static int
dsl_scan_recurse(dsl_scan_t *scn, dsl_dataset_t *ds, dmu_objset_type_t ostype,
    dnode_phys_t *dnp, const blkptr_t *bp,
    const zbookmark_phys_t *zb, dmu_tx_t *tx)
{
	dsl_pool_t *dp = scn->scn_dp;
	int zio_flags = ZIO_FLAG_CANFAIL | ZIO_FLAG_SCAN_THREAD;
	int err;

	if (BP_GET_LEVEL(bp) > 0) {
		arc_flags_t flags = ARC_FLAG_WAIT;
		int i;
		blkptr_t *cbp;
		int epb = BP_GET_LSIZE(bp) >> SPA_BLKPTRSHIFT;
		arc_buf_t *buf;

		err = arc_read(NULL, dp->dp_spa, bp, arc_getbuf_func, &buf,
		    ZIO_PRIORITY_ASYNC_READ, zio_flags, &flags, zb);
		if (err) {
			scn->scn_phys.scn_errors++;
			return (err);
		}
		for (i = 0, cbp = buf->b_data; i < epb; i++, cbp++) {
			dsl_scan_prefetch(scn, buf, cbp, zb->zb_objset,
			    zb->zb_object, zb->zb_blkid * epb + i);
		}
		for (i = 0, cbp = buf->b_data; i < epb; i++, cbp++) {
			zbookmark_phys_t czb;

			SET_BOOKMARK(&czb, zb->zb_objset, zb->zb_object,
			    zb->zb_level - 1,
			    zb->zb_blkid * epb + i);
			dsl_scan_visitbp(cbp, &czb, dnp,
			    ds, scn, ostype, tx);
		}
		arc_buf_destroy(buf, &buf);
	} else if (BP_GET_TYPE(bp) == DMU_OT_DNODE) {
		arc_flags_t flags = ARC_FLAG_WAIT;
		dnode_phys_t *cdnp;
		int i, j;
		int epb = BP_GET_LSIZE(bp) >> DNODE_SHIFT;
		arc_buf_t *buf;

		err = arc_read(NULL, dp->dp_spa, bp, arc_getbuf_func, &buf,
		    ZIO_PRIORITY_ASYNC_READ, zio_flags, &flags, zb);
		if (err) {
			scn->scn_phys.scn_errors++;
			return (err);
		}
		for (i = 0, cdnp = buf->b_data; i < epb;
		    i += cdnp->dn_extra_slots + 1,
		    cdnp += cdnp->dn_extra_slots + 1) {
			for (j = 0; j < cdnp->dn_nblkptr; j++) {
				blkptr_t *cbp = &cdnp->dn_blkptr[j];
				dsl_scan_prefetch(scn, buf, cbp,
				    zb->zb_objset, zb->zb_blkid * epb + i, j);
			}
		}
		for (i = 0, cdnp = buf->b_data; i < epb;
		    i += cdnp->dn_extra_slots + 1,
		    cdnp += cdnp->dn_extra_slots + 1) {
			dsl_scan_visitdnode(scn, ds, ostype,
			    cdnp, zb->zb_blkid * epb + i, tx);
		}

		arc_buf_destroy(buf, &buf);
	} else if (BP_GET_TYPE(bp) == DMU_OT_OBJSET) {
		arc_flags_t flags = ARC_FLAG_WAIT;
		objset_phys_t *osp;
		arc_buf_t *buf;

		err = arc_read(NULL, dp->dp_spa, bp, arc_getbuf_func, &buf,
		    ZIO_PRIORITY_ASYNC_READ, zio_flags, &flags, zb);
		if (err) {
			scn->scn_phys.scn_errors++;
			return (err);
		}

		osp = buf->b_data;

		dsl_scan_visitdnode(scn, ds, osp->os_type,
		    &osp->os_meta_dnode, DMU_META_DNODE_OBJECT, tx);

		if (OBJSET_BUF_HAS_USERUSED(buf)) {
			/*
			 * We also always visit user/group accounting
			 * objects, and never skip them, even if we are
			 * suspending.  This is necessary so that the space
			 * deltas from this txg get integrated.
			 */
			dsl_scan_visitdnode(scn, ds, osp->os_type,
			    &osp->os_groupused_dnode,
			    DMU_GROUPUSED_OBJECT, tx);
			dsl_scan_visitdnode(scn, ds, osp->os_type,
			    &osp->os_userused_dnode,
			    DMU_USERUSED_OBJECT, tx);
		}
		arc_buf_destroy(buf, &buf);
	}

	return (0);
}

inline __attribute__((always_inline)) static void
dsl_scan_visitdnode(dsl_scan_t *scn, dsl_dataset_t *ds,
    dmu_objset_type_t ostype, dnode_phys_t *dnp,
    uint64_t object, dmu_tx_t *tx)
{
	int j;

	for (j = 0; j < dnp->dn_nblkptr; j++) {
		zbookmark_phys_t czb;

		SET_BOOKMARK(&czb, ds ? ds->ds_object : 0, object,
		    dnp->dn_nlevels - 1, j);
		dsl_scan_visitbp(&dnp->dn_blkptr[j],
		    &czb, dnp, ds, scn, ostype, tx);
	}

	if (dnp->dn_flags & DNODE_FLAG_SPILL_BLKPTR) {
		zbookmark_phys_t czb;
		SET_BOOKMARK(&czb, ds ? ds->ds_object : 0, object,
		    0, DMU_SPILL_BLKID);
		dsl_scan_visitbp(DN_SPILL_BLKPTR(dnp),
		    &czb, dnp, ds, scn, ostype, tx);
	}
}

/*
 * The arguments are in this order because mdb can only print the
 * first 5; we want them to be useful.
 */
static void
dsl_scan_visitbp(blkptr_t *bp, const zbookmark_phys_t *zb,
    dnode_phys_t *dnp, dsl_dataset_t *ds, dsl_scan_t *scn,
    dmu_objset_type_t ostype, dmu_tx_t *tx)
{
	dsl_pool_t *dp = scn->scn_dp;
	blkptr_t *bp_toread;

	bp_toread = kmem_alloc(sizeof (blkptr_t), KM_SLEEP);
	*bp_toread = *bp;

	/* ASSERT(pbuf == NULL || arc_released(pbuf)); */

	if (dsl_scan_check_suspend(scn, zb))
		goto out;

	if (dsl_scan_check_resume(scn, dnp, zb))
		goto out;

	if (BP_IS_HOLE(bp))
		goto out;

	scn->scn_visited_this_txg++;

	/*
	 * This debugging is commented out to conserve stack space.  This
	 * function is called recursively and the debugging addes several
	 * bytes to the stack for each call.  It can be commented back in
	 * if required to debug an issue in dsl_scan_visitbp().
	 *
	 * dprintf_bp(bp,
	 *    "visiting ds=%p/%llu zb=%llx/%llx/%llx/%llx bp=%p",
	 *    ds, ds ? ds->ds_object : 0,
	 *    zb->zb_objset, zb->zb_object, zb->zb_level, zb->zb_blkid,
	 *    bp);
	 */

	if (bp->blk_birth <= scn->scn_phys.scn_cur_min_txg)
		goto out;

	if (dsl_scan_recurse(scn, ds, ostype, dnp, bp_toread, zb, tx) != 0)
		goto out;

	/*
	 * If dsl_scan_ddt() has already visited this block, it will have
	 * already done any translations or scrubbing, so don't call the
	 * callback again.
	 */
	if (ddt_class_contains(dp->dp_spa,
	    scn->scn_phys.scn_ddt_class_max, bp)) {
		goto out;
	}

	/*
	 * If this block is from the future (after cur_max_txg), then we
	 * are doing this on behalf of a deleted snapshot, and we will
	 * revisit the future block on the next pass of this dataset.
	 * Don't scan it now unless we need to because something
	 * under it was modified.
	 */
	if (BP_PHYSICAL_BIRTH(bp) <= scn->scn_phys.scn_cur_max_txg) {
		scan_funcs[scn->scn_phys.scn_func](dp, bp, zb);
	}
out:
	kmem_free(bp_toread, sizeof (blkptr_t));
}

static void
dsl_scan_visit_rootbp(dsl_scan_t *scn, dsl_dataset_t *ds, blkptr_t *bp,
    dmu_tx_t *tx)
{
	zbookmark_phys_t zb;

	SET_BOOKMARK(&zb, ds ? ds->ds_object : DMU_META_OBJSET,
	    ZB_ROOT_OBJECT, ZB_ROOT_LEVEL, ZB_ROOT_BLKID);
	dsl_scan_visitbp(bp, &zb, NULL,
	    ds, scn, DMU_OST_NONE, tx);

	dprintf_ds(ds, "finished scan%s", "");
}

void
dsl_scan_ds_destroyed(dsl_dataset_t *ds, dmu_tx_t *tx)
{
	dsl_pool_t *dp = ds->ds_dir->dd_pool;
	dsl_scan_t *scn = dp->dp_scan;
	uint64_t mintxg;

	if (scn->scn_phys.scn_state != DSS_SCANNING)
		return;

	if (scn->scn_phys.scn_bookmark.zb_objset == ds->ds_object) {
		if (ds->ds_is_snapshot) {
			/*
			 * Note:
			 *  - scn_cur_{min,max}_txg stays the same.
			 *  - Setting the flag is not really necessary if
			 *    scn_cur_max_txg == scn_max_txg, because there
			 *    is nothing after this snapshot that we care
			 *    about.  However, we set it anyway and then
			 *    ignore it when we retraverse it in
			 *    dsl_scan_visitds().
			 */
			scn->scn_phys.scn_bookmark.zb_objset =
			    dsl_dataset_phys(ds)->ds_next_snap_obj;
			zfs_dbgmsg("destroying ds %llu; currently traversing; "
			    "reset zb_objset to %llu",
			    (u_longlong_t)ds->ds_object,
			    (u_longlong_t)dsl_dataset_phys(ds)->
			    ds_next_snap_obj);
			scn->scn_phys.scn_flags |= DSF_VISIT_DS_AGAIN;
		} else {
			SET_BOOKMARK(&scn->scn_phys.scn_bookmark,
			    ZB_DESTROYED_OBJSET, 0, 0, 0);
			zfs_dbgmsg("destroying ds %llu; currently traversing; "
			    "reset bookmark to -1,0,0,0",
			    (u_longlong_t)ds->ds_object);
		}
	} else if (zap_lookup_int_key(dp->dp_meta_objset,
	    scn->scn_phys.scn_queue_obj, ds->ds_object, &mintxg) == 0) {
		ASSERT3U(dsl_dataset_phys(ds)->ds_num_children, <=, 1);
		VERIFY3U(0, ==, zap_remove_int(dp->dp_meta_objset,
		    scn->scn_phys.scn_queue_obj, ds->ds_object, tx));
		if (ds->ds_is_snapshot) {
			/*
			 * We keep the same mintxg; it could be >
			 * ds_creation_txg if the previous snapshot was
			 * deleted too.
			 */
			VERIFY(zap_add_int_key(dp->dp_meta_objset,
			    scn->scn_phys.scn_queue_obj,
			    dsl_dataset_phys(ds)->ds_next_snap_obj,
			    mintxg, tx) == 0);
			zfs_dbgmsg("destroying ds %llu; in queue; "
			    "replacing with %llu",
			    (u_longlong_t)ds->ds_object,
			    (u_longlong_t)dsl_dataset_phys(ds)->
			    ds_next_snap_obj);
		} else {
			zfs_dbgmsg("destroying ds %llu; in queue; removing",
			    (u_longlong_t)ds->ds_object);
		}
	}

	/*
	 * dsl_scan_sync() should be called after this, and should sync
	 * out our changed state, but just to be safe, do it here.
	 */
	dsl_scan_sync_state(scn, tx);
}

void
dsl_scan_ds_snapshotted(dsl_dataset_t *ds, dmu_tx_t *tx)
{
	dsl_pool_t *dp = ds->ds_dir->dd_pool;
	dsl_scan_t *scn = dp->dp_scan;
	uint64_t mintxg;

	if (scn->scn_phys.scn_state != DSS_SCANNING)
		return;

	ASSERT(dsl_dataset_phys(ds)->ds_prev_snap_obj != 0);

	if (scn->scn_phys.scn_bookmark.zb_objset == ds->ds_object) {
		scn->scn_phys.scn_bookmark.zb_objset =
		    dsl_dataset_phys(ds)->ds_prev_snap_obj;
		zfs_dbgmsg("snapshotting ds %llu; currently traversing; "
		    "reset zb_objset to %llu",
		    (u_longlong_t)ds->ds_object,
		    (u_longlong_t)dsl_dataset_phys(ds)->ds_prev_snap_obj);
	} else if (zap_lookup_int_key(dp->dp_meta_objset,
	    scn->scn_phys.scn_queue_obj, ds->ds_object, &mintxg) == 0) {
		VERIFY3U(0, ==, zap_remove_int(dp->dp_meta_objset,
		    scn->scn_phys.scn_queue_obj, ds->ds_object, tx));
		VERIFY(zap_add_int_key(dp->dp_meta_objset,
		    scn->scn_phys.scn_queue_obj,
		    dsl_dataset_phys(ds)->ds_prev_snap_obj, mintxg, tx) == 0);
		zfs_dbgmsg("snapshotting ds %llu; in queue; "
		    "replacing with %llu",
		    (u_longlong_t)ds->ds_object,
		    (u_longlong_t)dsl_dataset_phys(ds)->ds_prev_snap_obj);
	}
	dsl_scan_sync_state(scn, tx);
}

void
dsl_scan_ds_clone_swapped(dsl_dataset_t *ds1, dsl_dataset_t *ds2, dmu_tx_t *tx)
{
	dsl_pool_t *dp = ds1->ds_dir->dd_pool;
	dsl_scan_t *scn = dp->dp_scan;
	uint64_t mintxg;

	if (scn->scn_phys.scn_state != DSS_SCANNING)
		return;

	if (scn->scn_phys.scn_bookmark.zb_objset == ds1->ds_object) {
		scn->scn_phys.scn_bookmark.zb_objset = ds2->ds_object;
		zfs_dbgmsg("clone_swap ds %llu; currently traversing; "
		    "reset zb_objset to %llu",
		    (u_longlong_t)ds1->ds_object,
		    (u_longlong_t)ds2->ds_object);
	} else if (scn->scn_phys.scn_bookmark.zb_objset == ds2->ds_object) {
		scn->scn_phys.scn_bookmark.zb_objset = ds1->ds_object;
		zfs_dbgmsg("clone_swap ds %llu; currently traversing; "
		    "reset zb_objset to %llu",
		    (u_longlong_t)ds2->ds_object,
		    (u_longlong_t)ds1->ds_object);
	}

	if (zap_lookup_int_key(dp->dp_meta_objset, scn->scn_phys.scn_queue_obj,
	    ds1->ds_object, &mintxg) == 0) {
		int err;

		ASSERT3U(mintxg, ==, dsl_dataset_phys(ds1)->ds_prev_snap_txg);
		ASSERT3U(mintxg, ==, dsl_dataset_phys(ds2)->ds_prev_snap_txg);
		VERIFY3U(0, ==, zap_remove_int(dp->dp_meta_objset,
		    scn->scn_phys.scn_queue_obj, ds1->ds_object, tx));
		err = zap_add_int_key(dp->dp_meta_objset,
		    scn->scn_phys.scn_queue_obj, ds2->ds_object, mintxg, tx);
		VERIFY(err == 0 || err == EEXIST);
		if (err == EEXIST) {
			/* Both were there to begin with */
			VERIFY(0 == zap_add_int_key(dp->dp_meta_objset,
			    scn->scn_phys.scn_queue_obj,
			    ds1->ds_object, mintxg, tx));
		}
		zfs_dbgmsg("clone_swap ds %llu; in queue; "
		    "replacing with %llu",
		    (u_longlong_t)ds1->ds_object,
		    (u_longlong_t)ds2->ds_object);
	} else if (zap_lookup_int_key(dp->dp_meta_objset,
	    scn->scn_phys.scn_queue_obj, ds2->ds_object, &mintxg) == 0) {
		ASSERT3U(mintxg, ==, dsl_dataset_phys(ds1)->ds_prev_snap_txg);
		ASSERT3U(mintxg, ==, dsl_dataset_phys(ds2)->ds_prev_snap_txg);
		VERIFY3U(0, ==, zap_remove_int(dp->dp_meta_objset,
		    scn->scn_phys.scn_queue_obj, ds2->ds_object, tx));
		VERIFY(0 == zap_add_int_key(dp->dp_meta_objset,
		    scn->scn_phys.scn_queue_obj, ds1->ds_object, mintxg, tx));
		zfs_dbgmsg("clone_swap ds %llu; in queue; "
		    "replacing with %llu",
		    (u_longlong_t)ds2->ds_object,
		    (u_longlong_t)ds1->ds_object);
	}

	dsl_scan_sync_state(scn, tx);
}

struct enqueue_clones_arg {
	dmu_tx_t *tx;
	uint64_t originobj;
};

/* ARGSUSED */
static int
enqueue_clones_cb(dsl_pool_t *dp, dsl_dataset_t *hds, void *arg)
{
	struct enqueue_clones_arg *eca = arg;
	dsl_dataset_t *ds;
	int err;
	dsl_scan_t *scn = dp->dp_scan;

	if (dsl_dir_phys(hds->ds_dir)->dd_origin_obj != eca->originobj)
		return (0);

	err = dsl_dataset_hold_obj(dp, hds->ds_object, FTAG, &ds);
	if (err)
		return (err);

	while (dsl_dataset_phys(ds)->ds_prev_snap_obj != eca->originobj) {
		dsl_dataset_t *prev;
		err = dsl_dataset_hold_obj(dp,
		    dsl_dataset_phys(ds)->ds_prev_snap_obj, FTAG, &prev);

		dsl_dataset_rele(ds, FTAG);
		if (err)
			return (err);
		ds = prev;
	}
	VERIFY(zap_add_int_key(dp->dp_meta_objset,
	    scn->scn_phys.scn_queue_obj, ds->ds_object,
	    dsl_dataset_phys(ds)->ds_prev_snap_txg, eca->tx) == 0);
	dsl_dataset_rele(ds, FTAG);
	return (0);
}

static void
dsl_scan_visitds(dsl_scan_t *scn, uint64_t dsobj, dmu_tx_t *tx)
{
	dsl_pool_t *dp = scn->scn_dp;
	dsl_dataset_t *ds;
	objset_t *os;
	char *dsname;

	VERIFY3U(0, ==, dsl_dataset_hold_obj(dp, dsobj, FTAG, &ds));

	if (scn->scn_phys.scn_cur_min_txg >=
	    scn->scn_phys.scn_max_txg) {
		/*
		 * This can happen if this snapshot was created after the
		 * scan started, and we already completed a previous snapshot
		 * that was created after the scan started.  This snapshot
		 * only references blocks with:
		 *
		 *	birth < our ds_creation_txg
		 *	cur_min_txg is no less than ds_creation_txg.
		 *	We have already visited these blocks.
		 * or
		 *	birth > scn_max_txg
		 *	The scan requested not to visit these blocks.
		 *
		 * Subsequent snapshots (and clones) can reference our
		 * blocks, or blocks with even higher birth times.
		 * Therefore we do not need to visit them either,
		 * so we do not add them to the work queue.
		 *
		 * Note that checking for cur_min_txg >= cur_max_txg
		 * is not sufficient, because in that case we may need to
		 * visit subsequent snapshots.  This happens when min_txg > 0,
		 * which raises cur_min_txg.  In this case we will visit
		 * this dataset but skip all of its blocks, because the
		 * rootbp's birth time is < cur_min_txg.  Then we will
		 * add the next snapshots/clones to the work queue.
		 */
		char *dsname = kmem_alloc(ZFS_MAX_DATASET_NAME_LEN, KM_SLEEP);
		dsl_dataset_name(ds, dsname);
		zfs_dbgmsg("scanning dataset %llu (%s) is unnecessary because "
		    "cur_min_txg (%llu) >= max_txg (%llu)",
		    dsobj, dsname,
		    scn->scn_phys.scn_cur_min_txg,
		    scn->scn_phys.scn_max_txg);
		kmem_free(dsname, MAXNAMELEN);

		goto out;
	}

	if (dmu_objset_from_ds(ds, &os))
		goto out;

	/*
	 * Only the ZIL in the head (non-snapshot) is valid.  Even though
	 * snapshots can have ZIL block pointers (which may be the same
	 * BP as in the head), they must be ignored.  So we traverse the
	 * ZIL here, rather than in scan_recurse(), because the regular
	 * snapshot block-sharing rules don't apply to it.
	 */
	if (DSL_SCAN_IS_SCRUB_RESILVER(scn) && !ds->ds_is_snapshot)
		dsl_scan_zil(dp, &os->os_zil_header);

	/*
	 * Iterate over the bps in this ds.
	 */
	dmu_buf_will_dirty(ds->ds_dbuf, tx);
	rrw_enter(&ds->ds_bp_rwlock, RW_READER, FTAG);
	dsl_scan_visit_rootbp(scn, ds, &dsl_dataset_phys(ds)->ds_bp, tx);
	rrw_exit(&ds->ds_bp_rwlock, FTAG);

	dsname = kmem_alloc(ZFS_MAX_DATASET_NAME_LEN, KM_SLEEP);
	dsl_dataset_name(ds, dsname);
	zfs_dbgmsg("scanned dataset %llu (%s) with min=%llu max=%llu; "
	    "suspending=%u",
	    (longlong_t)dsobj, dsname,
	    (longlong_t)scn->scn_phys.scn_cur_min_txg,
	    (longlong_t)scn->scn_phys.scn_cur_max_txg,
	    (int)scn->scn_suspending);
	kmem_free(dsname, ZFS_MAX_DATASET_NAME_LEN);

	if (scn->scn_suspending)
		goto out;

	/*
	 * We've finished this pass over this dataset.
	 */

	/*
	 * If we did not completely visit this dataset, do another pass.
	 */
	if (scn->scn_phys.scn_flags & DSF_VISIT_DS_AGAIN) {
		zfs_dbgmsg("incomplete pass; visiting again");
		scn->scn_phys.scn_flags &= ~DSF_VISIT_DS_AGAIN;
		VERIFY(zap_add_int_key(dp->dp_meta_objset,
		    scn->scn_phys.scn_queue_obj, ds->ds_object,
		    scn->scn_phys.scn_cur_max_txg, tx) == 0);
		goto out;
	}

	/*
	 * Add descendent datasets to work queue.
	 */
	if (dsl_dataset_phys(ds)->ds_next_snap_obj != 0) {
		VERIFY(zap_add_int_key(dp->dp_meta_objset,
		    scn->scn_phys.scn_queue_obj,
		    dsl_dataset_phys(ds)->ds_next_snap_obj,
		    dsl_dataset_phys(ds)->ds_creation_txg, tx) == 0);
	}
	if (dsl_dataset_phys(ds)->ds_num_children > 1) {
		boolean_t usenext = B_FALSE;
		if (dsl_dataset_phys(ds)->ds_next_clones_obj != 0) {
			uint64_t count;
			/*
			 * A bug in a previous version of the code could
			 * cause upgrade_clones_cb() to not set
			 * ds_next_snap_obj when it should, leading to a
			 * missing entry.  Therefore we can only use the
			 * next_clones_obj when its count is correct.
			 */
			int err = zap_count(dp->dp_meta_objset,
			    dsl_dataset_phys(ds)->ds_next_clones_obj, &count);
			if (err == 0 &&
			    count == dsl_dataset_phys(ds)->ds_num_children - 1)
				usenext = B_TRUE;
		}

		if (usenext) {
			VERIFY0(zap_join_key(dp->dp_meta_objset,
			    dsl_dataset_phys(ds)->ds_next_clones_obj,
			    scn->scn_phys.scn_queue_obj,
			    dsl_dataset_phys(ds)->ds_creation_txg, tx));
		} else {
			struct enqueue_clones_arg eca;
			eca.tx = tx;
			eca.originobj = ds->ds_object;

			VERIFY0(dmu_objset_find_dp(dp, dp->dp_root_dir_obj,
			    enqueue_clones_cb, &eca, DS_FIND_CHILDREN));
		}
	}

out:
	dsl_dataset_rele(ds, FTAG);
}

/* ARGSUSED */
static int
enqueue_cb(dsl_pool_t *dp, dsl_dataset_t *hds, void *arg)
{
	dmu_tx_t *tx = arg;
	dsl_dataset_t *ds;
	int err;
	dsl_scan_t *scn = dp->dp_scan;

	err = dsl_dataset_hold_obj(dp, hds->ds_object, FTAG, &ds);
	if (err)
		return (err);

	while (dsl_dataset_phys(ds)->ds_prev_snap_obj != 0) {
		dsl_dataset_t *prev;
		err = dsl_dataset_hold_obj(dp,
		    dsl_dataset_phys(ds)->ds_prev_snap_obj, FTAG, &prev);
		if (err) {
			dsl_dataset_rele(ds, FTAG);
			return (err);
		}

		/*
		 * If this is a clone, we don't need to worry about it for now.
		 */
		if (dsl_dataset_phys(prev)->ds_next_snap_obj != ds->ds_object) {
			dsl_dataset_rele(ds, FTAG);
			dsl_dataset_rele(prev, FTAG);
			return (0);
		}
		dsl_dataset_rele(ds, FTAG);
		ds = prev;
	}

	VERIFY(zap_add_int_key(dp->dp_meta_objset, scn->scn_phys.scn_queue_obj,
	    ds->ds_object, dsl_dataset_phys(ds)->ds_prev_snap_txg, tx) == 0);
	dsl_dataset_rele(ds, FTAG);
	return (0);
}

/*
 * Scrub/dedup interaction.
 *
 * If there are N references to a deduped block, we don't want to scrub it
 * N times -- ideally, we should scrub it exactly once.
 *
 * We leverage the fact that the dde's replication class (enum ddt_class)
 * is ordered from highest replication class (DDT_CLASS_DITTO) to lowest
 * (DDT_CLASS_UNIQUE) so that we may walk the DDT in that order.
 *
 * To prevent excess scrubbing, the scrub begins by walking the DDT
 * to find all blocks with refcnt > 1, and scrubs each of these once.
 * Since there are two replication classes which contain blocks with
 * refcnt > 1, we scrub the highest replication class (DDT_CLASS_DITTO) first.
 * Finally the top-down scrub begins, only visiting blocks with refcnt == 1.
 *
 * There would be nothing more to say if a block's refcnt couldn't change
 * during a scrub, but of course it can so we must account for changes
 * in a block's replication class.
 *
 * Here's an example of what can occur:
 *
 * If a block has refcnt > 1 during the DDT scrub phase, but has refcnt == 1
 * when visited during the top-down scrub phase, it will be scrubbed twice.
 * This negates our scrub optimization, but is otherwise harmless.
 *
 * If a block has refcnt == 1 during the DDT scrub phase, but has refcnt > 1
 * on each visit during the top-down scrub phase, it will never be scrubbed.
 * To catch this, ddt_sync_entry() notifies the scrub code whenever a block's
 * reference class transitions to a higher level (i.e DDT_CLASS_UNIQUE to
 * DDT_CLASS_DUPLICATE); if it transitions from refcnt == 1 to refcnt > 1
 * while a scrub is in progress, it scrubs the block right then.
 */
static void
dsl_scan_ddt(dsl_scan_t *scn, dmu_tx_t *tx)
{
	ddt_bookmark_t *ddb = &scn->scn_phys.scn_ddt_bookmark;
	ddt_entry_t dde;
	int error;
	uint64_t n = 0;

	bzero(&dde, sizeof (ddt_entry_t));

	while ((error = ddt_walk(scn->scn_dp->dp_spa, ddb, &dde)) == 0) {
		ddt_t *ddt;

		if (ddb->ddb_class > scn->scn_phys.scn_ddt_class_max)
			break;
		dprintf("visiting ddb=%llu/%llu/%llu/%llx\n",
		    (longlong_t)ddb->ddb_class,
		    (longlong_t)ddb->ddb_type,
		    (longlong_t)ddb->ddb_checksum,
		    (longlong_t)ddb->ddb_cursor);

		/* There should be no pending changes to the dedup table */
		ddt = scn->scn_dp->dp_spa->spa_ddt[ddb->ddb_checksum];
		ASSERT(avl_first(&ddt->ddt_tree) == NULL);

		dsl_scan_ddt_entry(scn, ddb->ddb_checksum, &dde, tx);
		n++;

		if (dsl_scan_check_suspend(scn, NULL))
			break;
	}

	zfs_dbgmsg("scanned %llu ddt entries with class_max = %u; "
	    "suspending=%u", (longlong_t)n,
	    (int)scn->scn_phys.scn_ddt_class_max, (int)scn->scn_suspending);

	ASSERT(error == 0 || error == ENOENT);
	ASSERT(error != ENOENT ||
	    ddb->ddb_class > scn->scn_phys.scn_ddt_class_max);
}

/* ARGSUSED */
void
dsl_scan_ddt_entry(dsl_scan_t *scn, enum zio_checksum checksum,
    ddt_entry_t *dde, dmu_tx_t *tx)
{
	const ddt_key_t *ddk = &dde->dde_key;
	ddt_phys_t *ddp = dde->dde_phys;
	blkptr_t bp;
	zbookmark_phys_t zb = { 0 };
	int p;

	if (scn->scn_phys.scn_state != DSS_SCANNING)
		return;

	for (p = 0; p < DDT_PHYS_TYPES; p++, ddp++) {
		if (ddp->ddp_phys_birth == 0 ||
		    ddp->ddp_phys_birth > scn->scn_phys.scn_max_txg)
			continue;
		ddt_bp_create(checksum, ddk, ddp, &bp);

		scn->scn_visited_this_txg++;
		scan_funcs[scn->scn_phys.scn_func](scn->scn_dp, &bp, &zb);
	}
}

static void
dsl_scan_visit(dsl_scan_t *scn, dmu_tx_t *tx)
{
	dsl_pool_t *dp = scn->scn_dp;
	zap_cursor_t *zc;
	zap_attribute_t *za;

	if (scn->scn_phys.scn_ddt_bookmark.ddb_class <=
	    scn->scn_phys.scn_ddt_class_max) {
		scn->scn_phys.scn_cur_min_txg = scn->scn_phys.scn_min_txg;
		scn->scn_phys.scn_cur_max_txg = scn->scn_phys.scn_max_txg;
		dsl_scan_ddt(scn, tx);
		if (scn->scn_suspending)
			return;
	}

	if (scn->scn_phys.scn_bookmark.zb_objset == DMU_META_OBJSET) {
		/* First do the MOS & ORIGIN */

		scn->scn_phys.scn_cur_min_txg = scn->scn_phys.scn_min_txg;
		scn->scn_phys.scn_cur_max_txg = scn->scn_phys.scn_max_txg;
		dsl_scan_visit_rootbp(scn, NULL,
		    &dp->dp_meta_rootbp, tx);
		spa_set_rootblkptr(dp->dp_spa, &dp->dp_meta_rootbp);
		if (scn->scn_suspending)
			return;

		if (spa_version(dp->dp_spa) < SPA_VERSION_DSL_SCRUB) {
			VERIFY0(dmu_objset_find_dp(dp, dp->dp_root_dir_obj,
			    enqueue_cb, tx, DS_FIND_CHILDREN));
		} else {
			dsl_scan_visitds(scn,
			    dp->dp_origin_snap->ds_object, tx);
		}
		ASSERT(!scn->scn_suspending);
	} else if (scn->scn_phys.scn_bookmark.zb_objset !=
	    ZB_DESTROYED_OBJSET) {
		/*
		 * If we were suspended, continue from here.  Note if the
		 * ds we were suspended on was deleted, the zb_objset may
		 * be -1, so we will skip this and find a new objset
		 * below.
		 */
		dsl_scan_visitds(scn, scn->scn_phys.scn_bookmark.zb_objset, tx);
		if (scn->scn_suspending)
			return;
	}

	/*
	 * In case we were suspended right at the end of the ds, zero the
	 * bookmark so we don't think that we're still trying to resume.
	 */
	bzero(&scn->scn_phys.scn_bookmark, sizeof (zbookmark_phys_t));
	zc = kmem_alloc(sizeof (zap_cursor_t), KM_SLEEP);
	za = kmem_alloc(sizeof (zap_attribute_t), KM_SLEEP);

	/* keep pulling things out of the zap-object-as-queue */
	while (zap_cursor_init(zc, dp->dp_meta_objset,
	    scn->scn_phys.scn_queue_obj),
	    zap_cursor_retrieve(zc, za) == 0) {
		dsl_dataset_t *ds;
		uint64_t dsobj;

		dsobj = zfs_strtonum(za->za_name, NULL);
		VERIFY3U(0, ==, zap_remove_int(dp->dp_meta_objset,
		    scn->scn_phys.scn_queue_obj, dsobj, tx));

		/* Set up min/max txg */
		VERIFY3U(0, ==, dsl_dataset_hold_obj(dp, dsobj, FTAG, &ds));
		if (za->za_first_integer != 0) {
			scn->scn_phys.scn_cur_min_txg =
			    MAX(scn->scn_phys.scn_min_txg,
			    za->za_first_integer);
		} else {
			scn->scn_phys.scn_cur_min_txg =
			    MAX(scn->scn_phys.scn_min_txg,
			    dsl_dataset_phys(ds)->ds_prev_snap_txg);
		}
		scn->scn_phys.scn_cur_max_txg = dsl_scan_ds_maxtxg(ds);
		dsl_dataset_rele(ds, FTAG);

		dsl_scan_visitds(scn, dsobj, tx);
		zap_cursor_fini(zc);
		if (scn->scn_suspending)
			goto out;
	}
	zap_cursor_fini(zc);
out:
	kmem_free(za, sizeof (zap_attribute_t));
	kmem_free(zc, sizeof (zap_cursor_t));
}

static boolean_t
dsl_scan_free_should_suspend(dsl_scan_t *scn)
{
	uint64_t elapsed_nanosecs;

	if (zfs_recover)
		return (B_FALSE);

	if (scn->scn_visited_this_txg >= zfs_free_max_blocks)
		return (B_TRUE);

	elapsed_nanosecs = gethrtime() - scn->scn_sync_start_time;
	return (elapsed_nanosecs / NANOSEC > zfs_txg_timeout ||
	    (NSEC2MSEC(elapsed_nanosecs) > zfs_free_min_time_ms &&
	    txg_sync_waiting(scn->scn_dp)) ||
	    spa_shutting_down(scn->scn_dp->dp_spa));
}

static int
dsl_scan_free_block_cb(void *arg, const blkptr_t *bp, dmu_tx_t *tx)
{
	dsl_scan_t *scn = arg;

	if (!scn->scn_is_bptree ||
	    (BP_GET_LEVEL(bp) == 0 && BP_GET_TYPE(bp) != DMU_OT_OBJSET)) {
		if (dsl_scan_free_should_suspend(scn))
			return (SET_ERROR(ERESTART));
	}

	zio_nowait(zio_free_sync(scn->scn_zio_root, scn->scn_dp->dp_spa,
	    dmu_tx_get_txg(tx), bp, 0));
	dsl_dir_diduse_space(tx->tx_pool->dp_free_dir, DD_USED_HEAD,
	    -bp_get_dsize_sync(scn->scn_dp->dp_spa, bp),
	    -BP_GET_PSIZE(bp), -BP_GET_UCSIZE(bp), tx);
	scn->scn_visited_this_txg++;
	return (0);
}

boolean_t
dsl_scan_active(dsl_scan_t *scn)
{
	spa_t *spa = scn->scn_dp->dp_spa;
	uint64_t used = 0, comp, uncomp;

	if (spa->spa_load_state != SPA_LOAD_NONE)
		return (B_FALSE);
	if (spa_shutting_down(spa))
		return (B_FALSE);
	if ((scn->scn_phys.scn_state == DSS_SCANNING &&
	    !dsl_scan_is_paused_scrub(scn)) ||
	    (scn->scn_async_destroying && !scn->scn_async_stalled))
		return (B_TRUE);

	if (spa_version(scn->scn_dp->dp_spa) >= SPA_VERSION_DEADLISTS) {
		(void) bpobj_space(&scn->scn_dp->dp_free_bpobj,
		    &used, &comp, &uncomp);
	}
	return (used != 0);
}

/* Called whenever a txg syncs. */
void
dsl_scan_sync(dsl_pool_t *dp, dmu_tx_t *tx)
{
	dsl_scan_t *scn = dp->dp_scan;
	spa_t *spa = dp->dp_spa;
	int err = 0;

	/*
	 * Check for scn_restart_txg before checking spa_load_state, so
	 * that we can restart an old-style scan while the pool is being
	 * imported (see dsl_scan_init).
	 */
	if (dsl_scan_restarting(scn, tx)) {
		pool_scan_func_t func = POOL_SCAN_SCRUB;
		dsl_scan_done(scn, B_FALSE, tx);
		if (vdev_resilver_needed(spa->spa_root_vdev, NULL, NULL))
			func = POOL_SCAN_RESILVER;
		zfs_dbgmsg("restarting scan func=%u txg=%llu",
		    func, tx->tx_txg);
		dsl_scan_setup_sync(&func, tx);
	}

	/*
	 * Only process scans in sync pass 1.
	 */
	if (spa_sync_pass(dp->dp_spa) > 1)
		return;

	/*
	 * If the spa is shutting down, then stop scanning. This will
	 * ensure that the scan does not dirty any new data during the
	 * shutdown phase.
	 */
	if (spa_shutting_down(spa))
		return;

	/*
	 * If the scan is inactive due to a stalled async destroy, try again.
	 */
	if (!scn->scn_async_stalled && !dsl_scan_active(scn))
		return;

	scn->scn_visited_this_txg = 0;
	scn->scn_suspending = B_FALSE;
	scn->scn_sync_start_time = gethrtime();
	spa->spa_scrub_active = B_TRUE;

	/*
	 * First process the async destroys.  If we suspend, don't do
	 * any scrubbing or resilvering.  This ensures that there are no
	 * async destroys while we are scanning, so the scan code doesn't
	 * have to worry about traversing it.  It is also faster to free the
	 * blocks than to scrub them.
	 */
	if (zfs_free_bpobj_enabled &&
	    spa_version(dp->dp_spa) >= SPA_VERSION_DEADLISTS) {
		scn->scn_is_bptree = B_FALSE;
		scn->scn_zio_root = zio_root(dp->dp_spa, NULL,
		    NULL, ZIO_FLAG_MUSTSUCCEED);
		err = bpobj_iterate(&dp->dp_free_bpobj,
		    dsl_scan_free_block_cb, scn, tx);
		VERIFY3U(0, ==, zio_wait(scn->scn_zio_root));

		if (err != 0 && err != ERESTART)
			zfs_panic_recover("error %u from bpobj_iterate()", err);
	}

	if (err == 0 && spa_feature_is_active(spa, SPA_FEATURE_ASYNC_DESTROY)) {
		ASSERT(scn->scn_async_destroying);
		scn->scn_is_bptree = B_TRUE;
		scn->scn_zio_root = zio_root(dp->dp_spa, NULL,
		    NULL, ZIO_FLAG_MUSTSUCCEED);
		err = bptree_iterate(dp->dp_meta_objset,
		    dp->dp_bptree_obj, B_TRUE, dsl_scan_free_block_cb, scn, tx);
		VERIFY0(zio_wait(scn->scn_zio_root));

		if (err == EIO || err == ECKSUM) {
			err = 0;
		} else if (err != 0 && err != ERESTART) {
			zfs_panic_recover("error %u from "
			    "traverse_dataset_destroyed()", err);
		}

		if (bptree_is_empty(dp->dp_meta_objset, dp->dp_bptree_obj)) {
			/* finished; deactivate async destroy feature */
			spa_feature_decr(spa, SPA_FEATURE_ASYNC_DESTROY, tx);
			ASSERT(!spa_feature_is_active(spa,
			    SPA_FEATURE_ASYNC_DESTROY));
			VERIFY0(zap_remove(dp->dp_meta_objset,
			    DMU_POOL_DIRECTORY_OBJECT,
			    DMU_POOL_BPTREE_OBJ, tx));
			VERIFY0(bptree_free(dp->dp_meta_objset,
			    dp->dp_bptree_obj, tx));
			dp->dp_bptree_obj = 0;
			scn->scn_async_destroying = B_FALSE;
			scn->scn_async_stalled = B_FALSE;
		} else {
			/*
			 * If we didn't make progress, mark the async
			 * destroy as stalled, so that we will not initiate
			 * a spa_sync() on its behalf.  Note that we only
			 * check this if we are not finished, because if the
			 * bptree had no blocks for us to visit, we can
			 * finish without "making progress".
			 */
			scn->scn_async_stalled =
			    (scn->scn_visited_this_txg == 0);
		}
	}
	if (scn->scn_visited_this_txg) {
		zfs_dbgmsg("freed %llu blocks in %llums from "
		    "free_bpobj/bptree txg %llu; err=%u",
		    (longlong_t)scn->scn_visited_this_txg,
		    (longlong_t)
		    NSEC2MSEC(gethrtime() - scn->scn_sync_start_time),
		    (longlong_t)tx->tx_txg, err);
		scn->scn_visited_this_txg = 0;

		/*
		 * Write out changes to the DDT that may be required as a
		 * result of the blocks freed.  This ensures that the DDT
		 * is clean when a scrub/resilver runs.
		 */
		ddt_sync(spa, tx->tx_txg);
	}
	if (err != 0)
		return;
	if (dp->dp_free_dir != NULL && !scn->scn_async_destroying &&
	    zfs_free_leak_on_eio &&
	    (dsl_dir_phys(dp->dp_free_dir)->dd_used_bytes != 0 ||
	    dsl_dir_phys(dp->dp_free_dir)->dd_compressed_bytes != 0 ||
	    dsl_dir_phys(dp->dp_free_dir)->dd_uncompressed_bytes != 0)) {
		/*
		 * We have finished background destroying, but there is still
		 * some space left in the dp_free_dir. Transfer this leaked
		 * space to the dp_leak_dir.
		 */
		if (dp->dp_leak_dir == NULL) {
			rrw_enter(&dp->dp_config_rwlock, RW_WRITER, FTAG);
			(void) dsl_dir_create_sync(dp, dp->dp_root_dir,
			    LEAK_DIR_NAME, tx);
			VERIFY0(dsl_pool_open_special_dir(dp,
			    LEAK_DIR_NAME, &dp->dp_leak_dir));
			rrw_exit(&dp->dp_config_rwlock, FTAG);
		}
		dsl_dir_diduse_space(dp->dp_leak_dir, DD_USED_HEAD,
		    dsl_dir_phys(dp->dp_free_dir)->dd_used_bytes,
		    dsl_dir_phys(dp->dp_free_dir)->dd_compressed_bytes,
		    dsl_dir_phys(dp->dp_free_dir)->dd_uncompressed_bytes, tx);
		dsl_dir_diduse_space(dp->dp_free_dir, DD_USED_HEAD,
		    -dsl_dir_phys(dp->dp_free_dir)->dd_used_bytes,
		    -dsl_dir_phys(dp->dp_free_dir)->dd_compressed_bytes,
		    -dsl_dir_phys(dp->dp_free_dir)->dd_uncompressed_bytes, tx);
	}
	if (dp->dp_free_dir != NULL && !scn->scn_async_destroying) {
		/* finished; verify that space accounting went to zero */
		ASSERT0(dsl_dir_phys(dp->dp_free_dir)->dd_used_bytes);
		ASSERT0(dsl_dir_phys(dp->dp_free_dir)->dd_compressed_bytes);
		ASSERT0(dsl_dir_phys(dp->dp_free_dir)->dd_uncompressed_bytes);
	}

	if (scn->scn_phys.scn_state != DSS_SCANNING)
		return;

	if (scn->scn_done_txg == tx->tx_txg) {
		ASSERT(!scn->scn_suspending);
		/* finished with scan. */
		zfs_dbgmsg("txg %llu scan complete", tx->tx_txg);
		dsl_scan_done(scn, B_TRUE, tx);
		ASSERT3U(spa->spa_scrub_inflight, ==, 0);
		dsl_scan_sync_state(scn, tx);
		return;
	}

	if (dsl_scan_is_paused_scrub(scn))
		return;

	if (scn->scn_phys.scn_ddt_bookmark.ddb_class <=
	    scn->scn_phys.scn_ddt_class_max) {
		zfs_dbgmsg("doing scan sync txg %llu; "
		    "ddt bm=%llu/%llu/%llu/%llx",
		    (longlong_t)tx->tx_txg,
		    (longlong_t)scn->scn_phys.scn_ddt_bookmark.ddb_class,
		    (longlong_t)scn->scn_phys.scn_ddt_bookmark.ddb_type,
		    (longlong_t)scn->scn_phys.scn_ddt_bookmark.ddb_checksum,
		    (longlong_t)scn->scn_phys.scn_ddt_bookmark.ddb_cursor);
		ASSERT(scn->scn_phys.scn_bookmark.zb_objset == 0);
		ASSERT(scn->scn_phys.scn_bookmark.zb_object == 0);
		ASSERT(scn->scn_phys.scn_bookmark.zb_level == 0);
		ASSERT(scn->scn_phys.scn_bookmark.zb_blkid == 0);
	} else {
		zfs_dbgmsg("doing scan sync txg %llu; bm=%llu/%llu/%llu/%llu",
		    (longlong_t)tx->tx_txg,
		    (longlong_t)scn->scn_phys.scn_bookmark.zb_objset,
		    (longlong_t)scn->scn_phys.scn_bookmark.zb_object,
		    (longlong_t)scn->scn_phys.scn_bookmark.zb_level,
		    (longlong_t)scn->scn_phys.scn_bookmark.zb_blkid);
	}

	scn->scn_zio_root = zio_root(dp->dp_spa, NULL,
	    NULL, ZIO_FLAG_CANFAIL);
	dsl_pool_config_enter(dp, FTAG);
	dsl_scan_visit(scn, tx);
	dsl_pool_config_exit(dp, FTAG);
	(void) zio_wait(scn->scn_zio_root);
	scn->scn_zio_root = NULL;

	zfs_dbgmsg("visited %llu blocks in %llums",
	    (longlong_t)scn->scn_visited_this_txg,
	    (longlong_t)NSEC2MSEC(gethrtime() - scn->scn_sync_start_time));

	if (!scn->scn_suspending) {
		scn->scn_done_txg = tx->tx_txg + 1;
		zfs_dbgmsg("txg %llu traversal complete, waiting till txg %llu",
		    tx->tx_txg, scn->scn_done_txg);
	}

	if (DSL_SCAN_IS_SCRUB_RESILVER(scn)) {
		mutex_enter(&spa->spa_scrub_lock);
		while (spa->spa_scrub_inflight > 0) {
			cv_wait(&spa->spa_scrub_io_cv,
			    &spa->spa_scrub_lock);
		}
		mutex_exit(&spa->spa_scrub_lock);
	}

	dsl_scan_sync_state(scn, tx);
}

/*
 * This will start a new scan, or restart an existing one.
 */
void
dsl_resilver_restart(dsl_pool_t *dp, uint64_t txg)
{
	if (txg == 0) {
		dmu_tx_t *tx;
		tx = dmu_tx_create_dd(dp->dp_mos_dir);
		VERIFY(0 == dmu_tx_assign(tx, TXG_WAIT));

		txg = dmu_tx_get_txg(tx);
		dp->dp_scan->scn_restart_txg = txg;
		dmu_tx_commit(tx);
	} else {
		dp->dp_scan->scn_restart_txg = txg;
	}
	zfs_dbgmsg("restarting resilver txg=%llu", txg);
}

boolean_t
dsl_scan_resilvering(dsl_pool_t *dp)
{
	return (dp->dp_scan->scn_phys.scn_state == DSS_SCANNING &&
	    dp->dp_scan->scn_phys.scn_func == POOL_SCAN_RESILVER);
}

/*
 * scrub consumers
 */

static void
count_block(zfs_all_blkstats_t *zab, const blkptr_t *bp)
{
	int i;

	/*
	 * If we resume after a reboot, zab will be NULL; don't record
	 * incomplete stats in that case.
	 */
	if (zab == NULL)
		return;

	for (i = 0; i < 4; i++) {
		int l = (i < 2) ? BP_GET_LEVEL(bp) : DN_MAX_LEVELS;
		int t = (i & 1) ? BP_GET_TYPE(bp) : DMU_OT_TOTAL;
		int equal;
		zfs_blkstat_t *zb;

		if (t & DMU_OT_NEWTYPE)
			t = DMU_OT_OTHER;

		zb = &zab->zab_type[l][t];
		zb->zb_count++;
		zb->zb_asize += BP_GET_ASIZE(bp);
		zb->zb_lsize += BP_GET_LSIZE(bp);
		zb->zb_psize += BP_GET_PSIZE(bp);
		zb->zb_gangs += BP_COUNT_GANG(bp);

		switch (BP_GET_NDVAS(bp)) {
		case 2:
			if (DVA_GET_VDEV(&bp->blk_dva[0]) ==
			    DVA_GET_VDEV(&bp->blk_dva[1]))
				zb->zb_ditto_2_of_2_samevdev++;
			break;
		case 3:
			equal = (DVA_GET_VDEV(&bp->blk_dva[0]) ==
			    DVA_GET_VDEV(&bp->blk_dva[1])) +
			    (DVA_GET_VDEV(&bp->blk_dva[0]) ==
			    DVA_GET_VDEV(&bp->blk_dva[2])) +
			    (DVA_GET_VDEV(&bp->blk_dva[1]) ==
			    DVA_GET_VDEV(&bp->blk_dva[2]));
			if (equal == 1)
				zb->zb_ditto_2_of_3_samevdev++;
			else if (equal == 3)
				zb->zb_ditto_3_of_3_samevdev++;
			break;
		}
	}
}

static void
dsl_scan_scrub_done(zio_t *zio)
{
	spa_t *spa = zio->io_spa;

	abd_free(zio->io_abd);

	mutex_enter(&spa->spa_scrub_lock);
	spa->spa_scrub_inflight--;
	cv_broadcast(&spa->spa_scrub_io_cv);

	if (zio->io_error && (zio->io_error != ECKSUM ||
	    !(zio->io_flags & ZIO_FLAG_SPECULATIVE))) {
		spa->spa_dsl_pool->dp_scan->scn_phys.scn_errors++;
	}
	mutex_exit(&spa->spa_scrub_lock);
}

static boolean_t
dsl_scan_need_resilver(spa_t *spa, const dva_t *dva, size_t psize,
    uint64_t phys_birth)
{
	vdev_t *vd;

	if (DVA_GET_GANG(dva)) {
		/*
		 * Gang members may be spread across multiple
		 * vdevs, so the best estimate we have is the
		 * scrub range, which has already been checked.
		 * XXX -- it would be better to change our
		 * allocation policy to ensure that all
		 * gang members reside on the same vdev.
		 */
		return (B_TRUE);
	}

	vd = vdev_lookup_top(spa, DVA_GET_VDEV(dva));

	/*
	 * Check if the txg falls within the range which must be
	 * resilvered.  DVAs outside this range can always be skipped.
	 */
	if (!vdev_dtl_contains(vd, DTL_PARTIAL, phys_birth, 1))
		return (B_FALSE);

	/*
	 * Check if the top-level vdev must resilver this offset.
	 * When the offset does not intersect with a dirty leaf DTL
	 * then it may be possible to skip the resilver IO.  The psize
	 * is provided instead of asize to simplify the check for RAIDZ.
	 */
	if (!vdev_dtl_need_resilver(vd, DVA_GET_OFFSET(dva), psize))
		return (B_FALSE);

	return (B_TRUE);
}

static int
dsl_scan_scrub_cb(dsl_pool_t *dp,
    const blkptr_t *bp, const zbookmark_phys_t *zb)
{
	dsl_scan_t *scn = dp->dp_scan;
	size_t psize = BP_GET_PSIZE(bp);
	spa_t *spa = dp->dp_spa;
	uint64_t phys_birth = BP_PHYSICAL_BIRTH(bp);
	boolean_t needs_io = B_FALSE;
	int zio_flags = ZIO_FLAG_SCAN_THREAD | ZIO_FLAG_RAW | ZIO_FLAG_CANFAIL;
	int scan_delay = 0;
	int d;

	if (phys_birth <= scn->scn_phys.scn_min_txg ||
	    phys_birth >= scn->scn_phys.scn_max_txg)
		return (0);

	count_block(dp->dp_blkstats, bp);

	if (BP_IS_EMBEDDED(bp))
		return (0);

	ASSERT(DSL_SCAN_IS_SCRUB_RESILVER(scn));
	if (scn->scn_phys.scn_func == POOL_SCAN_SCRUB) {
		zio_flags |= ZIO_FLAG_SCRUB;
		needs_io = B_TRUE;
		scan_delay = zfs_scrub_delay;
	} else {
		ASSERT3U(scn->scn_phys.scn_func, ==, POOL_SCAN_RESILVER);
		zio_flags |= ZIO_FLAG_RESILVER;
		needs_io = B_FALSE;
		scan_delay = zfs_resilver_delay;
	}

	/* If it's an intent log block, failure is expected. */
	if (zb->zb_level == ZB_ZIL_LEVEL)
		zio_flags |= ZIO_FLAG_SPECULATIVE;

	for (d = 0; d < BP_GET_NDVAS(bp); d++) {
		const dva_t *dva = &bp->blk_dva[d];

		/*
		 * Keep track of how much data we've examined so that
		 * zpool(1M) status can make useful progress reports.
		 */
		scn->scn_phys.scn_examined += DVA_GET_ASIZE(dva);
		spa->spa_scan_pass_exam += DVA_GET_ASIZE(dva);

		/* if it's a resilver, this may not be in the target range */
		if (!needs_io)
			needs_io = dsl_scan_need_resilver(spa, dva, psize,
			    phys_birth);
	}

	if (needs_io && !zfs_no_scrub_io) {
		vdev_t *rvd = spa->spa_root_vdev;
		uint64_t maxinflight = rvd->vdev_children * zfs_top_maxinflight;

		mutex_enter(&spa->spa_scrub_lock);
		while (spa->spa_scrub_inflight >= maxinflight)
			cv_wait(&spa->spa_scrub_io_cv, &spa->spa_scrub_lock);
		spa->spa_scrub_inflight++;
		mutex_exit(&spa->spa_scrub_lock);

		/*
		 * If we're seeing recent (zfs_scan_idle) "important" I/Os
		 * then throttle our workload to limit the impact of a scan.
		 */
		if (ddi_get_lbolt64() - spa->spa_last_io <= zfs_scan_idle)
			delay(scan_delay);

		zio_nowait(zio_read(NULL, spa, bp,
		    abd_alloc_for_io(psize, B_FALSE),
		    psize, dsl_scan_scrub_done, NULL,
		    ZIO_PRIORITY_SCRUB, zio_flags, zb));
	}

	/* do not relocate this block */
	return (0);
}

/*
 * Called by the ZFS_IOC_POOL_SCAN ioctl to start a scrub or resilver.
 * Can also be called to resume a paused scrub.
 */
int
dsl_scan(dsl_pool_t *dp, pool_scan_func_t func)
{
	spa_t *spa = dp->dp_spa;
	dsl_scan_t *scn = dp->dp_scan;

	/*
	 * Purge all vdev caches and probe all devices.  We do this here
	 * rather than in sync context because this requires a writer lock
	 * on the spa_config lock, which we can't do from sync context.  The
	 * spa_scrub_reopen flag indicates that vdev_open() should not
	 * attempt to start another scrub.
	 */
	spa_vdev_state_enter(spa, SCL_NONE);
	spa->spa_scrub_reopen = B_TRUE;
	vdev_reopen(spa->spa_root_vdev);
	spa->spa_scrub_reopen = B_FALSE;
	(void) spa_vdev_state_exit(spa, NULL, 0);

	if (func == POOL_SCAN_SCRUB && dsl_scan_is_paused_scrub(scn)) {
		/* got scrub start cmd, resume paused scrub */
		int err = dsl_scrub_set_pause_resume(scn->scn_dp,
		    POOL_SCRUB_NORMAL);
		if (err == 0)
			return (ECANCELED);

		return (SET_ERROR(err));
	}

	return (dsl_sync_task(spa_name(spa), dsl_scan_setup_check,
	    dsl_scan_setup_sync, &func, 0, ZFS_SPACE_CHECK_NONE));
}

static boolean_t
dsl_scan_restarting(dsl_scan_t *scn, dmu_tx_t *tx)
{
	return (scn->scn_restart_txg != 0 &&
	    scn->scn_restart_txg <= tx->tx_txg);
}

#if defined(_KERNEL) && defined(HAVE_SPL)
module_param(zfs_top_maxinflight, int, 0644);
MODULE_PARM_DESC(zfs_top_maxinflight, "Max I/Os per top-level");

module_param(zfs_resilver_delay, int, 0644);
MODULE_PARM_DESC(zfs_resilver_delay, "Number of ticks to delay resilver");

module_param(zfs_scrub_delay, int, 0644);
MODULE_PARM_DESC(zfs_scrub_delay, "Number of ticks to delay scrub");

module_param(zfs_scan_idle, int, 0644);
MODULE_PARM_DESC(zfs_scan_idle, "Idle window in clock ticks");

module_param(zfs_scan_min_time_ms, int, 0644);
MODULE_PARM_DESC(zfs_scan_min_time_ms, "Min millisecs to scrub per txg");

module_param(zfs_free_min_time_ms, int, 0644);
MODULE_PARM_DESC(zfs_free_min_time_ms, "Min millisecs to free per txg");

module_param(zfs_resilver_min_time_ms, int, 0644);
MODULE_PARM_DESC(zfs_resilver_min_time_ms, "Min millisecs to resilver per txg");

module_param(zfs_no_scrub_io, int, 0644);
MODULE_PARM_DESC(zfs_no_scrub_io, "Set to disable scrub I/O");

module_param(zfs_no_scrub_prefetch, int, 0644);
MODULE_PARM_DESC(zfs_no_scrub_prefetch, "Set to disable scrub prefetching");

/* CSTYLED */
module_param(zfs_free_max_blocks, ulong, 0644);
MODULE_PARM_DESC(zfs_free_max_blocks, "Max number of blocks freed in one txg");

module_param(zfs_free_bpobj_enabled, int, 0644);
MODULE_PARM_DESC(zfs_free_bpobj_enabled, "Enable processing of the free_bpobj");
#endif
