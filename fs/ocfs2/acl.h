/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * acl.h
 *
 * Copyright (C) 2004, 2008 Oracle.  All rights reserved.
 */

#ifndef OCFS2_ACL_H
#define OCFS2_ACL_H

#include <linux/posix_acl_xattr.h>

struct ocfs2_acl_entry {
	__le16 e_tag;
	__le16 e_perm;
	__le32 e_id;
};

struct posix_acl *ocfs2_iop_get_acl(struct inode *inode, int type, bool rcu);
int ocfs2_iop_set_acl(struct mnt_idmap *idmap, struct dentry *dentry,
		      struct posix_acl *acl, int type);
extern int ocfs2_acl_chmod(struct inode *, struct buffer_head *);
struct ocfs2_acl_state {
	struct posix_acl *default_acl;
	struct posix_acl *acl;
	umode_t mode;
};

int ocfs2_acl_init_prepare(struct inode *inode, struct inode *dir,
			   struct buffer_head *dir_bh,
			   struct ocfs2_acl_state *state);
void ocfs2_acl_init_release(struct ocfs2_acl_state *state);
int ocfs2_init_acl(handle_t *handle, struct inode *inode,
		   struct buffer_head *di_bh,
		   struct ocfs2_alloc_context *meta_ac,
		   struct ocfs2_alloc_context *data_ac,
		   struct ocfs2_acl_state *state);

#endif /* OCFS2_ACL_H */
