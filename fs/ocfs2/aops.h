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

int walk_page_buffers(	handle_t *handle,
			struct buffer_head *head,
			unsigned from,
			unsigned to,
			int *partial,
			int (*fn)(	handle_t *handle,
					struct buffer_head *bh));

struct ocfs2_write_ctxt;
typedef int (ocfs2_page_writer)(struct inode *, struct ocfs2_write_ctxt *,
				u64 *, unsigned int *, unsigned int *);

ssize_t ocfs2_buffered_write_cluster(struct file *file, loff_t pos,
				     size_t count, ocfs2_page_writer *actor,
				     void *priv);

struct ocfs2_write_ctxt {
	size_t				w_count;
	loff_t				w_pos;
	u32				w_cpos;
	unsigned int			w_finished_copy;

	/* This is true if page_size > cluster_size */
	unsigned int			w_large_pages;

	/* Filler callback and private data */
	ocfs2_page_writer		*w_write_data_page;
	void				*w_private;

	/* Only valid for the filler callback */
	struct page			*w_this_page;
	unsigned int			w_this_page_new;
};

struct ocfs2_buffered_write_priv {
	char				*b_src_buf;
	const struct iovec		*b_cur_iov; /* Current iovec */
	size_t				b_cur_off; /* Offset in the
						    * current iovec */
};
int ocfs2_map_and_write_user_data(struct inode *inode,
				  struct ocfs2_write_ctxt *wc,
				  u64 *p_blkno,
				  unsigned int *ret_from,
				  unsigned int *ret_to);

struct ocfs2_splice_write_priv {
	struct splice_desc		*s_sd;
	struct pipe_buffer		*s_buf;
	struct pipe_inode_info		*s_pipe;
	/* Neither offset value is ever larger than one page */
	unsigned int			s_offset;
	unsigned int			s_buf_offset;
};
int ocfs2_map_and_write_splice_data(struct inode *inode,
				    struct ocfs2_write_ctxt *wc,
				    u64 *p_blkno,
				    unsigned int *ret_from,
				    unsigned int *ret_to);

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
