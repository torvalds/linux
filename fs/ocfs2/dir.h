/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * dir.h
 *
 * Function prototypes
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

#ifndef OCFS2_DIR_H
#define OCFS2_DIR_H

int ocfs2_check_dir_for_entry(struct inode *dir,
			      const char *name,
			      int namelen);
int ocfs2_empty_dir(struct inode *inode);  /* FIXME: to namei.c */
int ocfs2_find_files_on_disk(const char *name,
			     int namelen,
			     u64 *blkno,
			     struct inode *inode,
			     struct buffer_head **dirent_bh,
			     struct ocfs2_dir_entry **dirent);
int ocfs2_readdir(struct file *filp, void *dirent, filldir_t filldir);
int ocfs2_prepare_dir_for_insert(struct ocfs2_super *osb,
				 struct inode *dir,
				 struct buffer_head *parent_fe_bh,
				 const char *name,
				 int namelen,
				 struct buffer_head **ret_de_bh);
struct ocfs2_alloc_context;
int ocfs2_do_extend_dir(struct super_block *sb,
			struct ocfs2_journal_handle *handle,
			struct inode *dir,
			struct buffer_head *parent_fe_bh,
			struct ocfs2_alloc_context *data_ac,
			struct ocfs2_alloc_context *meta_ac,
			struct buffer_head **new_bh);
#endif /* OCFS2_DIR_H */
