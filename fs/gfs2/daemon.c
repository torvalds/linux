/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2006 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License version 2.
 */

#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/completion.h>
#include <linux/buffer_head.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/gfs2_ondisk.h>
#include <linux/lm_interface.h>
#include <linux/freezer.h>

#include "gfs2.h"
#include "incore.h"
#include "daemon.h"
#include "glock.h"
#include "log.h"
#include "quota.h"
#include "recovery.h"
#include "super.h"
#include "util.h"

/* This uses schedule_timeout() instead of msleep() because it's good for
   the daemons to wake up more often than the timeout when unmounting so
   the user's unmount doesn't sit there forever.

   The kthread functions used to start these daemons block and flush signals. */

/**
 * gfs2_glockd - Reclaim unused glock structures
 * @sdp: Pointer to GFS2 superblock
 *
 * One or more of these daemons run, reclaiming glocks on sd_reclaim_list.
 * Number of daemons can be set by user, with num_glockd mount option.
 */

int gfs2_glockd(void *data)
{
	struct gfs2_sbd *sdp = data;

	while (!kthread_should_stop()) {
		while (atomic_read(&sdp->sd_reclaim_count))
			gfs2_reclaim_glock(sdp);

		wait_event_interruptible(sdp->sd_reclaim_wq,
					 (atomic_read(&sdp->sd_reclaim_count) ||
					 kthread_should_stop()));
		if (freezing(current))
			refrigerator();
	}

	return 0;
}

/**
 * gfs2_recoverd - Recover dead machine's journals
 * @sdp: Pointer to GFS2 superblock
 *
 */

int gfs2_recoverd(void *data)
{
	struct gfs2_sbd *sdp = data;
	unsigned long t;

	while (!kthread_should_stop()) {
		gfs2_check_journals(sdp);
		t = gfs2_tune_get(sdp,  gt_recoverd_secs) * HZ;
		if (freezing(current))
			refrigerator();
		schedule_timeout_interruptible(t);
	}

	return 0;
}

/**
 * gfs2_quotad - Write cached quota changes into the quota file
 * @sdp: Pointer to GFS2 superblock
 *
 */

int gfs2_quotad(void *data)
{
	struct gfs2_sbd *sdp = data;
	unsigned long t;
	int error;

	while (!kthread_should_stop()) {
		/* Update the master statfs file */

		t = sdp->sd_statfs_sync_time +
		    gfs2_tune_get(sdp, gt_statfs_quantum) * HZ;

		if (time_after_eq(jiffies, t)) {
			error = gfs2_statfs_sync(sdp);
			if (error &&
			    error != -EROFS &&
			    !test_bit(SDF_SHUTDOWN, &sdp->sd_flags))
				fs_err(sdp, "quotad: (1) error=%d\n", error);
			sdp->sd_statfs_sync_time = jiffies;
		}

		/* Update quota file */

		t = sdp->sd_quota_sync_time +
		    gfs2_tune_get(sdp, gt_quota_quantum) * HZ;

		if (time_after_eq(jiffies, t)) {
			error = gfs2_quota_sync(sdp);
			if (error &&
			    error != -EROFS &&
			    !test_bit(SDF_SHUTDOWN, &sdp->sd_flags))
				fs_err(sdp, "quotad: (2) error=%d\n", error);
			sdp->sd_quota_sync_time = jiffies;
		}

		gfs2_quota_scan(sdp);

		t = gfs2_tune_get(sdp, gt_quotad_secs) * HZ;
		if (freezing(current))
			refrigerator();
		schedule_timeout_interruptible(t);
	}

	return 0;
}

