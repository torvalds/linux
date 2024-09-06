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
	error = gfs2_glock_nq_init(ip->i_gl, LM_ST_SHARED, LM_FLAG_NOEXP |
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
	error = gfs2_find_jhead(jd, &head, false);
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
	int flags = LM_FLAG_NOEXP | GL_EXACT;
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

static void signal_our_withdraw(struct gfs2_sbd *sdp)
{
	struct gfs2_glock *live_gl = sdp->sd_live_gh.gh_gl;
	struct inode *inode;
	struct gfs2_inode *ip;
	struct gfs2_glock *i_gl;
	u64 no_formal_ino;
	int ret = 0;
	int tries;

	if (test_bit(SDF_NORECOVERY, &sdp->sd_flags) || !sdp->sd_jdesc)
		return;

	gfs2_ail_drain(sdp); /* frees all transactions */
	inode = sdp->sd_jdesc->jd_inode;
	ip = GFS2_I(inode);
	i_gl = ip->i_gl;
	no_formal_ino = ip->i_no_formal_ino;

	/* Prevent any glock dq until withdraw recovery is complete */
	set_bit(SDF_WITHDRAW_RECOVERY, &sdp->sd_flags);
	/*
	 * Don't tell dlm we're bailing until we have no more buffers in the
	 * wind. If journal had an IO error, the log code should just purge
	 * the outstanding buffers rather than submitting new IO. Making the
	 * file system read-only will flush the journal, etc.
	 *
	 * During a normal unmount, gfs2_make_fs_ro calls gfs2_log_shutdown
	 * which clears SDF_JOURNAL_LIVE. In a withdraw, we must not write
	 * any UNMOUNT log header, so we can't call gfs2_log_shutdown, and
	 * therefore we need to clear SDF_JOURNAL_LIVE manually.
	 */
	clear_bit(SDF_JOURNAL_LIVE, &sdp->sd_flags);
	if (!sb_rdonly(sdp->sd_vfs)) {
		bool locked = mutex_trylock(&sdp->sd_freeze_mutex);

		wake_up(&sdp->sd_logd_waitq);
		wake_up(&sdp->sd_quota_wait);

		wait_event_timeout(sdp->sd_log_waitq,
				   gfs2_log_is_empty(sdp),
				   HZ * 5);

		sdp->sd_vfs->s_flags |= SB_RDONLY;

		if (locked)
			mutex_unlock(&sdp->sd_freeze_mutex);

		/*
		 * Dequeue any pending non-system glock holders that can no
		 * longer be granted because the file system is withdrawn.
		 */
		gfs2_gl_dq_holders(sdp);
	}

	if (sdp->sd_lockstruct.ls_ops->lm_lock == NULL) { /* lock_nolock */
		if (!ret)
			ret = -EIO;
		clear_bit(SDF_WITHDRAW_RECOVERY, &sdp->sd_flags);
		goto skip_recovery;
	}
	/*
	 * Drop the glock for our journal so another node can recover it.
	 */
	if (gfs2_holder_initialized(&sdp->sd_journal_gh)) {
		gfs2_glock_dq_wait(&sdp->sd_journal_gh);
		gfs2_holder_uninit(&sdp->sd_journal_gh);
	}
	sdp->sd_jinode_gh.gh_flags |= GL_NOCACHE;
	gfs2_glock_dq(&sdp->sd_jinode_gh);
	gfs2_thaw_freeze_initiator(sdp->sd_vfs);
	wait_on_bit(&i_gl->gl_flags, GLF_DEMOTE, TASK_UNINTERRUPTIBLE);

	/*
	 * holder_uninit to force glock_put, to force dlm to let go
	 */
	gfs2_holder_uninit(&sdp->sd_jinode_gh);

	/*
	 * Note: We need to be careful here:
	 * Our iput of jd_inode will evict it. The evict will dequeue its
	 * glock, but the glock dq will wait for the withdraw unless we have
	 * exception code in glock_dq.
	 */
	iput(inode);
	sdp->sd_jdesc->jd_inode = NULL;
	/*
	 * Wait until the journal inode's glock is freed. This allows try locks
	 * on other nodes to be successful, otherwise we remain the owner of
	 * the glock as far as dlm is concerned.
	 */
	if (i_gl->gl_ops->go_unlocked) {
		set_bit(GLF_UNLOCKED, &i_gl->gl_flags);
		wait_on_bit(&i_gl->gl_flags, GLF_UNLOCKED, TASK_UNINTERRUPTIBLE);
	}

	/*
	 * Dequeue the "live" glock, but keep a reference so it's never freed.
	 */
	gfs2_glock_hold(live_gl);
	gfs2_glock_dq_wait(&sdp->sd_live_gh);
	/*
	 * We enqueue the "live" glock in EX so that all other nodes
	 * get a demote request and act on it. We don't really want the
	 * lock in EX, so we send a "try" lock with 1CB to produce a callback.
	 */
	fs_warn(sdp, "Requesting recovery of jid %d.\n",
		sdp->sd_lockstruct.ls_jid);
	gfs2_holder_reinit(LM_ST_EXCLUSIVE,
			   LM_FLAG_TRY_1CB | LM_FLAG_NOEXP | GL_NOPID,
			   &sdp->sd_live_gh);
	msleep(GL_GLOCK_MAX_HOLD);
	/*
	 * This will likely fail in a cluster, but succeed standalone:
	 */
	ret = gfs2_glock_nq(&sdp->sd_live_gh);

	/*
	 * If we actually got the "live" lock in EX mode, there are no other
	 * nodes available to replay our journal. So we try to replay it
	 * ourselves. We hold the "live" glock to prevent other mounters
	 * during recovery, then just dequeue it and reacquire it in our
	 * normal SH mode. Just in case the problem that caused us to
	 * withdraw prevents us from recovering our journal (e.g. io errors
	 * and such) we still check if the journal is clean before proceeding
	 * but we may wait forever until another mounter does the recovery.
	 */
	if (ret == 0) {
		fs_warn(sdp, "No other mounters found. Trying to recover our "
			"own journal jid %d.\n", sdp->sd_lockstruct.ls_jid);
		if (gfs2_recover_journal(sdp->sd_jdesc, 1))
			fs_warn(sdp, "Unable to recover our journal jid %d.\n",
				sdp->sd_lockstruct.ls_jid);
		gfs2_glock_dq_wait(&sdp->sd_live_gh);
		gfs2_holder_reinit(LM_ST_SHARED,
				   LM_FLAG_NOEXP | GL_EXACT | GL_NOPID,
				   &sdp->sd_live_gh);
		gfs2_glock_nq(&sdp->sd_live_gh);
	}

	gfs2_glock_put(live_gl); /* drop extra reference we acquired */
	clear_bit(SDF_WITHDRAW_RECOVERY, &sdp->sd_flags);

	/*
	 * At this point our journal is evicted, so we need to get a new inode
	 * for it. Once done, we need to call gfs2_find_jhead which
	 * calls gfs2_map_journal_extents to map it for us again.
	 *
	 * Note that we don't really want it to look up a FREE block. The
	 * GFS2_BLKST_FREE simply overrides a block check in gfs2_inode_lookup
	 * which would otherwise fail because it requires grabbing an rgrp
	 * glock, which would fail with -EIO because we're withdrawing.
	 */
	inode = gfs2_inode_lookup(sdp->sd_vfs, DT_UNKNOWN,
				  sdp->sd_jdesc->jd_no_addr, no_formal_ino,
				  GFS2_BLKST_FREE);
	if (IS_ERR(inode)) {
		fs_warn(sdp, "Reprocessing of jid %d failed with %ld.\n",
			sdp->sd_lockstruct.ls_jid, PTR_ERR(inode));
		goto skip_recovery;
	}
	sdp->sd_jdesc->jd_inode = inode;
	d_mark_dontcache(inode);

	/*
	 * Now wait until recovery is complete.
	 */
	for (tries = 0; tries < 10; tries++) {
		ret = check_journal_clean(sdp, sdp->sd_jdesc, false);
		if (!ret)
			break;
		msleep(HZ);
		fs_warn(sdp, "Waiting for journal recovery jid %d.\n",
			sdp->sd_lockstruct.ls_jid);
	}
skip_recovery:
	if (!ret)
		fs_warn(sdp, "Journal recovery complete for jid %d.\n",
			sdp->sd_lockstruct.ls_jid);
	else
		fs_warn(sdp, "Journal recovery skipped for jid %d until next "
			"mount.\n", sdp->sd_lockstruct.ls_jid);
	fs_warn(sdp, "Glock dequeues delayed: %lu\n", sdp->sd_glock_dqs_held);
	sdp->sd_glock_dqs_held = 0;
	wake_up_bit(&sdp->sd_flags, SDF_WITHDRAW_RECOVERY);
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

int gfs2_withdraw(struct gfs2_sbd *sdp)
{
	struct lm_lockstruct *ls = &sdp->sd_lockstruct;
	const struct lm_lockops *lm = ls->ls_ops;

	if (sdp->sd_args.ar_errors == GFS2_ERRORS_WITHDRAW) {
		unsigned long old = READ_ONCE(sdp->sd_flags), new;

		do {
			if (old & BIT(SDF_WITHDRAWN)) {
				wait_on_bit(&sdp->sd_flags,
					    SDF_WITHDRAW_IN_PROG,
					    TASK_UNINTERRUPTIBLE);
				return -1;
			}
			new = old | BIT(SDF_WITHDRAWN) | BIT(SDF_WITHDRAW_IN_PROG);
		} while (unlikely(!try_cmpxchg(&sdp->sd_flags, &old, new)));

		fs_err(sdp, "about to withdraw this file system\n");
		BUG_ON(sdp->sd_args.ar_debug);

		signal_our_withdraw(sdp);

		kobject_uevent(&sdp->sd_kobj, KOBJ_OFFLINE);

		if (!strcmp(sdp->sd_lockstruct.ls_ops->lm_proto_name, "lock_dlm"))
			wait_for_completion(&sdp->sd_wdack);

		if (lm->lm_unmount) {
			fs_err(sdp, "telling LM to unmount\n");
			lm->lm_unmount(sdp);
		}
		fs_err(sdp, "File system withdrawn\n");
		dump_stack();
		clear_bit(SDF_WITHDRAW_IN_PROG, &sdp->sd_flags);
		smp_mb__after_atomic();
		wake_up_bit(&sdp->sd_flags, SDF_WITHDRAW_IN_PROG);
	}

	if (sdp->sd_args.ar_errors == GFS2_ERRORS_PANIC)
		panic("GFS2: fsid=%s: panic requested\n", sdp->sd_fsname);

	return -1;
}

/*
 * gfs2_assert_withdraw_i - Cause the machine to withdraw if @assertion is false
 */

void gfs2_assert_withdraw_i(struct gfs2_sbd *sdp, char *assertion,
			    const char *function, char *file, unsigned int line,
			    bool delayed)
{
	if (gfs2_withdrawing_or_withdrawn(sdp))
		return;

	fs_err(sdp,
	       "fatal: assertion \"%s\" failed - "
	       "function = %s, file = %s, line = %u\n",
	       assertion, function, file, line);

	/*
	 * If errors=panic was specified on mount, it won't help to delay the
	 * withdraw.
	 */
	if (sdp->sd_args.ar_errors == GFS2_ERRORS_PANIC)
		delayed = false;

	if (delayed)
		gfs2_withdraw_delayed(sdp);
	else
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
 * Returns: -1 if this call withdrew the machine,
 *          -2 if it was already withdrawn
 */

int gfs2_meta_check_ii(struct gfs2_sbd *sdp, struct buffer_head *bh,
		       const char *function, char *file,
		       unsigned int line)
{
	int me;

	gfs2_lm(sdp,
		"fatal: invalid metadata block - "
		"bh = %llu (bad magic number), "
		"function = %s, file = %s, line = %u\n",
		(unsigned long long)bh->b_blocknr,
		function, file, line);
	me = gfs2_withdraw(sdp);
	return (me) ? -1 : -2;
}

/*
 * gfs2_metatype_check_ii - Flag a metadata type consistency error and withdraw
 * Returns: -1 if this call withdrew the machine,
 *          -2 if it was already withdrawn
 */

int gfs2_metatype_check_ii(struct gfs2_sbd *sdp, struct buffer_head *bh,
			   u16 type, u16 t, const char *function,
			   char *file, unsigned int line)
{
	int me;

	gfs2_lm(sdp,
		"fatal: invalid metadata block - "
		"bh = %llu (type: exp=%u, found=%u), "
		"function = %s, file = %s, line = %u\n",
		(unsigned long long)bh->b_blocknr, type, t,
		function, file, line);
	me = gfs2_withdraw(sdp);
	return (me) ? -1 : -2;
}

/*
 * gfs2_io_error_i - Flag an I/O error and withdraw
 * Returns: -1 if this call withdrew the machine,
 *          0 if it was already withdrawn
 */

int gfs2_io_error_i(struct gfs2_sbd *sdp, const char *function, char *file,
		    unsigned int line)
{
	gfs2_lm(sdp,
		"fatal: I/O error - "
		"function = %s, file = %s, line = %u\n",
		function, file, line);
	return gfs2_withdraw(sdp);
}

/*
 * gfs2_io_error_bh_i - Flag a buffer I/O error
 * @withdraw: withdraw the filesystem
 */

void gfs2_io_error_bh_i(struct gfs2_sbd *sdp, struct buffer_head *bh,
			const char *function, char *file, unsigned int line,
			bool withdraw)
{
	if (gfs2_withdrawing_or_withdrawn(sdp))
		return;

	fs_err(sdp, "fatal: I/O error - "
	       "block = %llu, "
	       "function = %s, file = %s, line = %u\n",
	       (unsigned long long)bh->b_blocknr, function, file, line);
	if (withdraw)
		gfs2_withdraw(sdp);
}

