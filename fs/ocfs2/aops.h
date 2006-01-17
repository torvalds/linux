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

int ocfs2_prepare_write(struct file *file, struct page *page,
			unsigned from, unsigned to);

struct ocfs2_journal_handle *ocfs2_start_walk_page_trans(struct inode *inode,
							 struct page *page,
							 unsigned from,
							 unsigned to);

/* all ocfs2_dio_end_io()'s fault */
#define ocfs2_iocb_is_rw_locked(iocb) \
	test_bit(0, (unsigned long *)&iocb->private)
#define ocfs2_iocb_set_rw_locked(iocb) \
	set_bit(0, (unsigned long *)&iocb->private)
#define ocfs2_iocb_clear_rw_locked(iocb) \
	clear_bit(0, (unsigned long *)&iocb->private)

#endif /* OCFS2_FILE_H */
