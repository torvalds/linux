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
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * Copyright (c) 2012, 2018 by Delphix. All rights reserved.
 * Copyright (c) 2014 Integros [integros.com]
 */

#include <sys/zfs_context.h>
#include <sys/vdev_impl.h>
#include <sys/spa_impl.h>
#include <sys/zio.h>
#include <sys/avl.h>
#include <sys/dsl_pool.h>
#include <sys/metaslab_impl.h>
#include <sys/abd.h>

/*
 * ZFS I/O Scheduler
 * ---------------
 *
 * ZFS issues I/O operations to leaf vdevs to satisfy and complete zios.  The
 * I/O scheduler determines when and in what order those operations are
 * issued.  The I/O scheduler divides operations into six I/O classes
 * prioritized in the following order: sync read, sync write, async read,
 * async write, scrub/resilver and trim.  Each queue defines the minimum and
 * maximum number of concurrent operations that may be issued to the device.
 * In addition, the device has an aggregate maximum. Note that the sum of the
 * per-queue minimums must not exceed the aggregate maximum, and if the
 * aggregate maximum is equal to or greater than the sum of the per-queue
 * maximums, the per-queue minimum has no effect.
 *
 * For many physical devices, throughput increases with the number of
 * concurrent operations, but latency typically suffers. Further, physical
 * devices typically have a limit at which more concurrent operations have no
 * effect on throughput or can actually cause it to decrease.
 *
 * The scheduler selects the next operation to issue by first looking for an
 * I/O class whose minimum has not been satisfied. Once all are satisfied and
 * the aggregate maximum has not been hit, the scheduler looks for classes
 * whose maximum has not been satisfied. Iteration through the I/O classes is
 * done in the order specified above. No further operations are issued if the
 * aggregate maximum number of concurrent operations has been hit or if there
 * are no operations queued for an I/O class that has not hit its maximum.
 * Every time an I/O is queued or an operation completes, the I/O scheduler
 * looks for new operations to issue.
 *
 * All I/O classes have a fixed maximum number of outstanding operations
 * except for the async write class. Asynchronous writes represent the data
 * that is committed to stable storage during the syncing stage for
 * transaction groups (see txg.c). Transaction groups enter the syncing state
 * periodically so the number of queued async writes will quickly burst up and
 * then bleed down to zero. Rather than servicing them as quickly as possible,
 * the I/O scheduler changes the maximum number of active async write I/Os
 * according to the amount of dirty data in the pool (see dsl_pool.c). Since
 * both throughput and latency typically increase with the number of
 * concurrent operations issued to physical devices, reducing the burstiness
 * in the number of concurrent operations also stabilizes the response time of
 * operations from other -- and in particular synchronous -- queues. In broad
 * strokes, the I/O scheduler will issue more concurrent operations from the
 * async write queue as there's more dirty data in the pool.
 *
 * Async Writes
 *
 * The number of concurrent operations issued for the async write I/O class
 * follows a piece-wise linear function defined by a few adjustable points.
 *
 *        |                   o---------| <-- zfs_vdev_async_write_max_active
 *   ^    |                  /^         |
 *   |    |                 / |         |
 * active |                /  |         |
 *  I/O   |               /   |         |
 * count  |              /    |         |
 *        |             /     |         |
 *        |------------o      |         | <-- zfs_vdev_async_write_min_active
 *       0|____________^______|_________|
 *        0%           |      |       100% of zfs_dirty_data_max
 *                     |      |
 *                     |      `-- zfs_vdev_async_write_active_max_dirty_percent
 *                     `--------- zfs_vdev_async_write_active_min_dirty_percent
 *
 * Until the amount of dirty data exceeds a minimum percentage of the dirty
 * data allowed in the pool, the I/O scheduler will limit the number of
 * concurrent operations to the minimum. As that threshold is crossed, the
 * number of concurrent operations issued increases linearly to the maximum at
 * the specified maximum percentage of the dirty data allowed in the pool.
 *
 * Ideally, the amount of dirty data on a busy pool will stay in the sloped
 * part of the function between zfs_vdev_async_write_active_min_dirty_percent
 * and zfs_vdev_async_write_active_max_dirty_percent. If it exceeds the
 * maximum percentage, this indicates that the rate of incoming data is
 * greater than the rate that the backend storage can handle. In this case, we
 * must further throttle incoming writes (see dmu_tx_delay() for details).
 */

/*
 * The maximum number of I/Os active to each device.  Ideally, this will be >=
 * the sum of each queue's max_active.  It must be at least the sum of each
 * queue's min_active.
 */
uint32_t zfs_vdev_max_active = 1000;

/*
 * Per-queue limits on the number of I/Os active to each device.  If the
 * sum of the queue's max_active is < zfs_vdev_max_active, then the
 * min_active comes into play.  We will send min_active from each queue,
 * and then select from queues in the order defined by zio_priority_t.
 *
 * In general, smaller max_active's will lead to lower latency of synchronous
 * operations.  Larger max_active's may lead to higher overall throughput,
 * depending on underlying storage.
 *
 * The ratio of the queues' max_actives determines the balance of performance
 * between reads, writes, and scrubs.  E.g., increasing
 * zfs_vdev_scrub_max_active will cause the scrub or resilver to complete
 * more quickly, but reads and writes to have higher latency and lower
 * throughput.
 */
uint32_t zfs_vdev_sync_read_min_active = 10;
uint32_t zfs_vdev_sync_read_max_active = 10;
uint32_t zfs_vdev_sync_write_min_active = 10;
uint32_t zfs_vdev_sync_write_max_active = 10;
uint32_t zfs_vdev_async_read_min_active = 1;
uint32_t zfs_vdev_async_read_max_active = 3;
uint32_t zfs_vdev_async_write_min_active = 1;
uint32_t zfs_vdev_async_write_max_active = 10;
uint32_t zfs_vdev_scrub_min_active = 1;
uint32_t zfs_vdev_scrub_max_active = 2;
uint32_t zfs_vdev_trim_min_active = 1;
/*
 * TRIM max active is large in comparison to the other values due to the fact
 * that TRIM IOs are coalesced at the device layer. This value is set such
 * that a typical SSD can process the queued IOs in a single request.
 */
uint32_t zfs_vdev_trim_max_active = 64;
uint32_t zfs_vdev_removal_min_active = 1;
uint32_t zfs_vdev_removal_max_active = 2;
uint32_t zfs_vdev_initializing_min_active = 1;
uint32_t zfs_vdev_initializing_max_active = 1;


/*
 * When the pool has less than zfs_vdev_async_write_active_min_dirty_percent
 * dirty data, use zfs_vdev_async_write_min_active.  When it has more than
 * zfs_vdev_async_write_active_max_dirty_percent, use
 * zfs_vdev_async_write_max_active. The value is linearly interpolated
 * between min and max.
 */
int zfs_vdev_async_write_active_min_dirty_percent = 30;
int zfs_vdev_async_write_active_max_dirty_percent = 60;

/*
 * To reduce IOPs, we aggregate small adjacent I/Os into one large I/O.
 * For read I/Os, we also aggregate across small adjacency gaps; for writes
 * we include spans of optional I/Os to aid aggregation at the disk even when
 * they aren't able to help us aggregate at this level.
 */
int zfs_vdev_aggregation_limit = 1 << 20;
int zfs_vdev_aggregation_limit_non_rotating = SPA_OLD_MAXBLOCKSIZE;
int zfs_vdev_read_gap_limit = 32 << 10;
int zfs_vdev_write_gap_limit = 4 << 10;

/*
 * Define the queue depth percentage for each top-level. This percentage is
 * used in conjunction with zfs_vdev_async_max_active to determine how many
 * allocations a specific top-level vdev should handle. Once the queue depth
 * reaches zfs_vdev_queue_depth_pct * zfs_vdev_async_write_max_active / 100
 * then allocator will stop allocating blocks on that top-level device.
 * The default kernel setting is 1000% which will yield 100 allocations per
 * device. For userland testing, the default setting is 300% which equates
 * to 30 allocations per device.
 */
#ifdef _KERNEL
int zfs_vdev_queue_depth_pct = 1000;
#else
int zfs_vdev_queue_depth_pct = 300;
#endif

/*
 * When performing allocations for a given metaslab, we want to make sure that
 * there are enough IOs to aggregate together to improve throughput. We want to
 * ensure that there are at least 128k worth of IOs that can be aggregated, and
 * we assume that the average allocation size is 4k, so we need the queue depth
 * to be 32 per allocator to get good aggregation of sequential writes.
 */
int zfs_vdev_def_queue_depth = 32;

#ifdef __FreeBSD__
#ifdef _KERNEL
SYSCTL_DECL(_vfs_zfs_vdev);

static int sysctl_zfs_async_write_active_min_dirty_percent(SYSCTL_HANDLER_ARGS);
SYSCTL_PROC(_vfs_zfs_vdev, OID_AUTO, async_write_active_min_dirty_percent,
    CTLTYPE_UINT | CTLFLAG_MPSAFE | CTLFLAG_RWTUN, 0, sizeof(int),
    sysctl_zfs_async_write_active_min_dirty_percent, "I",
    "Percentage of async write dirty data below which "
    "async_write_min_active is used.");

static int sysctl_zfs_async_write_active_max_dirty_percent(SYSCTL_HANDLER_ARGS);
SYSCTL_PROC(_vfs_zfs_vdev, OID_AUTO, async_write_active_max_dirty_percent,
    CTLTYPE_UINT | CTLFLAG_MPSAFE | CTLFLAG_RWTUN, 0, sizeof(int),
    sysctl_zfs_async_write_active_max_dirty_percent, "I",
    "Percentage of async write dirty data above which "
    "async_write_max_active is used.");

SYSCTL_UINT(_vfs_zfs_vdev, OID_AUTO, max_active, CTLFLAG_RWTUN,
    &zfs_vdev_max_active, 0,
    "The maximum number of I/Os of all types active for each device.");

#define ZFS_VDEV_QUEUE_KNOB_MIN(name)					\
SYSCTL_UINT(_vfs_zfs_vdev, OID_AUTO, name ## _min_active, CTLFLAG_RWTUN,\
    &zfs_vdev_ ## name ## _min_active, 0,				\
    "Initial number of I/O requests of type " #name			\
    " active for each device");

#define ZFS_VDEV_QUEUE_KNOB_MAX(name)					\
SYSCTL_UINT(_vfs_zfs_vdev, OID_AUTO, name ## _max_active, CTLFLAG_RWTUN,\
    &zfs_vdev_ ## name ## _max_active, 0,				\
    "Maximum number of I/O requests of type " #name			\
    " active for each device");

ZFS_VDEV_QUEUE_KNOB_MIN(sync_read);
ZFS_VDEV_QUEUE_KNOB_MAX(sync_read);
ZFS_VDEV_QUEUE_KNOB_MIN(sync_write);
ZFS_VDEV_QUEUE_KNOB_MAX(sync_write);
ZFS_VDEV_QUEUE_KNOB_MIN(async_read);
ZFS_VDEV_QUEUE_KNOB_MAX(async_read);
ZFS_VDEV_QUEUE_KNOB_MIN(async_write);
ZFS_VDEV_QUEUE_KNOB_MAX(async_write);
ZFS_VDEV_QUEUE_KNOB_MIN(scrub);
ZFS_VDEV_QUEUE_KNOB_MAX(scrub);
ZFS_VDEV_QUEUE_KNOB_MIN(trim);
ZFS_VDEV_QUEUE_KNOB_MAX(trim);
ZFS_VDEV_QUEUE_KNOB_MIN(removal);
ZFS_VDEV_QUEUE_KNOB_MAX(removal);
ZFS_VDEV_QUEUE_KNOB_MIN(initializing);
ZFS_VDEV_QUEUE_KNOB_MAX(initializing);

#undef ZFS_VDEV_QUEUE_KNOB

SYSCTL_INT(_vfs_zfs_vdev, OID_AUTO, aggregation_limit, CTLFLAG_RWTUN,
    &zfs_vdev_aggregation_limit, 0,
    "I/O requests are aggregated up to this size");
SYSCTL_INT(_vfs_zfs_vdev, OID_AUTO, aggregation_limit_non_rotating, CTLFLAG_RWTUN,
    &zfs_vdev_aggregation_limit_non_rotating, 0,
    "I/O requests are aggregated up to this size for non-rotating media");
SYSCTL_INT(_vfs_zfs_vdev, OID_AUTO, read_gap_limit, CTLFLAG_RWTUN,
    &zfs_vdev_read_gap_limit, 0,
    "Acceptable gap between two reads being aggregated");
SYSCTL_INT(_vfs_zfs_vdev, OID_AUTO, write_gap_limit, CTLFLAG_RWTUN,
    &zfs_vdev_write_gap_limit, 0,
    "Acceptable gap between two writes being aggregated");
SYSCTL_INT(_vfs_zfs_vdev, OID_AUTO, queue_depth_pct, CTLFLAG_RWTUN,
    &zfs_vdev_queue_depth_pct, 0,
    "Queue depth percentage for each top-level");
SYSCTL_INT(_vfs_zfs_vdev, OID_AUTO, def_queue_depth, CTLFLAG_RWTUN,
    &zfs_vdev_def_queue_depth, 0,
    "Default queue depth for each allocator");

static int
sysctl_zfs_async_write_active_min_dirty_percent(SYSCTL_HANDLER_ARGS)
{
	int val, err;

	val = zfs_vdev_async_write_active_min_dirty_percent;
	err = sysctl_handle_int(oidp, &val, 0, req);
	if (err != 0 || req->newptr == NULL)
		return (err);
	
	if (val < 0 || val > 100 ||
	    val >= zfs_vdev_async_write_active_max_dirty_percent)
		return (EINVAL);

	zfs_vdev_async_write_active_min_dirty_percent = val;

	return (0);
}

static int
sysctl_zfs_async_write_active_max_dirty_percent(SYSCTL_HANDLER_ARGS)
{
	int val, err;

	val = zfs_vdev_async_write_active_max_dirty_percent;
	err = sysctl_handle_int(oidp, &val, 0, req);
	if (err != 0 || req->newptr == NULL)
		return (err);

	if (val < 0 || val > 100 ||
	    val <= zfs_vdev_async_write_active_min_dirty_percent)
		return (EINVAL);

	zfs_vdev_async_write_active_max_dirty_percent = val;

	return (0);
}
#endif
#endif

int
vdev_queue_offset_compare(const void *x1, const void *x2)
{
	const zio_t *z1 = (const zio_t *)x1;
	const zio_t *z2 = (const zio_t *)x2;

	int cmp = AVL_CMP(z1->io_offset, z2->io_offset);

	if (likely(cmp))
		return (cmp);

	return (AVL_PCMP(z1, z2));
}

static inline avl_tree_t *
vdev_queue_class_tree(vdev_queue_t *vq, zio_priority_t p)
{
	return (&vq->vq_class[p].vqc_queued_tree);
}

static inline avl_tree_t *
vdev_queue_type_tree(vdev_queue_t *vq, zio_type_t t)
{
	if (t == ZIO_TYPE_READ)
		return (&vq->vq_read_offset_tree);
	else if (t == ZIO_TYPE_WRITE)
		return (&vq->vq_write_offset_tree);
	else
		return (NULL);
}

int
vdev_queue_timestamp_compare(const void *x1, const void *x2)
{
	const zio_t *z1 = x1;
	const zio_t *z2 = x2;

	if (z1->io_timestamp < z2->io_timestamp)
		return (-1);
	if (z1->io_timestamp > z2->io_timestamp)
		return (1);

	if (z1->io_offset < z2->io_offset)
		return (-1);
	if (z1->io_offset > z2->io_offset)
		return (1);

	if (z1 < z2)
		return (-1);
	if (z1 > z2)
		return (1);

	return (0);
}

void
vdev_queue_init(vdev_t *vd)
{
	vdev_queue_t *vq = &vd->vdev_queue;

	mutex_init(&vq->vq_lock, NULL, MUTEX_DEFAULT, NULL);
	vq->vq_vdev = vd;

	avl_create(&vq->vq_active_tree, vdev_queue_offset_compare,
	    sizeof (zio_t), offsetof(struct zio, io_queue_node));
	avl_create(vdev_queue_type_tree(vq, ZIO_TYPE_READ),
	    vdev_queue_offset_compare, sizeof (zio_t),
	    offsetof(struct zio, io_offset_node));
	avl_create(vdev_queue_type_tree(vq, ZIO_TYPE_WRITE),
	    vdev_queue_offset_compare, sizeof (zio_t),
	    offsetof(struct zio, io_offset_node));

	for (zio_priority_t p = 0; p < ZIO_PRIORITY_NUM_QUEUEABLE; p++) {
		int (*compfn) (const void *, const void *);

		/*
		 * The synchronous i/o queues are dispatched in FIFO rather
		 * than LBA order.  This provides more consistent latency for
		 * these i/os.
		 */
		if (p == ZIO_PRIORITY_SYNC_READ || p == ZIO_PRIORITY_SYNC_WRITE)
			compfn = vdev_queue_timestamp_compare;
		else
			compfn = vdev_queue_offset_compare;

		avl_create(vdev_queue_class_tree(vq, p), compfn,
		    sizeof (zio_t), offsetof(struct zio, io_queue_node));
	}

	vq->vq_lastoffset = 0;
}

void
vdev_queue_fini(vdev_t *vd)
{
	vdev_queue_t *vq = &vd->vdev_queue;

	for (zio_priority_t p = 0; p < ZIO_PRIORITY_NUM_QUEUEABLE; p++)
		avl_destroy(vdev_queue_class_tree(vq, p));
	avl_destroy(&vq->vq_active_tree);
	avl_destroy(vdev_queue_type_tree(vq, ZIO_TYPE_READ));
	avl_destroy(vdev_queue_type_tree(vq, ZIO_TYPE_WRITE));

	mutex_destroy(&vq->vq_lock);
}

static void
vdev_queue_io_add(vdev_queue_t *vq, zio_t *zio)
{
	spa_t *spa = zio->io_spa;
	avl_tree_t *qtt;

	ASSERT(MUTEX_HELD(&vq->vq_lock));
	ASSERT3U(zio->io_priority, <, ZIO_PRIORITY_NUM_QUEUEABLE);
	avl_add(vdev_queue_class_tree(vq, zio->io_priority), zio);
	qtt = vdev_queue_type_tree(vq, zio->io_type);
	if (qtt)
		avl_add(qtt, zio);

#ifdef illumos
	mutex_enter(&spa->spa_iokstat_lock);
	spa->spa_queue_stats[zio->io_priority].spa_queued++;
	if (spa->spa_iokstat != NULL)
		kstat_waitq_enter(spa->spa_iokstat->ks_data);
	mutex_exit(&spa->spa_iokstat_lock);
#endif
}

static void
vdev_queue_io_remove(vdev_queue_t *vq, zio_t *zio)
{
	spa_t *spa = zio->io_spa;
	avl_tree_t *qtt;

	ASSERT(MUTEX_HELD(&vq->vq_lock));
	ASSERT3U(zio->io_priority, <, ZIO_PRIORITY_NUM_QUEUEABLE);
	avl_remove(vdev_queue_class_tree(vq, zio->io_priority), zio);
	qtt = vdev_queue_type_tree(vq, zio->io_type);
	if (qtt)
		avl_remove(qtt, zio);

#ifdef illumos
	mutex_enter(&spa->spa_iokstat_lock);
	ASSERT3U(spa->spa_queue_stats[zio->io_priority].spa_queued, >, 0);
	spa->spa_queue_stats[zio->io_priority].spa_queued--;
	if (spa->spa_iokstat != NULL)
		kstat_waitq_exit(spa->spa_iokstat->ks_data);
	mutex_exit(&spa->spa_iokstat_lock);
#endif
}

static void
vdev_queue_pending_add(vdev_queue_t *vq, zio_t *zio)
{
	spa_t *spa = zio->io_spa;
	ASSERT(MUTEX_HELD(&vq->vq_lock));
	ASSERT3U(zio->io_priority, <, ZIO_PRIORITY_NUM_QUEUEABLE);
	vq->vq_class[zio->io_priority].vqc_active++;
	avl_add(&vq->vq_active_tree, zio);

#ifdef illumos
	mutex_enter(&spa->spa_iokstat_lock);
	spa->spa_queue_stats[zio->io_priority].spa_active++;
	if (spa->spa_iokstat != NULL)
		kstat_runq_enter(spa->spa_iokstat->ks_data);
	mutex_exit(&spa->spa_iokstat_lock);
#endif
}

static void
vdev_queue_pending_remove(vdev_queue_t *vq, zio_t *zio)
{
	spa_t *spa = zio->io_spa;
	ASSERT(MUTEX_HELD(&vq->vq_lock));
	ASSERT3U(zio->io_priority, <, ZIO_PRIORITY_NUM_QUEUEABLE);
	vq->vq_class[zio->io_priority].vqc_active--;
	avl_remove(&vq->vq_active_tree, zio);

#ifdef illumos
	mutex_enter(&spa->spa_iokstat_lock);
	ASSERT3U(spa->spa_queue_stats[zio->io_priority].spa_active, >, 0);
	spa->spa_queue_stats[zio->io_priority].spa_active--;
	if (spa->spa_iokstat != NULL) {
		kstat_io_t *ksio = spa->spa_iokstat->ks_data;

		kstat_runq_exit(spa->spa_iokstat->ks_data);
		if (zio->io_type == ZIO_TYPE_READ) {
			ksio->reads++;
			ksio->nread += zio->io_size;
		} else if (zio->io_type == ZIO_TYPE_WRITE) {
			ksio->writes++;
			ksio->nwritten += zio->io_size;
		}
	}
	mutex_exit(&spa->spa_iokstat_lock);
#endif
}

static void
vdev_queue_agg_io_done(zio_t *aio)
{
	if (aio->io_type == ZIO_TYPE_READ) {
		zio_t *pio;
		zio_link_t *zl = NULL;
		while ((pio = zio_walk_parents(aio, &zl)) != NULL) {
			abd_copy_off(pio->io_abd, aio->io_abd,
			    0, pio->io_offset - aio->io_offset, pio->io_size);
		}
	}

	abd_free(aio->io_abd);
}

static int
vdev_queue_class_min_active(zio_priority_t p)
{
	switch (p) {
	case ZIO_PRIORITY_SYNC_READ:
		return (zfs_vdev_sync_read_min_active);
	case ZIO_PRIORITY_SYNC_WRITE:
		return (zfs_vdev_sync_write_min_active);
	case ZIO_PRIORITY_ASYNC_READ:
		return (zfs_vdev_async_read_min_active);
	case ZIO_PRIORITY_ASYNC_WRITE:
		return (zfs_vdev_async_write_min_active);
	case ZIO_PRIORITY_SCRUB:
		return (zfs_vdev_scrub_min_active);
	case ZIO_PRIORITY_TRIM:
		return (zfs_vdev_trim_min_active);
	case ZIO_PRIORITY_REMOVAL:
		return (zfs_vdev_removal_min_active);
	case ZIO_PRIORITY_INITIALIZING:
		return (zfs_vdev_initializing_min_active);
	default:
		panic("invalid priority %u", p);
		return (0);
	}
}

static __noinline int
vdev_queue_max_async_writes(spa_t *spa)
{
	int writes;
	uint64_t dirty = spa->spa_dsl_pool->dp_dirty_total;
	uint64_t min_bytes = zfs_dirty_data_max *
	    zfs_vdev_async_write_active_min_dirty_percent / 100;
	uint64_t max_bytes = zfs_dirty_data_max *
	    zfs_vdev_async_write_active_max_dirty_percent / 100;

	/*
	 * Sync tasks correspond to interactive user actions. To reduce the
	 * execution time of those actions we push data out as fast as possible.
	 */
	if (spa_has_pending_synctask(spa)) {
		return (zfs_vdev_async_write_max_active);
	}

	if (dirty < min_bytes)
		return (zfs_vdev_async_write_min_active);
	if (dirty > max_bytes)
		return (zfs_vdev_async_write_max_active);

	/*
	 * linear interpolation:
	 * slope = (max_writes - min_writes) / (max_bytes - min_bytes)
	 * move right by min_bytes
	 * move up by min_writes
	 */
	writes = (dirty - min_bytes) *
	    (zfs_vdev_async_write_max_active -
	    zfs_vdev_async_write_min_active) /
	    (max_bytes - min_bytes) +
	    zfs_vdev_async_write_min_active;
	ASSERT3U(writes, >=, zfs_vdev_async_write_min_active);
	ASSERT3U(writes, <=, zfs_vdev_async_write_max_active);
	return (writes);
}

static int
vdev_queue_class_max_active(spa_t *spa, zio_priority_t p)
{
	switch (p) {
	case ZIO_PRIORITY_SYNC_READ:
		return (zfs_vdev_sync_read_max_active);
	case ZIO_PRIORITY_SYNC_WRITE:
		return (zfs_vdev_sync_write_max_active);
	case ZIO_PRIORITY_ASYNC_READ:
		return (zfs_vdev_async_read_max_active);
	case ZIO_PRIORITY_ASYNC_WRITE:
		return (vdev_queue_max_async_writes(spa));
	case ZIO_PRIORITY_SCRUB:
		return (zfs_vdev_scrub_max_active);
	case ZIO_PRIORITY_TRIM:
		return (zfs_vdev_trim_max_active);
	case ZIO_PRIORITY_REMOVAL:
		return (zfs_vdev_removal_max_active);
	case ZIO_PRIORITY_INITIALIZING:
		return (zfs_vdev_initializing_max_active);
	default:
		panic("invalid priority %u", p);
		return (0);
	}
}

/*
 * Return the i/o class to issue from, or ZIO_PRIORITY_MAX_QUEUEABLE if
 * there is no eligible class.
 */
static zio_priority_t
vdev_queue_class_to_issue(vdev_queue_t *vq)
{
	spa_t *spa = vq->vq_vdev->vdev_spa;
	zio_priority_t p;

	ASSERT(MUTEX_HELD(&vq->vq_lock));

	if (avl_numnodes(&vq->vq_active_tree) >= zfs_vdev_max_active)
		return (ZIO_PRIORITY_NUM_QUEUEABLE);

	/* find a queue that has not reached its minimum # outstanding i/os */
	for (p = 0; p < ZIO_PRIORITY_NUM_QUEUEABLE; p++) {
		if (avl_numnodes(vdev_queue_class_tree(vq, p)) > 0 &&
		    vq->vq_class[p].vqc_active <
		    vdev_queue_class_min_active(p))
			return (p);
	}

	/*
	 * If we haven't found a queue, look for one that hasn't reached its
	 * maximum # outstanding i/os.
	 */
	for (p = 0; p < ZIO_PRIORITY_NUM_QUEUEABLE; p++) {
		if (avl_numnodes(vdev_queue_class_tree(vq, p)) > 0 &&
		    vq->vq_class[p].vqc_active <
		    vdev_queue_class_max_active(spa, p))
			return (p);
	}

	/* No eligible queued i/os */
	return (ZIO_PRIORITY_NUM_QUEUEABLE);
}

/*
 * Compute the range spanned by two i/os, which is the endpoint of the last
 * (lio->io_offset + lio->io_size) minus start of the first (fio->io_offset).
 * Conveniently, the gap between fio and lio is given by -IO_SPAN(lio, fio);
 * thus fio and lio are adjacent if and only if IO_SPAN(lio, fio) == 0.
 */
#define	IO_SPAN(fio, lio) ((lio)->io_offset + (lio)->io_size - (fio)->io_offset)
#define	IO_GAP(fio, lio) (-IO_SPAN(lio, fio))

static zio_t *
vdev_queue_aggregate(vdev_queue_t *vq, zio_t *zio)
{
	zio_t *first, *last, *aio, *dio, *mandatory, *nio;
	zio_link_t *zl = NULL;
	uint64_t maxgap = 0;
	uint64_t size;
	uint64_t limit;
	int maxblocksize;
	boolean_t stretch;
	avl_tree_t *t;
	enum zio_flag flags;

	ASSERT(MUTEX_HELD(&vq->vq_lock));

	maxblocksize = spa_maxblocksize(vq->vq_vdev->vdev_spa);
	if (vq->vq_vdev->vdev_nonrot)
		limit = zfs_vdev_aggregation_limit_non_rotating;
	else
		limit = zfs_vdev_aggregation_limit;
	limit = MAX(MIN(limit, maxblocksize), 0);

	if (zio->io_flags & ZIO_FLAG_DONT_AGGREGATE || limit == 0)
		return (NULL);

	first = last = zio;

	if (zio->io_type == ZIO_TYPE_READ)
		maxgap = zfs_vdev_read_gap_limit;

	/*
	 * We can aggregate I/Os that are sufficiently adjacent and of
	 * the same flavor, as expressed by the AGG_INHERIT flags.
	 * The latter requirement is necessary so that certain
	 * attributes of the I/O, such as whether it's a normal I/O
	 * or a scrub/resilver, can be preserved in the aggregate.
	 * We can include optional I/Os, but don't allow them
	 * to begin a range as they add no benefit in that situation.
	 */

	/*
	 * We keep track of the last non-optional I/O.
	 */
	mandatory = (first->io_flags & ZIO_FLAG_OPTIONAL) ? NULL : first;

	/*
	 * Walk backwards through sufficiently contiguous I/Os
	 * recording the last non-optional I/O.
	 */
	flags = zio->io_flags & ZIO_FLAG_AGG_INHERIT;
	t = vdev_queue_type_tree(vq, zio->io_type);
	while (t != NULL && (dio = AVL_PREV(t, first)) != NULL &&
	    (dio->io_flags & ZIO_FLAG_AGG_INHERIT) == flags &&
	    IO_SPAN(dio, last) <= limit &&
	    IO_GAP(dio, first) <= maxgap &&
	    dio->io_type == zio->io_type) {
		first = dio;
		if (mandatory == NULL && !(first->io_flags & ZIO_FLAG_OPTIONAL))
			mandatory = first;
	}

	/*
	 * Skip any initial optional I/Os.
	 */
	while ((first->io_flags & ZIO_FLAG_OPTIONAL) && first != last) {
		first = AVL_NEXT(t, first);
		ASSERT(first != NULL);
	}

	/*
	 * Walk forward through sufficiently contiguous I/Os.
	 * The aggregation limit does not apply to optional i/os, so that
	 * we can issue contiguous writes even if they are larger than the
	 * aggregation limit.
	 */
	while ((dio = AVL_NEXT(t, last)) != NULL &&
	    (dio->io_flags & ZIO_FLAG_AGG_INHERIT) == flags &&
	    (IO_SPAN(first, dio) <= limit ||
	    (dio->io_flags & ZIO_FLAG_OPTIONAL)) &&
	    IO_SPAN(first, dio) <= maxblocksize &&
	    IO_GAP(last, dio) <= maxgap &&
	    dio->io_type == zio->io_type) {
		last = dio;
		if (!(last->io_flags & ZIO_FLAG_OPTIONAL))
			mandatory = last;
	}

	/*
	 * Now that we've established the range of the I/O aggregation
	 * we must decide what to do with trailing optional I/Os.
	 * For reads, there's nothing to do. While we are unable to
	 * aggregate further, it's possible that a trailing optional
	 * I/O would allow the underlying device to aggregate with
	 * subsequent I/Os. We must therefore determine if the next
	 * non-optional I/O is close enough to make aggregation
	 * worthwhile.
	 */
	stretch = B_FALSE;
	if (zio->io_type == ZIO_TYPE_WRITE && mandatory != NULL) {
		zio_t *nio = last;
		while ((dio = AVL_NEXT(t, nio)) != NULL &&
		    IO_GAP(nio, dio) == 0 &&
		    IO_GAP(mandatory, dio) <= zfs_vdev_write_gap_limit) {
			nio = dio;
			if (!(nio->io_flags & ZIO_FLAG_OPTIONAL)) {
				stretch = B_TRUE;
				break;
			}
		}
	}

	if (stretch) {
		/*
		 * We are going to include an optional io in our aggregated
		 * span, thus closing the write gap.  Only mandatory i/os can
		 * start aggregated spans, so make sure that the next i/o
		 * after our span is mandatory.
		 */
		dio = AVL_NEXT(t, last);
		dio->io_flags &= ~ZIO_FLAG_OPTIONAL;
	} else {
		/* do not include the optional i/o */
		while (last != mandatory && last != first) {
			ASSERT(last->io_flags & ZIO_FLAG_OPTIONAL);
			last = AVL_PREV(t, last);
			ASSERT(last != NULL);
		}
	}

	if (first == last)
		return (NULL);

	size = IO_SPAN(first, last);
	ASSERT3U(size, <=, maxblocksize);

	aio = zio_vdev_delegated_io(first->io_vd, first->io_offset,
	    abd_alloc_for_io(size, B_TRUE), size, first->io_type,
	    zio->io_priority, flags | ZIO_FLAG_DONT_CACHE | ZIO_FLAG_DONT_QUEUE,
	    vdev_queue_agg_io_done, NULL);
	aio->io_timestamp = first->io_timestamp;

	nio = first;
	do {
		dio = nio;
		nio = AVL_NEXT(t, dio);
		ASSERT3U(dio->io_type, ==, aio->io_type);

		if (dio->io_flags & ZIO_FLAG_NODATA) {
			ASSERT3U(dio->io_type, ==, ZIO_TYPE_WRITE);
			abd_zero_off(aio->io_abd,
			    dio->io_offset - aio->io_offset, dio->io_size);
		} else if (dio->io_type == ZIO_TYPE_WRITE) {
			abd_copy_off(aio->io_abd, dio->io_abd,
			    dio->io_offset - aio->io_offset, 0, dio->io_size);
		}

		zio_add_child(dio, aio);
		vdev_queue_io_remove(vq, dio);
	} while (dio != last);

	/*
	 * We need to drop the vdev queue's lock to avoid a deadlock that we
	 * could encounter since this I/O will complete immediately.
	 */
	mutex_exit(&vq->vq_lock);
	while ((dio = zio_walk_parents(aio, &zl)) != NULL) {
		zio_vdev_io_bypass(dio);
		zio_execute(dio);
	}
	mutex_enter(&vq->vq_lock);

	return (aio);
}

static zio_t *
vdev_queue_io_to_issue(vdev_queue_t *vq)
{
	zio_t *zio, *aio;
	zio_priority_t p;
	avl_index_t idx;
	avl_tree_t *tree;
	zio_t search;

again:
	ASSERT(MUTEX_HELD(&vq->vq_lock));

	p = vdev_queue_class_to_issue(vq);

	if (p == ZIO_PRIORITY_NUM_QUEUEABLE) {
		/* No eligible queued i/os */
		return (NULL);
	}

	/*
	 * For LBA-ordered queues (async / scrub / initializing), issue the
	 * i/o which follows the most recently issued i/o in LBA (offset) order.
	 *
	 * For FIFO queues (sync), issue the i/o with the lowest timestamp.
	 */
	tree = vdev_queue_class_tree(vq, p);
	search.io_timestamp = 0;
	search.io_offset = vq->vq_last_offset + 1;
	VERIFY3P(avl_find(tree, &search, &idx), ==, NULL);
	zio = avl_nearest(tree, idx, AVL_AFTER);
	if (zio == NULL)
		zio = avl_first(tree);
	ASSERT3U(zio->io_priority, ==, p);

	aio = vdev_queue_aggregate(vq, zio);
	if (aio != NULL)
		zio = aio;
	else
		vdev_queue_io_remove(vq, zio);

	/*
	 * If the I/O is or was optional and therefore has no data, we need to
	 * simply discard it. We need to drop the vdev queue's lock to avoid a
	 * deadlock that we could encounter since this I/O will complete
	 * immediately.
	 */
	if (zio->io_flags & ZIO_FLAG_NODATA) {
		mutex_exit(&vq->vq_lock);
		zio_vdev_io_bypass(zio);
		zio_execute(zio);
		mutex_enter(&vq->vq_lock);
		goto again;
	}

	vdev_queue_pending_add(vq, zio);
	vq->vq_last_offset = zio->io_offset;

	return (zio);
}

zio_t *
vdev_queue_io(zio_t *zio)
{
	vdev_queue_t *vq = &zio->io_vd->vdev_queue;
	zio_t *nio;

	if (zio->io_flags & ZIO_FLAG_DONT_QUEUE)
		return (zio);

	/*
	 * Children i/os inherent their parent's priority, which might
	 * not match the child's i/o type.  Fix it up here.
	 */
	if (zio->io_type == ZIO_TYPE_READ) {
		if (zio->io_priority != ZIO_PRIORITY_SYNC_READ &&
		    zio->io_priority != ZIO_PRIORITY_ASYNC_READ &&
		    zio->io_priority != ZIO_PRIORITY_SCRUB &&
		    zio->io_priority != ZIO_PRIORITY_REMOVAL &&
		    zio->io_priority != ZIO_PRIORITY_INITIALIZING)
			zio->io_priority = ZIO_PRIORITY_ASYNC_READ;
	} else if (zio->io_type == ZIO_TYPE_WRITE) {
		if (zio->io_priority != ZIO_PRIORITY_SYNC_WRITE &&
		    zio->io_priority != ZIO_PRIORITY_ASYNC_WRITE &&
		    zio->io_priority != ZIO_PRIORITY_REMOVAL &&
		    zio->io_priority != ZIO_PRIORITY_INITIALIZING)
			zio->io_priority = ZIO_PRIORITY_ASYNC_WRITE;
	} else {
		ASSERT(zio->io_type == ZIO_TYPE_FREE);
		zio->io_priority = ZIO_PRIORITY_TRIM;
	}

	zio->io_flags |= ZIO_FLAG_DONT_CACHE | ZIO_FLAG_DONT_QUEUE;

	mutex_enter(&vq->vq_lock);
	zio->io_timestamp = gethrtime();
	vdev_queue_io_add(vq, zio);
	nio = vdev_queue_io_to_issue(vq);
	mutex_exit(&vq->vq_lock);

	if (nio == NULL)
		return (NULL);

	if (nio->io_done == vdev_queue_agg_io_done) {
		zio_nowait(nio);
		return (NULL);
	}

	return (nio);
}

void
vdev_queue_io_done(zio_t *zio)
{
	vdev_queue_t *vq = &zio->io_vd->vdev_queue;
	zio_t *nio;

	mutex_enter(&vq->vq_lock);

	vdev_queue_pending_remove(vq, zio);

	vq->vq_io_complete_ts = gethrtime();

	while ((nio = vdev_queue_io_to_issue(vq)) != NULL) {
		mutex_exit(&vq->vq_lock);
		if (nio->io_done == vdev_queue_agg_io_done) {
			zio_nowait(nio);
		} else {
			zio_vdev_io_reissue(nio);
			zio_execute(nio);
		}
		mutex_enter(&vq->vq_lock);
	}

	mutex_exit(&vq->vq_lock);
}

void
vdev_queue_change_io_priority(zio_t *zio, zio_priority_t priority)
{
	vdev_queue_t *vq = &zio->io_vd->vdev_queue;
	avl_tree_t *tree;

	/*
	 * ZIO_PRIORITY_NOW is used by the vdev cache code and the aggregate zio
	 * code to issue IOs without adding them to the vdev queue. In this
	 * case, the zio is already going to be issued as quickly as possible
	 * and so it doesn't need any reprioitization to help.
	 */
	if (zio->io_priority == ZIO_PRIORITY_NOW)
		return;

	ASSERT3U(zio->io_priority, <, ZIO_PRIORITY_NUM_QUEUEABLE);
	ASSERT3U(priority, <, ZIO_PRIORITY_NUM_QUEUEABLE);

	if (zio->io_type == ZIO_TYPE_READ) {
		if (priority != ZIO_PRIORITY_SYNC_READ &&
		    priority != ZIO_PRIORITY_ASYNC_READ &&
		    priority != ZIO_PRIORITY_SCRUB)
			priority = ZIO_PRIORITY_ASYNC_READ;
	} else {
		ASSERT(zio->io_type == ZIO_TYPE_WRITE);
		if (priority != ZIO_PRIORITY_SYNC_WRITE &&
		    priority != ZIO_PRIORITY_ASYNC_WRITE)
			priority = ZIO_PRIORITY_ASYNC_WRITE;
	}

	mutex_enter(&vq->vq_lock);

	/*
	 * If the zio is in none of the queues we can simply change
	 * the priority. If the zio is waiting to be submitted we must
	 * remove it from the queue and re-insert it with the new priority.
	 * Otherwise, the zio is currently active and we cannot change its
	 * priority.
	 */
	tree = vdev_queue_class_tree(vq, zio->io_priority);
	if (avl_find(tree, zio, NULL) == zio) {
		avl_remove(vdev_queue_class_tree(vq, zio->io_priority), zio);
		zio->io_priority = priority;
		avl_add(vdev_queue_class_tree(vq, zio->io_priority), zio);
	} else if (avl_find(&vq->vq_active_tree, zio, NULL) != zio) {
		zio->io_priority = priority;
	}

	mutex_exit(&vq->vq_lock);
}

/*
 * As these three methods are only used for load calculations we're not concerned
 * if we get an incorrect value on 32bit platforms due to lack of vq_lock mutex
 * use here, instead we prefer to keep it lock free for performance.
 */ 
int
vdev_queue_length(vdev_t *vd)
{
	return (avl_numnodes(&vd->vdev_queue.vq_active_tree));
}

uint64_t
vdev_queue_lastoffset(vdev_t *vd)
{
	return (vd->vdev_queue.vq_lastoffset);
}

void
vdev_queue_register_lastoffset(vdev_t *vd, zio_t *zio)
{
	vd->vdev_queue.vq_lastoffset = zio->io_offset + zio->io_size;
}
