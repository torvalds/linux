/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2005 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License v.2.
 */

#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/completion.h>
#include <linux/buffer_head.h>
#include <asm/semaphore.h>

#include "gfs2.h"
#include "bmap.h"
#include "glock.h"
#include "glops.h"
#include "inode.h"
#include "log.h"
#include "meta_io.h"
#include "page.h"
#include "recovery.h"
#include "rgrp.h"

/**
 * meta_go_sync - sync out the metadata for this glock
 * @gl: the glock
 * @flags: DIO_*
 *
 * Called when demoting or unlocking an EX glock.  We must flush
 * to disk all dirty buffers/pages relating to this glock, and must not
 * not return to caller to demote/unlock the glock until I/O is complete.
 */

static void meta_go_sync(struct gfs2_glock *gl, int flags)
{
	if (!(flags & DIO_METADATA))
		return;

	if (test_and_clear_bit(GLF_DIRTY, &gl->gl_flags)) {
		gfs2_log_flush_glock(gl);
		gfs2_meta_sync(gl, flags | DIO_START | DIO_WAIT);
		if (flags & DIO_RELEASE)
			gfs2_ail_empty_gl(gl);
	}

	clear_bit(GLF_SYNC, &gl->gl_flags);
}

/**
 * meta_go_inval - invalidate the metadata for this glock
 * @gl: the glock
 * @flags:
 *
 */

static void meta_go_inval(struct gfs2_glock *gl, int flags)
{
	if (!(flags & DIO_METADATA))
		return;

	gfs2_meta_inval(gl);
	gl->gl_vn++;
}

/**
 * meta_go_demote_ok - Check to see if it's ok to unlock a glock
 * @gl: the glock
 *
 * Returns: 1 if we have no cached data; ok to demote meta glock
 */

static int meta_go_demote_ok(struct gfs2_glock *gl)
{
	return !gl->gl_aspace->i_mapping->nrpages;
}

/**
 * inode_go_xmote_th - promote/demote a glock
 * @gl: the glock
 * @state: the requested state
 * @flags:
 *
 */

static void inode_go_xmote_th(struct gfs2_glock *gl, unsigned int state,
			      int flags)
{
	if (gl->gl_state != LM_ST_UNLOCKED)
		gfs2_pte_inval(gl);
	gfs2_glock_xmote_th(gl, state, flags);
}

/**
 * inode_go_xmote_bh - After promoting/demoting a glock
 * @gl: the glock
 *
 */

static void inode_go_xmote_bh(struct gfs2_glock *gl)
{
	struct gfs2_holder *gh = gl->gl_req_gh;
	struct buffer_head *bh;
	int error;

	if (gl->gl_state != LM_ST_UNLOCKED &&
	    (!gh || !(gh->gh_flags & GL_SKIP))) {
		error = gfs2_meta_read(gl, gl->gl_name.ln_number, DIO_START,
				       &bh);
		if (!error)
			brelse(bh);
	}
}

/**
 * inode_go_drop_th - unlock a glock
 * @gl: the glock
 *
 * Invoked from rq_demote().
 * Another node needs the lock in EXCLUSIVE mode, or lock (unused for too long)
 * is being purged from our node's glock cache; we're dropping lock.
 */

static void inode_go_drop_th(struct gfs2_glock *gl)
{
	gfs2_pte_inval(gl);
	gfs2_glock_drop_th(gl);
}

/**
 * inode_go_sync - Sync the dirty data and/or metadata for an inode glock
 * @gl: the glock protecting the inode
 * @flags:
 *
 */

static void inode_go_sync(struct gfs2_glock *gl, int flags)
{
	int meta = (flags & DIO_METADATA);
	int data = (flags & DIO_DATA);

	if (test_bit(GLF_DIRTY, &gl->gl_flags)) {
		if (meta && data) {
			gfs2_page_sync(gl, flags | DIO_START);
			gfs2_log_flush_glock(gl);
			gfs2_meta_sync(gl, flags | DIO_START | DIO_WAIT);
			gfs2_page_sync(gl, flags | DIO_WAIT);
			clear_bit(GLF_DIRTY, &gl->gl_flags);
		} else if (meta) {
			gfs2_log_flush_glock(gl);
			gfs2_meta_sync(gl, flags | DIO_START | DIO_WAIT);
		} else if (data)
			gfs2_page_sync(gl, flags | DIO_START | DIO_WAIT);
		if (flags & DIO_RELEASE)
			gfs2_ail_empty_gl(gl);
	}

	clear_bit(GLF_SYNC, &gl->gl_flags);
}

/**
 * inode_go_inval - prepare a inode glock to be released
 * @gl: the glock
 * @flags:
 *
 */

static void inode_go_inval(struct gfs2_glock *gl, int flags)
{
	int meta = (flags & DIO_METADATA);
	int data = (flags & DIO_DATA);

	if (meta) {
		gfs2_meta_inval(gl);
		gl->gl_vn++;
	}
	if (data)
		gfs2_page_inval(gl);
}

/**
 * inode_go_demote_ok - Check to see if it's ok to unlock an inode glock
 * @gl: the glock
 *
 * Returns: 1 if it's ok
 */

static int inode_go_demote_ok(struct gfs2_glock *gl)
{
	struct gfs2_sbd *sdp = gl->gl_sbd;
	int demote = 0;

	if (!get_gl2ip(gl) && !gl->gl_aspace->i_mapping->nrpages)
		demote = 1;
	else if (!sdp->sd_args.ar_localcaching &&
		 time_after_eq(jiffies, gl->gl_stamp +
			       gfs2_tune_get(sdp, gt_demote_secs) * HZ))
		demote = 1;

	return demote;
}

/**
 * inode_go_lock - operation done after an inode lock is locked by a process
 * @gl: the glock
 * @flags:
 *
 * Returns: errno
 */

static int inode_go_lock(struct gfs2_holder *gh)
{
	struct gfs2_glock *gl = gh->gh_gl;
	struct gfs2_inode *ip = get_gl2ip(gl);
	int error = 0;

	if (!ip)
		return 0;

	if (ip->i_vn != gl->gl_vn) {
		error = gfs2_inode_refresh(ip);
		if (error)
			return error;
		gfs2_inode_attr_in(ip);
	}

	if ((ip->i_di.di_flags & GFS2_DIF_TRUNC_IN_PROG) &&
	    (gl->gl_state == LM_ST_EXCLUSIVE) &&
	    (gh->gh_flags & GL_LOCAL_EXCL))
		error = gfs2_truncatei_resume(ip);

	return error;
}

/**
 * inode_go_unlock - operation done before an inode lock is unlocked by a
 *		     process
 * @gl: the glock
 * @flags:
 *
 */

static void inode_go_unlock(struct gfs2_holder *gh)
{
	struct gfs2_glock *gl = gh->gh_gl;
	struct gfs2_inode *ip = get_gl2ip(gl);

	if (ip && test_bit(GLF_DIRTY, &gl->gl_flags))
		gfs2_inode_attr_in(ip);

	if (ip)
		gfs2_meta_cache_flush(ip);
}

/**
 * inode_greedy -
 * @gl: the glock
 *
 */

static void inode_greedy(struct gfs2_glock *gl)
{
	struct gfs2_sbd *sdp = gl->gl_sbd;
	struct gfs2_inode *ip = get_gl2ip(gl);
	unsigned int quantum = gfs2_tune_get(sdp, gt_greedy_quantum);
	unsigned int max = gfs2_tune_get(sdp, gt_greedy_max);
	unsigned int new_time;

	spin_lock(&ip->i_spin);

	if (time_after(ip->i_last_pfault + quantum, jiffies)) {
		new_time = ip->i_greedy + quantum;
		if (new_time > max)
			new_time = max;
	} else {
		new_time = ip->i_greedy - quantum;
		if (!new_time || new_time > max)
			new_time = 1;
	}

	ip->i_greedy = new_time;

	spin_unlock(&ip->i_spin);

	gfs2_inode_put(ip);
}

/**
 * rgrp_go_demote_ok - Check to see if it's ok to unlock a RG's glock
 * @gl: the glock
 *
 * Returns: 1 if it's ok
 */

static int rgrp_go_demote_ok(struct gfs2_glock *gl)
{
	return !gl->gl_aspace->i_mapping->nrpages;
}

/**
 * rgrp_go_lock - operation done after an rgrp lock is locked by
 *    a first holder on this node.
 * @gl: the glock
 * @flags:
 *
 * Returns: errno
 */

static int rgrp_go_lock(struct gfs2_holder *gh)
{
	return gfs2_rgrp_bh_get(get_gl2rgd(gh->gh_gl));
}

/**
 * rgrp_go_unlock - operation done before an rgrp lock is unlocked by
 *    a last holder on this node.
 * @gl: the glock
 * @flags:
 *
 */

static void rgrp_go_unlock(struct gfs2_holder *gh)
{
	gfs2_rgrp_bh_put(get_gl2rgd(gh->gh_gl));
}

/**
 * trans_go_xmote_th - promote/demote the transaction glock
 * @gl: the glock
 * @state: the requested state
 * @flags:
 *
 */

static void trans_go_xmote_th(struct gfs2_glock *gl, unsigned int state,
			      int flags)
{
	struct gfs2_sbd *sdp = gl->gl_sbd;

	if (gl->gl_state != LM_ST_UNLOCKED &&
	    test_bit(SDF_JOURNAL_LIVE, &sdp->sd_flags)) {
		gfs2_meta_syncfs(sdp);
		gfs2_log_shutdown(sdp);
	}

	gfs2_glock_xmote_th(gl, state, flags);
}

/**
 * trans_go_xmote_bh - After promoting/demoting the transaction glock
 * @gl: the glock
 *
 */

static void trans_go_xmote_bh(struct gfs2_glock *gl)
{
	struct gfs2_sbd *sdp = gl->gl_sbd;
	struct gfs2_glock *j_gl = sdp->sd_jdesc->jd_inode->i_gl;
	struct gfs2_log_header head;
	int error;

	if (gl->gl_state != LM_ST_UNLOCKED &&
	    test_bit(SDF_JOURNAL_LIVE, &sdp->sd_flags)) {
		gfs2_meta_cache_flush(sdp->sd_jdesc->jd_inode);
		j_gl->gl_ops->go_inval(j_gl, DIO_METADATA | DIO_DATA);

		error = gfs2_find_jhead(sdp->sd_jdesc, &head);
		if (error)
			gfs2_consist(sdp);
		if (!(head.lh_flags & GFS2_LOG_HEAD_UNMOUNT))
			gfs2_consist(sdp);

		/*  Initialize some head of the log stuff  */
		if (!test_bit(SDF_SHUTDOWN, &sdp->sd_flags)) {
			sdp->sd_log_sequence = head.lh_sequence + 1;
			gfs2_log_pointers_init(sdp, head.lh_blkno);
		}
	}
}

/**
 * trans_go_drop_th - unlock the transaction glock
 * @gl: the glock
 *
 * We want to sync the device even with localcaching.  Remember
 * that localcaching journal replay only marks buffers dirty.
 */

static void trans_go_drop_th(struct gfs2_glock *gl)
{
	struct gfs2_sbd *sdp = gl->gl_sbd;

	if (test_bit(SDF_JOURNAL_LIVE, &sdp->sd_flags)) {
		gfs2_meta_syncfs(sdp);
		gfs2_log_shutdown(sdp);
	}

	gfs2_glock_drop_th(gl);
}

/**
 * quota_go_demote_ok - Check to see if it's ok to unlock a quota glock
 * @gl: the glock
 *
 * Returns: 1 if it's ok
 */

static int quota_go_demote_ok(struct gfs2_glock *gl)
{
	return !atomic_read(&gl->gl_lvb_count);
}

struct gfs2_glock_operations gfs2_meta_glops = {
	.go_xmote_th = gfs2_glock_xmote_th,
	.go_drop_th = gfs2_glock_drop_th,
	.go_sync = meta_go_sync,
	.go_inval = meta_go_inval,
	.go_demote_ok = meta_go_demote_ok,
	.go_type = LM_TYPE_META
};

struct gfs2_glock_operations gfs2_inode_glops = {
	.go_xmote_th = inode_go_xmote_th,
	.go_xmote_bh = inode_go_xmote_bh,
	.go_drop_th = inode_go_drop_th,
	.go_sync = inode_go_sync,
	.go_inval = inode_go_inval,
	.go_demote_ok = inode_go_demote_ok,
	.go_lock = inode_go_lock,
	.go_unlock = inode_go_unlock,
	.go_greedy = inode_greedy,
	.go_type = LM_TYPE_INODE
};

struct gfs2_glock_operations gfs2_rgrp_glops = {
	.go_xmote_th = gfs2_glock_xmote_th,
	.go_drop_th = gfs2_glock_drop_th,
	.go_sync = meta_go_sync,
	.go_inval = meta_go_inval,
	.go_demote_ok = rgrp_go_demote_ok,
	.go_lock = rgrp_go_lock,
	.go_unlock = rgrp_go_unlock,
	.go_type = LM_TYPE_RGRP
};

struct gfs2_glock_operations gfs2_trans_glops = {
	.go_xmote_th = trans_go_xmote_th,
	.go_xmote_bh = trans_go_xmote_bh,
	.go_drop_th = trans_go_drop_th,
	.go_type = LM_TYPE_NONDISK
};

struct gfs2_glock_operations gfs2_iopen_glops = {
	.go_xmote_th = gfs2_glock_xmote_th,
	.go_drop_th = gfs2_glock_drop_th,
	.go_callback = gfs2_iopen_go_callback,
	.go_type = LM_TYPE_IOPEN
};

struct gfs2_glock_operations gfs2_flock_glops = {
	.go_xmote_th = gfs2_glock_xmote_th,
	.go_drop_th = gfs2_glock_drop_th,
	.go_type = LM_TYPE_FLOCK
};

struct gfs2_glock_operations gfs2_nondisk_glops = {
	.go_xmote_th = gfs2_glock_xmote_th,
	.go_drop_th = gfs2_glock_drop_th,
	.go_type = LM_TYPE_NONDISK
};

struct gfs2_glock_operations gfs2_quota_glops = {
	.go_xmote_th = gfs2_glock_xmote_th,
	.go_drop_th = gfs2_glock_drop_th,
	.go_demote_ok = quota_go_demote_ok,
	.go_type = LM_TYPE_QUOTA
};

struct gfs2_glock_operations gfs2_journal_glops = {
	.go_xmote_th = gfs2_glock_xmote_th,
	.go_drop_th = gfs2_glock_drop_th,
	.go_type = LM_TYPE_JOURNAL
};

