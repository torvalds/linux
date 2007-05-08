/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2006 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License version 2.
 */

#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/completion.h>
#include <linux/buffer_head.h>
#include <linux/gfs2_ondisk.h>
#include <linux/lm_interface.h>

#include "gfs2.h"
#include "incore.h"
#include "bmap.h"
#include "glock.h"
#include "glops.h"
#include "inode.h"
#include "log.h"
#include "meta_io.h"
#include "recovery.h"
#include "rgrp.h"
#include "util.h"
#include "trans.h"

/**
 * ail_empty_gl - remove all buffers for a given lock from the AIL
 * @gl: the glock
 *
 * None of the buffers should be dirty, locked, or pinned.
 */

static void gfs2_ail_empty_gl(struct gfs2_glock *gl)
{
	struct gfs2_sbd *sdp = gl->gl_sbd;
	unsigned int blocks;
	struct list_head *head = &gl->gl_ail_list;
	struct gfs2_bufdata *bd;
	struct buffer_head *bh;
	u64 blkno;
	int error;

	blocks = atomic_read(&gl->gl_ail_count);
	if (!blocks)
		return;

	error = gfs2_trans_begin(sdp, 0, blocks);
	if (gfs2_assert_withdraw(sdp, !error))
		return;

	gfs2_log_lock(sdp);
	while (!list_empty(head)) {
		bd = list_entry(head->next, struct gfs2_bufdata,
				bd_ail_gl_list);
		bh = bd->bd_bh;
		blkno = bh->b_blocknr;
		gfs2_assert_withdraw(sdp, !buffer_busy(bh));

		bd->bd_ail = NULL;
		list_del(&bd->bd_ail_st_list);
		list_del(&bd->bd_ail_gl_list);
		atomic_dec(&gl->gl_ail_count);
		brelse(bh);
		gfs2_log_unlock(sdp);

		gfs2_trans_add_revoke(sdp, blkno);

		gfs2_log_lock(sdp);
	}
	gfs2_assert_withdraw(sdp, !atomic_read(&gl->gl_ail_count));
	gfs2_log_unlock(sdp);

	gfs2_trans_end(sdp);
	gfs2_log_flush(sdp, NULL);
}

/**
 * gfs2_pte_inval - Sync and invalidate all PTEs associated with a glock
 * @gl: the glock
 *
 */

static void gfs2_pte_inval(struct gfs2_glock *gl)
{
	struct gfs2_inode *ip;
	struct inode *inode;

	ip = gl->gl_object;
	inode = &ip->i_inode;
	if (!ip || !S_ISREG(inode->i_mode))
		return;

	if (!test_bit(GIF_PAGED, &ip->i_flags))
		return;

	unmap_shared_mapping_range(inode->i_mapping, 0, 0);

	if (test_bit(GIF_SW_PAGED, &ip->i_flags))
		set_bit(GLF_DIRTY, &gl->gl_flags);

	clear_bit(GIF_SW_PAGED, &ip->i_flags);
}

/**
 * meta_go_sync - sync out the metadata for this glock
 * @gl: the glock
 *
 * Called when demoting or unlocking an EX glock.  We must flush
 * to disk all dirty buffers/pages relating to this glock, and must not
 * not return to caller to demote/unlock the glock until I/O is complete.
 */

static void meta_go_sync(struct gfs2_glock *gl)
{
	if (gl->gl_state != LM_ST_EXCLUSIVE)
		return;

	if (test_and_clear_bit(GLF_DIRTY, &gl->gl_flags)) {
		gfs2_log_flush(gl->gl_sbd, gl);
		gfs2_meta_sync(gl);
		gfs2_ail_empty_gl(gl);
	}
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
 * inode_go_sync - Sync the dirty data and/or metadata for an inode glock
 * @gl: the glock protecting the inode
 *
 */

static void inode_go_sync(struct gfs2_glock *gl)
{
	struct gfs2_inode *ip = gl->gl_object;

	if (ip && !S_ISREG(ip->i_inode.i_mode))
		ip = NULL;

	if (test_bit(GLF_DIRTY, &gl->gl_flags)) {
		gfs2_log_flush(gl->gl_sbd, gl);
		if (ip)
			filemap_fdatawrite(ip->i_inode.i_mapping);
		gfs2_meta_sync(gl);
		if (ip) {
			struct address_space *mapping = ip->i_inode.i_mapping;
			int error = filemap_fdatawait(mapping);
			mapping_set_error(mapping, error);
		}
		clear_bit(GLF_DIRTY, &gl->gl_flags);
		gfs2_ail_empty_gl(gl);
	}
}

/**
 * inode_go_xmote_th - promote/demote a glock
 * @gl: the glock
 * @state: the requested state
 * @flags:
 *
 */

static void inode_go_xmote_th(struct gfs2_glock *gl)
{
	if (gl->gl_state != LM_ST_UNLOCKED)
		gfs2_pte_inval(gl);
	if (gl->gl_state == LM_ST_EXCLUSIVE)
		inode_go_sync(gl);
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
		error = gfs2_meta_read(gl, gl->gl_name.ln_number, 0, &bh);
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
	if (gl->gl_state == LM_ST_EXCLUSIVE)
		inode_go_sync(gl);
}

/**
 * inode_go_inval - prepare a inode glock to be released
 * @gl: the glock
 * @flags:
 *
 */

static void inode_go_inval(struct gfs2_glock *gl, int flags)
{
	struct gfs2_inode *ip = gl->gl_object;
	int meta = (flags & DIO_METADATA);

	if (meta) {
		gfs2_meta_inval(gl);
		if (ip)
			set_bit(GIF_INVALID, &ip->i_flags);
	}

	if (ip && S_ISREG(ip->i_inode.i_mode)) {
		truncate_inode_pages(ip->i_inode.i_mapping, 0);
		clear_bit(GIF_PAGED, &ip->i_flags);
	}
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

	if (!gl->gl_object && !gl->gl_aspace->i_mapping->nrpages)
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
	struct gfs2_inode *ip = gl->gl_object;
	int error = 0;

	if (!ip)
		return 0;

	if (test_bit(GIF_INVALID, &ip->i_flags)) {
		error = gfs2_inode_refresh(ip);
		if (error)
			return error;
	}

	if ((ip->i_di.di_flags & GFS2_DIF_TRUNC_IN_PROG) &&
	    (gl->gl_state == LM_ST_EXCLUSIVE) &&
	    (gh->gh_state == LM_ST_EXCLUSIVE))
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
	struct gfs2_inode *ip = gl->gl_object;

	if (ip)
		gfs2_meta_cache_flush(ip);
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
	return gfs2_rgrp_bh_get(gh->gh_gl->gl_object);
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
	gfs2_rgrp_bh_put(gh->gh_gl->gl_object);
}

/**
 * trans_go_xmote_th - promote/demote the transaction glock
 * @gl: the glock
 * @state: the requested state
 * @flags:
 *
 */

static void trans_go_xmote_th(struct gfs2_glock *gl)
{
	struct gfs2_sbd *sdp = gl->gl_sbd;

	if (gl->gl_state != LM_ST_UNLOCKED &&
	    test_bit(SDF_JOURNAL_LIVE, &sdp->sd_flags)) {
		gfs2_meta_syncfs(sdp);
		gfs2_log_shutdown(sdp);
	}
}

/**
 * trans_go_xmote_bh - After promoting/demoting the transaction glock
 * @gl: the glock
 *
 */

static void trans_go_xmote_bh(struct gfs2_glock *gl)
{
	struct gfs2_sbd *sdp = gl->gl_sbd;
	struct gfs2_inode *ip = GFS2_I(sdp->sd_jdesc->jd_inode);
	struct gfs2_glock *j_gl = ip->i_gl;
	struct gfs2_log_header_host head;
	int error;

	if (gl->gl_state != LM_ST_UNLOCKED &&
	    test_bit(SDF_JOURNAL_LIVE, &sdp->sd_flags)) {
		gfs2_meta_cache_flush(GFS2_I(sdp->sd_jdesc->jd_inode));
		j_gl->gl_ops->go_inval(j_gl, DIO_METADATA);

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

const struct gfs2_glock_operations gfs2_meta_glops = {
	.go_xmote_th = meta_go_sync,
	.go_drop_th = meta_go_sync,
	.go_type = LM_TYPE_META,
};

const struct gfs2_glock_operations gfs2_inode_glops = {
	.go_xmote_th = inode_go_xmote_th,
	.go_xmote_bh = inode_go_xmote_bh,
	.go_drop_th = inode_go_drop_th,
	.go_inval = inode_go_inval,
	.go_demote_ok = inode_go_demote_ok,
	.go_lock = inode_go_lock,
	.go_unlock = inode_go_unlock,
	.go_type = LM_TYPE_INODE,
};

const struct gfs2_glock_operations gfs2_rgrp_glops = {
	.go_xmote_th = meta_go_sync,
	.go_drop_th = meta_go_sync,
	.go_inval = meta_go_inval,
	.go_demote_ok = rgrp_go_demote_ok,
	.go_lock = rgrp_go_lock,
	.go_unlock = rgrp_go_unlock,
	.go_type = LM_TYPE_RGRP,
};

const struct gfs2_glock_operations gfs2_trans_glops = {
	.go_xmote_th = trans_go_xmote_th,
	.go_xmote_bh = trans_go_xmote_bh,
	.go_drop_th = trans_go_drop_th,
	.go_type = LM_TYPE_NONDISK,
};

const struct gfs2_glock_operations gfs2_iopen_glops = {
	.go_type = LM_TYPE_IOPEN,
};

const struct gfs2_glock_operations gfs2_flock_glops = {
	.go_type = LM_TYPE_FLOCK,
};

const struct gfs2_glock_operations gfs2_nondisk_glops = {
	.go_type = LM_TYPE_NONDISK,
};

const struct gfs2_glock_operations gfs2_quota_glops = {
	.go_demote_ok = quota_go_demote_ok,
	.go_type = LM_TYPE_QUOTA,
};

const struct gfs2_glock_operations gfs2_journal_glops = {
	.go_type = LM_TYPE_JOURNAL,
};

