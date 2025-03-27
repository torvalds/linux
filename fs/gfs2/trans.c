// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2006 Red Hat, Inc.  All rights reserved.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

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
#include "inode.h"
#include "log.h"
#include "lops.h"
#include "meta_io.h"
#include "trans.h"
#include "util.h"
#include "trace_gfs2.h"

static void gfs2_print_trans(struct gfs2_sbd *sdp, const struct gfs2_trans *tr)
{
	fs_warn(sdp, "Transaction created at: %pSR\n", (void *)tr->tr_ip);
	fs_warn(sdp, "blocks=%u revokes=%u reserved=%u touched=%u\n",
		tr->tr_blocks, tr->tr_revokes, tr->tr_reserved,
		test_bit(TR_TOUCHED, &tr->tr_flags));
	fs_warn(sdp, "Buf %u/%u Databuf %u/%u Revoke %u\n",
		tr->tr_num_buf_new, tr->tr_num_buf_rm,
		tr->tr_num_databuf_new, tr->tr_num_databuf_rm,
		tr->tr_num_revoke);
}

int __gfs2_trans_begin(struct gfs2_trans *tr, struct gfs2_sbd *sdp,
		       unsigned int blocks, unsigned int revokes,
		       unsigned long ip)
{
	unsigned int extra_revokes;

	if (current->journal_info) {
		gfs2_print_trans(sdp, current->journal_info);
		BUG();
	}
	BUG_ON(blocks == 0 && revokes == 0);

	if (!test_bit(SDF_JOURNAL_LIVE, &sdp->sd_flags))
		return -EROFS;

	tr->tr_ip = ip;
	tr->tr_blocks = blocks;
	tr->tr_revokes = revokes;
	tr->tr_reserved = GFS2_LOG_FLUSH_MIN_BLOCKS;
	if (blocks) {
		/*
		 * The reserved blocks are either used for data or metadata.
		 * We can have mixed data and metadata, each with its own log
		 * descriptor block; see calc_reserved().
		 */
		tr->tr_reserved += blocks + 1 + DIV_ROUND_UP(blocks - 1, databuf_limit(sdp));
	}
	INIT_LIST_HEAD(&tr->tr_databuf);
	INIT_LIST_HEAD(&tr->tr_buf);
	INIT_LIST_HEAD(&tr->tr_list);
	INIT_LIST_HEAD(&tr->tr_ail1_list);
	INIT_LIST_HEAD(&tr->tr_ail2_list);

	if (gfs2_assert_warn(sdp, tr->tr_reserved <= sdp->sd_jdesc->jd_blocks))
		return -EINVAL;

	sb_start_intwrite(sdp->sd_vfs);

	/*
	 * Try the reservations under sd_log_flush_lock to prevent log flushes
	 * from creating inconsistencies between the number of allocated and
	 * reserved revokes.  If that fails, do a full-block allocation outside
	 * of the lock to avoid stalling log flushes.  Then, allot the
	 * appropriate number of blocks to revokes, use as many revokes locally
	 * as needed, and "release" the surplus into the revokes pool.
	 */

	down_read(&sdp->sd_log_flush_lock);
	if (gfs2_log_try_reserve(sdp, tr, &extra_revokes))
		goto reserved;
	up_read(&sdp->sd_log_flush_lock);
	gfs2_log_reserve(sdp, tr, &extra_revokes);
	down_read(&sdp->sd_log_flush_lock);

reserved:
	gfs2_log_release_revokes(sdp, extra_revokes);
	if (unlikely(!test_bit(SDF_JOURNAL_LIVE, &sdp->sd_flags))) {
		gfs2_log_release_revokes(sdp, tr->tr_revokes);
		up_read(&sdp->sd_log_flush_lock);
		gfs2_log_release(sdp, tr->tr_reserved);
		sb_end_intwrite(sdp->sd_vfs);
		return -EROFS;
	}

	current->journal_info = tr;

	return 0;
}

int gfs2_trans_begin(struct gfs2_sbd *sdp, unsigned int blocks,
		     unsigned int revokes)
{
	struct gfs2_trans *tr;
	int error;

	tr = kmem_cache_zalloc(gfs2_trans_cachep, GFP_NOFS);
	if (!tr)
		return -ENOMEM;
	error = __gfs2_trans_begin(tr, sdp, blocks, revokes, _RET_IP_);
	if (error)
		kmem_cache_free(gfs2_trans_cachep, tr);
	return error;
}

void gfs2_trans_end(struct gfs2_sbd *sdp)
{
	struct gfs2_trans *tr = current->journal_info;
	s64 nbuf;

	current->journal_info = NULL;

	if (!test_bit(TR_TOUCHED, &tr->tr_flags)) {
		gfs2_log_release_revokes(sdp, tr->tr_revokes);
		up_read(&sdp->sd_log_flush_lock);
		gfs2_log_release(sdp, tr->tr_reserved);
		if (!test_bit(TR_ONSTACK, &tr->tr_flags))
			gfs2_trans_free(sdp, tr);
		sb_end_intwrite(sdp->sd_vfs);
		return;
	}

	gfs2_log_release_revokes(sdp, tr->tr_revokes - tr->tr_num_revoke);

	nbuf = tr->tr_num_buf_new + tr->tr_num_databuf_new;
	nbuf -= tr->tr_num_buf_rm;
	nbuf -= tr->tr_num_databuf_rm;

	if (gfs2_assert_withdraw(sdp, nbuf <= tr->tr_blocks) ||
	    gfs2_assert_withdraw(sdp, tr->tr_num_revoke <= tr->tr_revokes))
		gfs2_print_trans(sdp, tr);

	gfs2_log_commit(sdp, tr);
	if (!test_bit(TR_ONSTACK, &tr->tr_flags) &&
	    !test_bit(TR_ATTACHED, &tr->tr_flags))
		gfs2_trans_free(sdp, tr);
	up_read(&sdp->sd_log_flush_lock);

	if (sdp->sd_vfs->s_flags & SB_SYNCHRONOUS)
		gfs2_log_flush(sdp, NULL, GFS2_LOG_HEAD_FLUSH_NORMAL |
			       GFS2_LFC_TRANS_END);
	sb_end_intwrite(sdp->sd_vfs);
}

static struct gfs2_bufdata *gfs2_alloc_bufdata(struct gfs2_glock *gl,
					       struct buffer_head *bh)
{
	struct gfs2_bufdata *bd;

	bd = kmem_cache_zalloc(gfs2_bufdata_cachep, GFP_NOFS | __GFP_NOFAIL);
	bd->bd_bh = bh;
	bd->bd_gl = gl;
	INIT_LIST_HEAD(&bd->bd_list);
	INIT_LIST_HEAD(&bd->bd_ail_st_list);
	INIT_LIST_HEAD(&bd->bd_ail_gl_list);
	bh->b_private = bd;
	return bd;
}

/**
 * gfs2_trans_add_data - Add a databuf to the transaction.
 * @gl: The inode glock associated with the buffer
 * @bh: The buffer to add
 *
 * This is used in journaled data mode.
 * We need to journal the data block in the same way as metadata in
 * the functions above. The difference is that here we have a tag
 * which is two __be64's being the block number (as per meta data)
 * and a flag which says whether the data block needs escaping or
 * not. This means we need a new log entry for each 251 or so data
 * blocks, which isn't an enormous overhead but twice as much as
 * for normal metadata blocks.
 */
void gfs2_trans_add_data(struct gfs2_glock *gl, struct buffer_head *bh)
{
	struct gfs2_trans *tr = current->journal_info;
	struct gfs2_sbd *sdp = gl->gl_name.ln_sbd;
	struct gfs2_bufdata *bd;

	lock_buffer(bh);
	if (buffer_pinned(bh)) {
		set_bit(TR_TOUCHED, &tr->tr_flags);
		goto out;
	}
	gfs2_log_lock(sdp);
	bd = bh->b_private;
	if (bd == NULL) {
		gfs2_log_unlock(sdp);
		unlock_buffer(bh);
		if (bh->b_private == NULL)
			bd = gfs2_alloc_bufdata(gl, bh);
		else
			bd = bh->b_private;
		lock_buffer(bh);
		gfs2_log_lock(sdp);
	}
	gfs2_assert(sdp, bd->bd_gl == gl);
	set_bit(TR_TOUCHED, &tr->tr_flags);
	if (list_empty(&bd->bd_list)) {
		set_bit(GLF_LFLUSH, &bd->bd_gl->gl_flags);
		set_bit(GLF_DIRTY, &bd->bd_gl->gl_flags);
		gfs2_pin(sdp, bd->bd_bh);
		tr->tr_num_databuf_new++;
		list_add_tail(&bd->bd_list, &tr->tr_databuf);
	}
	gfs2_log_unlock(sdp);
out:
	unlock_buffer(bh);
}

void gfs2_trans_add_meta(struct gfs2_glock *gl, struct buffer_head *bh)
{

	struct gfs2_sbd *sdp = gl->gl_name.ln_sbd;
	struct super_block *sb = sdp->sd_vfs;
	struct gfs2_bufdata *bd;
	struct gfs2_meta_header *mh;
	struct gfs2_trans *tr = current->journal_info;
	bool withdraw = false;

	lock_buffer(bh);
	if (buffer_pinned(bh)) {
		set_bit(TR_TOUCHED, &tr->tr_flags);
		goto out;
	}
	gfs2_log_lock(sdp);
	bd = bh->b_private;
	if (bd == NULL) {
		gfs2_log_unlock(sdp);
		unlock_buffer(bh);
		folio_lock(bh->b_folio);
		if (bh->b_private == NULL)
			bd = gfs2_alloc_bufdata(gl, bh);
		else
			bd = bh->b_private;
		folio_unlock(bh->b_folio);
		lock_buffer(bh);
		gfs2_log_lock(sdp);
	}
	gfs2_assert(sdp, bd->bd_gl == gl);
	set_bit(TR_TOUCHED, &tr->tr_flags);
	if (!list_empty(&bd->bd_list))
		goto out_unlock;
	set_bit(GLF_LFLUSH, &bd->bd_gl->gl_flags);
	set_bit(GLF_DIRTY, &bd->bd_gl->gl_flags);
	mh = (struct gfs2_meta_header *)bd->bd_bh->b_data;
	if (unlikely(mh->mh_magic != cpu_to_be32(GFS2_MAGIC))) {
		fs_err(sdp, "Attempting to add uninitialised block to "
		       "journal (inplace block=%lld)\n",
		       (unsigned long long)bd->bd_bh->b_blocknr);
		BUG();
	}
	if (gfs2_withdrawing_or_withdrawn(sdp)) {
		fs_info(sdp, "GFS2:adding buf while withdrawn! 0x%llx\n",
			(unsigned long long)bd->bd_bh->b_blocknr);
		goto out_unlock;
	}
	if (unlikely(sb->s_writers.frozen == SB_FREEZE_COMPLETE)) {
		fs_info(sdp, "GFS2:adding buf while frozen\n");
		withdraw = true;
		goto out_unlock;
	}
	gfs2_pin(sdp, bd->bd_bh);
	mh->__pad0 = cpu_to_be64(0);
	mh->mh_jid = cpu_to_be32(sdp->sd_jdesc->jd_jid);
	list_add(&bd->bd_list, &tr->tr_buf);
	tr->tr_num_buf_new++;
out_unlock:
	gfs2_log_unlock(sdp);
	if (withdraw)
		gfs2_assert_withdraw(sdp, 0);
out:
	unlock_buffer(bh);
}

void gfs2_trans_add_revoke(struct gfs2_sbd *sdp, struct gfs2_bufdata *bd)
{
	struct gfs2_trans *tr = current->journal_info;

	BUG_ON(!list_empty(&bd->bd_list));
	gfs2_add_revoke(sdp, bd);
	set_bit(TR_TOUCHED, &tr->tr_flags);
	tr->tr_num_revoke++;
}

void gfs2_trans_remove_revoke(struct gfs2_sbd *sdp, u64 blkno, unsigned int len)
{
	struct gfs2_bufdata *bd, *tmp;
	unsigned int n = len;

	gfs2_log_lock(sdp);
	list_for_each_entry_safe(bd, tmp, &sdp->sd_log_revokes, bd_list) {
		if ((bd->bd_blkno >= blkno) && (bd->bd_blkno < (blkno + len))) {
			list_del_init(&bd->bd_list);
			gfs2_assert_withdraw(sdp, sdp->sd_log_num_revoke);
			sdp->sd_log_num_revoke--;
			if (bd->bd_gl)
				gfs2_glock_remove_revoke(bd->bd_gl);
			kmem_cache_free(gfs2_bufdata_cachep, bd);
			gfs2_log_release_revokes(sdp, 1);
			if (--n == 0)
				break;
		}
	}
	gfs2_log_unlock(sdp);
}

void gfs2_trans_free(struct gfs2_sbd *sdp, struct gfs2_trans *tr)
{
	if (tr == NULL)
		return;

	gfs2_assert_warn(sdp, list_empty(&tr->tr_ail1_list));
	gfs2_assert_warn(sdp, list_empty(&tr->tr_ail2_list));
	gfs2_assert_warn(sdp, list_empty(&tr->tr_databuf));
	gfs2_assert_warn(sdp, list_empty(&tr->tr_buf));
	kmem_cache_free(gfs2_trans_cachep, tr);
}
