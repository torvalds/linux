/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2005 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License v.2.
 */

#ifndef __RGRP_DOT_H__
#define __RGRP_DOT_H__

void gfs2_rgrp_verify(struct gfs2_rgrpd *rgd);

struct gfs2_rgrpd *gfs2_blk2rgrpd(struct gfs2_sbd *sdp, uint64_t blk);
struct gfs2_rgrpd *gfs2_rgrpd_get_first(struct gfs2_sbd *sdp);
struct gfs2_rgrpd *gfs2_rgrpd_get_next(struct gfs2_rgrpd *rgd);

void gfs2_clear_rgrpd(struct gfs2_sbd *sdp);
int gfs2_rindex_hold(struct gfs2_sbd *sdp, struct gfs2_holder *ri_gh);

int gfs2_rgrp_bh_get(struct gfs2_rgrpd *rgd);
void gfs2_rgrp_bh_hold(struct gfs2_rgrpd *rgd);
void gfs2_rgrp_bh_put(struct gfs2_rgrpd *rgd);

void gfs2_rgrp_repolish_clones(struct gfs2_rgrpd *rgd);

struct gfs2_alloc *gfs2_alloc_get(struct gfs2_inode *ip);
void gfs2_alloc_put(struct gfs2_inode *ip);

int gfs2_inplace_reserve_i(struct gfs2_inode *ip,
			 char *file, unsigned int line);
#define gfs2_inplace_reserve(ip) \
gfs2_inplace_reserve_i((ip), __FILE__, __LINE__)

void gfs2_inplace_release(struct gfs2_inode *ip);

unsigned char gfs2_get_block_type(struct gfs2_rgrpd *rgd, uint64_t block);

uint64_t gfs2_alloc_data(struct gfs2_inode *ip);
uint64_t gfs2_alloc_meta(struct gfs2_inode *ip);
uint64_t gfs2_alloc_di(struct gfs2_inode *ip);

void gfs2_free_data(struct gfs2_inode *ip, uint64_t bstart, uint32_t blen);
void gfs2_free_meta(struct gfs2_inode *ip, uint64_t bstart, uint32_t blen);
void gfs2_free_uninit_di(struct gfs2_rgrpd *rgd, uint64_t blkno);
void gfs2_free_di(struct gfs2_rgrpd *rgd, struct gfs2_inode *ip);

struct gfs2_rgrp_list {
	unsigned int rl_rgrps;
	unsigned int rl_space;
	struct gfs2_rgrpd **rl_rgd;
	struct gfs2_holder *rl_ghs;
};

void gfs2_rlist_add(struct gfs2_sbd *sdp, struct gfs2_rgrp_list *rlist,
		    uint64_t block);
void gfs2_rlist_alloc(struct gfs2_rgrp_list *rlist, unsigned int state,
		      int flags);
void gfs2_rlist_free(struct gfs2_rgrp_list *rlist);

#endif /* __RGRP_DOT_H__ */
