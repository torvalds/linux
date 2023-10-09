/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2006 Red Hat, Inc.  All rights reserved.
 */

#ifndef __RECOVERY_DOT_H__
#define __RECOVERY_DOT_H__

#include "incore.h"

extern struct workqueue_struct *gfs2_recovery_wq;

static inline void gfs2_replay_incr_blk(struct gfs2_jdesc *jd, u32 *blk)
{
	if (++*blk == jd->jd_blocks)
	        *blk = 0;
}

int gfs2_replay_read_block(struct gfs2_jdesc *jd, unsigned int blk,
			   struct buffer_head **bh);

int gfs2_revoke_add(struct gfs2_jdesc *jd, u64 blkno, unsigned int where);
int gfs2_revoke_check(struct gfs2_jdesc *jd, u64 blkno, unsigned int where);
void gfs2_revoke_clean(struct gfs2_jdesc *jd);

int gfs2_recover_journal(struct gfs2_jdesc *gfs2_jd, bool wait);
void gfs2_recover_func(struct work_struct *work);
int __get_log_header(struct gfs2_sbd *sdp,
		     const struct gfs2_log_header *lh, unsigned int blkno,
		     struct gfs2_log_header_host *head);

#endif /* __RECOVERY_DOT_H__ */

