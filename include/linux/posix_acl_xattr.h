/* SPDX-License-Identifier: GPL-2.0 */
/*
  File: linux/posix_acl_xattr.h

  Extended attribute system call representation of Access Control Lists.

  Copyright (C) 2000 by Andreas Gruenbacher <a.gruenbacher@computer.org>
  Copyright (C) 2002 SGI - Silicon Graphics, Inc <linux-xfs@oss.sgi.com>
 */
#ifndef _POSIX_ACL_XATTR_H
#define _POSIX_ACL_XATTR_H

#include <uapi/linux/xattr.h>
#include <uapi/linux/posix_acl_xattr.h>
#include <linux/posix_acl.h>

static inline size_t
posix_acl_xattr_size(int count)
{
	return (sizeof(struct posix_acl_xattr_header) +
		(count * sizeof(struct posix_acl_xattr_entry)));
}

static inline int
posix_acl_xattr_count(size_t size)
{
	if (size < sizeof(struct posix_acl_xattr_header))
		return -1;
	size -= sizeof(struct posix_acl_xattr_header);
	if (size % sizeof(struct posix_acl_xattr_entry))
		return -1;
	return size / sizeof(struct posix_acl_xattr_entry);
}

#ifdef CONFIG_FS_POSIX_ACL
void posix_acl_fix_xattr_from_user(void *value, size_t size);
void posix_acl_fix_xattr_to_user(void *value, size_t size);
void posix_acl_getxattr_idmapped_mnt(struct user_namespace *mnt_userns,
				     const struct inode *inode,
				     void *value, size_t size);
#else
static inline void posix_acl_fix_xattr_from_user(void *value, size_t size)
{
}
static inline void posix_acl_fix_xattr_to_user(void *value, size_t size)
{
}
static inline void
posix_acl_getxattr_idmapped_mnt(struct user_namespace *mnt_userns,
				const struct inode *inode, void *value,
				size_t size)
{
}
#endif

struct posix_acl *posix_acl_from_xattr(struct user_namespace *user_ns, 
				       const void *value, size_t size);
int posix_acl_to_xattr(struct user_namespace *user_ns,
		       const struct posix_acl *acl, void *buffer, size_t size);
struct posix_acl *vfs_set_acl_prepare(struct user_namespace *mnt_userns,
				      struct user_namespace *fs_userns,
				      const void *value, size_t size);

extern const struct xattr_handler posix_acl_access_xattr_handler;
extern const struct xattr_handler posix_acl_default_xattr_handler;

#endif	/* _POSIX_ACL_XATTR_H */
