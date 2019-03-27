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
 * Copyright (c) 2012 Pawel Jakub Dawidek <pawel@dawidek.net>.
 * All rights reserved.
 */

#include <sys/zfs_context.h>
#include <sys/spa_impl.h>
#include <sys/vdev_impl.h>
#include <sys/trim_map.h>
#include <sys/time.h>

/*
 * Calculate the zio end, upgrading based on ashift which would be
 * done by zio_vdev_io_start.
 *
 * This makes free range consolidation much more effective
 * than it would otherwise be as well as ensuring that entire
 * blocks are invalidated by writes.
 */
#define	TRIM_ZIO_END(vd, offset, size)	(offset +		\
 	P2ROUNDUP(size, 1ULL << vd->vdev_top->vdev_ashift))

/* Maximal segment size for ATA TRIM. */
#define TRIM_MAP_SIZE_FACTOR	(512 << 16)

#define TRIM_MAP_SEGS(size)	(1 + (size) / TRIM_MAP_SIZE_FACTOR)

#define TRIM_MAP_ADD(tm, ts)	do {				\
	list_insert_tail(&(tm)->tm_head, (ts));			\
	(tm)->tm_pending += TRIM_MAP_SEGS((ts)->ts_end - (ts)->ts_start); \
} while (0)

#define TRIM_MAP_REM(tm, ts)	do {				\
	list_remove(&(tm)->tm_head, (ts));			\
	(tm)->tm_pending -= TRIM_MAP_SEGS((ts)->ts_end - (ts)->ts_start); \
} while (0)

typedef struct trim_map {
	list_t		tm_head;		/* List of segments sorted by txg. */
	avl_tree_t	tm_queued_frees;	/* AVL tree of segments waiting for TRIM. */
	avl_tree_t	tm_inflight_frees;	/* AVL tree of in-flight TRIMs. */
	avl_tree_t	tm_inflight_writes;	/* AVL tree of in-flight writes. */
	list_t		tm_pending_writes;	/* Writes blocked on in-flight frees. */
	kmutex_t	tm_lock;
	uint64_t	tm_pending;		/* Count of pending TRIMs. */
} trim_map_t;

typedef struct trim_seg {
	avl_node_t	ts_node;	/* AVL node. */
	list_node_t	ts_next;	/* List element. */
	uint64_t	ts_start;	/* Starting offset of this segment. */
	uint64_t	ts_end;		/* Ending offset (non-inclusive). */
	uint64_t	ts_txg;		/* Segment creation txg. */
	hrtime_t	ts_time;	/* Segment creation time. */
} trim_seg_t;

extern boolean_t zfs_trim_enabled;

static u_int trim_txg_delay = 32;	/* Keep deleted data up to 32 TXG */
static u_int trim_timeout = 30;		/* Keep deleted data up to 30s */
static u_int trim_max_interval = 1;	/* 1s delays between TRIMs */
static u_int trim_vdev_max_pending = 10000; /* Keep up to 10K segments */

SYSCTL_DECL(_vfs_zfs);
SYSCTL_NODE(_vfs_zfs, OID_AUTO, trim, CTLFLAG_RD, 0, "ZFS TRIM");

SYSCTL_UINT(_vfs_zfs_trim, OID_AUTO, txg_delay, CTLFLAG_RWTUN, &trim_txg_delay,
    0, "Delay TRIMs by up to this many TXGs");
SYSCTL_UINT(_vfs_zfs_trim, OID_AUTO, timeout, CTLFLAG_RWTUN, &trim_timeout, 0,
    "Delay TRIMs by up to this many seconds");
SYSCTL_UINT(_vfs_zfs_trim, OID_AUTO, max_interval, CTLFLAG_RWTUN,
    &trim_max_interval, 0,
    "Maximum interval between TRIM queue processing (seconds)");

SYSCTL_DECL(_vfs_zfs_vdev);
SYSCTL_UINT(_vfs_zfs_vdev, OID_AUTO, trim_max_pending, CTLFLAG_RWTUN,
    &trim_vdev_max_pending, 0,
    "Maximum pending TRIM segments for a vdev");

static void trim_map_vdev_commit_done(spa_t *spa, vdev_t *vd);

static int
trim_map_seg_compare(const void *x1, const void *x2)
{
	const trim_seg_t *s1 = x1;
	const trim_seg_t *s2 = x2;

	if (s1->ts_start < s2->ts_start) {
		if (s1->ts_end > s2->ts_start)
			return (0);
		return (-1);
	}
	if (s1->ts_start > s2->ts_start) {
		if (s1->ts_start < s2->ts_end)
			return (0);
		return (1);
	}
	return (0);
}

static int
trim_map_zio_compare(const void *x1, const void *x2)
{
	const zio_t *z1 = x1;
	const zio_t *z2 = x2;

	if (z1->io_offset < z2->io_offset) {
		if (z1->io_offset + z1->io_size > z2->io_offset)
			return (0);
		return (-1);
	}
	if (z1->io_offset > z2->io_offset) {
		if (z1->io_offset < z2->io_offset + z2->io_size)
			return (0);
		return (1);
	}
	return (0);
}

void
trim_map_create(vdev_t *vd)
{
	trim_map_t *tm;

	ASSERT(zfs_trim_enabled && !vd->vdev_notrim &&
		vd->vdev_ops->vdev_op_leaf);

	tm = kmem_zalloc(sizeof (*tm), KM_SLEEP);
	mutex_init(&tm->tm_lock, NULL, MUTEX_DEFAULT, NULL);
	list_create(&tm->tm_head, sizeof (trim_seg_t),
	    offsetof(trim_seg_t, ts_next));
	list_create(&tm->tm_pending_writes, sizeof (zio_t),
	    offsetof(zio_t, io_trim_link));
	avl_create(&tm->tm_queued_frees, trim_map_seg_compare,
	    sizeof (trim_seg_t), offsetof(trim_seg_t, ts_node));
	avl_create(&tm->tm_inflight_frees, trim_map_seg_compare,
	    sizeof (trim_seg_t), offsetof(trim_seg_t, ts_node));
	avl_create(&tm->tm_inflight_writes, trim_map_zio_compare,
	    sizeof (zio_t), offsetof(zio_t, io_trim_node));
	vd->vdev_trimmap = tm;
}

void
trim_map_destroy(vdev_t *vd)
{
	trim_map_t *tm;
	trim_seg_t *ts;

	ASSERT(vd->vdev_ops->vdev_op_leaf);

	if (!zfs_trim_enabled)
		return;

	tm = vd->vdev_trimmap;
	if (tm == NULL)
		return;

	/*
	 * We may have been called before trim_map_vdev_commit_done()
	 * had a chance to run, so do it now to prune the remaining
	 * inflight frees.
	 */
	trim_map_vdev_commit_done(vd->vdev_spa, vd);

	mutex_enter(&tm->tm_lock);
	while ((ts = list_head(&tm->tm_head)) != NULL) {
		avl_remove(&tm->tm_queued_frees, ts);
		TRIM_MAP_REM(tm, ts);
		kmem_free(ts, sizeof (*ts));
	}
	mutex_exit(&tm->tm_lock);

	avl_destroy(&tm->tm_queued_frees);
	avl_destroy(&tm->tm_inflight_frees);
	avl_destroy(&tm->tm_inflight_writes);
	list_destroy(&tm->tm_pending_writes);
	list_destroy(&tm->tm_head);
	mutex_destroy(&tm->tm_lock);
	kmem_free(tm, sizeof (*tm));
	vd->vdev_trimmap = NULL;
}

static void
trim_map_segment_add(trim_map_t *tm, uint64_t start, uint64_t end, uint64_t txg)
{
	avl_index_t where;
	trim_seg_t tsearch, *ts_before, *ts_after, *ts;
	boolean_t merge_before, merge_after;
	hrtime_t time;

	ASSERT(MUTEX_HELD(&tm->tm_lock));
	VERIFY(start < end);

	time = gethrtime();
	tsearch.ts_start = start;
	tsearch.ts_end = end;

	ts = avl_find(&tm->tm_queued_frees, &tsearch, &where);
	if (ts != NULL) {
		if (start < ts->ts_start)
			trim_map_segment_add(tm, start, ts->ts_start, txg);
		if (end > ts->ts_end)
			trim_map_segment_add(tm, ts->ts_end, end, txg);
		return;
	}

	ts_before = avl_nearest(&tm->tm_queued_frees, where, AVL_BEFORE);
	ts_after = avl_nearest(&tm->tm_queued_frees, where, AVL_AFTER);

	merge_before = (ts_before != NULL && ts_before->ts_end == start);
	merge_after = (ts_after != NULL && ts_after->ts_start == end);

	if (merge_before && merge_after) {
		avl_remove(&tm->tm_queued_frees, ts_before);
		TRIM_MAP_REM(tm, ts_before);
		TRIM_MAP_REM(tm, ts_after);
		ts_after->ts_start = ts_before->ts_start;
		ts_after->ts_txg = txg;
		ts_after->ts_time = time;
		TRIM_MAP_ADD(tm, ts_after);
		kmem_free(ts_before, sizeof (*ts_before));
	} else if (merge_before) {
		TRIM_MAP_REM(tm, ts_before);
		ts_before->ts_end = end;
		ts_before->ts_txg = txg;
		ts_before->ts_time = time;
		TRIM_MAP_ADD(tm, ts_before);
	} else if (merge_after) {
		TRIM_MAP_REM(tm, ts_after);
		ts_after->ts_start = start;
		ts_after->ts_txg = txg;
		ts_after->ts_time = time;
		TRIM_MAP_ADD(tm, ts_after);
	} else {
		ts = kmem_alloc(sizeof (*ts), KM_SLEEP);
		ts->ts_start = start;
		ts->ts_end = end;
		ts->ts_txg = txg;
		ts->ts_time = time;
		avl_insert(&tm->tm_queued_frees, ts, where);
		TRIM_MAP_ADD(tm, ts);
	}
}

static void
trim_map_segment_remove(trim_map_t *tm, trim_seg_t *ts, uint64_t start,
    uint64_t end)
{
	trim_seg_t *nts;
	boolean_t left_over, right_over;

	ASSERT(MUTEX_HELD(&tm->tm_lock));

	left_over = (ts->ts_start < start);
	right_over = (ts->ts_end > end);

	TRIM_MAP_REM(tm, ts);
	if (left_over && right_over) {
		nts = kmem_alloc(sizeof (*nts), KM_SLEEP);
		nts->ts_start = end;
		nts->ts_end = ts->ts_end;
		nts->ts_txg = ts->ts_txg;
		nts->ts_time = ts->ts_time;
		ts->ts_end = start;
		avl_insert_here(&tm->tm_queued_frees, nts, ts, AVL_AFTER);
		TRIM_MAP_ADD(tm, ts);
		TRIM_MAP_ADD(tm, nts);
	} else if (left_over) {
		ts->ts_end = start;
		TRIM_MAP_ADD(tm, ts);
	} else if (right_over) {
		ts->ts_start = end;
		TRIM_MAP_ADD(tm, ts);
	} else {
		avl_remove(&tm->tm_queued_frees, ts);
		kmem_free(ts, sizeof (*ts));
	}
}

static void
trim_map_free_locked(trim_map_t *tm, uint64_t start, uint64_t end, uint64_t txg)
{
	zio_t zsearch, *zs;

	ASSERT(MUTEX_HELD(&tm->tm_lock));

	zsearch.io_offset = start;
	zsearch.io_size = end - start;

	zs = avl_find(&tm->tm_inflight_writes, &zsearch, NULL);
	if (zs == NULL) {
		trim_map_segment_add(tm, start, end, txg);
		return;
	}
	if (start < zs->io_offset)
		trim_map_free_locked(tm, start, zs->io_offset, txg);
	if (zs->io_offset + zs->io_size < end)
		trim_map_free_locked(tm, zs->io_offset + zs->io_size, end, txg);
}

void
trim_map_free(vdev_t *vd, uint64_t offset, uint64_t size, uint64_t txg)
{
	trim_map_t *tm = vd->vdev_trimmap;

	if (!zfs_trim_enabled || vd->vdev_notrim || tm == NULL)
		return;

	mutex_enter(&tm->tm_lock);
	trim_map_free_locked(tm, offset, TRIM_ZIO_END(vd, offset, size), txg);
	mutex_exit(&tm->tm_lock);
}

boolean_t
trim_map_write_start(zio_t *zio)
{
	vdev_t *vd = zio->io_vd;
	trim_map_t *tm = vd->vdev_trimmap;
	trim_seg_t tsearch, *ts;
	boolean_t left_over, right_over;
	uint64_t start, end;

	if (!zfs_trim_enabled || vd->vdev_notrim || tm == NULL)
		return (B_TRUE);

	start = zio->io_offset;
	end = TRIM_ZIO_END(zio->io_vd, start, zio->io_size);
	tsearch.ts_start = start;
	tsearch.ts_end = end;

	mutex_enter(&tm->tm_lock);

	/*
	 * Checking for colliding in-flight frees.
	 */
	ts = avl_find(&tm->tm_inflight_frees, &tsearch, NULL);
	if (ts != NULL) {
		list_insert_tail(&tm->tm_pending_writes, zio);
		mutex_exit(&tm->tm_lock);
		return (B_FALSE);
	}

	/*
	 * Loop until all overlapping segments are removed.
	 */
	while ((ts = avl_find(&tm->tm_queued_frees, &tsearch, NULL)) != NULL) {
		trim_map_segment_remove(tm, ts, start, end);
	}

	avl_add(&tm->tm_inflight_writes, zio);

	mutex_exit(&tm->tm_lock);

	return (B_TRUE);
}

void
trim_map_write_done(zio_t *zio)
{
	vdev_t *vd = zio->io_vd;
	trim_map_t *tm = vd->vdev_trimmap;

	/*
	 * Don't check for vdev_notrim, since the write could have
	 * started before vdev_notrim was set.
	 */
	if (!zfs_trim_enabled || tm == NULL)
		return;

	mutex_enter(&tm->tm_lock);
	/*
	 * Don't fail if the write isn't in the tree, since the write
	 * could have started after vdev_notrim was set.
	 */
	if (zio->io_trim_node.avl_child[0] ||
	    zio->io_trim_node.avl_child[1] ||
	    AVL_XPARENT(&zio->io_trim_node) ||
	    tm->tm_inflight_writes.avl_root == &zio->io_trim_node)
		avl_remove(&tm->tm_inflight_writes, zio);
	mutex_exit(&tm->tm_lock);
}

/*
 * Return the oldest segment (the one with the lowest txg / time) or NULL if:
 * 1. The list is empty
 * 2. The first element's txg is greater than txgsafe
 * 3. The first element's txg is not greater than the txg argument and the
 *    the first element's time is not greater than time argument
 */
static trim_seg_t *
trim_map_first(trim_map_t *tm, uint64_t txg, uint64_t txgsafe, hrtime_t time,
    boolean_t force)
{
	trim_seg_t *ts;

	ASSERT(MUTEX_HELD(&tm->tm_lock));
	VERIFY(txgsafe >= txg);

	ts = list_head(&tm->tm_head);
	if (ts != NULL && ts->ts_txg <= txgsafe &&
	    (ts->ts_txg <= txg || ts->ts_time <= time || force))
		return (ts);
	return (NULL);
}

static void
trim_map_vdev_commit(spa_t *spa, zio_t *zio, vdev_t *vd)
{
	trim_map_t *tm = vd->vdev_trimmap;
	trim_seg_t *ts;
	uint64_t size, offset, txgtarget, txgsafe;
	int64_t hard, soft;
	hrtime_t timelimit;

	ASSERT(vd->vdev_ops->vdev_op_leaf);

	if (tm == NULL)
		return;

	timelimit = gethrtime() - (hrtime_t)trim_timeout * NANOSEC;
	if (vd->vdev_isl2cache) {
		txgsafe = UINT64_MAX;
		txgtarget = UINT64_MAX;
	} else {
		txgsafe = MIN(spa_last_synced_txg(spa), spa_freeze_txg(spa));
		if (txgsafe > trim_txg_delay)
			txgtarget = txgsafe - trim_txg_delay;
		else
			txgtarget = 0;
	}

	mutex_enter(&tm->tm_lock);
	hard = 0;
	if (tm->tm_pending > trim_vdev_max_pending)
		hard = (tm->tm_pending - trim_vdev_max_pending) / 4;
	soft = P2ROUNDUP(hard + tm->tm_pending / trim_timeout + 1, 64);
	/* Loop until we have sent all outstanding free's */
	while (soft > 0 &&
	    (ts = trim_map_first(tm, txgtarget, txgsafe, timelimit, hard > 0))
	    != NULL) {
		TRIM_MAP_REM(tm, ts);
		avl_remove(&tm->tm_queued_frees, ts);
		avl_add(&tm->tm_inflight_frees, ts);
		size = ts->ts_end - ts->ts_start;
		offset = ts->ts_start;
		/*
		 * We drop the lock while we call zio_nowait as the IO
		 * scheduler can result in a different IO being run e.g.
		 * a write which would result in a recursive lock.
		 */
		mutex_exit(&tm->tm_lock);

		zio_nowait(zio_trim(zio, spa, vd, offset, size));

		soft -= TRIM_MAP_SEGS(size);
		hard -= TRIM_MAP_SEGS(size);
		mutex_enter(&tm->tm_lock);
	}
	mutex_exit(&tm->tm_lock);
}

static void
trim_map_vdev_commit_done(spa_t *spa, vdev_t *vd)
{
	trim_map_t *tm = vd->vdev_trimmap;
	trim_seg_t *ts;
	list_t pending_writes;
	zio_t *zio;
	uint64_t start, size;
	void *cookie;

	ASSERT(vd->vdev_ops->vdev_op_leaf);

	if (tm == NULL)
		return;

	mutex_enter(&tm->tm_lock);
	if (!avl_is_empty(&tm->tm_inflight_frees)) {
		cookie = NULL;
		while ((ts = avl_destroy_nodes(&tm->tm_inflight_frees,
		    &cookie)) != NULL) {
			kmem_free(ts, sizeof (*ts));
		}
	}
	list_create(&pending_writes, sizeof (zio_t), offsetof(zio_t,
	    io_trim_link));
	list_move_tail(&pending_writes, &tm->tm_pending_writes);
	mutex_exit(&tm->tm_lock);

	while ((zio = list_remove_head(&pending_writes)) != NULL) {
		zio_vdev_io_reissue(zio);
		zio_execute(zio);
	}
	list_destroy(&pending_writes);
}

static void
trim_map_commit(spa_t *spa, zio_t *zio, vdev_t *vd)
{
	int c;

	if (vd == NULL)
		return;

	if (vd->vdev_ops->vdev_op_leaf) {
		trim_map_vdev_commit(spa, zio, vd);
	} else {
		for (c = 0; c < vd->vdev_children; c++)
			trim_map_commit(spa, zio, vd->vdev_child[c]);
	}
}

static void
trim_map_commit_done(spa_t *spa, vdev_t *vd)
{
	int c;

	if (vd == NULL)
		return;

	if (vd->vdev_ops->vdev_op_leaf) {
		trim_map_vdev_commit_done(spa, vd);
	} else {
		for (c = 0; c < vd->vdev_children; c++)
			trim_map_commit_done(spa, vd->vdev_child[c]);
	}
}

static void
trim_thread(void *arg)
{
	spa_t *spa = arg;
	zio_t *zio;

#ifdef _KERNEL
	(void) snprintf(curthread->td_name, sizeof(curthread->td_name),
	    "trim %s", spa_name(spa));
#endif

	for (;;) {
		mutex_enter(&spa->spa_trim_lock);
		if (spa->spa_trim_thread == NULL) {
			spa->spa_trim_thread = curthread;
			cv_signal(&spa->spa_trim_cv);
			mutex_exit(&spa->spa_trim_lock);
			thread_exit();
		}

		(void) cv_timedwait(&spa->spa_trim_cv, &spa->spa_trim_lock,
		    hz * trim_max_interval);
		mutex_exit(&spa->spa_trim_lock);

		zio = zio_root(spa, NULL, NULL, ZIO_FLAG_CANFAIL);

		spa_config_enter(spa, SCL_STATE, FTAG, RW_READER);
		trim_map_commit(spa, zio, spa->spa_root_vdev);
		(void) zio_wait(zio);
		trim_map_commit_done(spa, spa->spa_root_vdev);
		spa_config_exit(spa, SCL_STATE, FTAG);
	}
}

void
trim_thread_create(spa_t *spa)
{

	if (!zfs_trim_enabled)
		return;

	mutex_init(&spa->spa_trim_lock, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&spa->spa_trim_cv, NULL, CV_DEFAULT, NULL);
	mutex_enter(&spa->spa_trim_lock);
	spa->spa_trim_thread = thread_create(NULL, 0, trim_thread, spa, 0, &p0,
	    TS_RUN, minclsyspri);
	mutex_exit(&spa->spa_trim_lock);
}

void
trim_thread_destroy(spa_t *spa)
{

	if (!zfs_trim_enabled)
		return;
	if (spa->spa_trim_thread == NULL)
		return;

	mutex_enter(&spa->spa_trim_lock);
	/* Setting spa_trim_thread to NULL tells the thread to stop. */
	spa->spa_trim_thread = NULL;
	cv_signal(&spa->spa_trim_cv);
	/* The thread will set it back to != NULL on exit. */
	while (spa->spa_trim_thread == NULL)
		cv_wait(&spa->spa_trim_cv, &spa->spa_trim_lock);
	spa->spa_trim_thread = NULL;
	mutex_exit(&spa->spa_trim_lock);

	cv_destroy(&spa->spa_trim_cv);
	mutex_destroy(&spa->spa_trim_lock);
}

void
trim_thread_wakeup(spa_t *spa)
{

	if (!zfs_trim_enabled)
		return;
	if (spa->spa_trim_thread == NULL)
		return;

	mutex_enter(&spa->spa_trim_lock);
	cv_signal(&spa->spa_trim_cv);
	mutex_exit(&spa->spa_trim_lock);
}
