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
 * Copyright (c) 2017 by Lawrence Livermore National Security, LLC.
 */

#include <sys/abd.h>
#include <sys/mmp.h>
#include <sys/spa.h>
#include <sys/spa_impl.h>
#include <sys/vdev.h>
#include <sys/vdev_impl.h>
#include <sys/zfs_context.h>
#include <sys/callb.h>

/*
 * Multi-Modifier Protection (MMP) attempts to prevent a user from importing
 * or opening a pool on more than one host at a time.  In particular, it
 * prevents "zpool import -f" on a host from succeeding while the pool is
 * already imported on another host.  There are many other ways in which a
 * device could be used by two hosts for different purposes at the same time
 * resulting in pool damage.  This implementation does not attempt to detect
 * those cases.
 *
 * MMP operates by ensuring there are frequent visible changes on disk (a
 * "heartbeat") at all times.  And by altering the import process to check
 * for these changes and failing the import when they are detected.  This
 * functionality is enabled by setting the 'multihost' pool property to on.
 *
 * Uberblocks written by the txg_sync thread always go into the first
 * (N-MMP_BLOCKS_PER_LABEL) slots, the remaining slots are reserved for MMP.
 * They are used to hold uberblocks which are exactly the same as the last
 * synced uberblock except that the ub_timestamp is frequently updated.
 * Like all other uberblocks, the slot is written with an embedded checksum,
 * and slots with invalid checksums are ignored.  This provides the
 * "heartbeat", with no risk of overwriting good uberblocks that must be
 * preserved, e.g. previous txgs and associated block pointers.
 *
 * Two optional fields are added to uberblock structure: ub_mmp_magic and
 * ub_mmp_delay.  The magic field allows zfs to tell whether ub_mmp_delay is
 * valid.  The delay field is a decaying average of the amount of time between
 * completion of successive MMP writes, in nanoseconds.  It is used to predict
 * how long the import must wait to detect activity in the pool, before
 * concluding it is not in use.
 *
 * During import an activity test may now be performed to determine if
 * the pool is in use.  The activity test is typically required if the
 * ZPOOL_CONFIG_HOSTID does not match the system hostid, the pool state is
 * POOL_STATE_ACTIVE, and the pool is not a root pool.
 *
 * The activity test finds the "best" uberblock (highest txg & timestamp),
 * waits some time, and then finds the "best" uberblock again.  If the txg
 * and timestamp in both "best" uberblocks do not match, the pool is in use
 * by another host and the import fails.  Since the granularity of the
 * timestamp is in seconds this activity test must take a bare minimum of one
 * second.  In order to assure the accuracy of the activity test, the default
 * values result in an activity test duration of 10x the mmp write interval.
 *
 * The "zpool import"  activity test can be expected to take a minimum time of
 * zfs_multihost_import_intervals * zfs_multihost_interval milliseconds.  If the
 * "best" uberblock has a valid ub_mmp_delay field, then the duration of the
 * test may take longer if MMP writes were occurring less frequently than
 * expected.  Additionally, the duration is then extended by a random 25% to
 * attempt to to detect simultaneous imports.  For example, if both partner
 * hosts are rebooted at the same time and automatically attempt to import the
 * pool.
 */

/*
 * Used to control the frequency of mmp writes which are performed when the
 * 'multihost' pool property is on.  This is one factor used to determine the
 * length of the activity check during import.
 *
 * The mmp write period is zfs_multihost_interval / leaf-vdevs milliseconds.
 * This means that on average an mmp write will be issued for each leaf vdev
 * every zfs_multihost_interval milliseconds.  In practice, the observed period
 * can vary with the I/O load and this observed value is the delay which is
 * stored in the uberblock.  The minimum allowed value is 100 ms.
 */
ulong_t zfs_multihost_interval = MMP_DEFAULT_INTERVAL;

/*
 * Used to control the duration of the activity test on import.  Smaller values
 * of zfs_multihost_import_intervals will reduce the import time but increase
 * the risk of failing to detect an active pool.  The total activity check time
 * is never allowed to drop below one second.  A value of 0 is ignored and
 * treated as if it was set to 1.
 */
uint_t zfs_multihost_import_intervals = MMP_DEFAULT_IMPORT_INTERVALS;

/*
 * Controls the behavior of the pool when mmp write failures are detected.
 *
 * When zfs_multihost_fail_intervals = 0 then mmp write failures are ignored.
 * The failures will still be reported to the ZED which depending on its
 * configuration may take action such as suspending the pool or taking a
 * device offline.
 *
 * When zfs_multihost_fail_intervals > 0 then sequential mmp write failures will
 * cause the pool to be suspended.  This occurs when
 * zfs_multihost_fail_intervals * zfs_multihost_interval milliseconds have
 * passed since the last successful mmp write.  This guarantees the activity
 * test will see mmp writes if the
 * pool is imported.
 */
uint_t zfs_multihost_fail_intervals = MMP_DEFAULT_FAIL_INTERVALS;

static void mmp_thread(spa_t *spa);

void
mmp_init(spa_t *spa)
{
	mmp_thread_t *mmp = &spa->spa_mmp;

	mutex_init(&mmp->mmp_thread_lock, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&mmp->mmp_thread_cv, NULL, CV_DEFAULT, NULL);
	mutex_init(&mmp->mmp_io_lock, NULL, MUTEX_DEFAULT, NULL);
}

void
mmp_fini(spa_t *spa)
{
	mmp_thread_t *mmp = &spa->spa_mmp;

	mutex_destroy(&mmp->mmp_thread_lock);
	cv_destroy(&mmp->mmp_thread_cv);
	mutex_destroy(&mmp->mmp_io_lock);
}

static void
mmp_thread_enter(mmp_thread_t *mmp, callb_cpr_t *cpr)
{
	CALLB_CPR_INIT(cpr, &mmp->mmp_thread_lock, callb_generic_cpr, FTAG);
	mutex_enter(&mmp->mmp_thread_lock);
}

static void
mmp_thread_exit(mmp_thread_t *mmp, kthread_t **mpp, callb_cpr_t *cpr)
{
	ASSERT(*mpp != NULL);
	*mpp = NULL;
	cv_broadcast(&mmp->mmp_thread_cv);
	CALLB_CPR_EXIT(cpr);		/* drops &mmp->mmp_thread_lock */
	thread_exit();
}

void
mmp_thread_start(spa_t *spa)
{
	mmp_thread_t *mmp = &spa->spa_mmp;

	if (spa_writeable(spa)) {
		mutex_enter(&mmp->mmp_thread_lock);
		if (!mmp->mmp_thread) {
			dprintf("mmp_thread_start pool %s\n",
			    spa->spa_name);
			mmp->mmp_thread = thread_create(NULL, 0, mmp_thread,
			    spa, 0, &p0, TS_RUN, defclsyspri);
		}
		mutex_exit(&mmp->mmp_thread_lock);
	}
}

void
mmp_thread_stop(spa_t *spa)
{
	mmp_thread_t *mmp = &spa->spa_mmp;

	mutex_enter(&mmp->mmp_thread_lock);
	mmp->mmp_thread_exiting = 1;
	cv_broadcast(&mmp->mmp_thread_cv);

	while (mmp->mmp_thread) {
		cv_wait(&mmp->mmp_thread_cv, &mmp->mmp_thread_lock);
	}
	mutex_exit(&mmp->mmp_thread_lock);

	ASSERT(mmp->mmp_thread == NULL);
	mmp->mmp_thread_exiting = 0;
}

/*
 * Choose a leaf vdev to write an MMP block to.  It must not have an
 * outstanding mmp write (if so then there is a problem, and a new write will
 * also block).  If there is no usable leaf in this subtree return NULL,
 * otherwise return a pointer to the leaf.
 *
 * When walking the subtree, a random child is chosen as the starting point so
 * that when the tree is healthy, the leaf chosen will be random with even
 * distribution.  If there are unhealthy vdevs in the tree, the distribution
 * will be really poor only if a large proportion of the vdevs are unhealthy,
 * in which case there are other more pressing problems.
 */
static vdev_t *
mmp_random_leaf(vdev_t *vd)
{
	int child_idx;

	if (!vdev_writeable(vd))
		return (NULL);

	if (vd->vdev_ops->vdev_op_leaf)
		return (vd->vdev_mmp_pending == 0 ? vd : NULL);

	child_idx = spa_get_random(vd->vdev_children);
	for (int offset = vd->vdev_children; offset > 0; offset--) {
		vdev_t *leaf;
		vdev_t *child = vd->vdev_child[(child_idx + offset) %
		    vd->vdev_children];

		leaf = mmp_random_leaf(child);
		if (leaf)
			return (leaf);
	}

	return (NULL);
}

static void
mmp_write_done(zio_t *zio)
{
	spa_t *spa = zio->io_spa;
	vdev_t *vd = zio->io_vd;
	mmp_thread_t *mts = zio->io_private;

	mutex_enter(&mts->mmp_io_lock);
	vd->vdev_mmp_pending = 0;

	if (zio->io_error)
		goto unlock;

	/*
	 * Mmp writes are queued on a fixed schedule, but under many
	 * circumstances, such as a busy device or faulty hardware,
	 * the writes will complete at variable, much longer,
	 * intervals.  In these cases, another node checking for
	 * activity must wait longer to account for these delays.
	 *
	 * The mmp_delay is calculated as a decaying average of the interval
	 * between completed mmp writes.  This is used to predict how long
	 * the import must wait to detect activity in the pool, before
	 * concluding it is not in use.
	 *
	 * Do not set mmp_delay if the multihost property is not on,
	 * so as not to trigger an activity check on import.
	 */
	if (spa_multihost(spa)) {
		hrtime_t delay = gethrtime() - mts->mmp_last_write;

		if (delay > mts->mmp_delay)
			mts->mmp_delay = delay;
		else
			mts->mmp_delay = (delay + mts->mmp_delay * 127) /
			    128;
	} else {
		mts->mmp_delay = 0;
	}
	mts->mmp_last_write = gethrtime();

unlock:
	mutex_exit(&mts->mmp_io_lock);
	spa_config_exit(spa, SCL_STATE, FTAG);

	abd_free(zio->io_abd);
}

/*
 * When the uberblock on-disk is updated by a spa_sync,
 * creating a new "best" uberblock, update the one stored
 * in the mmp thread state, used for mmp writes.
 */
void
mmp_update_uberblock(spa_t *spa, uberblock_t *ub)
{
	mmp_thread_t *mmp = &spa->spa_mmp;

	mutex_enter(&mmp->mmp_io_lock);
	mmp->mmp_ub = *ub;
	mmp->mmp_ub.ub_timestamp = gethrestime_sec();
	mutex_exit(&mmp->mmp_io_lock);
}

/*
 * Choose a random vdev, label, and MMP block, and write over it
 * with a copy of the last-synced uberblock, whose timestamp
 * has been updated to reflect that the pool is in use.
 */
static void
mmp_write_uberblock(spa_t *spa)
{
	int flags = ZIO_FLAG_CONFIG_WRITER | ZIO_FLAG_CANFAIL;
	mmp_thread_t *mmp = &spa->spa_mmp;
	uberblock_t *ub;
	vdev_t *vd;
	int label;
	uint64_t offset;

	spa_config_enter(spa, SCL_STATE, FTAG, RW_READER);
	vd = mmp_random_leaf(spa->spa_root_vdev);
	if (vd == NULL) {
		spa_config_exit(spa, SCL_STATE, FTAG);
		return;
	}

	mutex_enter(&mmp->mmp_io_lock);

	if (mmp->mmp_zio_root == NULL)
		mmp->mmp_zio_root = zio_root(spa, NULL, NULL,
		    flags | ZIO_FLAG_GODFATHER);

	ub = &mmp->mmp_ub;
	ub->ub_timestamp = gethrestime_sec();
	ub->ub_mmp_magic = MMP_MAGIC;
	ub->ub_mmp_delay = mmp->mmp_delay;
	vd->vdev_mmp_pending = gethrtime();

	zio_t *zio  = zio_null(mmp->mmp_zio_root, spa, NULL, NULL, NULL, flags);
	abd_t *ub_abd = abd_alloc_for_io(VDEV_UBERBLOCK_SIZE(vd), B_TRUE);
	abd_zero(ub_abd, VDEV_UBERBLOCK_SIZE(vd));
	abd_copy_from_buf(ub_abd, ub, sizeof (uberblock_t));

	mutex_exit(&mmp->mmp_io_lock);

	offset = VDEV_UBERBLOCK_OFFSET(vd, VDEV_UBERBLOCK_COUNT(vd) -
	    MMP_BLOCKS_PER_LABEL + spa_get_random(MMP_BLOCKS_PER_LABEL));

	label = spa_get_random(VDEV_LABELS);
	vdev_label_write(zio, vd, label, ub_abd, offset,
	    VDEV_UBERBLOCK_SIZE(vd), mmp_write_done, mmp,
	    flags | ZIO_FLAG_DONT_PROPAGATE);

	spa_mmp_history_add(ub->ub_txg, ub->ub_timestamp, ub->ub_mmp_delay, vd,
	    label);

	zio_nowait(zio);
}

static void
mmp_thread(spa_t *spa)
{
	mmp_thread_t *mmp = &spa->spa_mmp;
	boolean_t last_spa_suspended = spa_suspended(spa);
	boolean_t last_spa_multihost = spa_multihost(spa);
	callb_cpr_t cpr;
	hrtime_t max_fail_ns = zfs_multihost_fail_intervals *
	    MSEC2NSEC(MAX(zfs_multihost_interval, MMP_MIN_INTERVAL));

	mmp_thread_enter(mmp, &cpr);

	/*
	 * The mmp_write_done() function calculates mmp_delay based on the
	 * prior value of mmp_delay and the elapsed time since the last write.
	 * For the first mmp write, there is no "last write", so we start
	 * with fake, but reasonable, default non-zero values.
	 */
	mmp->mmp_delay = MSEC2NSEC(MAX(zfs_multihost_interval,
	    MMP_MIN_INTERVAL)) / MAX(vdev_count_leaves(spa), 1);
	mmp->mmp_last_write = gethrtime() - mmp->mmp_delay;

	while (!mmp->mmp_thread_exiting) {
		uint64_t mmp_fail_intervals = zfs_multihost_fail_intervals;
		uint64_t mmp_interval = MSEC2NSEC(
		    MAX(zfs_multihost_interval, MMP_MIN_INTERVAL));
		boolean_t suspended = spa_suspended(spa);
		boolean_t multihost = spa_multihost(spa);
		hrtime_t start, next_time;

		start = gethrtime();
		if (multihost) {
			next_time = start + mmp_interval /
			    MAX(vdev_count_leaves(spa), 1);
		} else {
			next_time = start + MSEC2NSEC(MMP_DEFAULT_INTERVAL);
		}

		/*
		 * When MMP goes off => on, or spa goes suspended =>
		 * !suspended, we know no writes occurred recently.  We
		 * update mmp_last_write to give us some time to try.
		 */
		if ((!last_spa_multihost && multihost) ||
		    (last_spa_suspended && !suspended)) {
			mutex_enter(&mmp->mmp_io_lock);
			mmp->mmp_last_write = gethrtime();
			mutex_exit(&mmp->mmp_io_lock);
		} else if (last_spa_multihost && !multihost) {
			mutex_enter(&mmp->mmp_io_lock);
			mmp->mmp_delay = 0;
			mutex_exit(&mmp->mmp_io_lock);
		}
		last_spa_multihost = multihost;
		last_spa_suspended = suspended;

		/*
		 * Smooth max_fail_ns when its factors are decreased, because
		 * making (max_fail_ns < mmp_interval) results in the pool being
		 * immediately suspended before writes can occur at the new
		 * higher frequency.
		 */
		if ((mmp_interval * mmp_fail_intervals) < max_fail_ns) {
			max_fail_ns = ((31 * max_fail_ns) + (mmp_interval *
			    mmp_fail_intervals)) / 32;
		} else {
			max_fail_ns = mmp_interval * mmp_fail_intervals;
		}

		/*
		 * Suspend the pool if no MMP write has succeeded in over
		 * mmp_interval * mmp_fail_intervals nanoseconds.
		 */
		if (!suspended && mmp_fail_intervals && multihost &&
		    (start - mmp->mmp_last_write) > max_fail_ns) {
			zio_suspend(spa, NULL);
		}

		if (multihost)
			mmp_write_uberblock(spa);

		CALLB_CPR_SAFE_BEGIN(&cpr);
		(void) cv_timedwait_sig(&mmp->mmp_thread_cv,
		    &mmp->mmp_thread_lock, ddi_get_lbolt() +
		    ((next_time - gethrtime()) / (NANOSEC / hz)));
		CALLB_CPR_SAFE_END(&cpr, &mmp->mmp_thread_lock);
	}

	/* Outstanding writes are allowed to complete. */
	if (mmp->mmp_zio_root)
		zio_wait(mmp->mmp_zio_root);

	mmp->mmp_zio_root = NULL;
	mmp_thread_exit(mmp, &mmp->mmp_thread, &cpr);
}

/*
 * Signal the MMP thread to wake it, when it is sleeping on
 * its cv.  Used when some module parameter has changed and
 * we want the thread to know about it.
 * Only signal if the pool is active and mmp thread is
 * running, otherwise there is no thread to wake.
 */
static void
mmp_signal_thread(spa_t *spa)
{
	mmp_thread_t *mmp = &spa->spa_mmp;

	mutex_enter(&mmp->mmp_thread_lock);
	if (mmp->mmp_thread)
		cv_broadcast(&mmp->mmp_thread_cv);
	mutex_exit(&mmp->mmp_thread_lock);
}

void
mmp_signal_all_threads(void)
{
	spa_t *spa = NULL;

	mutex_enter(&spa_namespace_lock);
	while ((spa = spa_next(spa))) {
		if (spa->spa_state == POOL_STATE_ACTIVE)
			mmp_signal_thread(spa);
	}
	mutex_exit(&spa_namespace_lock);
}

#if defined(_KERNEL) && defined(HAVE_SPL)
#include <linux/mod_compat.h>

static int
param_set_multihost_interval(const char *val, zfs_kernel_param_t *kp)
{
	int ret;

	ret = param_set_ulong(val, kp);
	if (ret < 0)
		return (ret);

	mmp_signal_all_threads();

	return (ret);
}

/* BEGIN CSTYLED */
module_param(zfs_multihost_fail_intervals, uint, 0644);
MODULE_PARM_DESC(zfs_multihost_fail_intervals,
	"Max allowed period without a successful mmp write");

module_param_call(zfs_multihost_interval, param_set_multihost_interval,
    param_get_ulong, &zfs_multihost_interval, 0644);
MODULE_PARM_DESC(zfs_multihost_interval,
	"Milliseconds between mmp writes to each leaf");

module_param(zfs_multihost_import_intervals, uint, 0644);
MODULE_PARM_DESC(zfs_multihost_import_intervals,
	"Number of zfs_multihost_interval periods to wait for activity");
/* END CSTYLED */
#endif
