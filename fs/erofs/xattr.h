/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2017-2018 HUAWEI, Inc.
 *             https://www.huawei.com/
 */
#ifndef __EROFS_XATTR_H
#define __EROFS_XATTR_H

#include "internal.h"
#include <linux/posix_acl_xattr.h>
#include <linux/xattr.h>

/* Attribute not found */
#define ENOATTR         ENODATA

static inline unsigned int inlinexattr_header_size(struct inode *inode)
{
	return sizeof(struct erofs_xattr_ibody_header) +
		sizeof(u32) * EROFS_I(inode)->xattr_shared_count;
}

static inline erofs_blk_t xattrblock_addr(struct erofs_sb_info *sbi,
					  unsigned int xattr_id)
{
#ifdef CONFIG_EROFS_FS_XATTR
	return sbi->xattr_blkaddr +
		xattr_id * sizeof(__u32) / EROFS_BLKSIZ;
#else
	return 0;
#endif
}

static inline unsigned int xattrblock_offset(struct erofs_sb_info *sbi,
					     unsigned int xattr_id)
{
	return (xattr_id * sizeof(__u32)) % EROFS_BLKSIZ;
}

#ifdef CONFIG_EROFS_FS_XATTR
extern const struct xattr_handler erofs_xattr_user_handler;
extern const struct xattr_handler erofs_xattr_trusted_handler;
#ifdef CONFIG_EROFS_FS_SECURITY
extern const struct xattr_handler erofs_xattr_security_handler;
#endif

static inline const struct xattr_handler *erofs_xattr_handler(unsigned int idx)
{
	static const struct xattr_handler *xattr_handler_map[] = {
		[EROFS_XATTR_INDEX_USER] = &erofs_xattr_user_handler,
#ifdef CONFIG_EROFS_FS_POSIX_ACL
		[EROFS_XATTR_INDEX_POSIX_ACL_ACCESS] =
			&posix_acl_access_xattr_handler,
		[EROFS_XATTR_INDEX_POSIX_ACL_DEFAULT] =
			&posix_acl_default_xattr_handler,
#endif
		[EROFS_XATTR_INDEX_TRUSTED] = &erofs_xattr_trusted_handler,
#ifdef CONFIG_EROFS_FS_SECURITY
		[EROFS_XATTR_INDEX_SECURITY] = &erofs_xattr_security_handler,
#endif
	};

	return idx && idx < ARRAY_SIZE(xattr_handler_map) ?
		xattr_handler_map[idx] : NULL;
}

extern const struct xattr_handler *erofs_xattr_handlers[];

int erofs_getxattr(struct inode *, int, const char *, void *, size_t);
ssize_t erofs_listxattr(struct dentry *, char *, size_t);
#else
static inline int erofs_getxattr(struct inode *inode, int index,
				 const char *name, void *buffer,
				 size_t buffer_size)
{
	return -EOPNOTSUPP;
}

#define erofs_listxattr (NULL)
#define erofs_xattr_handlers (NULL)
#endif	/* !CONFIG_EROFS_FS_XATTR */

#ifdef CONFIG_EROFS_FS_POSIX_ACL
struct posix_acl *erofs_get_acl(struct inode *inode, int type, bool rcu);
#else
#define erofs_get_acl	(NULL)
#endif

#endif
