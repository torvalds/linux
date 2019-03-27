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
 * Copyright (c) 2011, 2018 by Delphix. All rights reserved.
 * Copyright 2016 Gary Mills
 * Copyright (c) 2011, 2017 by Delphix. All rights reserved.
 * Copyright 2017 Joyent, Inc.
 * Copyright (c) 2017 Datto Inc.
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
#include <sys/range_tree.h>
#ifdef _KERNEL
#include <sys/zfs_vfsops.h>
#endif

/*
 * Grand theory statement on scan queue sorting
 *
 * Scanning is implemented by recursively traversing all indirection levels
 * in an object and reading all blocks referenced from said objects. This
 * results in us approximately traversing the object from lowest logical
 * offset to the highest. For best performance, we would want the logical
 * blocks to be physically contiguous. However, this is frequently not the
 * case with pools given the allocation patterns of copy-on-write filesystems.
 * So instead, we put the I/Os into a reordering queue and issue them in a
 * way that will most benefit physical disks (LBA-order).
 *
 * Queue management:
 *
 * Ideally, we would want to scan all metadata and queue up all block I/O
 * prior to starting to issue it, because that allows us to do an optimal
 * sorting job. This can however consume large amounts of memory. Therefore
 * we continuously monitor the size of the queues and constrain them to 5%
 * (zfs_scan_mem_lim_fact) of physmem. If the queues grow larger than this
 * limit, we clear out a few of the largest extents at the head of the queues
 * to make room for more scanning. Hopefully, these extents will be fairly
 * large and contiguous, allowing us to approach sequential I/O throughput
 * even without a fully sorted tree.
 *
 * Metadata scanning takes place in dsl_scan_visit(), which is called from
 * dsl_scan_sync() every spa_sync(). If we have either fully scanned all
 * metadata on the pool, or we need to make room in memory because our
 * queues are too large, dsl_scan_visit() is postponed and
 * scan_io_queues_run() is called from dsl_scan_sync() instead. This implies
 * that metadata scanning and queued I/O issuing are mutually exclusive. This
 * allows us to provide maximum sequential I/O throughput for the majority of
 * I/O's issued since sequential I/O performance is significantly negatively
 * impacted if it is interleaved with random I/O.
 *
 * Implementation Notes
 *
 * One side effect of the queued scanning algorithm is that the scanning code
 * needs to be notified whenever a block is freed. This is needed to allow
 * the scanning code to remove these I/Os from the issuing queue. Additionally,
 * we do not attempt to queue gang blocks to be issued sequentially since this
 * is very hard to do and would have an extremely limitted performance benefit.
 * Instead, we simply issue gang I/Os as soon as we find them using the legacy
 * algorithm.
 *
 * Backwards compatibility
 *
 * This new algorithm is backwards compatible with the legacy on-disk data
 * structures (and therefore does not require a new feature flag).
 * Periodically during scanning (see zfs_scan_checkpoint_intval), the scan
 * will stop scanning metadata (in logical order) and wait for all outstanding
 * sorted I/O to complete. Once this is done, we write out a checkpoint
 * bookmark, indicating that we have scanned everything logically before it.
 * If the pool is imported on a machine without the new sorting algorithm,
 * the scan simply resumes from the last checkpoint using the legacy algorithm.
 */

typedef int (scan_cb_t)(dsl_pool_t *, const blkptr_t *,
    const zbookmark_phys_t *);

static scan_cb_t dsl_scan_scrub_cb;

static int scan_ds_queue_compare(const void *a, const void *b);
static int scan_prefetch_queue_compare(const void *a, const void *b);
static void scan_ds_queue_clear(dsl_scan_t *scn);
static boolean_t scan_ds_queue_contains(dsl_scan_t *scn, uint64_t dsobj,
    uint64_t *txg);
static void scan_ds_queue_insert(dsl_scan_t *scn, uint64_t dsobj, uint64_t txg);
static void scan_ds_queue_remove(dsl_scan_t *scn, uint64_t dsobj);
static void scan_ds_queue_sync(dsl_scan_t *scn, dmu_tx_t *tx);

extern int zfs_vdev_async_write_active_min_dirty_percent;

/*
 * By default zfs will check to ensure it is not over the hard memory
 * limit before each txg. If finer-grained control of this is needed
 * this value can be set to 1 to enable checking before scanning each
 * block.
 */
int zfs_scan_strict_mem_lim = B_FALSE;

/*
 * Maximum number of parallelly executing I/Os per top-level vdev.
 * Tune with care. Very high settings (hundreds) are known to trigger
 * some firmware bugs and resets on certain SSDs.
 */
int zfs_top_maxinflight = 32;		/* maximum I/Os per top-level */
unsigned int zfs_resilver_delay = 2;	/* number of ticks to delay resilver -- 2 is a good number */
unsigned int zfs_scrub_delay = 4;	/* number of ticks to delay scrub -- 4 is a good number */
unsigned int zfs_scan_idle = 50;	/* idle window in clock ticks */

/*
 * Maximum number of parallelly executed bytes per leaf vdev. We attempt
 * to strike a balance here between keeping the vdev queues full of I/Os
 * at all times and not overflowing the queues to cause long latency,
 * which would cause long txg sync times. No matter what, we will not
 * overload the drives with I/O, since that is protected by
 * zfs_vdev_scrub_max_active.
 */
unsigned long zfs_scan_vdev_limit = 4 << 20;

int zfs_scan_issue_strategy = 0;
int zfs_scan_legacy = B_FALSE;	/* don't queue & sort zios, go direct */
uint64_t zfs_scan_max_ext_gap = 2 << 20;	/* in bytes */

unsigned int zfs_scan_checkpoint_intval = 7200;	/* seconds */
#define	ZFS_SCAN_CHECKPOINT_INTVAL	SEC_TO_TICK(zfs_scan_checkpoint_intval)

/*
 * fill_weight is non-tunable at runtime, so we copy it at module init from
 * zfs_scan_fill_weight. Runtime adjustments to zfs_scan_fill_weight would
 * break queue sorting.
 */
uint64_t zfs_scan_fill_weight = 3;
static uint64_t fill_weight;

/* See dsl_scan_should_clear() for details on the memory limit tunables */
uint64_t zfs_scan_mem_lim_min = 16 << 20;	/* bytes */
uint64_t zfs_scan_mem_lim_soft_max = 128 << 20;	/* bytes */
int zfs_scan_mem_lim_fact = 20;		/* fraction of physmem */
int zfs_scan_mem_lim_soft_fact = 20;	/* fraction of mem lim above */

unsigned int zfs_scrub_min_time_ms = 1000; /* min millisecs to scrub per txg */
unsigned int zfs_free_min_time_ms = 1000; /* min millisecs to free per txg */
unsigned int zfs_obsolete_min_time_ms = 500; /* min millisecs to obsolete per txg */
unsigned int zfs_resilver_min_time_ms = 3000; /* min millisecs to resilver per txg */
boolean_t zfs_no_scrub_io = B_FALSE; /* set to disable scrub i/o */
boolean_t zfs_no_scrub_prefetch = B_FALSE; /* set to disable scrub prefetch */

SYSCTL_DECL(_vfs_zfs);
SYSCTL_UINT(_vfs_zfs, OID_AUTO, top_maxinflight, CTLFLAG_RWTUN,
    &zfs_top_maxinflight, 0, "Maximum I/Os per top-level vdev");
SYSCTL_UINT(_vfs_zfs, OID_AUTO, resilver_delay, CTLFLAG_RWTUN,
    &zfs_resilver_delay, 0, "Number of ticks to delay resilver");
SYSCTL_UINT(_vfs_zfs, OID_AUTO, scrub_delay, CTLFLAG_RWTUN,
    &zfs_scrub_delay, 0, "Number of ticks to delay scrub");
SYSCTL_UINT(_vfs_zfs, OID_AUTO, scan_idle, CTLFLAG_RWTUN,
    &zfs_scan_idle, 0, "Idle scan window in clock ticks");
SYSCTL_UINT(_vfs_zfs, OID_AUTO, scan_min_time_ms, CTLFLAG_RWTUN,
    &zfs_scrub_min_time_ms, 0, "Min millisecs to scrub per txg");
SYSCTL_UINT(_vfs_zfs, OID_AUTO, free_min_time_ms, CTLFLAG_RWTUN,
    &zfs_free_min_time_ms, 0, "Min millisecs to free per txg");
SYSCTL_UINT(_vfs_zfs, OID_AUTO, resilver_min_time_ms, CTLFLAG_RWTUN,
    &zfs_resilver_min_time_ms, 0, "Min millisecs to resilver per txg");
SYSCTL_INT(_vfs_zfs, OID_AUTO, no_scrub_io, CTLFLAG_RWTUN,
    &zfs_no_scrub_io, 0, "Disable scrub I/O");
SYSCTL_INT(_vfs_zfs, OID_AUTO, no_scrub_prefetch, CTLFLAG_RWTUN,
    &zfs_no_scrub_prefetch, 0, "Disable scrub prefetching");
SYSCTL_UINT(_vfs_zfs, OID_AUTO, zfs_scan_legacy, CTLFLAG_RWTUN,
    &zfs_scan_legacy, 0, "Scrub using legacy non-sequential method");
SYSCTL_UINT(_vfs_zfs, OID_AUTO, zfs_scan_checkpoint_interval, CTLFLAG_RWTUN,
    &zfs_scan_checkpoint_intval, 0, "Scan progress on-disk checkpointing interval");

enum ddt_class zfs_scrub_ddt_class_max = DDT_CLASS_DUPLICATE;
/* max number of blocks to free in a single TXG */
uint64_t zfs_async_block_max_blocks = UINT64_MAX;
SYSCTL_UQUAD(_vfs_zfs, OID_AUTO, free_max_blocks, CTLFLAG_RWTUN,
    &zfs_async_block_max_blocks, 0, "Maximum number of blocks to free in one TXG");

/*
 * We wait a few txgs after importing a pool to begin scanning so that
 * the import / mounting code isn't held up by scrub / resilver IO.
 * Unfortunately, it is a bit difficult to determine exactly how long
 * this will take since userspace will trigger fs mounts asynchronously
 * and the kernel will create zvol minors asynchronously. As a result,
 * the value provided here is a bit arbitrary, but represents a
 * reasonable estimate of how many txgs it will take to finish fully
 * importing a pool
 */
#define        SCAN_IMPORT_WAIT_TXGS           5


#define	DSL_SCAN_IS_SCRUB_RESILVER(scn) \
	((scn)->scn_phys.scn_func == POOL_SCAN_SCRUB || \
	(scn)->scn_phys.scn_func == POOL_SCAN_RESILVER)

extern int zfs_txg_timeout;

/*
 * Enable/disable the processing of the free_bpobj object.
 */
boolean_t zfs_free_bpobj_enabled = B_TRUE;

SYSCTL_INT(_vfs_zfs, OID_AUTO, free_bpobj_enabled, CTLFLAG_RWTUN,
    &zfs_free_bpobj_enabled, 0, "Enable free_bpobj processing");

/* the order has to match pool_scan_type */
static scan_cb_t *scan_funcs[POOL_SCAN_FUNCS] = {
	NULL,
	dsl_scan_scrub_cb,	/* POOL_SCAN_SCRUB */
	dsl_scan_scrub_cb,	/* POOL_SCAN_RESILVER */
};

/* In core node for the scn->scn_queue. Represents a dataset to be scanned */
typedef struct {
	uint64_t	sds_dsobj;
	uint64_t	sds_txg;
	avl_node_t	sds_node;
} scan_ds_t;

/*
 * This controls what conditions are placed on dsl_scan_sync_state():
 * SYNC_OPTIONAL) write out scn_phys iff scn_bytes_pending == 0
 * SYNC_MANDATORY) write out scn_phys always. scn_bytes_pending must be 0.
 * SYNC_CACHED) if scn_bytes_pending == 0, write out scn_phys. Otherwise
 *	write out the scn_phys_cached version.
 * See dsl_scan_sync_state for details.
 */
typedef enum {
	SYNC_OPTIONAL,
	SYNC_MANDATORY,
	SYNC_CACHED
} state_sync_type_t;

/*
 * This struct represents the minimum information needed to reconstruct a
 * zio for sequential scanning. This is useful because many of these will
 * accumulate in the sequential IO queues before being issued, so saving
 * memory matters here.
 */
typedef struct scan_io {
	/* fields from blkptr_t */
	uint64_t		sio_offset;
	uint64_t		sio_blk_prop;
	uint64_t		sio_phys_birth;
	uint64_t		sio_birth;
	zio_cksum_t		sio_cksum;
	uint32_t		sio_asize;

	/* fields from zio_t */
	int			sio_flags;
	zbookmark_phys_t	sio_zb;

	/* members for queue sorting */
	union {
		avl_node_t	sio_addr_node; /* link into issueing queue */
		list_node_t	sio_list_node; /* link for issuing to disk */
	} sio_nodes;
} scan_io_t;

struct dsl_scan_io_queue {
	dsl_scan_t	*q_scn; /* associated dsl_scan_t */
	vdev_t		*q_vd; /* top-level vdev that this queue represents */

	/* trees used for sorting I/Os and extents of I/Os */
	range_tree_t	*q_exts_by_addr;
	avl_tree_t	q_exts_by_size;
	avl_tree_t	q_sios_by_addr;

	/* members for zio rate limiting */
	uint64_t	q_maxinflight_bytes;
	uint64_t	q_inflight_bytes;
	kcondvar_t	q_zio_cv; /* used under vd->vdev_scan_io_queue_lock */

	/* per txg statistics */
	uint64_t	q_total_seg_size_this_txg;
	uint64_t	q_segs_this_txg;
	uint64_t	q_total_zio_size_this_txg;
	uint64_t	q_zios_this_txg;
};

/* private data for dsl_scan_prefetch_cb() */
typedef struct scan_prefetch_ctx {
	refcount_t spc_refcnt;		/* refcount for memory management */
	dsl_scan_t *spc_scn;		/* dsl_scan_t for the pool */
	boolean_t spc_root;		/* is this prefetch for an objset? */
	uint8_t spc_indblkshift;	/* dn_indblkshift of current dnode */
	uint16_t spc_datablkszsec;	/* dn_idatablkszsec of current dnode */
} scan_prefetch_ctx_t;

/* private data for dsl_scan_prefetch() */
typedef struct scan_prefetch_issue_ctx {
	avl_node_t spic_avl_node;	/* link into scn->scn_prefetch_queue */
	scan_prefetch_ctx_t *spic_spc;	/* spc for the callback */
	blkptr_t spic_bp;		/* bp to prefetch */
	zbookmark_phys_t spic_zb;	/* bookmark to prefetch */
} scan_prefetch_issue_ctx_t;

static void scan_exec_io(dsl_pool_t *dp, const blkptr_t *bp, int zio_flags,
    const zbookmark_phys_t *zb, dsl_scan_io_queue_t *queue);
static void scan_io_queue_insert_impl(dsl_scan_io_queue_t *queue,
    scan_io_t *sio);

static dsl_scan_io_queue_t *scan_io_queue_create(vdev_t *vd);
static void scan_io_queues_destroy(dsl_scan_t *scn);

static kmem_cache_t *sio_cache;

void
scan_init(void)
{
	/*
	 * This is used in ext_size_compare() to weight segments
	 * based on how sparse they are. This cannot be changed
	 * mid-scan and the tree comparison functions don't currently
	 * have a mechansim for passing additional context to the
	 * compare functions. Thus we store this value globally and
	 * we only allow it to be set at module intiailization time
	 */
	fill_weight = zfs_scan_fill_weight;
	
	sio_cache = kmem_cache_create("sio_cache",
	    sizeof (scan_io_t), 0, NULL, NULL, NULL, NULL, NULL, 0);
}

void
scan_fini(void)
{
	kmem_cache_destroy(sio_cache);
}

static inline boolean_t
dsl_scan_is_running(const dsl_scan_t *scn)
{
	return (scn->scn_phys.scn_state == DSS_SCANNING);
}

boolean_t
dsl_scan_resilvering(dsl_pool_t *dp)
{
	return (dsl_scan_is_running(dp->dp_scan) &&
	    dp->dp_scan->scn_phys.scn_func == POOL_SCAN_RESILVER);
}

static inline void
sio2bp(const scan_io_t *sio, blkptr_t *bp, uint64_t vdev_id)
{
	bzero(bp, sizeof (*bp));
	DVA_SET_ASIZE(&bp->blk_dva[0], sio->sio_asize);
	DVA_SET_VDEV(&bp->blk_dva[0], vdev_id);
	DVA_SET_OFFSET(&bp->blk_dva[0], sio->sio_offset);
	bp->blk_prop = sio->sio_blk_prop;
	bp->blk_phys_birth = sio->sio_phys_birth;
	bp->blk_birth = sio->sio_birth;
	bp->blk_fill = 1;	/* we always only work with data pointers */
	bp->blk_cksum = sio->sio_cksum;
}

static inline void
bp2sio(const blkptr_t *bp, scan_io_t *sio, int dva_i)
{
	/* we discard the vdev id, since we can deduce it from the queue */
	sio->sio_offset = DVA_GET_OFFSET(&bp->blk_dva[dva_i]);
	sio->sio_asize = DVA_GET_ASIZE(&bp->blk_dva[dva_i]);
	sio->sio_blk_prop = bp->blk_prop;
	sio->sio_phys_birth = bp->blk_phys_birth;
	sio->sio_birth = bp->blk_birth;
	sio->sio_cksum = bp->blk_cksum;
}

void
dsl_scan_global_init(void)
{
	/*
	 * This is used in ext_size_compare() to weight segments
	 * based on how sparse they are. This cannot be changed
	 * mid-scan and the tree comparison functions don't currently
	 * have a mechansim for passing additional context to the
	 * compare functions. Thus we store this value globally and
	 * we only allow it to be set at module intiailization time
	 */
	fill_weight = zfs_scan_fill_weight;
}

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

	bcopy(&scn->scn_phys, &scn->scn_phys_cached, sizeof (scn->scn_phys));
	avl_create(&scn->scn_queue, scan_ds_queue_compare, sizeof (scan_ds_t),
	    offsetof(scan_ds_t, sds_node));
	avl_create(&scn->scn_prefetch_queue, scan_prefetch_queue_compare,
	    sizeof (scan_prefetch_issue_ctx_t),
	    offsetof(scan_prefetch_issue_ctx_t, spic_avl_node));

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
		    (longlong_t)scn->scn_restart_txg);

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
		if (err == ENOENT)
			return (0);
		else if (err)
			return (err);

		/*
		 * We might be restarting after a reboot, so jump the issued
		 * counter to how far we've scanned. We know we're consistent
		 * up to here.
		 */
		scn->scn_issued_before_pass = scn->scn_phys.scn_examined;

		if (dsl_scan_is_running(scn) &&
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
			    (longlong_t)scn->scn_restart_txg);
		}
	}

	/* reload the queue into the in-core state */
	if (scn->scn_phys.scn_queue_obj != 0) {
		zap_cursor_t zc;
		zap_attribute_t za;

		for (zap_cursor_init(&zc, dp->dp_meta_objset,
		    scn->scn_phys.scn_queue_obj);
		    zap_cursor_retrieve(&zc, &za) == 0;
		    (void) zap_cursor_advance(&zc)) {
			scan_ds_queue_insert(scn,
			    zfs_strtonum(za.za_name, NULL),
			    za.za_first_integer);
		}
		zap_cursor_fini(&zc);
	}

	spa_scan_stat_init(spa);
	return (0);
}

void
dsl_scan_fini(dsl_pool_t *dp)
{
	if (dp->dp_scan != NULL) {
		dsl_scan_t *scn = dp->dp_scan;

		if (scn->scn_taskq != NULL)
			taskq_destroy(scn->scn_taskq);
		scan_ds_queue_clear(scn);
		avl_destroy(&scn->scn_queue);
		avl_destroy(&scn->scn_prefetch_queue);

		kmem_free(dp->dp_scan, sizeof (dsl_scan_t));
		dp->dp_scan = NULL;
	}
}

static boolean_t
dsl_scan_restarting(dsl_scan_t *scn, dmu_tx_t *tx)
{
	return (scn->scn_restart_txg != 0 &&
	    scn->scn_restart_txg <= tx->tx_txg);
}

boolean_t
dsl_scan_scrubbing(const dsl_pool_t *dp)
{
	dsl_scan_phys_t *scn_phys = &dp->dp_scan->scn_phys;

	return (scn_phys->scn_state == DSS_SCANNING &&
	    scn_phys->scn_func == POOL_SCAN_SCRUB);
}

boolean_t
dsl_scan_is_paused_scrub(const dsl_scan_t *scn)
{
	return (dsl_scan_scrubbing(scn->scn_dp) &&
	    scn->scn_phys.scn_flags & DSF_SCRUB_PAUSED);
}

/*
 * Writes out a persistent dsl_scan_phys_t record to the pool directory.
 * Because we can be running in the block sorting algorithm, we do not always
 * want to write out the record, only when it is "safe" to do so. This safety
 * condition is achieved by making sure that the sorting queues are empty
 * (scn_bytes_pending == 0). When this condition is not true, the sync'd state
 * is inconsistent with how much actual scanning progress has been made. The
 * kind of sync to be performed is specified by the sync_type argument. If the
 * sync is optional, we only sync if the queues are empty. If the sync is
 * mandatory, we do a hard ASSERT to make sure that the queues are empty. The
 * third possible state is a "cached" sync. This is done in response to:
 * 1) The dataset that was in the last sync'd dsl_scan_phys_t having been
 *	destroyed, so we wouldn't be able to restart scanning from it.
 * 2) The snapshot that was in the last sync'd dsl_scan_phys_t having been
 *	superseded by a newer snapshot.
 * 3) The dataset that was in the last sync'd dsl_scan_phys_t having been
 *	swapped with its clone.
 * In all cases, a cached sync simply rewrites the last record we've written,
 * just slightly modified. For the modifications that are performed to the
 * last written dsl_scan_phys_t, see dsl_scan_ds_destroyed,
 * dsl_scan_ds_snapshotted and dsl_scan_ds_clone_swapped.
 */
static void
dsl_scan_sync_state(dsl_scan_t *scn, dmu_tx_t *tx, state_sync_type_t sync_type)
{
	int i;
	spa_t *spa = scn->scn_dp->dp_spa;

	ASSERT(sync_type != SYNC_MANDATORY || scn->scn_bytes_pending == 0);
	if (scn->scn_bytes_pending == 0) {
		for (i = 0; i < spa->spa_root_vdev->vdev_children; i++) {
			vdev_t *vd = spa->spa_root_vdev->vdev_child[i];
			dsl_scan_io_queue_t *q = vd->vdev_scan_io_queue;

			if (q == NULL)
				continue;

			mutex_enter(&vd->vdev_scan_io_queue_lock);
			ASSERT3P(avl_first(&q->q_sios_by_addr), ==, NULL);
			ASSERT3P(avl_first(&q->q_exts_by_size), ==, NULL);
			ASSERT3P(range_tree_first(q->q_exts_by_addr), ==, NULL);
			mutex_exit(&vd->vdev_scan_io_queue_lock);
		}

		if (scn->scn_phys.scn_queue_obj != 0)
			scan_ds_queue_sync(scn, tx);
		VERIFY0(zap_update(scn->scn_dp->dp_meta_objset,
		    DMU_POOL_DIRECTORY_OBJECT,
		    DMU_POOL_SCAN, sizeof (uint64_t), SCAN_PHYS_NUMINTS,
		    &scn->scn_phys, tx));
		bcopy(&scn->scn_phys, &scn->scn_phys_cached,
		    sizeof (scn->scn_phys));

		if (scn->scn_checkpointing)
			zfs_dbgmsg("finish scan checkpoint");

		scn->scn_checkpointing = B_FALSE;
		scn->scn_last_checkpoint = ddi_get_lbolt();
	} else if (sync_type == SYNC_CACHED) {
		VERIFY0(zap_update(scn->scn_dp->dp_meta_objset,
		    DMU_POOL_DIRECTORY_OBJECT,
		    DMU_POOL_SCAN, sizeof (uint64_t), SCAN_PHYS_NUMINTS,
		    &scn->scn_phys_cached, tx));
	}
}

/* ARGSUSED */
static int
dsl_scan_setup_check(void *arg, dmu_tx_t *tx)
{
	dsl_scan_t *scn = dmu_tx_pool(tx)->dp_scan;

	if (dsl_scan_is_running(scn))
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

	ASSERT(!dsl_scan_is_running(scn));
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
	scn->scn_issued_before_pass = 0;
	scn->scn_restart_txg = 0;
	scn->scn_done_txg = 0;
	scn->scn_last_checkpoint = 0;
	scn->scn_checkpointing = B_FALSE;
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
		    kmem_alloc(sizeof (zfs_all_blkstats_t), KM_SLEEP);
		mutex_init(&dp->dp_blkstats->zab_lock, NULL,
		    MUTEX_DEFAULT, NULL);
	}
	bzero(&dp->dp_blkstats->zab_type, sizeof (dp->dp_blkstats->zab_type));

	if (spa_version(spa) < SPA_VERSION_DSL_SCRUB)
		ot = DMU_OT_ZAP_OTHER;

	scn->scn_phys.scn_queue_obj = zap_create(dp->dp_meta_objset,
	    ot ? ot : DMU_OT_SCAN_QUEUE, DMU_OT_NONE, 0, tx);

	bcopy(&scn->scn_phys, &scn->scn_phys_cached, sizeof (scn->scn_phys));

	dsl_scan_sync_state(scn, tx, SYNC_MANDATORY);

	spa_history_log_internal(spa, "scan setup", tx,
	    "func=%u mintxg=%llu maxtxg=%llu",
	    *funcp, scn->scn_phys.scn_min_txg, scn->scn_phys.scn_max_txg);
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
		if (err == 0) {
			spa_event_notify(spa, NULL, NULL, ESC_ZFS_SCRUB_RESUME);
			return (ECANCELED);
		}
		return (SET_ERROR(err));
	}

	return (dsl_sync_task(spa_name(spa), dsl_scan_setup_check,
	    dsl_scan_setup_sync, &func, 0, ZFS_SPACE_CHECK_EXTRA_RESERVED));
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
		VERIFY0(dmu_object_free(dp->dp_meta_objset,
		    scn->scn_phys.scn_queue_obj, tx));
		scn->scn_phys.scn_queue_obj = 0;
	}
	scan_ds_queue_clear(scn);

	scn->scn_phys.scn_flags &= ~DSF_SCRUB_PAUSED;

	/*
	 * If we were "restarted" from a stopped state, don't bother
	 * with anything else.
	 */
	if (!dsl_scan_is_running(scn)) {
		ASSERT(!scn->scn_is_sorted);
		return;
	}

	if (scn->scn_is_sorted) {
		scan_io_queues_destroy(scn);
		scn->scn_is_sorted = B_FALSE;

		if (scn->scn_taskq != NULL) {
			taskq_destroy(scn->scn_taskq);
			scn->scn_taskq = NULL;
		}
	}

	scn->scn_phys.scn_state = complete ? DSS_FINISHED : DSS_CANCELED;

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
		spa->spa_scrub_started = B_FALSE;
		spa->spa_scrub_active = B_FALSE;

		/*
		 * If the scrub/resilver completed, update all DTLs to
		 * reflect this.  Whether it succeeded or not, vacate
		 * all temporary scrub DTLs.
		 *
		 * As the scrub does not currently support traversing
		 * data that have been freed but are part of a checkpoint,
		 * we don't mark the scrub as done in the DTLs as faults
		 * may still exist in those vdevs.
		 */
		if (complete &&
		    !spa_feature_is_active(spa, SPA_FEATURE_POOL_CHECKPOINT)) {
			vdev_dtl_reassess(spa->spa_root_vdev, tx->tx_txg,
			    scn->scn_phys.scn_max_txg, B_TRUE);

			spa_event_notify(spa, NULL, NULL,
			    scn->scn_phys.scn_min_txg ?
			    ESC_ZFS_RESILVER_FINISH : ESC_ZFS_SCRUB_FINISH);
		} else {
			vdev_dtl_reassess(spa->spa_root_vdev, tx->tx_txg,
			    0, B_TRUE);
		}
		spa_errlog_rotate(spa);

		/*
		 * We may have finished replacing a device.
		 * Let the async thread assess this and handle the detach.
		 */
		spa_async_request(spa, SPA_ASYNC_RESILVER_DONE);
	}

	scn->scn_phys.scn_end_time = gethrestime_sec();

	ASSERT(!dsl_scan_is_running(scn));
}

/* ARGSUSED */
static int
dsl_scan_cancel_check(void *arg, dmu_tx_t *tx)
{
	dsl_scan_t *scn = dmu_tx_pool(tx)->dp_scan;

	if (!dsl_scan_is_running(scn))
		return (SET_ERROR(ENOENT));
	return (0);
}

/* ARGSUSED */
static void
dsl_scan_cancel_sync(void *arg, dmu_tx_t *tx)
{
	dsl_scan_t *scn = dmu_tx_pool(tx)->dp_scan;

	dsl_scan_done(scn, B_FALSE, tx);
	dsl_scan_sync_state(scn, tx, SYNC_MANDATORY);
	spa_event_notify(scn->scn_dp->dp_spa, NULL, NULL, ESC_ZFS_SCRUB_ABORT);
}

int
dsl_scan_cancel(dsl_pool_t *dp)
{
	return (dsl_sync_task(spa_name(dp->dp_spa), dsl_scan_cancel_check,
	    dsl_scan_cancel_sync, NULL, 3, ZFS_SPACE_CHECK_RESERVED));
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
		dsl_scan_sync_state(scn, tx, SYNC_CACHED);
		spa_event_notify(spa, NULL, NULL, ESC_ZFS_SCRUB_PAUSED);
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
			dsl_scan_sync_state(scn, tx, SYNC_CACHED);
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


/* start a new scan, or restart an existing one. */
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

void
dsl_free(dsl_pool_t *dp, uint64_t txg, const blkptr_t *bp)
{
	zio_free(dp->dp_spa, txg, bp);
}

void
dsl_free_sync(zio_t *pio, dsl_pool_t *dp, uint64_t txg, const blkptr_t *bpp)
{
	ASSERT(dsl_pool_sync_context(dp));
	zio_nowait(zio_free_sync(pio, dp->dp_spa, txg, bpp, BP_GET_PSIZE(bpp),
	    pio->io_flags));
}

static int
scan_ds_queue_compare(const void *a, const void *b)
{
	const scan_ds_t *sds_a = a, *sds_b = b;

	if (sds_a->sds_dsobj < sds_b->sds_dsobj)
		return (-1);
	if (sds_a->sds_dsobj == sds_b->sds_dsobj)
		return (0);
	return (1);
}

static void
scan_ds_queue_clear(dsl_scan_t *scn)
{
	void *cookie = NULL;
	scan_ds_t *sds;
	while ((sds = avl_destroy_nodes(&scn->scn_queue, &cookie)) != NULL) {
		kmem_free(sds, sizeof (*sds));
	}
}

static boolean_t
scan_ds_queue_contains(dsl_scan_t *scn, uint64_t dsobj, uint64_t *txg)
{
	scan_ds_t srch, *sds;

	srch.sds_dsobj = dsobj;
	sds = avl_find(&scn->scn_queue, &srch, NULL);
	if (sds != NULL && txg != NULL)
		*txg = sds->sds_txg;
	return (sds != NULL);
}

static void
scan_ds_queue_insert(dsl_scan_t *scn, uint64_t dsobj, uint64_t txg)
{
	scan_ds_t *sds;
	avl_index_t where;

	sds = kmem_zalloc(sizeof (*sds), KM_SLEEP);
	sds->sds_dsobj = dsobj;
	sds->sds_txg = txg;

	VERIFY3P(avl_find(&scn->scn_queue, sds, &where), ==, NULL);
	avl_insert(&scn->scn_queue, sds, where);
}

static void
scan_ds_queue_remove(dsl_scan_t *scn, uint64_t dsobj)
{
	scan_ds_t srch, *sds;

	srch.sds_dsobj = dsobj;

	sds = avl_find(&scn->scn_queue, &srch, NULL);
	VERIFY(sds != NULL);
	avl_remove(&scn->scn_queue, sds);
	kmem_free(sds, sizeof (*sds));
}

static void
scan_ds_queue_sync(dsl_scan_t *scn, dmu_tx_t *tx)
{
	dsl_pool_t *dp = scn->scn_dp;
	spa_t *spa = dp->dp_spa;
	dmu_object_type_t ot = (spa_version(spa) >= SPA_VERSION_DSL_SCRUB) ?
	    DMU_OT_SCAN_QUEUE : DMU_OT_ZAP_OTHER;

	ASSERT0(scn->scn_bytes_pending);
	ASSERT(scn->scn_phys.scn_queue_obj != 0);

	VERIFY0(dmu_object_free(dp->dp_meta_objset,
	    scn->scn_phys.scn_queue_obj, tx));
	scn->scn_phys.scn_queue_obj = zap_create(dp->dp_meta_objset, ot,
	    DMU_OT_NONE, 0, tx);
	for (scan_ds_t *sds = avl_first(&scn->scn_queue);
	    sds != NULL; sds = AVL_NEXT(&scn->scn_queue, sds)) {
		VERIFY0(zap_add_int_key(dp->dp_meta_objset,
		    scn->scn_phys.scn_queue_obj, sds->sds_dsobj,
		    sds->sds_txg, tx));
	}
}

/*
 * Computes the memory limit state that we're currently in. A sorted scan
 * needs quite a bit of memory to hold the sorting queue, so we need to
 * reasonably constrain the size so it doesn't impact overall system
 * performance. We compute two limits:
 * 1) Hard memory limit: if the amount of memory used by the sorting
 *	queues on a pool gets above this value, we stop the metadata
 *	scanning portion and start issuing the queued up and sorted
 *	I/Os to reduce memory usage.
 *	This limit is calculated as a fraction of physmem (by default 5%).
 *	We constrain the lower bound of the hard limit to an absolute
 *	minimum of zfs_scan_mem_lim_min (default: 16 MiB). We also constrain
 *	the upper bound to 5% of the total pool size - no chance we'll
 *	ever need that much memory, but just to keep the value in check.
 * 2) Soft memory limit: once we hit the hard memory limit, we start
 *	issuing I/O to reduce queue memory usage, but we don't want to
 *	completely empty out the queues, since we might be able to find I/Os
 *	that will fill in the gaps of our non-sequential IOs at some point
 *	in the future. So we stop the issuing of I/Os once the amount of
 *	memory used drops below the soft limit (at which point we stop issuing
 *	I/O and start scanning metadata again).
 *
 *	This limit is calculated by subtracting a fraction of the hard
 *	limit from the hard limit. By default this fraction is 5%, so
 *	the soft limit is 95% of the hard limit. We cap the size of the
 *	difference between the hard and soft limits at an absolute
 *	maximum of zfs_scan_mem_lim_soft_max (default: 128 MiB) - this is
 *	sufficient to not cause too frequent switching between the
 *	metadata scan and I/O issue (even at 2k recordsize, 128 MiB's
 *	worth of queues is about 1.2 GiB of on-pool data, so scanning
 *	that should take at least a decent fraction of a second).
 */
static boolean_t
dsl_scan_should_clear(dsl_scan_t *scn)
{
	vdev_t *rvd = scn->scn_dp->dp_spa->spa_root_vdev;
	uint64_t mlim_hard, mlim_soft, mused;
	uint64_t alloc = metaslab_class_get_alloc(spa_normal_class(
	    scn->scn_dp->dp_spa));

	mlim_hard = MAX((physmem / zfs_scan_mem_lim_fact) * PAGESIZE,
	    zfs_scan_mem_lim_min);
	mlim_hard = MIN(mlim_hard, alloc / 20);
	mlim_soft = mlim_hard - MIN(mlim_hard / zfs_scan_mem_lim_soft_fact,
	    zfs_scan_mem_lim_soft_max);
	mused = 0;
	for (uint64_t i = 0; i < rvd->vdev_children; i++) {
		vdev_t *tvd = rvd->vdev_child[i];
		dsl_scan_io_queue_t *queue;

		mutex_enter(&tvd->vdev_scan_io_queue_lock);
		queue = tvd->vdev_scan_io_queue;
		if (queue != NULL) {
			/* #extents in exts_by_size = # in exts_by_addr */
			mused += avl_numnodes(&queue->q_exts_by_size) *
			    sizeof (range_seg_t) +
			    avl_numnodes(&queue->q_sios_by_addr) *
			    sizeof (scan_io_t);
		}
		mutex_exit(&tvd->vdev_scan_io_queue_lock);
	}

	dprintf("current scan memory usage: %llu bytes\n", (longlong_t)mused);

	if (mused == 0)
		ASSERT0(scn->scn_bytes_pending);

	/*
	 * If we are above our hard limit, we need to clear out memory.
	 * If we are below our soft limit, we need to accumulate sequential IOs.
	 * Otherwise, we should keep doing whatever we are currently doing.
	 */
	if (mused >= mlim_hard)
		return (B_TRUE);
	else if (mused < mlim_soft)
		return (B_FALSE);
	else
		return (scn->scn_clearing);
}

static boolean_t
dsl_scan_check_suspend(dsl_scan_t *scn, const zbookmark_phys_t *zb)
{
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
	 *  - we have scanned for at least the minimum time (default 1 sec
	 *    for scrub, 3 sec for resilver), and either we have sufficient
	 *    dirty data that we are starting to write more quickly
	 *    (default 30%), or someone is explicitly waiting for this txg
	 *    to complete.
	 *  or
	 *  - the spa is shutting down because this pool is being exported
	 *    or the machine is rebooting.
	 *  or
	 *  - the scan queue has reached its memory use limit
	 */
	uint64_t elapsed_nanosecs = gethrtime();
	uint64_t curr_time_ns = gethrtime();
	uint64_t scan_time_ns = curr_time_ns - scn->scn_sync_start_time;
	uint64_t sync_time_ns = curr_time_ns -
	    scn->scn_dp->dp_spa->spa_sync_starttime;

	int dirty_pct = scn->scn_dp->dp_dirty_total * 100 / zfs_dirty_data_max;
	int mintime = (scn->scn_phys.scn_func == POOL_SCAN_RESILVER) ?
	    zfs_resilver_min_time_ms : zfs_scrub_min_time_ms;

	if ((NSEC2MSEC(scan_time_ns) > mintime &&
            (dirty_pct >= zfs_vdev_async_write_active_min_dirty_percent ||
            txg_sync_waiting(scn->scn_dp) ||
            NSEC2SEC(sync_time_ns) >= zfs_txg_timeout)) ||
            spa_shutting_down(scn->scn_dp->dp_spa) ||
	    (zfs_scan_strict_mem_lim && dsl_scan_should_clear(scn))) {
		if (zb) {
			dprintf("suspending at bookmark %llx/%llx/%llx/%llx\n",
			    (longlong_t)zb->zb_objset,
			    (longlong_t)zb->zb_object,
			    (longlong_t)zb->zb_level,
			    (longlong_t)zb->zb_blkid);
			scn->scn_phys.scn_bookmark = *zb;
		} else {
			dsl_scan_phys_t *scnp = &scn->scn_phys;

			dprintf("suspending at at DDT bookmark "
			    "%llx/%llx/%llx/%llx\n",
			    (longlong_t)scnp->scn_ddt_bookmark.ddb_class,
			    (longlong_t)scnp->scn_ddt_bookmark.ddb_type,
			    (longlong_t)scnp->scn_ddt_bookmark.ddb_checksum,
			    (longlong_t)scnp->scn_ddt_bookmark.ddb_cursor);
		}
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
	if (claim_txg == 0 && bp->blk_birth >= spa_min_claim_txg(dp->dp_spa))
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

	ASSERT(spa_writeable(dp->dp_spa));

	/*
	 * We only want to visit blocks that have been claimed
	 * but not yet replayed.
	 */
	if (claim_txg == 0)
		return;

	zilog = zil_alloc(dp->dp_meta_objset, zh);

	(void) zil_parse(zilog, dsl_scan_zil_block, dsl_scan_zil_record, &zsa,
	    claim_txg);

	zil_free(zilog);
}

/*
 * We compare scan_prefetch_issue_ctx_t's based on their bookmarks. The idea
 * here is to sort the AVL tree by the order each block will be needed.
 */
static int
scan_prefetch_queue_compare(const void *a, const void *b)
{
	const scan_prefetch_issue_ctx_t *spic_a = a, *spic_b = b;
	const scan_prefetch_ctx_t *spc_a = spic_a->spic_spc;
	const scan_prefetch_ctx_t *spc_b = spic_b->spic_spc;

	return (zbookmark_compare(spc_a->spc_datablkszsec,
	    spc_a->spc_indblkshift, spc_b->spc_datablkszsec,
	    spc_b->spc_indblkshift, &spic_a->spic_zb, &spic_b->spic_zb));
}

static void
scan_prefetch_ctx_rele(scan_prefetch_ctx_t *spc, void *tag)
{
	if (refcount_remove(&spc->spc_refcnt, tag) == 0) {
		refcount_destroy(&spc->spc_refcnt);
		kmem_free(spc, sizeof (scan_prefetch_ctx_t));
	}
}

static scan_prefetch_ctx_t *
scan_prefetch_ctx_create(dsl_scan_t *scn, dnode_phys_t *dnp, void *tag)
{
	scan_prefetch_ctx_t *spc;

	spc = kmem_alloc(sizeof (scan_prefetch_ctx_t), KM_SLEEP);
	refcount_create(&spc->spc_refcnt);
	refcount_add(&spc->spc_refcnt, tag);
	spc->spc_scn = scn;
	if (dnp != NULL) {
		spc->spc_datablkszsec = dnp->dn_datablkszsec;
		spc->spc_indblkshift = dnp->dn_indblkshift;
		spc->spc_root = B_FALSE;
	} else {
		spc->spc_datablkszsec = 0;
		spc->spc_indblkshift = 0;
		spc->spc_root = B_TRUE;
	}

	return (spc);
}

static void
scan_prefetch_ctx_add_ref(scan_prefetch_ctx_t *spc, void *tag)
{
	refcount_add(&spc->spc_refcnt, tag);
}

static boolean_t
dsl_scan_check_prefetch_resume(scan_prefetch_ctx_t *spc,
    const zbookmark_phys_t *zb)
{
	zbookmark_phys_t *last_zb = &spc->spc_scn->scn_prefetch_bookmark;
	dnode_phys_t tmp_dnp;
	dnode_phys_t *dnp = (spc->spc_root) ? NULL : &tmp_dnp;

	if (zb->zb_objset != last_zb->zb_objset)
		return (B_TRUE);
	if ((int64_t)zb->zb_object < 0)
		return (B_FALSE);

	tmp_dnp.dn_datablkszsec = spc->spc_datablkszsec;
	tmp_dnp.dn_indblkshift = spc->spc_indblkshift;

	if (zbookmark_subtree_completed(dnp, zb, last_zb))
		return (B_TRUE);

	return (B_FALSE);
}

static void
dsl_scan_prefetch(scan_prefetch_ctx_t *spc, blkptr_t *bp, zbookmark_phys_t *zb)
{
	avl_index_t idx;
	dsl_scan_t *scn = spc->spc_scn;
	spa_t *spa = scn->scn_dp->dp_spa;
	scan_prefetch_issue_ctx_t *spic;

	if (zfs_no_scrub_prefetch)
		return;

	if (BP_IS_HOLE(bp) || bp->blk_birth <= scn->scn_phys.scn_cur_min_txg ||
	    (BP_GET_LEVEL(bp) == 0 && BP_GET_TYPE(bp) != DMU_OT_DNODE &&
	    BP_GET_TYPE(bp) != DMU_OT_OBJSET))
		return;

	if (dsl_scan_check_prefetch_resume(spc, zb))
		return;

	scan_prefetch_ctx_add_ref(spc, scn);
	spic = kmem_alloc(sizeof (scan_prefetch_issue_ctx_t), KM_SLEEP);
	spic->spic_spc = spc;
	spic->spic_bp = *bp;
	spic->spic_zb = *zb;

	/*
	 * Add the IO to the queue of blocks to prefetch. This allows us to
	 * prioritize blocks that we will need first for the main traversal
	 * thread.
	 */
	mutex_enter(&spa->spa_scrub_lock);
	if (avl_find(&scn->scn_prefetch_queue, spic, &idx) != NULL) {
		/* this block is already queued for prefetch */
		kmem_free(spic, sizeof (scan_prefetch_issue_ctx_t));
		scan_prefetch_ctx_rele(spc, scn);
		mutex_exit(&spa->spa_scrub_lock);
		return;
	}

	avl_insert(&scn->scn_prefetch_queue, spic, idx);
	cv_broadcast(&spa->spa_scrub_io_cv);
	mutex_exit(&spa->spa_scrub_lock);
}

static void
dsl_scan_prefetch_dnode(dsl_scan_t *scn, dnode_phys_t *dnp,
    uint64_t objset, uint64_t object)
{
	int i;
	zbookmark_phys_t zb;
	scan_prefetch_ctx_t *spc;

	if (dnp->dn_nblkptr == 0 && !(dnp->dn_flags & DNODE_FLAG_SPILL_BLKPTR))
		return;

	SET_BOOKMARK(&zb, objset, object, 0, 0);

	spc = scan_prefetch_ctx_create(scn, dnp, FTAG);

	for (i = 0; i < dnp->dn_nblkptr; i++) {
		zb.zb_level = BP_GET_LEVEL(&dnp->dn_blkptr[i]);
		zb.zb_blkid = i;
		dsl_scan_prefetch(spc, &dnp->dn_blkptr[i], &zb);
	}

	if (dnp->dn_flags & DNODE_FLAG_SPILL_BLKPTR) {
		zb.zb_level = 0;
		zb.zb_blkid = DMU_SPILL_BLKID;
		dsl_scan_prefetch(spc, &dnp->dn_spill, &zb);
	}

	scan_prefetch_ctx_rele(spc, FTAG);
}

void
dsl_scan_prefetch_cb(zio_t *zio, const zbookmark_phys_t *zb, const blkptr_t *bp,
    arc_buf_t *buf, void *private)
{
	scan_prefetch_ctx_t *spc = private;
	dsl_scan_t *scn = spc->spc_scn;
	spa_t *spa = scn->scn_dp->dp_spa;

	/* broadcast that the IO has completed for rate limitting purposes */
	mutex_enter(&spa->spa_scrub_lock);
	ASSERT3U(spa->spa_scrub_inflight, >=, BP_GET_PSIZE(bp));
	spa->spa_scrub_inflight -= BP_GET_PSIZE(bp);
	cv_broadcast(&spa->spa_scrub_io_cv);
	mutex_exit(&spa->spa_scrub_lock);

	/* if there was an error or we are done prefetching, just cleanup */
	if (buf == NULL || scn->scn_suspending)
		goto out;

	if (BP_GET_LEVEL(bp) > 0) {
		int i;
		blkptr_t *cbp;
		int epb = BP_GET_LSIZE(bp) >> SPA_BLKPTRSHIFT;
		zbookmark_phys_t czb;

		for (i = 0, cbp = buf->b_data; i < epb; i++, cbp++) {
			SET_BOOKMARK(&czb, zb->zb_objset, zb->zb_object,
			    zb->zb_level - 1, zb->zb_blkid * epb + i);
			dsl_scan_prefetch(spc, cbp, &czb);
		}
	} else if (BP_GET_TYPE(bp) == DMU_OT_DNODE) {
		dnode_phys_t *cdnp = buf->b_data;
		int i;
		int epb = BP_GET_LSIZE(bp) >> DNODE_SHIFT;

		for (i = 0, cdnp = buf->b_data; i < epb;
		    i += cdnp->dn_extra_slots + 1,
		    cdnp += cdnp->dn_extra_slots + 1) {
			dsl_scan_prefetch_dnode(scn, cdnp,
			    zb->zb_objset, zb->zb_blkid * epb + i);
		}
	} else if (BP_GET_TYPE(bp) == DMU_OT_OBJSET) {
		objset_phys_t *osp = buf->b_data;

		dsl_scan_prefetch_dnode(scn, &osp->os_meta_dnode,
		    zb->zb_objset, DMU_META_DNODE_OBJECT);

		if (OBJSET_BUF_HAS_USERUSED(buf)) {
			dsl_scan_prefetch_dnode(scn,
			    &osp->os_groupused_dnode, zb->zb_objset,
			    DMU_GROUPUSED_OBJECT);
			dsl_scan_prefetch_dnode(scn,
			    &osp->os_userused_dnode, zb->zb_objset,
			    DMU_USERUSED_OBJECT);
		}
	}

out:
	if (buf != NULL)
		arc_buf_destroy(buf, private);
	scan_prefetch_ctx_rele(spc, scn);
}

/* ARGSUSED */
static void
dsl_scan_prefetch_thread(void *arg)
{
	dsl_scan_t *scn = arg;
	spa_t *spa = scn->scn_dp->dp_spa;
	vdev_t *rvd = spa->spa_root_vdev;
	uint64_t maxinflight = rvd->vdev_children * zfs_top_maxinflight;
	scan_prefetch_issue_ctx_t *spic;

	/* loop until we are told to stop */
	while (!scn->scn_prefetch_stop) {
		arc_flags_t flags = ARC_FLAG_NOWAIT |
                    ARC_FLAG_PRESCIENT_PREFETCH | ARC_FLAG_PREFETCH;
		int zio_flags = ZIO_FLAG_CANFAIL | ZIO_FLAG_SCAN_THREAD;
		
		mutex_enter(&spa->spa_scrub_lock);

		/*
		 * Wait until we have an IO to issue and are not above our
		 * maximum in flight limit.
		 */
		while (!scn->scn_prefetch_stop &&
		    (avl_numnodes(&scn->scn_prefetch_queue) == 0 ||
		    spa->spa_scrub_inflight >= scn->scn_maxinflight_bytes)) {
			cv_wait(&spa->spa_scrub_io_cv, &spa->spa_scrub_lock);
		}

		/* recheck if we should stop since we waited for the cv */
		if (scn->scn_prefetch_stop) {
			mutex_exit(&spa->spa_scrub_lock);
			break;
		}

		/* remove the prefetch IO from the tree */
		spic = avl_first(&scn->scn_prefetch_queue);
		spa->spa_scrub_inflight += BP_GET_PSIZE(&spic->spic_bp);
		avl_remove(&scn->scn_prefetch_queue, spic);

		mutex_exit(&spa->spa_scrub_lock);

		/* issue the prefetch asynchronously */
		(void) arc_read(scn->scn_zio_root, scn->scn_dp->dp_spa,
		    &spic->spic_bp, dsl_scan_prefetch_cb, spic->spic_spc,
		    ZIO_PRIORITY_SCRUB, zio_flags, &flags, &spic->spic_zb);

		kmem_free(spic, sizeof (scan_prefetch_issue_ctx_t));
	}

	ASSERT(scn->scn_prefetch_stop);

	/* free any prefetches we didn't get to complete */
	mutex_enter(&spa->spa_scrub_lock);
	while ((spic = avl_first(&scn->scn_prefetch_queue)) != NULL) {
		avl_remove(&scn->scn_prefetch_queue, spic);
		scan_prefetch_ctx_rele(spic->spic_spc, scn);
		kmem_free(spic, sizeof (scan_prefetch_issue_ctx_t));
	}
	ASSERT0(avl_numnodes(&scn->scn_prefetch_queue));
	mutex_exit(&spa->spa_scrub_lock);
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

static void dsl_scan_visitbp(blkptr_t *bp, const zbookmark_phys_t *zb,
    dnode_phys_t *dnp, dsl_dataset_t *ds, dsl_scan_t *scn,
    dmu_objset_type_t ostype, dmu_tx_t *tx);
static void dsl_scan_visitdnode(
    dsl_scan_t *, dsl_dataset_t *ds, dmu_objset_type_t ostype,
    dnode_phys_t *dnp, uint64_t object, dmu_tx_t *tx);

/*
 * Return nonzero on i/o error.
 * Return new buf to write out in *bufp.
 */
static int
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
		    ZIO_PRIORITY_SCRUB, zio_flags, &flags, zb);
		if (err) {
			scn->scn_phys.scn_errors++;
			return (err);
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
		int i;
		int epb = BP_GET_LSIZE(bp) >> DNODE_SHIFT;
		arc_buf_t *buf;

		err = arc_read(NULL, dp->dp_spa, bp, arc_getbuf_func, &buf,
		    ZIO_PRIORITY_SCRUB, zio_flags, &flags, zb);
		if (err) {
			scn->scn_phys.scn_errors++;
			return (err);
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
		    ZIO_PRIORITY_SCRUB, zio_flags, &flags, zb);
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

static void
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
	blkptr_t *bp_toread = NULL;

	if (dsl_scan_check_suspend(scn, zb))
		return;

	if (dsl_scan_check_resume(scn, dnp, zb))
		return;

	scn->scn_visited_this_txg++;

	dprintf_bp(bp,
	    "visiting ds=%p/%llu zb=%llx/%llx/%llx/%llx bp=%p",
	    ds, ds ? ds->ds_object : 0,
	    zb->zb_objset, zb->zb_object, zb->zb_level, zb->zb_blkid,
	    bp);

	if (BP_IS_HOLE(bp)) {
		scn->scn_holes_this_txg++;
		return;
	}

	if (bp->blk_birth <= scn->scn_phys.scn_cur_min_txg) {
		scn->scn_lt_min_this_txg++;
		return;
	}

	bp_toread = kmem_alloc(sizeof (blkptr_t), KM_SLEEP);
	*bp_toread = *bp;

	if (dsl_scan_recurse(scn, ds, ostype, dnp, bp_toread, zb, tx) != 0)
		return;

	/*
	 * If dsl_scan_ddt() has already visited this block, it will have
	 * already done any translations or scrubbing, so don't call the
	 * callback again.
	 */
	if (ddt_class_contains(dp->dp_spa,
	    scn->scn_phys.scn_ddt_class_max, bp)) {
		scn->scn_ddt_contained_this_txg++;
		goto out;
	}

	/*
	 * If this block is from the future (after cur_max_txg), then we
	 * are doing this on behalf of a deleted snapshot, and we will
	 * revisit the future block on the next pass of this dataset.
	 * Don't scan it now unless we need to because something
	 * under it was modified.
	 */
	if (BP_PHYSICAL_BIRTH(bp) > scn->scn_phys.scn_cur_max_txg) {
		scn->scn_gt_max_this_txg++;
		goto out;
	}

	scan_funcs[scn->scn_phys.scn_func](dp, bp, zb);
out:
	kmem_free(bp_toread, sizeof (blkptr_t));
}

static void
dsl_scan_visit_rootbp(dsl_scan_t *scn, dsl_dataset_t *ds, blkptr_t *bp,
    dmu_tx_t *tx)
{
	zbookmark_phys_t zb;
	scan_prefetch_ctx_t *spc;

	SET_BOOKMARK(&zb, ds ? ds->ds_object : DMU_META_OBJSET,
	    ZB_ROOT_OBJECT, ZB_ROOT_LEVEL, ZB_ROOT_BLKID);

	if (ZB_IS_ZERO(&scn->scn_phys.scn_bookmark)) {
		SET_BOOKMARK(&scn->scn_prefetch_bookmark,
		    zb.zb_objset, 0, 0, 0);
	} else {
		scn->scn_prefetch_bookmark = scn->scn_phys.scn_bookmark;
	}

	scn->scn_objsets_visited_this_txg++;

	spc = scan_prefetch_ctx_create(scn, NULL, FTAG);
	dsl_scan_prefetch(spc, bp, &zb);
	scan_prefetch_ctx_rele(spc, FTAG);

	dsl_scan_visitbp(bp, &zb, NULL, ds, scn, DMU_OST_NONE, tx);

	dprintf_ds(ds, "finished scan%s", "");
}

static void
ds_destroyed_scn_phys(dsl_dataset_t *ds, dsl_scan_phys_t *scn_phys)
{
	if (scn_phys->scn_bookmark.zb_objset == ds->ds_object) {
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
			scn_phys->scn_bookmark.zb_objset =
			    dsl_dataset_phys(ds)->ds_next_snap_obj;
			zfs_dbgmsg("destroying ds %llu; currently traversing; "
			    "reset zb_objset to %llu",
			    (u_longlong_t)ds->ds_object,
			    (u_longlong_t)dsl_dataset_phys(ds)->
			    ds_next_snap_obj);
			scn_phys->scn_flags |= DSF_VISIT_DS_AGAIN;
		} else {
			SET_BOOKMARK(&scn_phys->scn_bookmark,
			    ZB_DESTROYED_OBJSET, 0, 0, 0);
			zfs_dbgmsg("destroying ds %llu; currently traversing; "
			    "reset bookmark to -1,0,0,0",
			    (u_longlong_t)ds->ds_object);
		}
	}
}

/*
 * Invoked when a dataset is destroyed. We need to make sure that:
 *
 * 1) If it is the dataset that was currently being scanned, we write
 *	a new dsl_scan_phys_t and marking the objset reference in it
 *	as destroyed.
 * 2) Remove it from the work queue, if it was present.
 *
 * If the dataset was actually a snapshot, instead of marking the dataset
 * as destroyed, we instead substitute the next snapshot in line.
 */
void
dsl_scan_ds_destroyed(dsl_dataset_t *ds, dmu_tx_t *tx)
{
	dsl_pool_t *dp = ds->ds_dir->dd_pool;
	dsl_scan_t *scn = dp->dp_scan;
	uint64_t mintxg;

	if (!dsl_scan_is_running(scn))
		return;

	ds_destroyed_scn_phys(ds, &scn->scn_phys);
	ds_destroyed_scn_phys(ds, &scn->scn_phys_cached);

	if (scan_ds_queue_contains(scn, ds->ds_object, &mintxg)) {
		scan_ds_queue_remove(scn, ds->ds_object);
		if (ds->ds_is_snapshot)
			scan_ds_queue_insert(scn,
			    dsl_dataset_phys(ds)->ds_next_snap_obj, mintxg);
	}

	if (zap_lookup_int_key(dp->dp_meta_objset, scn->scn_phys.scn_queue_obj,
	    ds->ds_object, &mintxg) == 0) {
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
	dsl_scan_sync_state(scn, tx, SYNC_CACHED);
}

static void
ds_snapshotted_bookmark(dsl_dataset_t *ds, zbookmark_phys_t *scn_bookmark)
{
	if (scn_bookmark->zb_objset == ds->ds_object) {
		scn_bookmark->zb_objset =
		    dsl_dataset_phys(ds)->ds_prev_snap_obj;
		zfs_dbgmsg("snapshotting ds %llu; currently traversing; "
		    "reset zb_objset to %llu",
		    (u_longlong_t)ds->ds_object,
		    (u_longlong_t)dsl_dataset_phys(ds)->ds_prev_snap_obj);
	}
}

/*
 * Called when a dataset is snapshotted. If we were currently traversing
 * this snapshot, we reset our bookmark to point at the newly created
 * snapshot. We also modify our work queue to remove the old snapshot and
 * replace with the new one.
 */
void
dsl_scan_ds_snapshotted(dsl_dataset_t *ds, dmu_tx_t *tx)
{
	dsl_pool_t *dp = ds->ds_dir->dd_pool;
	dsl_scan_t *scn = dp->dp_scan;
	uint64_t mintxg;

	if (!dsl_scan_is_running(scn))
		return;

	ASSERT(dsl_dataset_phys(ds)->ds_prev_snap_obj != 0);

	ds_snapshotted_bookmark(ds, &scn->scn_phys.scn_bookmark);
	ds_snapshotted_bookmark(ds, &scn->scn_phys_cached.scn_bookmark);

	if (scan_ds_queue_contains(scn, ds->ds_object, &mintxg)) {
		scan_ds_queue_remove(scn, ds->ds_object);
		scan_ds_queue_insert(scn,
		    dsl_dataset_phys(ds)->ds_prev_snap_obj, mintxg);
	}

	if (zap_lookup_int_key(dp->dp_meta_objset, scn->scn_phys.scn_queue_obj,
	    ds->ds_object, &mintxg) == 0) {
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

	dsl_scan_sync_state(scn, tx, SYNC_CACHED);
}

static void
ds_clone_swapped_bookmark(dsl_dataset_t *ds1, dsl_dataset_t *ds2,
    zbookmark_phys_t *scn_bookmark)
{
	if (scn_bookmark->zb_objset == ds1->ds_object) {
		scn_bookmark->zb_objset = ds2->ds_object;
		zfs_dbgmsg("clone_swap ds %llu; currently traversing; "
		    "reset zb_objset to %llu",
		    (u_longlong_t)ds1->ds_object,
		    (u_longlong_t)ds2->ds_object);
	} else if (scn_bookmark->zb_objset == ds2->ds_object) {
		scn_bookmark->zb_objset = ds1->ds_object;
		zfs_dbgmsg("clone_swap ds %llu; currently traversing; "
		    "reset zb_objset to %llu",
		    (u_longlong_t)ds2->ds_object,
		    (u_longlong_t)ds1->ds_object);
	}
}

/*
 * Called when a parent dataset and its clone are swapped. If we were
 * currently traversing the dataset, we need to switch to traversing the
 * newly promoted parent.
 */
void
dsl_scan_ds_clone_swapped(dsl_dataset_t *ds1, dsl_dataset_t *ds2, dmu_tx_t *tx)
{
	dsl_pool_t *dp = ds1->ds_dir->dd_pool;
	dsl_scan_t *scn = dp->dp_scan;
	uint64_t mintxg;

	if (!dsl_scan_is_running(scn))
		return;

	ds_clone_swapped_bookmark(ds1, ds2, &scn->scn_phys.scn_bookmark);
	ds_clone_swapped_bookmark(ds1, ds2, &scn->scn_phys_cached.scn_bookmark);

	if (scan_ds_queue_contains(scn, ds1->ds_object, &mintxg)) {
		scan_ds_queue_remove(scn, ds1->ds_object);
		scan_ds_queue_insert(scn, ds2->ds_object, mintxg);
	}
	if (scan_ds_queue_contains(scn, ds2->ds_object, &mintxg)) {
		scan_ds_queue_remove(scn, ds2->ds_object);
		scan_ds_queue_insert(scn, ds1->ds_object, mintxg);
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
	}
	if (zap_lookup_int_key(dp->dp_meta_objset, scn->scn_phys.scn_queue_obj,
	    ds2->ds_object, &mintxg) == 0) {
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

	dsl_scan_sync_state(scn, tx, SYNC_CACHED);
}

/* ARGSUSED */
static int
enqueue_clones_cb(dsl_pool_t *dp, dsl_dataset_t *hds, void *arg)
{
	uint64_t originobj = *(uint64_t *)arg;
	dsl_dataset_t *ds;
	int err;
	dsl_scan_t *scn = dp->dp_scan;

	if (dsl_dir_phys(hds->ds_dir)->dd_origin_obj != originobj)
		return (0);

	err = dsl_dataset_hold_obj(dp, hds->ds_object, FTAG, &ds);
	if (err)
		return (err);

	while (dsl_dataset_phys(ds)->ds_prev_snap_obj != originobj) {
		dsl_dataset_t *prev;
		err = dsl_dataset_hold_obj(dp,
		    dsl_dataset_phys(ds)->ds_prev_snap_obj, FTAG, &prev);

		dsl_dataset_rele(ds, FTAG);
		if (err)
			return (err);
		ds = prev;
	}
	scan_ds_queue_insert(scn, ds->ds_object,
	    dsl_dataset_phys(ds)->ds_prev_snap_txg);
	dsl_dataset_rele(ds, FTAG);
	return (0);
}

static void
dsl_scan_visitds(dsl_scan_t *scn, uint64_t dsobj, dmu_tx_t *tx)
{
	dsl_pool_t *dp = scn->scn_dp;
	dsl_dataset_t *ds;

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
		char *dsname = kmem_alloc(MAXNAMELEN, KM_SLEEP);
		dsl_dataset_name(ds, dsname);
		zfs_dbgmsg("scanning dataset %llu (%s) is unnecessary because "
		    "cur_min_txg (%llu) >= max_txg (%llu)",
		    (longlong_t)dsobj, dsname,
		    (longlong_t)scn->scn_phys.scn_cur_min_txg,
		    (longlong_t)scn->scn_phys.scn_max_txg);
		kmem_free(dsname, MAXNAMELEN);

		goto out;
	}

	/*
	 * Only the ZIL in the head (non-snapshot) is valid. Even though
	 * snapshots can have ZIL block pointers (which may be the same
	 * BP as in the head), they must be ignored. In addition, $ORIGIN
	 * doesn't have a objset (i.e. its ds_bp is a hole) so we don't
	 * need to look for a ZIL in it either. So we traverse the ZIL here,
	 * rather than in scan_recurse(), because the regular snapshot
	 * block-sharing rules don't apply to it.
	 */
	if (DSL_SCAN_IS_SCRUB_RESILVER(scn) && !dsl_dataset_is_snapshot(ds) &&
	    (dp->dp_origin_snap == NULL ||
	    ds->ds_dir != dp->dp_origin_snap->ds_dir)) {
		objset_t *os;
		if (dmu_objset_from_ds(ds, &os) != 0) {
			goto out;
		}
		dsl_scan_zil(dp, &os->os_zil_header);
	}

	/*
	 * Iterate over the bps in this ds.
	 */
	dmu_buf_will_dirty(ds->ds_dbuf, tx);
	rrw_enter(&ds->ds_bp_rwlock, RW_READER, FTAG);
	dsl_scan_visit_rootbp(scn, ds, &dsl_dataset_phys(ds)->ds_bp, tx);
	rrw_exit(&ds->ds_bp_rwlock, FTAG);

	char *dsname = kmem_alloc(ZFS_MAX_DATASET_NAME_LEN, KM_SLEEP);
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
		scan_ds_queue_insert(scn, ds->ds_object,
		    scn->scn_phys.scn_cur_max_txg);
		goto out;
	}

	/*
	 * Add descendent datasets to work queue.
	 */
	if (dsl_dataset_phys(ds)->ds_next_snap_obj != 0) {
		scan_ds_queue_insert(scn,
		    dsl_dataset_phys(ds)->ds_next_snap_obj,
		    dsl_dataset_phys(ds)->ds_creation_txg);
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
			zap_cursor_t zc;
			zap_attribute_t za;
			for (zap_cursor_init(&zc, dp->dp_meta_objset,
			    dsl_dataset_phys(ds)->ds_next_clones_obj);
			    zap_cursor_retrieve(&zc, &za) == 0;
			    (void) zap_cursor_advance(&zc)) {
				scan_ds_queue_insert(scn,
				    zfs_strtonum(za.za_name, NULL),
				    dsl_dataset_phys(ds)->ds_creation_txg);
			}
			zap_cursor_fini(&zc);
		} else {
			VERIFY0(dmu_objset_find_dp(dp, dp->dp_root_dir_obj,
			    enqueue_clones_cb, &ds->ds_object,
			    DS_FIND_CHILDREN));
		}
	}

out:
	dsl_dataset_rele(ds, FTAG);
}

/* ARGSUSED */
static int
enqueue_cb(dsl_pool_t *dp, dsl_dataset_t *hds, void *arg)
{
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

	scan_ds_queue_insert(scn, ds->ds_object,
	    dsl_dataset_phys(ds)->ds_prev_snap_txg);
	dsl_dataset_rele(ds, FTAG);
	return (0);
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
	ddt_entry_t dde = { 0 };
	int error;
	uint64_t n = 0;

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

static uint64_t
dsl_scan_ds_maxtxg(dsl_dataset_t *ds)
{
	uint64_t smt = ds->ds_dir->dd_pool->dp_scan->scn_phys.scn_max_txg;
	if (ds->ds_is_snapshot)
		return (MIN(smt, dsl_dataset_phys(ds)->ds_creation_txg));
	return (smt);
}

static void
dsl_scan_visit(dsl_scan_t *scn, dmu_tx_t *tx)
{
	scan_ds_t *sds;
	dsl_pool_t *dp = scn->scn_dp;

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
			    enqueue_cb, NULL, DS_FIND_CHILDREN));
		} else {
			dsl_scan_visitds(scn,
			    dp->dp_origin_snap->ds_object, tx);
		}
		ASSERT(!scn->scn_suspending);
	} else if (scn->scn_phys.scn_bookmark.zb_objset !=
	    ZB_DESTROYED_OBJSET) {
		uint64_t dsobj = scn->scn_phys.scn_bookmark.zb_objset;
		/*
		 * If we were suspended, continue from here. Note if the
		 * ds we were suspended on was deleted, the zb_objset may
		 * be -1, so we will skip this and find a new objset
		 * below.
		 */
		dsl_scan_visitds(scn, dsobj, tx);
		if (scn->scn_suspending)
			return;
	}

	/*
	 * In case we suspended right at the end of the ds, zero the
	 * bookmark so we don't think that we're still trying to resume.
	 */
	bzero(&scn->scn_phys.scn_bookmark, sizeof (zbookmark_phys_t));

	/*
	 * Keep pulling things out of the dataset avl queue. Updates to the
	 * persistent zap-object-as-queue happen only at checkpoints.
	 */
	while ((sds = avl_first(&scn->scn_queue)) != NULL) {
		dsl_dataset_t *ds;
		uint64_t dsobj = sds->sds_dsobj;
		uint64_t txg = sds->sds_txg;

		/* dequeue and free the ds from the queue */
		scan_ds_queue_remove(scn, dsobj);
		sds = NULL;	/* must not be touched after removal */

		/* Set up min / max txg */
		VERIFY3U(0, ==, dsl_dataset_hold_obj(dp, dsobj, FTAG, &ds));
		if (txg != 0) {
			scn->scn_phys.scn_cur_min_txg =
			    MAX(scn->scn_phys.scn_min_txg, txg);
		} else {
			scn->scn_phys.scn_cur_min_txg =
			    MAX(scn->scn_phys.scn_min_txg,
			    dsl_dataset_phys(ds)->ds_prev_snap_txg);
		}
		scn->scn_phys.scn_cur_max_txg = dsl_scan_ds_maxtxg(ds);
		dsl_dataset_rele(ds, FTAG);

		dsl_scan_visitds(scn, dsobj, tx);
		if (scn->scn_suspending)
			return;
	}
	/* No more objsets to fetch, we're done */
	scn->scn_phys.scn_bookmark.zb_objset = ZB_DESTROYED_OBJSET;
	ASSERT0(scn->scn_suspending);
}

static uint64_t
dsl_scan_count_leaves(vdev_t *vd)
{
	uint64_t i, leaves = 0;
	
	/* we only count leaves that belong to the main pool and are readable */
	if (vd->vdev_islog || vd->vdev_isspare ||
	    vd->vdev_isl2cache || !vdev_readable(vd))
		return (0);
	
	if (vd->vdev_ops->vdev_op_leaf)
		return (1);
	
	for (i = 0; i < vd->vdev_children; i++) {
		leaves += dsl_scan_count_leaves(vd->vdev_child[i]);
	}
	
	return (leaves);
}


static void
scan_io_queues_update_zio_stats(dsl_scan_io_queue_t *q, const blkptr_t *bp)
{
	int i;
	uint64_t cur_size = 0;

	for (i = 0; i < BP_GET_NDVAS(bp); i++) {
		cur_size += DVA_GET_ASIZE(&bp->blk_dva[i]);
	}

	q->q_total_zio_size_this_txg += cur_size;
	q->q_zios_this_txg++;
}

static void
scan_io_queues_update_seg_stats(dsl_scan_io_queue_t *q, uint64_t start,
    uint64_t end)
{
	q->q_total_seg_size_this_txg += end - start;
	q->q_segs_this_txg++;
}

static boolean_t
scan_io_queue_check_suspend(dsl_scan_t *scn)
{
	/* See comment in dsl_scan_check_suspend() */
	uint64_t curr_time_ns = gethrtime();
	uint64_t scan_time_ns = curr_time_ns - scn->scn_sync_start_time;
	uint64_t sync_time_ns = curr_time_ns -
	    scn->scn_dp->dp_spa->spa_sync_starttime;
	int dirty_pct = scn->scn_dp->dp_dirty_total * 100 / zfs_dirty_data_max;
	int mintime = (scn->scn_phys.scn_func == POOL_SCAN_RESILVER) ?
	    zfs_resilver_min_time_ms : zfs_scrub_min_time_ms;
       
	return ((NSEC2MSEC(scan_time_ns) > mintime &&
	    (dirty_pct >= zfs_vdev_async_write_active_min_dirty_percent ||
	    txg_sync_waiting(scn->scn_dp) ||
	    NSEC2SEC(sync_time_ns) >= zfs_txg_timeout)) ||
	    spa_shutting_down(scn->scn_dp->dp_spa));
}

/*
 * Given a list of scan_io_t's in io_list, this issues the io's out to
 * disk. This consumes the io_list and frees the scan_io_t's. This is
 * called when emptying queues, either when we're up against the memory
 * limit or when we have finished scanning. Returns B_TRUE if we stopped
 * processing the list before we finished. Any zios that were not issued
 * will remain in the io_list.
 */
static boolean_t
scan_io_queue_issue(dsl_scan_io_queue_t *queue, list_t *io_list)
{
	dsl_scan_t *scn = queue->q_scn;
	scan_io_t *sio;
	int64_t bytes_issued = 0;
	boolean_t suspended = B_FALSE;

	while ((sio = list_head(io_list)) != NULL) {
		blkptr_t bp;

		if (scan_io_queue_check_suspend(scn)) {
			suspended = B_TRUE;
			break;
		}

		sio2bp(sio, &bp, queue->q_vd->vdev_id);
		bytes_issued += sio->sio_asize;
		scan_exec_io(scn->scn_dp, &bp, sio->sio_flags,
		    &sio->sio_zb, queue);
		(void) list_remove_head(io_list);
		scan_io_queues_update_zio_stats(queue, &bp);
		kmem_free(sio, sizeof (*sio));
	}

	atomic_add_64(&scn->scn_bytes_pending, -bytes_issued);

	return (suspended);
}

/*
 * Given a range_seg_t (extent) and a list, this function passes over a
 * scan queue and gathers up the appropriate ios which fit into that
 * scan seg (starting from lowest LBA). At the end, we remove the segment
 * from the q_exts_by_addr range tree.
 */
static boolean_t
scan_io_queue_gather(dsl_scan_io_queue_t *queue, range_seg_t *rs, list_t *list)
{
	scan_io_t srch_sio, *sio, *next_sio;
	avl_index_t idx;
	uint_t num_sios = 0;
	int64_t bytes_issued = 0;

	ASSERT(rs != NULL);
	ASSERT(MUTEX_HELD(&queue->q_vd->vdev_scan_io_queue_lock));

	srch_sio.sio_offset = rs->rs_start;

	/*
	 * The exact start of the extent might not contain any matching zios,
	 * so if that's the case, examine the next one in the tree.
	 */
	sio = avl_find(&queue->q_sios_by_addr, &srch_sio, &idx);
	if (sio == NULL)
		sio = avl_nearest(&queue->q_sios_by_addr, idx, AVL_AFTER);

	while (sio != NULL && sio->sio_offset < rs->rs_end && num_sios <= 32) {
		ASSERT3U(sio->sio_offset, >=, rs->rs_start);
		ASSERT3U(sio->sio_offset + sio->sio_asize, <=, rs->rs_end);

		next_sio = AVL_NEXT(&queue->q_sios_by_addr, sio);
		avl_remove(&queue->q_sios_by_addr, sio);

		bytes_issued += sio->sio_asize;
		num_sios++;
		list_insert_tail(list, sio);
		sio = next_sio;
	}

	/*
	 * We limit the number of sios we process at once to 32 to avoid
	 * biting off more than we can chew. If we didn't take everything
	 * in the segment we update it to reflect the work we were able to
	 * complete. Otherwise, we remove it from the range tree entirely.
	 */
	if (sio != NULL && sio->sio_offset < rs->rs_end) {
		range_tree_adjust_fill(queue->q_exts_by_addr, rs,
		    -bytes_issued);
		range_tree_resize_segment(queue->q_exts_by_addr, rs,
		    sio->sio_offset, rs->rs_end - sio->sio_offset);

		return (B_TRUE);
	} else {
		range_tree_remove(queue->q_exts_by_addr, rs->rs_start,
		    rs->rs_end - rs->rs_start);
		return (B_FALSE);
	}
}


/*
 * This is called from the queue emptying thread and selects the next
 * extent from which we are to issue io's. The behavior of this function
 * depends on the state of the scan, the current memory consumption and
 * whether or not we are performing a scan shutdown.
 * 1) We select extents in an elevator algorithm (LBA-order) if the scan
 * 	needs to perform a checkpoint
 * 2) We select the largest available extent if we are up against the
 * 	memory limit.
 * 3) Otherwise we don't select any extents.
 */
static const range_seg_t *
scan_io_queue_fetch_ext(dsl_scan_io_queue_t *queue)
{
	dsl_scan_t *scn = queue->q_scn;

	ASSERT(MUTEX_HELD(&queue->q_vd->vdev_scan_io_queue_lock));
	ASSERT(scn->scn_is_sorted);

	/* handle tunable overrides */
	if (scn->scn_checkpointing || scn->scn_clearing) {
		if (zfs_scan_issue_strategy == 1) {
			return (range_tree_first(queue->q_exts_by_addr));
		} else if (zfs_scan_issue_strategy == 2) {
			return (avl_first(&queue->q_exts_by_size));
		}
	}

	/*
	 * During normal clearing, we want to issue our largest segments
	 * first, keeping IO as sequential as possible, and leaving the
	 * smaller extents for later with the hope that they might eventually
	 * grow to larger sequential segments. However, when the scan is
	 * checkpointing, no new extents will be added to the sorting queue,
	 * so the way we are sorted now is as good as it will ever get.
	 * In this case, we instead switch to issuing extents in LBA order.
	 */
	if (scn->scn_checkpointing) {
		return (range_tree_first(queue->q_exts_by_addr));
	} else if (scn->scn_clearing) {
		return (avl_first(&queue->q_exts_by_size));
	} else {
		return (NULL);
	}
}

static void
scan_io_queues_run_one(void *arg)
{
	dsl_scan_io_queue_t *queue = arg;
	kmutex_t *q_lock = &queue->q_vd->vdev_scan_io_queue_lock;
	boolean_t suspended = B_FALSE;
	range_seg_t *rs = NULL;
	scan_io_t *sio = NULL;
	list_t sio_list;
	uint64_t bytes_per_leaf = zfs_scan_vdev_limit;
	uint64_t nr_leaves = dsl_scan_count_leaves(queue->q_vd);

	ASSERT(queue->q_scn->scn_is_sorted);

	list_create(&sio_list, sizeof (scan_io_t),
	    offsetof(scan_io_t, sio_nodes.sio_list_node));
	mutex_enter(q_lock);

	/* calculate maximum in-flight bytes for this txg (min 1MB) */
	queue->q_maxinflight_bytes =
	    MAX(nr_leaves * bytes_per_leaf, 1ULL << 20);

	/* reset per-queue scan statistics for this txg */
	queue->q_total_seg_size_this_txg = 0;
	queue->q_segs_this_txg = 0;
	queue->q_total_zio_size_this_txg = 0;
	queue->q_zios_this_txg = 0;

	/* loop until we have run out of time or sios */
	while ((rs = (range_seg_t*)scan_io_queue_fetch_ext(queue)) != NULL) {
		uint64_t seg_start = 0, seg_end = 0;
		boolean_t more_left = B_TRUE;

		ASSERT(list_is_empty(&sio_list));

		/* loop while we still have sios left to process in this rs */
		while (more_left) {
			scan_io_t *first_sio, *last_sio;

			/*
			 * We have selected which extent needs to be
			 * processed next. Gather up the corresponding sios.
			 */
			more_left = scan_io_queue_gather(queue, rs, &sio_list);
			ASSERT(!list_is_empty(&sio_list));
			first_sio = list_head(&sio_list);
			last_sio = list_tail(&sio_list);

			seg_end = last_sio->sio_offset + last_sio->sio_asize;
			if (seg_start == 0)
				seg_start = first_sio->sio_offset;

			/*
			 * Issuing sios can take a long time so drop the
			 * queue lock. The sio queue won't be updated by
			 * other threads since we're in syncing context so
			 * we can be sure that our trees will remain exactly
			 * as we left them.
			 */
			mutex_exit(q_lock);
			suspended = scan_io_queue_issue(queue, &sio_list);
			mutex_enter(q_lock);

			if (suspended)
				break;
		}
		/* update statistics for debugging purposes */
		scan_io_queues_update_seg_stats(queue, seg_start, seg_end);
		
		if (suspended)
			break;
	}
		

	/* If we were suspended in the middle of processing,
	 * requeue any unfinished sios and exit.
	 */
	while ((sio = list_head(&sio_list)) != NULL) {
		list_remove(&sio_list, sio);
		scan_io_queue_insert_impl(queue, sio);
	}

	mutex_exit(q_lock);
	list_destroy(&sio_list);
}

/*
 * Performs an emptying run on all scan queues in the pool. This just
 * punches out one thread per top-level vdev, each of which processes
 * only that vdev's scan queue. We can parallelize the I/O here because
 * we know that each queue's io's only affect its own top-level vdev.
 *
 * This function waits for the queue runs to complete, and must be
 * called from dsl_scan_sync (or in general, syncing context).
 */
static void
scan_io_queues_run(dsl_scan_t *scn)
{
	spa_t *spa = scn->scn_dp->dp_spa;

	ASSERT(scn->scn_is_sorted);
	ASSERT(spa_config_held(spa, SCL_CONFIG, RW_READER));

	if (scn->scn_bytes_pending == 0)
		return;

	if (scn->scn_taskq == NULL) {
		char *tq_name = kmem_zalloc(ZFS_MAX_DATASET_NAME_LEN + 16,
		    KM_SLEEP);
		int nthreads = spa->spa_root_vdev->vdev_children;

		/*
		 * We need to make this taskq *always* execute as many
		 * threads in parallel as we have top-level vdevs and no
		 * less, otherwise strange serialization of the calls to
		 * scan_io_queues_run_one can occur during spa_sync runs
		 * and that significantly impacts performance.
		 */
		(void) snprintf(tq_name, ZFS_MAX_DATASET_NAME_LEN + 16,
		    "dsl_scan_tq_%s", spa->spa_name);
		scn->scn_taskq = taskq_create(tq_name, nthreads, minclsyspri,
		    nthreads, nthreads, TASKQ_PREPOPULATE);
		kmem_free(tq_name, ZFS_MAX_DATASET_NAME_LEN + 16);
	}

	for (uint64_t i = 0; i < spa->spa_root_vdev->vdev_children; i++) {
		vdev_t *vd = spa->spa_root_vdev->vdev_child[i];

		mutex_enter(&vd->vdev_scan_io_queue_lock);
		if (vd->vdev_scan_io_queue != NULL) {
			VERIFY(taskq_dispatch(scn->scn_taskq,
			    scan_io_queues_run_one, vd->vdev_scan_io_queue,
			    TQ_SLEEP) != TASKQID_INVALID);
		}
		mutex_exit(&vd->vdev_scan_io_queue_lock);
	}

	/*
	 * Wait for the queues to finish issuing thir IOs for this run
	 * before we return. There may still be IOs in flight at this
	 * point.
	 */
	taskq_wait(scn->scn_taskq);
}

static boolean_t
dsl_scan_async_block_should_pause(dsl_scan_t *scn)
{
	uint64_t elapsed_nanosecs;

	if (zfs_recover)
		return (B_FALSE);

	if (scn->scn_visited_this_txg >= zfs_async_block_max_blocks)
		return (B_TRUE);

	elapsed_nanosecs = gethrtime() - scn->scn_sync_start_time;
	return (elapsed_nanosecs / NANOSEC > zfs_txg_timeout ||
	    (NSEC2MSEC(elapsed_nanosecs) > scn->scn_async_block_min_time_ms &&
	    txg_sync_waiting(scn->scn_dp)) ||
	    spa_shutting_down(scn->scn_dp->dp_spa));
}

static int
dsl_scan_free_block_cb(void *arg, const blkptr_t *bp, dmu_tx_t *tx)
{
	dsl_scan_t *scn = arg;

	if (!scn->scn_is_bptree ||
	    (BP_GET_LEVEL(bp) == 0 && BP_GET_TYPE(bp) != DMU_OT_OBJSET)) {
		if (dsl_scan_async_block_should_pause(scn))
			return (SET_ERROR(ERESTART));
	}

	zio_nowait(zio_free_sync(scn->scn_zio_root, scn->scn_dp->dp_spa,
	    dmu_tx_get_txg(tx), bp, BP_GET_PSIZE(bp), 0));
	dsl_dir_diduse_space(tx->tx_pool->dp_free_dir, DD_USED_HEAD,
	    -bp_get_dsize_sync(scn->scn_dp->dp_spa, bp),
	    -BP_GET_PSIZE(bp), -BP_GET_UCSIZE(bp), tx);
	scn->scn_visited_this_txg++;
	return (0);
}

static void
dsl_scan_update_stats(dsl_scan_t *scn)
{
	spa_t *spa = scn->scn_dp->dp_spa;
	uint64_t i;
	uint64_t seg_size_total = 0, zio_size_total = 0;
	uint64_t seg_count_total = 0, zio_count_total = 0;

	for (i = 0; i < spa->spa_root_vdev->vdev_children; i++) {
		vdev_t *vd = spa->spa_root_vdev->vdev_child[i];
		dsl_scan_io_queue_t *queue = vd->vdev_scan_io_queue;

		if (queue == NULL)
			continue;

		seg_size_total += queue->q_total_seg_size_this_txg;
		zio_size_total += queue->q_total_zio_size_this_txg;
		seg_count_total += queue->q_segs_this_txg;
		zio_count_total += queue->q_zios_this_txg;
	}

	if (seg_count_total == 0 || zio_count_total == 0) {
		scn->scn_avg_seg_size_this_txg = 0;
		scn->scn_avg_zio_size_this_txg = 0;
		scn->scn_segs_this_txg = 0;
		scn->scn_zios_this_txg = 0;
		return;
	}

	scn->scn_avg_seg_size_this_txg = seg_size_total / seg_count_total;
	scn->scn_avg_zio_size_this_txg = zio_size_total / zio_count_total;
	scn->scn_segs_this_txg = seg_count_total;
	scn->scn_zios_this_txg = zio_count_total;
}

static int
dsl_scan_obsolete_block_cb(void *arg, const blkptr_t *bp, dmu_tx_t *tx)
{
	dsl_scan_t *scn = arg;
	const dva_t *dva = &bp->blk_dva[0];

	if (dsl_scan_async_block_should_pause(scn))
		return (SET_ERROR(ERESTART));

	spa_vdev_indirect_mark_obsolete(scn->scn_dp->dp_spa,
	    DVA_GET_VDEV(dva), DVA_GET_OFFSET(dva),
	    DVA_GET_ASIZE(dva), tx);
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
	if ((dsl_scan_is_running(scn) && !dsl_scan_is_paused_scrub(scn)) ||
	    (scn->scn_async_destroying && !scn->scn_async_stalled))
		return (B_TRUE);

	if (spa_version(scn->scn_dp->dp_spa) >= SPA_VERSION_DEADLISTS) {
		(void) bpobj_space(&scn->scn_dp->dp_free_bpobj,
		    &used, &comp, &uncomp);
	}
	return (used != 0);
}

static boolean_t
dsl_scan_need_resilver(spa_t *spa, const dva_t *dva, size_t psize,
    uint64_t phys_birth)
{
	vdev_t *vd;

	vd = vdev_lookup_top(spa, DVA_GET_VDEV(dva));

	if (vd->vdev_ops == &vdev_indirect_ops) {
		/*
		 * The indirect vdev can point to multiple
		 * vdevs.  For simplicity, always create
		 * the resilver zio_t. zio_vdev_io_start()
		 * will bypass the child resilver i/o's if
		 * they are on vdevs that don't have DTL's.
		 */
		return (B_TRUE);
	}

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
dsl_process_async_destroys(dsl_pool_t *dp, dmu_tx_t *tx)
{
	int err = 0;
	dsl_scan_t *scn = dp->dp_scan;
	spa_t *spa = dp->dp_spa;

	if (spa_suspend_async_destroy(spa))
		return (0);

	if (zfs_free_bpobj_enabled &&
	    spa_version(spa) >= SPA_VERSION_DEADLISTS) {
		scn->scn_is_bptree = B_FALSE;
		scn->scn_async_block_min_time_ms = zfs_free_min_time_ms;
		scn->scn_zio_root = zio_root(spa, NULL,
		    NULL, ZIO_FLAG_MUSTSUCCEED);
		err = bpobj_iterate(&dp->dp_free_bpobj,
		    dsl_scan_free_block_cb, scn, tx);
		VERIFY0(zio_wait(scn->scn_zio_root));
		scn->scn_zio_root = NULL;

		if (err != 0 && err != ERESTART)
			zfs_panic_recover("error %u from bpobj_iterate()", err);
	}

	if (err == 0 && spa_feature_is_active(spa, SPA_FEATURE_ASYNC_DESTROY)) {
		ASSERT(scn->scn_async_destroying);
		scn->scn_is_bptree = B_TRUE;
		scn->scn_zio_root = zio_root(spa, NULL,
		    NULL, ZIO_FLAG_MUSTSUCCEED);
		err = bptree_iterate(dp->dp_meta_objset,
		    dp->dp_bptree_obj, B_TRUE, dsl_scan_free_block_cb, scn, tx);
		VERIFY0(zio_wait(scn->scn_zio_root));
		scn->scn_zio_root = NULL;

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
		    "free_bpobj/bptree txg %llu; err=%d",
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
		return (err);
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

	EQUIV(bpobj_is_open(&dp->dp_obsolete_bpobj),
	    0 == zap_contains(dp->dp_meta_objset, DMU_POOL_DIRECTORY_OBJECT,
	    DMU_POOL_OBSOLETE_BPOBJ));
	if (err == 0 && bpobj_is_open(&dp->dp_obsolete_bpobj)) {
		ASSERT(spa_feature_is_active(dp->dp_spa,
		    SPA_FEATURE_OBSOLETE_COUNTS));

		scn->scn_is_bptree = B_FALSE;
		scn->scn_async_block_min_time_ms = zfs_obsolete_min_time_ms;
		err = bpobj_iterate(&dp->dp_obsolete_bpobj,
		    dsl_scan_obsolete_block_cb, scn, tx);
		if (err != 0 && err != ERESTART)
			zfs_panic_recover("error %u from bpobj_iterate()", err);

		if (bpobj_is_empty(&dp->dp_obsolete_bpobj))
			dsl_pool_destroy_obsolete_bpobj(dp, tx);
	}

	return (0);
}

/*
 * This is the primary entry point for scans that is called from syncing
 * context. Scans must happen entirely during syncing context so that we
 * cna guarantee that blocks we are currently scanning will not change out
 * from under us. While a scan is active, this funciton controls how quickly
 * transaction groups proceed, instead of the normal handling provided by
 * txg_sync_thread().
 */
void
dsl_scan_sync(dsl_pool_t *dp, dmu_tx_t *tx)
{
	dsl_scan_t *scn = dp->dp_scan;
	spa_t *spa = dp->dp_spa;
	int err = 0;
	state_sync_type_t sync_type = SYNC_OPTIONAL;

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
		    func, (longlong_t)tx->tx_txg);
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

	/* reset scan statistics */
	scn->scn_visited_this_txg = 0;
	scn->scn_holes_this_txg = 0;
	scn->scn_lt_min_this_txg = 0;
	scn->scn_gt_max_this_txg = 0;
	scn->scn_ddt_contained_this_txg = 0;
	scn->scn_objsets_visited_this_txg = 0;
	scn->scn_avg_seg_size_this_txg = 0;
	scn->scn_segs_this_txg = 0;
	scn->scn_avg_zio_size_this_txg = 0;
	scn->scn_zios_this_txg = 0;
	scn->scn_suspending = B_FALSE;
	scn->scn_sync_start_time = gethrtime();
	spa->spa_scrub_active = B_TRUE;

	/*
	 * First process the async destroys.  If we pause, don't do
	 * any scrubbing or resilvering.  This ensures that there are no
	 * async destroys while we are scanning, so the scan code doesn't
	 * have to worry about traversing it.  It is also faster to free the
	 * blocks than to scrub them.
	 */
	err = dsl_process_async_destroys(dp, tx);
	if (err != 0)
		return;

	if (!dsl_scan_is_running(scn) || dsl_scan_is_paused_scrub(scn))
		return;

	/*
	 * Wait a few txgs after importing to begin scanning so that
	 * we can get the pool imported quickly.
	 */
	if (spa->spa_syncing_txg < spa->spa_first_txg + SCAN_IMPORT_WAIT_TXGS)
		return;

	/*
	 * It is possible to switch from unsorted to sorted at any time,
	 * but afterwards the scan will remain sorted unless reloaded from
	 * a checkpoint after a reboot.
	 */
	if (!zfs_scan_legacy) {
		scn->scn_is_sorted = B_TRUE;
		if (scn->scn_last_checkpoint == 0)
			scn->scn_last_checkpoint = ddi_get_lbolt();
	}

	/*
	 * For sorted scans, determine what kind of work we will be doing
	 * this txg based on our memory limitations and whether or not we
	 * need to perform a checkpoint.
	 */
	if (scn->scn_is_sorted) {
		/*
		 * If we are over our checkpoint interval, set scn_clearing
		 * so that we can begin checkpointing immediately. The
		 * checkpoint allows us to save a consisent bookmark
		 * representing how much data we have scrubbed so far.
		 * Otherwise, use the memory limit to determine if we should
		 * scan for metadata or start issue scrub IOs. We accumulate
		 * metadata until we hit our hard memory limit at which point
		 * we issue scrub IOs until we are at our soft memory limit.
		 */
		if (scn->scn_checkpointing ||
		    ddi_get_lbolt() - scn->scn_last_checkpoint >
		    SEC_TO_TICK(zfs_scan_checkpoint_intval)) {
			if (!scn->scn_checkpointing)
				zfs_dbgmsg("begin scan checkpoint");

			scn->scn_checkpointing = B_TRUE;
			scn->scn_clearing = B_TRUE;
		} else {
			boolean_t should_clear = dsl_scan_should_clear(scn);
			if (should_clear && !scn->scn_clearing) {
				zfs_dbgmsg("begin scan clearing");
				scn->scn_clearing = B_TRUE;
			} else if (!should_clear && scn->scn_clearing) {
				zfs_dbgmsg("finish scan clearing");
				scn->scn_clearing = B_FALSE;
			}
		}
	} else {
		ASSERT0(scn->scn_checkpointing);
                ASSERT0(scn->scn_clearing);
	}

	if (!scn->scn_clearing && scn->scn_done_txg == 0) {
		/* Need to scan metadata for more blocks to scrub */
		dsl_scan_phys_t *scnp = &scn->scn_phys;
		taskqid_t prefetch_tqid;
		uint64_t bytes_per_leaf = zfs_scan_vdev_limit;
		uint64_t nr_leaves = dsl_scan_count_leaves(spa->spa_root_vdev);

		/*
		 * Calculate the max number of in-flight bytes for pool-wide
		 * scanning operations (minimum 1MB). Limits for the issuing
		 * phase are done per top-level vdev and are handled separately.
		 */
		scn->scn_maxinflight_bytes =
		    MAX(nr_leaves * bytes_per_leaf, 1ULL << 20);

		if (scnp->scn_ddt_bookmark.ddb_class <=
		    scnp->scn_ddt_class_max) {
			ASSERT(ZB_IS_ZERO(&scnp->scn_bookmark));
			zfs_dbgmsg("doing scan sync txg %llu; "
			    "ddt bm=%llu/%llu/%llu/%llx",
			    (longlong_t)tx->tx_txg,
			    (longlong_t)scnp->scn_ddt_bookmark.ddb_class,
			    (longlong_t)scnp->scn_ddt_bookmark.ddb_type,
			    (longlong_t)scnp->scn_ddt_bookmark.ddb_checksum,
			    (longlong_t)scnp->scn_ddt_bookmark.ddb_cursor);
		} else {
			zfs_dbgmsg("doing scan sync txg %llu; "
			    "bm=%llu/%llu/%llu/%llu",
			    (longlong_t)tx->tx_txg,
			    (longlong_t)scnp->scn_bookmark.zb_objset,
			    (longlong_t)scnp->scn_bookmark.zb_object,
			    (longlong_t)scnp->scn_bookmark.zb_level,
			    (longlong_t)scnp->scn_bookmark.zb_blkid);
		}

		scn->scn_zio_root = zio_root(dp->dp_spa, NULL,
		    NULL, ZIO_FLAG_CANFAIL);

		scn->scn_prefetch_stop = B_FALSE;
		prefetch_tqid = taskq_dispatch(dp->dp_sync_taskq,
		    dsl_scan_prefetch_thread, scn, TQ_SLEEP);
		ASSERT(prefetch_tqid != TASKQID_INVALID);

		dsl_pool_config_enter(dp, FTAG);
		dsl_scan_visit(scn, tx);
		dsl_pool_config_exit(dp, FTAG);

		mutex_enter(&dp->dp_spa->spa_scrub_lock);
		scn->scn_prefetch_stop = B_TRUE;
		cv_broadcast(&spa->spa_scrub_io_cv);
		mutex_exit(&dp->dp_spa->spa_scrub_lock);

		taskq_wait_id(dp->dp_sync_taskq, prefetch_tqid);
		(void) zio_wait(scn->scn_zio_root);
		scn->scn_zio_root = NULL;

		zfs_dbgmsg("scan visited %llu blocks in %llums "
		    "(%llu os's, %llu holes, %llu < mintxg, "
		    "%llu in ddt, %llu > maxtxg)",
		    (longlong_t)scn->scn_visited_this_txg,
		    (longlong_t)NSEC2MSEC(gethrtime() -
		    scn->scn_sync_start_time),
		    (longlong_t)scn->scn_objsets_visited_this_txg,
		    (longlong_t)scn->scn_holes_this_txg,
		    (longlong_t)scn->scn_lt_min_this_txg,
		    (longlong_t)scn->scn_ddt_contained_this_txg,
		    (longlong_t)scn->scn_gt_max_this_txg);

		if (!scn->scn_suspending) {
			ASSERT0(avl_numnodes(&scn->scn_queue));
			scn->scn_done_txg = tx->tx_txg + 1;
			if (scn->scn_is_sorted) {
				scn->scn_checkpointing = B_TRUE;
				scn->scn_clearing = B_TRUE;
			}
			zfs_dbgmsg("scan complete txg %llu",
				   (longlong_t)tx->tx_txg);
		}
	} else if (scn->scn_is_sorted && scn->scn_bytes_pending != 0) {
		/* need to issue scrubbing IOs from per-vdev queues */
		scn->scn_zio_root = zio_root(dp->dp_spa, NULL,
		    NULL, ZIO_FLAG_CANFAIL);
		scan_io_queues_run(scn);
		(void) zio_wait(scn->scn_zio_root);
		scn->scn_zio_root = NULL;

		/* calculate and dprintf the current memory usage */
		(void) dsl_scan_should_clear(scn);
		dsl_scan_update_stats(scn);

		zfs_dbgmsg("scrubbed %llu blocks (%llu segs) in %llums "
		    "(avg_block_size = %llu, avg_seg_size = %llu)",
		    (longlong_t)scn->scn_zios_this_txg,
		    (longlong_t)scn->scn_segs_this_txg,
		    (longlong_t)NSEC2MSEC(gethrtime() -
		    scn->scn_sync_start_time),
		    (longlong_t)scn->scn_avg_zio_size_this_txg,
		    (longlong_t)scn->scn_avg_seg_size_this_txg);
	} else if (scn->scn_done_txg != 0 && scn->scn_done_txg <= tx->tx_txg) {
		/* Finished with everything. Mark the scrub as complete */
		zfs_dbgmsg("scan issuing complete txg %llu",
		    (longlong_t)tx->tx_txg);
		ASSERT3U(scn->scn_done_txg, !=, 0);
		ASSERT0(spa->spa_scrub_inflight);
		ASSERT0(scn->scn_bytes_pending);
		dsl_scan_done(scn, B_TRUE, tx);
		sync_type = SYNC_MANDATORY;
	}

	dsl_scan_sync_state(scn, tx, sync_type);
}

static void
count_block(dsl_scan_t *scn, zfs_all_blkstats_t *zab, const blkptr_t *bp)
{
	int i;

	/* update the spa's stats on how many bytes we have issued */
	for (i = 0; i < BP_GET_NDVAS(bp); i++) {
		atomic_add_64(&scn->scn_dp->dp_spa->spa_scan_pass_issued,
		    DVA_GET_ASIZE(&bp->blk_dva[i]));
	}

	/*
	 * If we resume after a reboot, zab will be NULL; don't record
	 * incomplete stats in that case.
	 */
	if (zab == NULL)
		return;

	mutex_enter(&zab->zab_lock);

	for (i = 0; i < 4; i++) {
		int l = (i < 2) ? BP_GET_LEVEL(bp) : DN_MAX_LEVELS;
		int t = (i & 1) ? BP_GET_TYPE(bp) : DMU_OT_TOTAL;
		if (t & DMU_OT_NEWTYPE)
			t = DMU_OT_OTHER;
		zfs_blkstat_t *zb = &zab->zab_type[l][t];
		int equal;

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

	mutex_exit(&zab->zab_lock);
}

static void
scan_io_queue_insert_impl(dsl_scan_io_queue_t *queue, scan_io_t *sio)
{
	avl_index_t idx;
	int64_t asize = sio->sio_asize;
	dsl_scan_t *scn = queue->q_scn;

	ASSERT(MUTEX_HELD(&queue->q_vd->vdev_scan_io_queue_lock));

	if (avl_find(&queue->q_sios_by_addr, sio, &idx) != NULL) {
		/* block is already scheduled for reading */
		atomic_add_64(&scn->scn_bytes_pending, -asize);
		kmem_free(sio, sizeof (*sio));
		return;
	}
	avl_insert(&queue->q_sios_by_addr, sio, idx);
	range_tree_add(queue->q_exts_by_addr, sio->sio_offset, asize);
}

/*
 * Given all the info we got from our metadata scanning process, we
 * construct a scan_io_t and insert it into the scan sorting queue. The
 * I/O must already be suitable for us to process. This is controlled
 * by dsl_scan_enqueue().
 */
static void
scan_io_queue_insert(dsl_scan_io_queue_t *queue, const blkptr_t *bp, int dva_i,
    int zio_flags, const zbookmark_phys_t *zb)
{
	dsl_scan_t *scn = queue->q_scn;
	scan_io_t *sio = kmem_zalloc(sizeof (*sio), KM_SLEEP);

	ASSERT0(BP_IS_GANG(bp));
	ASSERT(MUTEX_HELD(&queue->q_vd->vdev_scan_io_queue_lock));

	bp2sio(bp, sio, dva_i);
	sio->sio_flags = zio_flags;
	sio->sio_zb = *zb;

	/*
	 * Increment the bytes pending counter now so that we can't
	 * get an integer underflow in case the worker processes the
	 * zio before we get to incrementing this counter.
	 */
	atomic_add_64(&scn->scn_bytes_pending, sio->sio_asize);

	scan_io_queue_insert_impl(queue, sio);
}

/*
 * Given a set of I/O parameters as discovered by the metadata traversal
 * process, attempts to place the I/O into the sorted queues (if allowed),
 * or immediately executes the I/O.
 */
static void
dsl_scan_enqueue(dsl_pool_t *dp, const blkptr_t *bp, int zio_flags,
    const zbookmark_phys_t *zb)
{
	spa_t *spa = dp->dp_spa;

	ASSERT(!BP_IS_EMBEDDED(bp));

	/*
	 * Gang blocks are hard to issue sequentially, so we just issue them
	 * here immediately instead of queuing them.
	 */
	if (!dp->dp_scan->scn_is_sorted || BP_IS_GANG(bp)) {
		scan_exec_io(dp, bp, zio_flags, zb, NULL);
		return;
	}
	for (int i = 0; i < BP_GET_NDVAS(bp); i++) {
		dva_t dva;
		vdev_t *vdev;

		dva = bp->blk_dva[i];
		vdev = vdev_lookup_top(spa, DVA_GET_VDEV(&dva));
		ASSERT(vdev != NULL);

		mutex_enter(&vdev->vdev_scan_io_queue_lock);
		if (vdev->vdev_scan_io_queue == NULL)
			vdev->vdev_scan_io_queue = scan_io_queue_create(vdev);
		ASSERT(dp->dp_scan != NULL);
		scan_io_queue_insert(vdev->vdev_scan_io_queue, bp,
		    i, zio_flags, zb);
		mutex_exit(&vdev->vdev_scan_io_queue_lock);
	}
}

static int
dsl_scan_scrub_cb(dsl_pool_t *dp,
    const blkptr_t *bp, const zbookmark_phys_t *zb)
{
	dsl_scan_t *scn = dp->dp_scan;
	spa_t *spa = dp->dp_spa;
	uint64_t phys_birth = BP_PHYSICAL_BIRTH(bp);
	size_t psize = BP_GET_PSIZE(bp);
	boolean_t needs_io;
	int zio_flags = ZIO_FLAG_SCAN_THREAD | ZIO_FLAG_RAW | ZIO_FLAG_CANFAIL;
	int d;

	if (phys_birth <= scn->scn_phys.scn_min_txg ||
	    phys_birth >= scn->scn_phys.scn_max_txg) {
		count_block(scn, dp->dp_blkstats, bp);
		return (0);
	}

	/* Embedded BP's have phys_birth==0, so we reject them above. */
	ASSERT(!BP_IS_EMBEDDED(bp));

	ASSERT(DSL_SCAN_IS_SCRUB_RESILVER(scn));
	if (scn->scn_phys.scn_func == POOL_SCAN_SCRUB) {
		zio_flags |= ZIO_FLAG_SCRUB;
		needs_io = B_TRUE;
	} else {
		ASSERT3U(scn->scn_phys.scn_func, ==, POOL_SCAN_RESILVER);
		zio_flags |= ZIO_FLAG_RESILVER;
		needs_io = B_FALSE;
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
		dsl_scan_enqueue(dp, bp, zio_flags, zb);
	} else {
		count_block(scn, dp->dp_blkstats, bp);
	}

	/* do not relocate this block */
	return (0);
}

static void
dsl_scan_scrub_done(zio_t *zio)
{
	spa_t *spa = zio->io_spa;
	blkptr_t *bp = zio->io_bp;
	dsl_scan_io_queue_t *queue = zio->io_private;

	abd_free(zio->io_abd);

	if (queue == NULL) {
		mutex_enter(&spa->spa_scrub_lock);
		ASSERT3U(spa->spa_scrub_inflight, >=, BP_GET_PSIZE(bp));
		spa->spa_scrub_inflight -= BP_GET_PSIZE(bp);
		cv_broadcast(&spa->spa_scrub_io_cv);
		mutex_exit(&spa->spa_scrub_lock);
	} else {
		mutex_enter(&queue->q_vd->vdev_scan_io_queue_lock);
		ASSERT3U(queue->q_inflight_bytes, >=, BP_GET_PSIZE(bp));
		queue->q_inflight_bytes -= BP_GET_PSIZE(bp);
		cv_broadcast(&queue->q_zio_cv);
		mutex_exit(&queue->q_vd->vdev_scan_io_queue_lock);
	}

	if (zio->io_error && (zio->io_error != ECKSUM ||
	    !(zio->io_flags & ZIO_FLAG_SPECULATIVE))) {
		atomic_inc_64(&spa->spa_dsl_pool->dp_scan->scn_phys.scn_errors);
	}
}

/*
 * Given a scanning zio's information, executes the zio. The zio need
 * not necessarily be only sortable, this function simply executes the
 * zio, no matter what it is. The optional queue argument allows the
 * caller to specify that they want per top level vdev IO rate limiting
 * instead of the legacy global limiting.
 */
static void
scan_exec_io(dsl_pool_t *dp, const blkptr_t *bp, int zio_flags,
    const zbookmark_phys_t *zb, dsl_scan_io_queue_t *queue)
{
	spa_t *spa = dp->dp_spa;
	dsl_scan_t *scn = dp->dp_scan;
	size_t size = BP_GET_PSIZE(bp);
	abd_t *data = abd_alloc_for_io(size, B_FALSE);
	unsigned int scan_delay = 0;

	if (queue == NULL) {
		mutex_enter(&spa->spa_scrub_lock);
		while (spa->spa_scrub_inflight >= scn->scn_maxinflight_bytes)
			cv_wait(&spa->spa_scrub_io_cv, &spa->spa_scrub_lock);
		spa->spa_scrub_inflight += BP_GET_PSIZE(bp);
		mutex_exit(&spa->spa_scrub_lock);
	} else {
		kmutex_t *q_lock = &queue->q_vd->vdev_scan_io_queue_lock;

		mutex_enter(q_lock);
		while (queue->q_inflight_bytes >= queue->q_maxinflight_bytes)
			cv_wait(&queue->q_zio_cv, q_lock);
		queue->q_inflight_bytes += BP_GET_PSIZE(bp);
		mutex_exit(q_lock);
	}

	if (zio_flags & ZIO_FLAG_RESILVER)
		scan_delay = zfs_resilver_delay;
	else {
		ASSERT(zio_flags & ZIO_FLAG_SCRUB);
		scan_delay = zfs_scrub_delay;
	}

	if (scan_delay && (ddi_get_lbolt64() - spa->spa_last_io <= zfs_scan_idle))
		delay(MAX((int)scan_delay, 0));
	
	count_block(dp->dp_scan, dp->dp_blkstats, bp);
	zio_nowait(zio_read(dp->dp_scan->scn_zio_root, spa, bp, data, size,
	    dsl_scan_scrub_done, queue, ZIO_PRIORITY_SCRUB, zio_flags, zb));
}

/*
 * This is the primary extent sorting algorithm. We balance two parameters:
 * 1) how many bytes of I/O are in an extent
 * 2) how well the extent is filled with I/O (as a fraction of its total size)
 * Since we allow extents to have gaps between their constituent I/Os, it's
 * possible to have a fairly large extent that contains the same amount of
 * I/O bytes than a much smaller extent, which just packs the I/O more tightly.
 * The algorithm sorts based on a score calculated from the extent's size,
 * the relative fill volume (in %) and a "fill weight" parameter that controls
 * the split between whether we prefer larger extents or more well populated
 * extents:
 *
 * SCORE = FILL_IN_BYTES + (FILL_IN_PERCENT * FILL_IN_BYTES * FILL_WEIGHT)
 *
 * Example:
 * 1) assume extsz = 64 MiB
 * 2) assume fill = 32 MiB (extent is half full)
 * 3) assume fill_weight = 3
 * 4)	SCORE = 32M + (((32M * 100) / 64M) * 3 * 32M) / 100
 *	SCORE = 32M + (50 * 3 * 32M) / 100
 *	SCORE = 32M + (4800M / 100)
 *	SCORE = 32M + 48M
 *	         ^     ^
 *	         |     +--- final total relative fill-based score
 *	         +--------- final total fill-based score
 *	SCORE = 80M
 *
 * As can be seen, at fill_ratio=3, the algorithm is slightly biased towards
 * extents that are more completely filled (in a 3:2 ratio) vs just larger.
 * Note that as an optimization, we replace multiplication and division by
 * 100 with bitshifting by 7 (which effecitvely multiplies and divides by 128).
 */
static int
ext_size_compare(const void *x, const void *y)
{
	const range_seg_t *rsa = x, *rsb = y;
	uint64_t sa = rsa->rs_end - rsa->rs_start,
	    sb = rsb->rs_end - rsb->rs_start;
	uint64_t score_a, score_b;

	score_a = rsa->rs_fill + ((((rsa->rs_fill << 7) / sa) *
	    fill_weight * rsa->rs_fill) >> 7);
	score_b = rsb->rs_fill + ((((rsb->rs_fill << 7) / sb) *
	    fill_weight * rsb->rs_fill) >> 7);

	if (score_a > score_b)
		return (-1);
	if (score_a == score_b) {
		if (rsa->rs_start < rsb->rs_start)
			return (-1);
		if (rsa->rs_start == rsb->rs_start)
			return (0);
		return (1);
	}
	return (1);
}

/*
 * Comparator for the q_sios_by_addr tree. Sorting is simply performed
 * based on LBA-order (from lowest to highest).
 */
static int
io_addr_compare(const void *x, const void *y)
{
	const scan_io_t *a = x, *b = y;

	if (a->sio_offset < b->sio_offset)
		return (-1);
	if (a->sio_offset == b->sio_offset)
		return (0);
	return (1);
}

/* IO queues are created on demand when they are needed. */
static dsl_scan_io_queue_t *
scan_io_queue_create(vdev_t *vd)
{
	dsl_scan_t *scn = vd->vdev_spa->spa_dsl_pool->dp_scan;
	dsl_scan_io_queue_t *q = kmem_zalloc(sizeof (*q), KM_SLEEP);

	q->q_scn = scn;
	q->q_vd = vd;
	cv_init(&q->q_zio_cv, NULL, CV_DEFAULT, NULL);
	q->q_exts_by_addr = range_tree_create_impl(&rt_avl_ops,
	    &q->q_exts_by_size, ext_size_compare, zfs_scan_max_ext_gap);
	avl_create(&q->q_sios_by_addr, io_addr_compare,
	    sizeof (scan_io_t), offsetof(scan_io_t, sio_nodes.sio_addr_node));

	return (q);
}

/*
 * Destroys a scan queue and all segments and scan_io_t's contained in it.
 * No further execution of I/O occurs, anything pending in the queue is
 * simply freed without being executed.
 */
void
dsl_scan_io_queue_destroy(dsl_scan_io_queue_t *queue)
{
	dsl_scan_t *scn = queue->q_scn;
	scan_io_t *sio;
	void *cookie = NULL;
	int64_t bytes_dequeued = 0;

	ASSERT(MUTEX_HELD(&queue->q_vd->vdev_scan_io_queue_lock));

	while ((sio = avl_destroy_nodes(&queue->q_sios_by_addr, &cookie)) !=
	    NULL) {
		ASSERT(range_tree_contains(queue->q_exts_by_addr,
		    sio->sio_offset, sio->sio_asize));
		bytes_dequeued += sio->sio_asize;
		kmem_free(sio, sizeof (*sio));
	}

	atomic_add_64(&scn->scn_bytes_pending, -bytes_dequeued);
	range_tree_vacate(queue->q_exts_by_addr, NULL, queue);
	range_tree_destroy(queue->q_exts_by_addr);
	avl_destroy(&queue->q_sios_by_addr);
	cv_destroy(&queue->q_zio_cv);

	kmem_free(queue, sizeof (*queue));
}

/*
 * Properly transfers a dsl_scan_queue_t from `svd' to `tvd'. This is
 * called on behalf of vdev_top_transfer when creating or destroying
 * a mirror vdev due to zpool attach/detach.
 */
void
dsl_scan_io_queue_vdev_xfer(vdev_t *svd, vdev_t *tvd)
{
	mutex_enter(&svd->vdev_scan_io_queue_lock);
	mutex_enter(&tvd->vdev_scan_io_queue_lock);

	VERIFY3P(tvd->vdev_scan_io_queue, ==, NULL);
	tvd->vdev_scan_io_queue = svd->vdev_scan_io_queue;
	svd->vdev_scan_io_queue = NULL;
	if (tvd->vdev_scan_io_queue != NULL)
		tvd->vdev_scan_io_queue->q_vd = tvd;

	mutex_exit(&tvd->vdev_scan_io_queue_lock);
	mutex_exit(&svd->vdev_scan_io_queue_lock);
}

static void
scan_io_queues_destroy(dsl_scan_t *scn)
{
	vdev_t *rvd = scn->scn_dp->dp_spa->spa_root_vdev;

	for (uint64_t i = 0; i < rvd->vdev_children; i++) {
		vdev_t *tvd = rvd->vdev_child[i];

		mutex_enter(&tvd->vdev_scan_io_queue_lock);
		if (tvd->vdev_scan_io_queue != NULL)
			dsl_scan_io_queue_destroy(tvd->vdev_scan_io_queue);
		tvd->vdev_scan_io_queue = NULL;
		mutex_exit(&tvd->vdev_scan_io_queue_lock);
	}
}

static void
dsl_scan_freed_dva(spa_t *spa, const blkptr_t *bp, int dva_i)
{
	dsl_pool_t *dp = spa->spa_dsl_pool;
	dsl_scan_t *scn = dp->dp_scan;
	vdev_t *vdev;
	kmutex_t *q_lock;
	dsl_scan_io_queue_t *queue;
	scan_io_t srch, *sio;
	avl_index_t idx;
	uint64_t start, size;

	vdev = vdev_lookup_top(spa, DVA_GET_VDEV(&bp->blk_dva[dva_i]));
	ASSERT(vdev != NULL);
	q_lock = &vdev->vdev_scan_io_queue_lock;
	queue = vdev->vdev_scan_io_queue;

	mutex_enter(q_lock);
	if (queue == NULL) {
		mutex_exit(q_lock);
		return;
	}

	bp2sio(bp, &srch, dva_i);
	start = srch.sio_offset;
	size = srch.sio_asize;

	/*
	 * We can find the zio in two states:
	 * 1) Cold, just sitting in the queue of zio's to be issued at
	 *	some point in the future. In this case, all we do is
	 *	remove the zio from the q_sios_by_addr tree, decrement
	 *	its data volume from the containing range_seg_t and
	 *	resort the q_exts_by_size tree to reflect that the
	 *	range_seg_t has lost some of its 'fill'. We don't shorten
	 *	the range_seg_t - this is usually rare enough not to be
	 *	worth the extra hassle of trying keep track of precise
	 *	extent boundaries.
	 * 2) Hot, where the zio is currently in-flight in
	 *	dsl_scan_issue_ios. In this case, we can't simply
	 *	reach in and stop the in-flight zio's, so we instead
	 *	block the caller. Eventually, dsl_scan_issue_ios will
	 *	be done with issuing the zio's it gathered and will
	 *	signal us.
	 */
	sio = avl_find(&queue->q_sios_by_addr, &srch, &idx);
	if (sio != NULL) {
		int64_t asize = sio->sio_asize;
		blkptr_t tmpbp;

		/* Got it while it was cold in the queue */
		ASSERT3U(start, ==, sio->sio_offset);
		ASSERT3U(size, ==, asize);
		avl_remove(&queue->q_sios_by_addr, sio);

		ASSERT(range_tree_contains(queue->q_exts_by_addr, start, size));
		range_tree_remove_fill(queue->q_exts_by_addr, start, size);

		/*
		 * We only update scn_bytes_pending in the cold path,
		 * otherwise it will already have been accounted for as
		 * part of the zio's execution.
		 */
		atomic_add_64(&scn->scn_bytes_pending, -asize);

		/* count the block as though we issued it */
		sio2bp(sio, &tmpbp, dva_i);
		count_block(scn, dp->dp_blkstats, &tmpbp);

		kmem_free(sio, sizeof (*sio));
	}
	mutex_exit(q_lock);
}

/*
 * Callback invoked when a zio_free() zio is executing. This needs to be
 * intercepted to prevent the zio from deallocating a particular portion
 * of disk space and it then getting reallocated and written to, while we
 * still have it queued up for processing.
 */
void
dsl_scan_freed(spa_t *spa, const blkptr_t *bp)
{
	dsl_pool_t *dp = spa->spa_dsl_pool;
	dsl_scan_t *scn = dp->dp_scan;

	ASSERT(!BP_IS_EMBEDDED(bp));
	ASSERT(scn != NULL);
	if (!dsl_scan_is_running(scn))
		return;

	for (int i = 0; i < BP_GET_NDVAS(bp); i++)
		dsl_scan_freed_dva(spa, bp, i);
}
