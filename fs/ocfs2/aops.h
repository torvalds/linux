/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * Copyright (C) 2002, 2004, 2005 Oracle.  All rights reserved.
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

#ifndef OCFS2_AOPS_H
#define OCFS2_AOPS_H

#include <linux/fs.h>

handle_t *ocfs2_start_walk_page_trans(struct inode *inode,
							 struct page *page,
							 unsigned from,
							 unsigned to);

int ocfs2_map_page_blocks(struct page *page, u64 *p_blkno,
			  struct inode *inode, unsigned int from,
			  unsigned int to, int new);

void ocfs2_unlock_and_free_pages(struct page **pages, int num_pages);

int walk_page_buffers(	handle_t *handle,
			struct buffer_head *head,
			unsigned from,
			unsigned to,
			int *partial,
			int (*fn)(	handle_t *handle,
					struct buffer_head *bh));

int ocfs2_write_end_nolock(struct address_space *mapping,
			   loff_t pos, unsigned len, unsigned copied, void *fsdata);

typedef enum {
	OCFS2_WRITE_BUFFER = 0,
	OCFS2_WRITE_DIRECT,
	OCFS2_WRITE_MMAP,
} ocfs2_write_type_t;

int ocfs2_write_begin_nolock(struct address_space *mapping,
			     loff_t pos, unsigned len, ocfs2_write_type_t type,
			     struct page **pagep, void **fsdata,
			     struct buffer_head *di_bh, struct page *mmap_page);

int ocfs2_read_inline_data(struct inode *inode, struct page *page,
			   struct buffer_head *di_bh);
int ocfs2_size_fits_inline_data(struct buffer_head *di_bh, u64 new_size);

int ocfs2_get_block(struct inode *inode, sector_t iblock,
		    struct buffer_head *bh_result, int create);
/* all ocfs2_dio_end_io()'s fault */
#define ocfs2_iocb_is_rw_locked(iocb) \
	test_bit(0, (unsigned long *)&iocb->private)
static inline void ocfs2_iocb_set_rw_locked(struct kiocb *iocb, int level)
{
	set_bit(0, (unsigned long *)&iocb->private);
	if (level)
		set_bit(1, (unsigned long *)&iocb->private);
	else
		clear_bit(1, (unsigned long *)&iocb->private);
}

/*
 * Using a named enum representing lock types in terms of #N bit stored in
 * iocb->private, which is going to be used for communication between
 * ocfs2_dio_end_io() and ocfs2_file_write/read_iter().
 */
enum ocfs2_iocb_lock_bits {
	OCFS2_IOCB_RW_LOCK = 0,
	OCFS2_IOCB_RW_LOCK_LEVEL,
	OCFS2_IOCB_NUM_LOCKS
};

#define ocfs2_iocb_clear_rw_locked(iocb) \
	clear_bit(OCFS2_IOCB_RW_LOCK, (unsigned long *)&iocb->private)
#define ocfs2_iocb_rw_locked_level(iocb) \
	test_bit(OCFS2_IOCB_RW_LOCK_LEVEL, (unsigned long *)&iocb->private)

#endif /* OCFS2_FILE_H */
