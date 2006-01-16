/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2005 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License v.2.
 */

#ifndef __TRANS_DOT_H__
#define __TRANS_DOT_H__

#define RES_DINODE	1
#define RES_INDIRECT	1
#define RES_JDATA	1
#define RES_DATA	1
#define RES_LEAF	1
#define RES_RG_BIT	2
#define RES_EATTR	1
#define RES_UNLINKED	1
#define RES_STATFS	1
#define RES_QUOTA	2

#define gfs2_trans_begin(sdp, blocks, revokes) \
gfs2_trans_begin_i((sdp), (blocks), (revokes), __FILE__, __LINE__)

int gfs2_trans_begin_i(struct gfs2_sbd *sdp,
		      unsigned int blocks, unsigned int revokes,
		      char *file, unsigned int line);

void gfs2_trans_end(struct gfs2_sbd *sdp);

void gfs2_trans_add_gl(struct gfs2_glock *gl);
void gfs2_trans_add_bh(struct gfs2_glock *gl, struct buffer_head *bh);
void gfs2_trans_add_revoke(struct gfs2_sbd *sdp, uint64_t blkno);
void gfs2_trans_add_unrevoke(struct gfs2_sbd *sdp, uint64_t blkno);
void gfs2_trans_add_rg(struct gfs2_rgrpd *rgd);
void gfs2_trans_add_databuf(struct gfs2_sbd *sdp, struct buffer_head *bh);

#endif /* __TRANS_DOT_H__ */
