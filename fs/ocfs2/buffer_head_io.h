/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * ocfs2_buffer_head.h
 *
 * Buffer cache handling functions defined
 *
 * Copyright (C) 2002, 2004 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 */

#ifndef OCFS2_BUFFER_HEAD_IO_H
#define OCFS2_BUFFER_HEAD_IO_H

#include <linux/buffer_head.h>

void ocfs2_end_buffer_io_sync(struct buffer_head *bh,
			     int uptodate);

static inline int ocfs2_read_block(struct ocfs2_super          *osb,
				   u64                  off,
				   struct buffer_head **bh,
				   int                  flags,
				   struct inode        *inode);

int ocfs2_write_block(struct ocfs2_super          *osb,
		      struct buffer_head  *bh,
		      struct inode        *inode);
int ocfs2_read_blocks(struct ocfs2_super          *osb,
		      u64                  block,
		      int                  nr,
		      struct buffer_head  *bhs[],
		      int                  flags,
		      struct inode        *inode);


#define OCFS2_BH_CACHED            1
#define OCFS2_BH_READAHEAD         8	/* use this to pass READA down to submit_bh */

static inline int ocfs2_read_block(struct ocfs2_super * osb, u64 off,
				   struct buffer_head **bh, int flags,
				   struct inode *inode)
{
	int status = 0;

	if (bh == NULL) {
		printk("ocfs2: bh == NULL\n");
		status = -EINVAL;
		goto bail;
	}

	status = ocfs2_read_blocks(osb, off, 1, bh,
				   flags, inode);

bail:
	return status;
}

#endif /* OCFS2_BUFFER_HEAD_IO_H */
