/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * file.h
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

#ifndef OCFS2_FILE_H
#define OCFS2_FILE_H

extern const struct file_operations ocfs2_fops;
extern const struct file_operations ocfs2_dops;
extern const struct inode_operations ocfs2_file_iops;
extern const struct inode_operations ocfs2_special_file_iops;
struct ocfs2_alloc_context;

enum ocfs2_alloc_restarted {
	RESTART_NONE = 0,
	RESTART_TRANS,
	RESTART_META
};
int ocfs2_do_extend_allocation(struct ocfs2_super *osb,
			       struct inode *inode,
			       u32 *logical_offset,
			       u32 clusters_to_add,
			       int mark_unwritten,
			       struct buffer_head *fe_bh,
			       handle_t *handle,
			       struct ocfs2_alloc_context *data_ac,
			       struct ocfs2_alloc_context *meta_ac,
			       enum ocfs2_alloc_restarted *reason_ret);
int ocfs2_extend_no_holes(struct inode *inode, u64 new_i_size,
			  u64 zero_to);
int ocfs2_lock_allocators(struct inode *inode, struct ocfs2_dinode *di,
			  u32 clusters_to_add, u32 extents_to_split,
			  struct ocfs2_alloc_context **data_ac,
			  struct ocfs2_alloc_context **meta_ac);
int ocfs2_setattr(struct dentry *dentry, struct iattr *attr);
int ocfs2_getattr(struct vfsmount *mnt, struct dentry *dentry,
		  struct kstat *stat);
int ocfs2_permission(struct inode *inode, int mask,
		     struct nameidata *nd);

int ocfs2_should_update_atime(struct inode *inode,
			      struct vfsmount *vfsmnt);
int ocfs2_update_inode_atime(struct inode *inode,
			     struct buffer_head *bh);

int ocfs2_change_file_space(struct file *file, unsigned int cmd,
			    struct ocfs2_space_resv *sr);

#endif /* OCFS2_FILE_H */
