/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2006 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License version 2.
 */

#ifndef __LOG_DOT_H__
#define __LOG_DOT_H__

#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/writeback.h>
#include "incore.h"

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

	if (!test_bit(GIF_ORDERED, &ip->i_flags)) {
		spin_lock(&sdp->sd_ordered_lock);
		if (!test_and_set_bit(GIF_ORDERED, &ip->i_flags))
			list_add(&ip->i_ordered, &sdp->sd_log_le_ordered);
		spin_unlock(&sdp->sd_ordered_lock);
	}
}
extern void gfs2_ordered_del_inode(struct gfs2_inode *ip);
extern unsigned int gfs2_struct2blk(struct gfs2_sbd *sdp, unsigned int nstruct,
			    unsigned int ssize);

extern void gfs2_log_release(struct gfs2_sbd *sdp, unsigned int blks);
extern int gfs2_log_reserve(struct gfs2_sbd *sdp, unsigned int blks);
enum gfs2_flush_type {
	NORMAL_FLUSH = 0,
	SYNC_FLUSH,
	SHUTDOWN_FLUSH,
	FREEZE_FLUSH
};
extern void gfs2_log_flush(struct gfs2_sbd *sdp, struct gfs2_glock *gl,
			   enum gfs2_flush_type type);
extern void gfs2_log_commit(struct gfs2_sbd *sdp, struct gfs2_trans *trans);
extern void gfs2_remove_from_ail(struct gfs2_bufdata *bd);
extern void gfs2_ail1_flush(struct gfs2_sbd *sdp, struct writeback_control *wbc);

extern void gfs2_log_shutdown(struct gfs2_sbd *sdp);
extern int gfs2_logd(void *data);
extern void gfs2_add_revoke(struct gfs2_sbd *sdp, struct gfs2_bufdata *bd);
extern void gfs2_write_revokes(struct gfs2_sbd *sdp);

#endif /* __LOG_DOT_H__ */
