/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2006 Red Hat, Inc.  All rights reserved.
 */

#ifndef __BMAP_DOT_H__
#define __BMAP_DOT_H__

#include <linux/iomap.h>

#include "inode.h"

struct inode;
struct gfs2_inode;
struct page;


/**
 * gfs2_write_calc_reserv - calculate number of blocks needed to write to a file
 * @ip: the file
 * @len: the number of bytes to be written to the file
 * @data_blocks: returns the number of data blocks required
 * @ind_blocks: returns the number of indirect blocks required
 *
 */

static inline void gfs2_write_calc_reserv(const struct gfs2_inode *ip,
					  unsigned int len,
					  unsigned int *data_blocks,
					  unsigned int *ind_blocks)
{
	const struct gfs2_sbd *sdp = GFS2_SB(&ip->i_inode);
	unsigned int tmp;

	BUG_ON(gfs2_is_dir(ip));
	*data_blocks = (len >> sdp->sd_sb.sb_bsize_shift) + 3;
	*ind_blocks = 3 * (sdp->sd_max_height - 1);

	for (tmp = *data_blocks; tmp > sdp->sd_diptrs;) {
		tmp = DIV_ROUND_UP(tmp, sdp->sd_inptrs);
		*ind_blocks += tmp;
	}
}

extern const struct iomap_ops gfs2_iomap_ops;
extern const struct iomap_writeback_ops gfs2_writeback_ops;

extern int gfs2_unstuff_dinode(struct gfs2_inode *ip);
extern int gfs2_block_map(struct inode *inode, sector_t lblock,
			  struct buffer_head *bh, int create);
extern int gfs2_iomap_get(struct inode *inode, loff_t pos, loff_t length,
			  struct iomap *iomap);
extern int gfs2_iomap_alloc(struct inode *inode, loff_t pos, loff_t length,
			    struct iomap *iomap);
extern int gfs2_get_extent(struct inode *inode, u64 lblock, u64 *dblock,
			   unsigned int *extlen);
extern int gfs2_alloc_extent(struct inode *inode, u64 lblock, u64 *dblock,
			     unsigned *extlen, bool *new);
extern int gfs2_setattr_size(struct inode *inode, u64 size);
extern void gfs2_trim_blocks(struct inode *inode);
extern int gfs2_truncatei_resume(struct gfs2_inode *ip);
extern int gfs2_file_dealloc(struct gfs2_inode *ip);
extern int gfs2_write_alloc_required(struct gfs2_inode *ip, u64 offset,
				     unsigned int len);
extern int gfs2_map_journal_extents(struct gfs2_sbd *sdp, struct gfs2_jdesc *jd);
extern void gfs2_free_journal_extents(struct gfs2_jdesc *jd);
extern int __gfs2_punch_hole(struct file *file, loff_t offset, loff_t length);

#endif /* __BMAP_DOT_H__ */
