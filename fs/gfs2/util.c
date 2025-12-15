// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2006 Red Hat, Inc.  All rights reserved.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/spinlock.h>
#include <linux/completion.h>
#include <linux/buffer_head.h>
#include <linux/kthread.h>
#include <linux/crc32.h>
#include <linux/gfs2_ondisk.h>
#include <linux/delay.h>
#include <linux/uaccess.h>

#include "gfs2.h"
#include "incore.h"
#include "glock.h"
#include "glops.h"
#include "log.h"
#include "lops.h"
#include "recovery.h"
#include "rgrp.h"
#include "super.h"
#include "util.h"

struct kmem_cache *gfs2_glock_cachep __read_mostly;
struct kmem_cache *gfs2_glock_aspace_cachep __read_mostly;
struct kmem_cache *gfs2_inode_cachep __read_mostly;
struct kmem_cache *gfs2_bufdata_cachep __read_mostly;
struct kmem_cache *gfs2_rgrpd_cachep __read_mostly;
struct kmem_cache *gfs2_quotad_cachep __read_mostly;
struct kmem_cache *gfs2_qadata_cachep __read_mostly;
struct kmem_cache *gfs2_trans_cachep __read_mostly;
mempool_t *gfs2_page_pool __read_mostly;

void gfs2_assert_i(struct gfs2_sbd *sdp)
{
	fs_emerg(sdp, "fatal assertion failed\n");
}

/**
 * check_journal_clean - Make sure a journal is clean for a spectator mount
 * @sdp: The GFS2 superblock
 * @jd: The journal descriptor
 * @verbose: Show more prints in the log
 *
 * Returns: 0 if the journal is clean or locked, else an error
 */
int check_journal_clean(struct gfs2_sbd *sdp, struct gfs2_jdesc *jd,
			bool verbose)
{
	int error;
	struct gfs2_holder j_gh;
	struct gfs2_log_header_host head;
	struct gfs2_inode *ip;

	ip = GFS2_I(jd->jd_inode);
	error = gfs2_glock_nq_init(ip->i_gl, LM_ST_SHARED, LM_FLAG_RECOVER |
				   GL_EXACT | GL_NOCACHE, &j_gh);
	if (error) {
		if (verbose)
			fs_err(sdp, "Error %d locking journal for spectator "
			       "mount.\n", error);
		return -EPERM;
	}
	error = gfs2_jdesc_check(jd);
	if (error) {
		if (verbose)
			fs_err(sdp, "Error checking journal for spectator "
			       "mount.\n");
		goto out_unlock;
	}
	error = gfs2_find_jhead(jd, &head);
	if (error) {
		if (verbose)
			fs_err(sdp, "Error parsing journal for spectator "
			       "mount.\n");
		goto out_unlock;
	}
	if (!(head.lh_flags & GFS2_LOG_HEAD_UNMOUNT)) {
		error = -EPERM;
		if (verbose)
			fs_err(sdp, "jid=%u: Journal is dirty, so the first "
			       "mounter must not be a spectator.\n",
			       jd->jd_jid);
	}

out_unlock:
	gfs2_glock_dq_uninit(&j_gh);
	return error;
}

/**
 * gfs2_freeze_lock_shared - hold the freeze glock
 * @sdp: the superblock
 */
int gfs2_freeze_lock_shared(struct gfs2_sbd *sdp)
{
	int flags = LM_FLAG_RECOVER | GL_EXACT;
	int error;

	error = gfs2_glock_nq_init(sdp->sd_freeze_gl, LM_ST_SHARED, flags,
				   &sdp->sd_freeze_gh);
	if (error && error != GLR_TRYFAILED)
		fs_err(sdp, "can't lock the freeze glock: %d\n", error);
	return error;
}

void gfs2_freeze_unlock(struct gfs2_sbd *sdp)
{
	if (gfs2_holder_initialized(&sdp->sd_freeze_gh))
		gfs2_glock_dq_uninit(&sdp->sd_freeze_gh);
}

static void do_withdraw(struct gfs2_sbd *sdp)
{
	down_write(&sdp->sd_log_flush_lock);
	if (!test_bit(SDF_JOURNAL_LIVE, &sdp->sd_flags)) {
		up_write(&sdp->sd_log_flush_lock);
		return;
	}
	clear_bit(SDF_JOURNAL_LIVE, &sdp->sd_flags);
	up_write(&sdp->sd_log_flush_lock);

	gfs2_ail_drain(sdp); /* frees all transactions */

	wake_up(&sdp->sd_logd_waitq);
	wake_up(&sdp->sd_quota_wait);

	wait_event_timeout(sdp->sd_log_waitq,
			   gfs2_log_is_empty(sdp),
			   HZ * 5);

	sdp->sd_vfs->s_flags |= SB_RDONLY;

	/*
	 * Dequeue any pending non-system glock holders that can no
	 * longer be granted because the file system is withdrawn.
	 */
	gfs2_withdraw_glocks(sdp);
}

void gfs2_lm(struct gfs2_sbd *sdp, const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;

	if (sdp->sd_args.ar_errors == GFS2_ERRORS_WITHDRAW &&
	    test_bit(SDF_WITHDRAWN, &sdp->sd_flags))
		return;

	va_start(args, fmt);
	vaf.fmt = fmt;
	vaf.va = &args;
	fs_err(sdp, "%pV", &vaf);
	va_end(args);
}

/**
 * gfs2_offline_uevent - run gfs2_withdraw_helper
 * @sdp: The GFS2 superblock
 */
static bool gfs2_offline_uevent(struct gfs2_sbd *sdp)
{
	struct lm_lockstruct *ls = &sdp->sd_lockstruct;
	long timeout;

	/* Skip protocol "lock_nolock" which doesn't require shared storage. */
	if (!ls->ls_ops->lm_lock)
		return false;

	/*
	 * The gfs2_withdraw_helper replies by writing one of the following
	 * status codes to "/sys$DEVPATH/lock_module/withdraw":
	 *
	 * 0 - The shared block device has been marked inactive.  Future write
	 *     operations will fail.
	 *
	 * 1 - The shared block device may still be active and carry out
	 *     write operations.
	 *
	 * If the "offline" uevent isn't reacted upon in time, the event
	 * handler is assumed to have failed.
	 */

	sdp->sd_withdraw_helper_status = -1;
	kobject_uevent(&sdp->sd_kobj, KOBJ_OFFLINE);
	timeout = gfs2_tune_get(sdp, gt_withdraw_helper_timeout) * HZ;
	wait_for_completion_timeout(&sdp->sd_withdraw_helper, timeout);
	if (sdp->sd_withdraw_helper_status == -1) {
		fs_err(sdp, "%s timed out\n", "gfs2_withdraw_helper");
	} else {
		fs_err(sdp, "%s %s with status %d\n",
		       "gfs2_withdraw_helper",
		       sdp->sd_withdraw_helper_status == 0 ?
		       "succeeded" : "failed",
		       sdp->sd_withdraw_helper_status);
	}
	return sdp->sd_withdraw_helper_status == 0;
}

void gfs2_withdraw_func(struct work_struct *work)
{
	struct gfs2_sbd *sdp = container_of(work, struct gfs2_sbd, sd_withdraw_work);
	struct lm_lockstruct *ls = &sdp->sd_lockstruct;
	const struct lm_lockops *lm = ls->ls_ops;
	bool device_inactive;

	if (test_bit(SDF_KILL, &sdp->sd_flags))
		return;

	BUG_ON(sdp->sd_args.ar_debug);

	/*
	 * Try to deactivate the shared block device so that no more I/O will
	 * go through.  If successful, we can immediately trigger remote
	 * recovery.  Otherwise, we must first empty out all our local caches.
	 */

	device_inactive = gfs2_offline_uevent(sdp);

	if (sdp->sd_args.ar_errors == GFS2_ERRORS_DEACTIVATE && !device_inactive)
		panic("GFS2: fsid=%s: panic requested\n", sdp->sd_fsname);

	if (lm->lm_unmount) {
		if (device_inactive) {
			lm->lm_unmount(sdp, false);
			do_withdraw(sdp);
		} else {
			do_withdraw(sdp);
			lm->lm_unmount(sdp, false);
		}
	} else {
		do_withdraw(sdp);
	}

	fs_err(sdp, "file system withdrawn\n");
}

void gfs2_withdraw(struct gfs2_sbd *sdp)
{
	if (sdp->sd_args.ar_errors == GFS2_ERRORS_WITHDRAW ||
	    sdp->sd_args.ar_errors == GFS2_ERRORS_DEACTIVATE) {
		if (test_and_set_bit(SDF_WITHDRAWN, &sdp->sd_flags))
			return;

		dump_stack();
		/*
		 * There is no need to withdraw when the superblock hasn't been
		 * fully initialized, yet.
		 */
		if (!(sdp->sd_vfs->s_flags & SB_BORN))
			return;
		fs_err(sdp, "about to withdraw this file system\n");
		schedule_work(&sdp->sd_withdraw_work);
		return;
	}

	if (sdp->sd_args.ar_errors == GFS2_ERRORS_PANIC)
		panic("GFS2: fsid=%s: panic requested\n", sdp->sd_fsname);
}

/*
 * gfs2_assert_withdraw_i - Cause the machine to withdraw if @assertion is false
 */

void gfs2_assert_withdraw_i(struct gfs2_sbd *sdp, char *assertion,
			    const char *function, char *file, unsigned int line)
{
	if (gfs2_withdrawn(sdp))
		return;

	fs_err(sdp,
	       "fatal: assertion \"%s\" failed - "
	       "function = %s, file = %s, line = %u\n",
	       assertion, function, file, line);

	gfs2_withdraw(sdp);
	dump_stack();
}

/*
 * gfs2_assert_warn_i - Print a message to the console if @assertion is false
 */

void gfs2_assert_warn_i(struct gfs2_sbd *sdp, char *assertion,
			const char *function, char *file, unsigned int line)
{
	if (time_before(jiffies,
			sdp->sd_last_warning +
			gfs2_tune_get(sdp, gt_complain_secs) * HZ))
		return;

	if (sdp->sd_args.ar_errors == GFS2_ERRORS_WITHDRAW)
		fs_warn(sdp, "warning: assertion \"%s\" failed - "
			"function = %s, file = %s, line = %u\n",
			assertion, function, file, line);

	if (sdp->sd_args.ar_debug)
		BUG();
	else
		dump_stack();

	if (sdp->sd_args.ar_errors == GFS2_ERRORS_PANIC)
		panic("GFS2: fsid=%s: warning: assertion \"%s\" failed - "
		      "function = %s, file = %s, line = %u\n",
		      sdp->sd_fsname, assertion,
		      function, file, line);

	sdp->sd_last_warning = jiffies;
}

/*
 * gfs2_consist_i - Flag a filesystem consistency error and withdraw
 */

void gfs2_consist_i(struct gfs2_sbd *sdp, const char *function,
		    char *file, unsigned int line)
{
	gfs2_lm(sdp,
		"fatal: filesystem consistency error - "
		"function = %s, file = %s, line = %u\n",
		function, file, line);
	gfs2_withdraw(sdp);
}

/*
 * gfs2_consist_inode_i - Flag an inode consistency error and withdraw
 */

void gfs2_consist_inode_i(struct gfs2_inode *ip,
			  const char *function, char *file, unsigned int line)
{
	struct gfs2_sbd *sdp = GFS2_SB(&ip->i_inode);

	gfs2_lm(sdp,
		"fatal: filesystem consistency error - "
		"inode = %llu %llu, "
		"function = %s, file = %s, line = %u\n",
		(unsigned long long)ip->i_no_formal_ino,
		(unsigned long long)ip->i_no_addr,
		function, file, line);
	gfs2_dump_glock(NULL, ip->i_gl, 1);
	gfs2_withdraw(sdp);
}

/*
 * gfs2_consist_rgrpd_i - Flag a RG consistency error and withdraw
 */

void gfs2_consist_rgrpd_i(struct gfs2_rgrpd *rgd,
			  const char *function, char *file, unsigned int line)
{
	struct gfs2_sbd *sdp = rgd->rd_sbd;
	char fs_id_buf[sizeof(sdp->sd_fsname) + 7];

	sprintf(fs_id_buf, "fsid=%s: ", sdp->sd_fsname);
	gfs2_rgrp_dump(NULL, rgd, fs_id_buf);
	gfs2_lm(sdp,
		"fatal: filesystem consistency error - "
		"RG = %llu, "
		"function = %s, file = %s, line = %u\n",
		(unsigned long long)rgd->rd_addr,
		function, file, line);
	gfs2_dump_glock(NULL, rgd->rd_gl, 1);
	gfs2_withdraw(sdp);
}

/*
 * gfs2_meta_check_ii - Flag a magic number consistency error and withdraw
 */

void gfs2_meta_check_ii(struct gfs2_sbd *sdp, struct buffer_head *bh,
			const char *function, char *file,
			unsigned int line)
{
	gfs2_lm(sdp,
		"fatal: invalid metadata block - "
		"bh = %llu (bad magic number), "
		"function = %s, file = %s, line = %u\n",
		(unsigned long long)bh->b_blocknr,
		function, file, line);
	gfs2_withdraw(sdp);
}

/*
 * gfs2_metatype_check_ii - Flag a metadata type consistency error and withdraw
 */

void gfs2_metatype_check_ii(struct gfs2_sbd *sdp, struct buffer_head *bh,
			    u16 type, u16 t, const char *function,
			    char *file, unsigned int line)
{
	gfs2_lm(sdp,
		"fatal: invalid metadata block - "
		"bh = %llu (type: exp=%u, found=%u), "
		"function = %s, file = %s, line = %u\n",
		(unsigned long long)bh->b_blocknr, type, t,
		function, file, line);
	gfs2_withdraw(sdp);
}

/*
 * gfs2_io_error_i - Flag an I/O error and withdraw
 * Returns: -1 if this call withdrew the machine,
 *          0 if it was already withdrawn
 */

void gfs2_io_error_i(struct gfs2_sbd *sdp, const char *function, char *file,
		     unsigned int line)
{
	gfs2_lm(sdp,
		"fatal: I/O error - "
		"function = %s, file = %s, line = %u\n",
		function, file, line);
	gfs2_withdraw(sdp);
}

/*
 * gfs2_io_error_bh_i - Flag a buffer I/O error and withdraw
 */

void gfs2_io_error_bh_i(struct gfs2_sbd *sdp, struct buffer_head *bh,
			const char *function, char *file, unsigned int line)
{
	if (gfs2_withdrawn(sdp))
		return;

	fs_err(sdp, "fatal: I/O error - "
	       "block = %llu, "
	       "function = %s, file = %s, line = %u\n",
	       (unsigned long long)bh->b_blocknr, function, file, line);
	gfs2_withdraw(sdp);
}
