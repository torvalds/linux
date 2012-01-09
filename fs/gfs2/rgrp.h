/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2008 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License version 2.
 */

#ifndef __RGRP_DOT_H__
#define __RGRP_DOT_H__

#include <linux/slab.h>

struct gfs2_rgrpd;
struct gfs2_sbd;
struct gfs2_holder;

extern void gfs2_rgrp_verify(struct gfs2_rgrpd *rgd);

extern struct gfs2_rgrpd *gfs2_blk2rgrpd(struct gfs2_sbd *sdp, u64 blk);
extern struct gfs2_rgrpd *gfs2_rgrpd_get_first(struct gfs2_sbd *sdp);
extern struct gfs2_rgrpd *gfs2_rgrpd_get_next(struct gfs2_rgrpd *rgd);

extern void gfs2_clear_rgrpd(struct gfs2_sbd *sdp);
extern int gfs2_rindex_update(struct gfs2_sbd *sdp);
extern void gfs2_free_clones(struct gfs2_rgrpd *rgd);
extern int gfs2_rgrp_go_lock(struct gfs2_holder *gh);
extern void gfs2_rgrp_go_unlock(struct gfs2_holder *gh);

extern struct gfs2_qadata *gfs2_qadata_get(struct gfs2_inode *ip);
static inline void gfs2_qadata_put(struct gfs2_inode *ip)
{
	BUG_ON(ip->i_qadata == NULL);
	kfree(ip->i_qadata);
	ip->i_qadata = NULL;
}

extern int gfs2_inplace_reserve(struct gfs2_inode *ip, u32 requested);
extern void gfs2_inplace_release(struct gfs2_inode *ip);

extern int gfs2_alloc_blocks(struct gfs2_inode *ip, u64 *bn, unsigned int *n,
			     bool dinode, u64 *generation);

extern void __gfs2_free_blocks(struct gfs2_inode *ip, u64 bstart, u32 blen, int meta);
extern void gfs2_free_meta(struct gfs2_inode *ip, u64 bstart, u32 blen);
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
extern void gfs2_rlist_alloc(struct gfs2_rgrp_list *rlist, unsigned int state);
extern void gfs2_rlist_free(struct gfs2_rgrp_list *rlist);
extern u64 gfs2_ri_total(struct gfs2_sbd *sdp);
extern int gfs2_rgrp_dump(struct seq_file *seq, const struct gfs2_glock *gl);
extern void gfs2_rgrp_send_discards(struct gfs2_sbd *sdp, u64 offset,
				    struct buffer_head *bh,
				    const struct gfs2_bitmap *bi);

#endif /* __RGRP_DOT_H__ */
