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

int ocfs2_prepare_write_nolock(struct inode *inode, struct page *page,
			       unsigned from, unsigned to);

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
			   loff_t pos, unsigned len, unsigned copied,
			   struct page *page, void *fsdata);

int ocfs2_write_begin_nolock(struct address_space *mapping,
			     loff_t pos, unsigned len, unsigned flags,
			     struct page **pagep, void **fsdata,
			     struct buffer_head *di_bh, struct page *mmap_page);

int ocfs2_read_inline_data(struct inode *inode, struct page *page,
			   struct buffer_head *di_bh);
int ocfs2_size_fits_inline_data(struct buffer_head *di_bh, u64 new_size);

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
#define ocfs2_iocb_clear_rw_locked(iocb) \
	clear_bit(0, (unsigned long *)&iocb->private)
#define ocfs2_iocb_rw_locked_level(iocb) \
	test_bit(1, (unsigned long *)&iocb->private)
#endif /* OCFS2_FILE_H */
