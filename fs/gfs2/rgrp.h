/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2008 Red Hat, Inc.  All rights reserved.
 */

#ifndef __RGRP_DOT_H__
#define __RGRP_DOT_H__

#include <linux/slab.h>
#include <linux/uaccess.h>

/* Since each block in the file system is represented by two bits in the
 * bitmap, one 64-bit word in the bitmap will represent 32 blocks.
 * By reserving 32 blocks at a time, we can optimize / shortcut how we search
 * through the bitmaps by looking a word at a time.
 */
#define RGRP_RSRV_MINBLKS 32
#define RGRP_RSRV_ADDBLKS 64

struct gfs2_rgrpd;
struct gfs2_sbd;
struct gfs2_holder;

extern void gfs2_rgrp_verify(struct gfs2_rgrpd *rgd);

extern struct gfs2_rgrpd *gfs2_blk2rgrpd(struct gfs2_sbd *sdp, u64 blk, bool exact);
extern struct gfs2_rgrpd *gfs2_rgrpd_get_first(struct gfs2_sbd *sdp);
extern struct gfs2_rgrpd *gfs2_rgrpd_get_next(struct gfs2_rgrpd *rgd);

extern void gfs2_clear_rgrpd(struct gfs2_sbd *sdp);
extern int gfs2_rindex_update(struct gfs2_sbd *sdp);
extern void gfs2_free_clones(struct gfs2_rgrpd *rgd);
extern int gfs2_rgrp_go_lock(struct gfs2_holder *gh);
extern void gfs2_rgrp_brelse(struct gfs2_rgrpd *rgd);
extern void gfs2_rgrp_go_unlock(struct gfs2_holder *gh);

extern struct gfs2_alloc *gfs2_alloc_get(struct gfs2_inode *ip);

#define GFS2_AF_ORLOV 1
extern int gfs2_inplace_reserve(struct gfs2_inode *ip,
				struct gfs2_alloc_parms *ap);
extern void gfs2_inplace_release(struct gfs2_inode *ip);

extern int gfs2_alloc_blocks(struct gfs2_inode *ip, u64 *bn, unsigned int *n,
			     bool dinode, u64 *generation);

extern int gfs2_rsqa_alloc(struct gfs2_inode *ip);
extern void gfs2_rs_deltree(struct gfs2_blkreserv *rs);
extern void gfs2_rsqa_delete(struct gfs2_inode *ip, atomic_t *wcount);
extern void __gfs2_free_blocks(struct gfs2_inode *ip, struct gfs2_rgrpd *rgd,
			       u64 bstart, u32 blen, int meta);
extern void gfs2_free_meta(struct gfs2_inode *ip, struct gfs2_rgrpd *rgd,
			   u64 bstart, u32 blen);
extern void gfs2_free_di(struct gfs2_rgrpd *rgd, struct gfs2_inode *ip);
extern void gfs2_unlink_di(struct inode *inode);
extern int gfs2_check_blk_type(struct gfs2_sbd *sdp, u64 no_addr,
			       unsigned int type);

struct gfs2_rgrp_list {
	unsigned int rl_rgrps;
	unsigned int rl_space;
	struct gfs2_rgrpd **rl_rgd;
	struct gfs2_holder *rl_ghs;
};

extern void gfs2_rlist_add(struct gfs2_inode *ip, struct gfs2_rgrp_list *rlist,
			   u64 block);
extern void gfs2_rlist_alloc(struct gfs2_rgrp_list *rlist);
extern void gfs2_rlist_free(struct gfs2_rgrp_list *rlist);
extern u64 gfs2_ri_total(struct gfs2_sbd *sdp);
extern void gfs2_rgrp_dump(struct seq_file *seq, struct gfs2_glock *gl);
extern int gfs2_rgrp_send_discards(struct gfs2_sbd *sdp, u64 offset,
				   struct buffer_head *bh,
				   const struct gfs2_bitmap *bi, unsigned minlen, u64 *ptrimmed);
extern int gfs2_fitrim(struct file *filp, void __user *argp);

/* This is how to tell if a reservation is in the rgrp tree: */
static inline bool gfs2_rs_active(const struct gfs2_blkreserv *rs)
{
	return rs && !RB_EMPTY_NODE(&rs->rs_node);
}

static inline int rgrp_contains_block(struct gfs2_rgrpd *rgd, u64 block)
{
	u64 first = rgd->rd_data0;
	u64 last = first + rgd->rd_data;
	return first <= block && block < last;
}

extern void check_and_update_goal(struct gfs2_inode *ip);
#endif /* __RGRP_DOT_H__ */
