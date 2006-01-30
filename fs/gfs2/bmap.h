/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2005 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License v.2.
 */

#ifndef __BMAP_DOT_H__
#define __BMAP_DOT_H__

typedef int (*gfs2_unstuffer_t) (struct gfs2_inode * ip,
				 struct buffer_head * dibh, uint64_t block,
				 void *private);
int gfs2_unstuffer_sync(struct gfs2_inode *ip, struct buffer_head *dibh,
			uint64_t block, void *private);
int gfs2_unstuff_dinode(struct gfs2_inode *ip, gfs2_unstuffer_t unstuffer,
			void *private);

int gfs2_block_map(struct gfs2_inode *ip,
		   uint64_t lblock, int *new,
		   uint64_t *dblock, uint32_t *extlen);

int gfs2_truncatei(struct gfs2_inode *ip, uint64_t size);
int gfs2_truncatei_resume(struct gfs2_inode *ip);
int gfs2_file_dealloc(struct gfs2_inode *ip);

void gfs2_write_calc_reserv(struct gfs2_inode *ip, unsigned int len,
			    unsigned int *data_blocks,
			    unsigned int *ind_blocks);
int gfs2_write_alloc_required(struct gfs2_inode *ip, uint64_t offset,
			      unsigned int len, int *alloc_required);

#endif /* __BMAP_DOT_H__ */
