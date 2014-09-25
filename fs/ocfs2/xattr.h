/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * xattr.h
 *
 * Copyright (C) 2004, 2008 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#ifndef OCFS2_XATTR_H
#define OCFS2_XATTR_H

#include <linux/init.h>
#include <linux/xattr.h>

enum ocfs2_xattr_type {
	OCFS2_XATTR_INDEX_USER = 1,
	OCFS2_XATTR_INDEX_POSIX_ACL_ACCESS,
	OCFS2_XATTR_INDEX_POSIX_ACL_DEFAULT,
	OCFS2_XATTR_INDEX_TRUSTED,
	OCFS2_XATTR_INDEX_SECURITY,
	OCFS2_XATTR_MAX
};

struct ocfs2_security_xattr_info {
	int enable;
	const char *name;
	void *value;
	size_t value_len;
};

extern const struct xattr_handler ocfs2_xattr_user_handler;
extern const struct xattr_handler ocfs2_xattr_trusted_handler;
extern const struct xattr_handler ocfs2_xattr_security_handler;
extern const struct xattr_handler *ocfs2_xattr_handlers[];

ssize_t ocfs2_listxattr(struct dentry *, char *, size_t);
int ocfs2_xattr_get_nolock(struct inode *, struct buffer_head *, int,
			   const char *, void *, size_t);
int ocfs2_xattr_set(struct inode *, int, const char *, const void *,
		    size_t, int);
int ocfs2_xattr_set_handle(handle_t *, struct inode *, struct buffer_head *,
			   int, const char *, const void *, size_t, int,
			   struct ocfs2_alloc_context *,
			   struct ocfs2_alloc_context *);
int ocfs2_has_inline_xattr_value_outside(struct inode *inode,
					 struct ocfs2_dinode *di);
int ocfs2_xattr_remove(struct inode *, struct buffer_head *);
int ocfs2_init_security_get(struct inode *, struct inode *,
			    const struct qstr *,
			    struct ocfs2_security_xattr_info *);
int ocfs2_init_security_set(handle_t *, struct inode *,
			    struct buffer_head *,
			    struct ocfs2_security_xattr_info *,
			    struct ocfs2_alloc_context *,
			    struct ocfs2_alloc_context *);
int ocfs2_calc_security_init(struct inode *,
			     struct ocfs2_security_xattr_info *,
			     int *, int *, struct ocfs2_alloc_context **);
int ocfs2_calc_xattr_init(struct inode *, struct buffer_head *,
			  umode_t, struct ocfs2_security_xattr_info *,
			  int *, int *, int *);

/*
 * xattrs can live inside an inode, as part of an external xattr block,
 * or inside an xattr bucket, which is the leaf of a tree rooted in an
 * xattr block.  Some of the xattr calls, especially the value setting
 * functions, want to treat each of these locations as equal.  Let's wrap
 * them in a structure that we can pass around instead of raw buffer_heads.
 */
struct ocfs2_xattr_value_buf {
	struct buffer_head		*vb_bh;
	ocfs2_journal_access_func	vb_access;
	struct ocfs2_xattr_value_root	*vb_xv;
};

int ocfs2_xattr_attach_refcount_tree(struct inode *inode,
				     struct buffer_head *fe_bh,
				     struct ocfs2_caching_info *ref_ci,
				     struct buffer_head *ref_root_bh,
				     struct ocfs2_cached_dealloc_ctxt *dealloc);
int ocfs2_reflink_xattrs(struct inode *old_inode,
			 struct buffer_head *old_bh,
			 struct inode *new_inode,
			 struct buffer_head *new_bh,
			 bool preserve_security);
int ocfs2_init_security_and_acl(struct inode *dir,
				struct inode *inode,
				const struct qstr *qstr,
				struct posix_acl *default_acl,
				struct posix_acl *acl);
#endif /* OCFS2_XATTR_H */
