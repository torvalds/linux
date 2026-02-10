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

#ifdef CONFIG_EROFS_FS_XATTR
extern const struct xattr_handler * const erofs_xattr_handlers[];

int erofs_xattr_prefixes_init(struct super_block *sb);
void erofs_xattr_prefixes_cleanup(struct super_block *sb);
ssize_t erofs_listxattr(struct dentry *, char *, size_t);
#else
static inline int erofs_xattr_prefixes_init(struct super_block *sb) { return 0; }
static inline void erofs_xattr_prefixes_cleanup(struct super_block *sb) {}

#define erofs_listxattr (NULL)
#define erofs_xattr_handlers (NULL)
#endif	/* !CONFIG_EROFS_FS_XATTR */

#ifdef CONFIG_EROFS_FS_POSIX_ACL
struct posix_acl *erofs_get_acl(struct inode *inode, int type, bool rcu);
#else
#define erofs_get_acl	(NULL)
#endif

int erofs_xattr_fill_inode_fingerprint(struct erofs_inode_fingerprint *fp,
				       struct inode *inode, const char *domain_id);
bool erofs_inode_has_noacl(struct inode *inode, void *kaddr, unsigned int ofs);
#endif
