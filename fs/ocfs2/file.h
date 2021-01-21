/* SPDX-License-Identifier: GPL-2.0-or-later */
/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * file.h
 *
 * Function prototypes
 *
 * Copyright (C) 2002, 2004 Oracle.  All rights reserved.
 */

#ifndef OCFS2_FILE_H
#define OCFS2_FILE_H

extern const struct file_operations ocfs2_fops;
extern const struct file_operations ocfs2_dops;
extern const struct file_operations ocfs2_fops_no_plocks;
extern const struct file_operations ocfs2_dops_no_plocks;
extern const struct inode_operations ocfs2_file_iops;
extern const struct inode_operations ocfs2_special_file_iops;
struct ocfs2_alloc_context;
enum ocfs2_alloc_restarted;

struct ocfs2_file_private {
	struct file		*fp_file;
	struct mutex		fp_mutex;
	struct ocfs2_lock_res	fp_flock;
};

int ocfs2_add_inode_data(struct ocfs2_super *osb,
			 struct inode *inode,
			 u32 *logical_offset,
			 u32 clusters_to_add,
			 int mark_unwritten,
			 struct buffer_head *fe_bh,
			 handle_t *handle,
			 struct ocfs2_alloc_context *data_ac,
			 struct ocfs2_alloc_context *meta_ac,
			 enum ocfs2_alloc_restarted *reason_ret);
int ocfs2_set_inode_size(handle_t *handle,
		struct inode *inode,
		struct buffer_head *fe_bh,
		u64 new_i_size);
int ocfs2_simple_size_update(struct inode *inode,
			     struct buffer_head *di_bh,
			     u64 new_i_size);
int ocfs2_truncate_file(struct inode *inode,
		struct buffer_head *di_bh,
		u64 new_i_size);
int ocfs2_extend_no_holes(struct inode *inode, struct buffer_head *di_bh,
			  u64 new_i_size, u64 zero_to);
int ocfs2_zero_extend(struct inode *inode, struct buffer_head *di_bh,
		      loff_t zero_to);
int ocfs2_setattr(struct user_namespace *mnt_userns, struct dentry *dentry,
		  struct iattr *attr);
int ocfs2_getattr(struct user_namespace *mnt_userns, const struct path *path,
		  struct kstat *stat, u32 request_mask, unsigned int flags);
int ocfs2_permission(struct user_namespace *mnt_userns,
		     struct inode *inode,
		     int mask);

int ocfs2_should_update_atime(struct inode *inode,
			      struct vfsmount *vfsmnt);
int ocfs2_update_inode_atime(struct inode *inode,
			     struct buffer_head *bh);

int ocfs2_change_file_space(struct file *file, unsigned int cmd,
			    struct ocfs2_space_resv *sr);

int ocfs2_check_range_for_refcount(struct inode *inode, loff_t pos,
				   size_t count);
int ocfs2_remove_inode_range(struct inode *inode,
			     struct buffer_head *di_bh, u64 byte_start,
			     u64 byte_len);
#endif /* OCFS2_FILE_H */
