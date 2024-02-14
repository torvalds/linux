/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2006 Red Hat, Inc.  All rights reserved.
 */

#ifndef __LOG_DOT_H__
#define __LOG_DOT_H__

#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/writeback.h>
#include "incore.h"
#include "inode.h"

/*
 * The minimum amount of log space required for a log flush is one block for
 * revokes and one block for the log header.  Log flushes other than
 * GFS2_LOG_HEAD_FLUSH_NORMAL may write one or two more log headers.
 */
#define GFS2_LOG_FLUSH_MIN_BLOCKS 4

/**
 * gfs2_log_lock - acquire the right to mess with the log manager
 * @sdp: the filesystem
 *
 */

static inline void gfs2_log_lock(struct gfs2_sbd *sdp)
__acquires(&sdp->sd_log_lock)
{
	spin_lock(&sdp->sd_log_lock);
}

/**
 * gfs2_log_unlock - release the right to mess with the log manager
 * @sdp: the filesystem
 *
 */

static inline void gfs2_log_unlock(struct gfs2_sbd *sdp)
__releases(&sdp->sd_log_lock)
{
	spin_unlock(&sdp->sd_log_lock);
}

static inline void gfs2_log_pointers_init(struct gfs2_sbd *sdp,
					  unsigned int value)
{
	if (++value == sdp->sd_jdesc->jd_blocks) {
		value = 0;
	}
	sdp->sd_log_tail = value;
	sdp->sd_log_flush_tail = value;
	sdp->sd_log_head = value;
}

static inline void gfs2_ordered_add_inode(struct gfs2_inode *ip)
{
	struct gfs2_sbd *sdp = GFS2_SB(&ip->i_inode);

	if (gfs2_is_jdata(ip) || !gfs2_is_ordered(sdp))
		return;

	if (list_empty(&ip->i_ordered)) {
		spin_lock(&sdp->sd_ordered_lock);
		if (list_empty(&ip->i_ordered))
			list_add(&ip->i_ordered, &sdp->sd_log_ordered);
		spin_unlock(&sdp->sd_ordered_lock);
	}
}

void gfs2_ordered_del_inode(struct gfs2_inode *ip);
unsigned int gfs2_struct2blk(struct gfs2_sbd *sdp, unsigned int nstruct);
void gfs2_remove_from_ail(struct gfs2_bufdata *bd);
bool gfs2_log_is_empty(struct gfs2_sbd *sdp);
void gfs2_log_release_revokes(struct gfs2_sbd *sdp, unsigned int revokes);
void gfs2_log_release(struct gfs2_sbd *sdp, unsigned int blks);
bool gfs2_log_try_reserve(struct gfs2_sbd *sdp, struct gfs2_trans *tr,
			  unsigned int *extra_revokes);
void gfs2_log_reserve(struct gfs2_sbd *sdp, struct gfs2_trans *tr,
		      unsigned int *extra_revokes);
void gfs2_write_log_header(struct gfs2_sbd *sdp, struct gfs2_jdesc *jd,
			   u64 seq, u32 tail, u32 lblock, u32 flags,
			   blk_opf_t op_flags);
void gfs2_log_flush(struct gfs2_sbd *sdp, struct gfs2_glock *gl,
		    u32 type);
void gfs2_log_commit(struct gfs2_sbd *sdp, struct gfs2_trans *trans);
void gfs2_ail1_flush(struct gfs2_sbd *sdp, struct writeback_control *wbc);
void log_flush_wait(struct gfs2_sbd *sdp);

int gfs2_logd(void *data);
void gfs2_add_revoke(struct gfs2_sbd *sdp, struct gfs2_bufdata *bd);
void gfs2_glock_remove_revoke(struct gfs2_glock *gl);
void gfs2_flush_revokes(struct gfs2_sbd *sdp);
void gfs2_ail_drain(struct gfs2_sbd *sdp);

#endif /* __LOG_DOT_H__ */
