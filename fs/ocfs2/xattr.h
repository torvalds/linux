/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * xattr.h
 *
 * Function prototypes
 *
 * Copyright (C) 2008 Oracle.  All rights reserved.
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

extern struct xattr_handler ocfs2_xattr_user_handler;
extern struct xattr_handler ocfs2_xattr_trusted_handler;

extern ssize_t ocfs2_listxattr(struct dentry *, char *, size_t);
extern int ocfs2_xattr_get(struct inode *, int, const char *, void *, size_t);
extern int ocfs2_xattr_set(struct inode *, int, const char *, const void *,
			   size_t, int);
extern int ocfs2_xattr_remove(struct inode *inode, struct buffer_head *di_bh);
extern struct xattr_handler *ocfs2_xattr_handlers[];

static inline u16 ocfs2_xattr_buckets_per_cluster(struct ocfs2_super *osb)
{
	return (1 << osb->s_clustersize_bits) / OCFS2_XATTR_BUCKET_SIZE;
}

static inline u16 ocfs2_blocks_per_xattr_bucket(struct super_block *sb)
{
	return OCFS2_XATTR_BUCKET_SIZE / (1 << sb->s_blocksize_bits);
}

static inline u16 ocfs2_xattr_max_xe_in_bucket(struct super_block *sb)
{
	u16 len = sb->s_blocksize -
		 offsetof(struct ocfs2_xattr_header, xh_entries);

	return len / sizeof(struct ocfs2_xattr_entry);
}
#endif /* OCFS2_XATTR_H */
