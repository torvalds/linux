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
#include <linux/kallsyms.h>
#include <linux/gfs2_ondisk.h>

#include "gfs2.h"
#include "incore.h"
#include "glock.h"
#include "log.h"
#include "lops.h"
#include "meta_io.h"
#include "trans.h"
#include "util.h"
#include "trace_gfs2.h"

int gfs2_trans_begin(struct gfs2_sbd *sdp, unsigned int blocks,
		     unsigned int revokes)
{
	struct gfs2_trans *tr;
	int error;

	BUG_ON(current->journal_info);
	BUG_ON(blocks == 0 && revokes == 0);

	if (!test_bit(SDF_JOURNAL_LIVE, &sdp->sd_flags))
		return -EROFS;

	tr = kzalloc(sizeof(struct gfs2_trans), GFP_NOFS);
	if (!tr)
		return -ENOMEM;

	tr->tr_ip = (unsigned long)__builtin_return_address(0);
	tr->tr_blocks = blocks;
	tr->tr_revokes = revokes;
	tr->tr_reserved = 1;
	if (blocks)
		tr->tr_reserved += 6 + blocks;
	if (revokes)
		tr->tr_reserved += gfs2_struct2blk(sdp, revokes,
						   sizeof(u64));
	INIT_LIST_HEAD(&tr->tr_list_buf);

	gfs2_holder_init(sdp->sd_trans_gl, LM_ST_SHARED, 0, &tr->tr_t_gh);

	error = gfs2_glock_nq(&tr->tr_t_gh);
	if (error)
		goto fail_holder_uninit;

	error = gfs2_log_reserve(sdp, tr->tr_reserved);
	if (error)
		goto fail_gunlock;

	current->journal_info = tr;

	return 0;

fail_gunlock:
	gfs2_glock_dq(&tr->tr_t_gh);

fail_holder_uninit:
	gfs2_holder_uninit(&tr->tr_t_gh);
	kfree(tr);

	return error;
}

/**
 * gfs2_log_release - Release a given number of log blocks
 * @sdp: The GFS2 superblock
 * @blks: The number of blocks
 *
 */

static void gfs2_log_release(struct gfs2_sbd *sdp, unsigned int blks)
{

	atomic_add(blks, &sdp->sd_log_blks_free);
	trace_gfs2_log_blocks(sdp, blks);
	gfs2_assert_withdraw(sdp, atomic_read(&sdp->sd_log_blks_free) <=
				  sdp->sd_jdesc->jd_blocks);
	up_read(&sdp->sd_log_flush_lock);
}

void gfs2_trans_end(struct gfs2_sbd *sdp)
{
	struct gfs2_trans *tr = current->journal_info;

	BUG_ON(!tr);
	current->journal_info = NULL;

	if (!tr->tr_touched) {
		gfs2_log_release(sdp, tr->tr_reserved);
		if (tr->tr_t_gh.gh_gl) {
			gfs2_glock_dq(&tr->tr_t_gh);
			gfs2_holder_uninit(&tr->tr_t_gh);
			kfree(tr);
		}
		return;
	}

	if (gfs2_assert_withdraw(sdp, tr->tr_num_buf <= tr->tr_blocks)) {
		fs_err(sdp, "tr_num_buf = %u, tr_blocks = %u ",
		       tr->tr_num_buf, tr->tr_blocks);
		print_symbol(KERN_WARNING "GFS2: Transaction created at: %s\n", tr->tr_ip);
	}
	if (gfs2_assert_withdraw(sdp, tr->tr_num_revoke <= tr->tr_revokes)) {
		fs_err(sdp, "tr_num_revoke = %u, tr_revokes = %u ",
		       tr->tr_num_revoke, tr->tr_revokes);
		print_symbol(KERN_WARNING "GFS2: Transaction created at: %s\n", tr->tr_ip);
	}

	gfs2_log_commit(sdp, tr);
	if (tr->tr_t_gh.gh_gl) {
		gfs2_glock_dq(&tr->tr_t_gh);
		gfs2_holder_uninit(&tr->tr_t_gh);
		kfree(tr);
	}

	if (sdp->sd_vfs->s_flags & MS_SYNCHRONOUS)
		gfs2_log_flush(sdp, NULL);
}

/**
 * gfs2_trans_add_bh - Add a to-be-modified buffer to the current transaction
 * @gl: the glock the buffer belongs to
 * @bh: The buffer to add
 * @meta: True in the case of adding metadata
 *
 */

void gfs2_trans_add_bh(struct gfs2_glock *gl, struct buffer_head *bh, int meta)
{
	struct gfs2_sbd *sdp = gl->gl_sbd;
	struct gfs2_bufdata *bd;

	lock_buffer(bh);
	gfs2_log_lock(sdp);
	bd = bh->b_private;
	if (bd)
		gfs2_assert(sdp, bd->bd_gl == gl);
	else {
		gfs2_log_unlock(sdp);
		unlock_buffer(bh);
		gfs2_attach_bufdata(gl, bh, meta);
		bd = bh->b_private;
		lock_buffer(bh);
		gfs2_log_lock(sdp);
	}
	lops_add(sdp, &bd->bd_le);
	gfs2_log_unlock(sdp);
	unlock_buffer(bh);
}

void gfs2_trans_add_revoke(struct gfs2_sbd *sdp, struct gfs2_bufdata *bd)
{
	BUG_ON(!list_empty(&bd->bd_le.le_list));
	BUG_ON(!list_empty(&bd->bd_ail_st_list));
	BUG_ON(!list_empty(&bd->bd_ail_gl_list));
	lops_init_le(&bd->bd_le, &gfs2_revoke_lops);
	lops_add(sdp, &bd->bd_le);
}

void gfs2_trans_add_unrevoke(struct gfs2_sbd *sdp, u64 blkno, unsigned int len)
{
	struct gfs2_bufdata *bd, *tmp;
	struct gfs2_trans *tr = current->journal_info;
	unsigned int n = len;

	gfs2_log_lock(sdp);
	list_for_each_entry_safe(bd, tmp, &sdp->sd_log_le_revoke, bd_le.le_list) {
		if ((bd->bd_blkno >= blkno) && (bd->bd_blkno < (blkno + len))) {
			list_del_init(&bd->bd_le.le_list);
			gfs2_assert_withdraw(sdp, sdp->sd_log_num_revoke);
			sdp->sd_log_num_revoke--;
			kmem_cache_free(gfs2_bufdata_cachep, bd);
			tr->tr_num_revoke_rm++;
			if (--n == 0)
				break;
		}
	}
	gfs2_log_unlock(sdp);
}

