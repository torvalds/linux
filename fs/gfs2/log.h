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
	sdp->sd_log_head = sdp->sd_log_tail = value;
}

static inline void gfs2_ordered_add_inode(struct gfs2_inode *ip)
{
	struct gfs2_sbd *sdp = GFS2_SB(&ip->i_inode);

	if (gfs2_is_jdata(ip) || !gfs2_is_ordered(sdp))
		return;

	if (!test_bit(GIF_ORDERED, &ip->i_flags)) {
		spin_lock(&sdp->sd_ordered_lock);
		if (!test_and_set_bit(GIF_ORDERED, &ip->i_flags))
			list_add(&ip->i_ordered, &sdp->sd_log_ordered);
		spin_unlock(&sdp->sd_ordered_lock);
	}
}

extern void gfs2_ordered_del_inode(struct gfs2_inode *ip);
extern unsigned int gfs2_struct2blk(struct gfs2_sbd *sdp, unsigned int nstruct);

extern void gfs2_log_release(struct gfs2_sbd *sdp, unsigned int blks);
extern int gfs2_log_reserve(struct gfs2_sbd *sdp, unsigned int blks);
extern void gfs2_write_log_header(struct gfs2_sbd *sdp, struct gfs2_jdesc *jd,
				  u64 seq, u32 tail, u32 lblock, u32 flags,
				  int op_flags);
extern void gfs2_log_flush(struct gfs2_sbd *sdp, struct gfs2_glock *gl,
			   u32 type);
extern void gfs2_log_commit(struct gfs2_sbd *sdp, struct gfs2_trans *trans);
extern void gfs2_ail1_flush(struct gfs2_sbd *sdp, struct writeback_control *wbc);

extern int gfs2_logd(void *data);
extern void gfs2_add_revoke(struct gfs2_sbd *sdp, struct gfs2_bufdata *bd);
extern void gfs2_glock_remove_revoke(struct gfs2_glock *gl);
extern void gfs2_write_revokes(struct gfs2_sbd *sdp);

#endif /* __LOG_DOT_H__ */
