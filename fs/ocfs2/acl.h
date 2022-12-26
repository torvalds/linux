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
int ocfs2_iop_set_acl(struct user_namespace *mnt_userns, struct dentry *dentry,
		      struct posix_acl *acl, int type);
extern int ocfs2_acl_chmod(struct inode *, struct buffer_head *);
extern int ocfs2_init_acl(handle_t *, struct inode *, struct inode *,
			  struct buffer_head *, struct buffer_head *,
			  struct ocfs2_alloc_context *,
			  struct ocfs2_alloc_context *);

#endif /* OCFS2_ACL_H */
