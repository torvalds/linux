/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2006 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License version 2.
 */

#ifndef __BMAP_DOT_H__
#define __BMAP_DOT_H__

struct inode;
struct gfs2_inode;
struct page;

int gfs2_unstuff_dinode(struct gfs2_inode *ip, struct page *page);
int gfs2_block_map(struct inode *inode, u64 lblock, int create, struct buffer_head *bh);
int gfs2_extent_map(struct inode *inode, u64 lblock, int *new, u64 *dblock, unsigned *extlen);

int gfs2_truncatei(struct gfs2_inode *ip, u64 size);
int gfs2_truncatei_resume(struct gfs2_inode *ip);
int gfs2_file_dealloc(struct gfs2_inode *ip);

void gfs2_write_calc_reserv(struct gfs2_inode *ip, unsigned int len,
			    unsigned int *data_blocks,
			    unsigned int *ind_blocks);
int gfs2_write_alloc_required(struct gfs2_inode *ip, u64 offset,
			      unsigned int len, int *alloc_required);

#endif /* __BMAP_DOT_H__ */
