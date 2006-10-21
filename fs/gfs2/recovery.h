/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2006 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License version 2.
 */

#ifndef __RECOVERY_DOT_H__
#define __RECOVERY_DOT_H__

#include "incore.h"

static inline void gfs2_replay_incr_blk(struct gfs2_sbd *sdp, unsigned int *blk)
{
	if (++*blk == sdp->sd_jdesc->jd_blocks)
	        *blk = 0;
}

int gfs2_replay_read_block(struct gfs2_jdesc *jd, unsigned int blk,
			   struct buffer_head **bh);

int gfs2_revoke_add(struct gfs2_sbd *sdp, u64 blkno, unsigned int where);
int gfs2_revoke_check(struct gfs2_sbd *sdp, u64 blkno, unsigned int where);
void gfs2_revoke_clean(struct gfs2_sbd *sdp);

int gfs2_find_jhead(struct gfs2_jdesc *jd,
		    struct gfs2_log_header *head);
int gfs2_recover_journal(struct gfs2_jdesc *gfs2_jd);
void gfs2_check_journals(struct gfs2_sbd *sdp);

#endif /* __RECOVERY_DOT_H__ */

