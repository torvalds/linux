/* SPDX-License-Identifier: GPL-2.0-or-later */
/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * namei.h
 *
 * Function prototypes
 *
 * Copyright (C) 2002, 2004 Oracle.  All rights reserved.
 */

#ifndef OCFS2_NAMEI_H
#define OCFS2_NAMEI_H

#define OCFS2_DIO_ORPHAN_PREFIX "dio-"
#define OCFS2_DIO_ORPHAN_PREFIX_LEN 4

extern const struct inode_operations ocfs2_dir_iops;

struct dentry *ocfs2_get_parent(struct dentry *child);

int ocfs2_orphan_del(struct ocfs2_super *osb,
		     handle_t *handle,
		     struct inode *orphan_dir_inode,
		     struct inode *inode,
		     struct buffer_head *orphan_dir_bh,
		     bool dio);
int ocfs2_create_inode_in_orphan(struct inode *dir,
				 int mode,
				 struct inode **new_inode);
int ocfs2_add_inode_to_orphan(struct ocfs2_super *osb,
		struct inode *inode);
int ocfs2_del_inode_from_orphan(struct ocfs2_super *osb,
		struct inode *inode, struct buffer_head *di_bh,
		int update_isize, loff_t end);
int ocfs2_mv_orphaned_inode_to_new(struct inode *dir,
				   struct inode *new_inode,
				   struct dentry *new_dentry);

#endif /* OCFS2_NAMEI_H */
