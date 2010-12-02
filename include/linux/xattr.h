/*
  File: linux/xattr.h

  Extended attributes handling.

  Copyright (C) 2001 by Andreas Gruenbacher <a.gruenbacher@computer.org>
  Copyright (c) 2001-2002 Silicon Graphics, Inc.  All Rights Reserved.
  Copyright (c) 2004 Red Hat, Inc., James Morris <jmorris@redhat.com>
*/
#ifndef _LINUX_XATTR_H
#define _LINUX_XATTR_H

#define XATTR_CREATE	0x1	/* set value, fail if attr already exists */
#define XATTR_REPLACE	0x2	/* set value, fail if attr does not exist */

#ifdef  __KERNEL__

#include <linux/types.h>

/* Namespaces */
#define XATTR_OS2_PREFIX "os2."
#define XATTR_OS2_PREFIX_LEN (sizeof (XATTR_OS2_PREFIX) - 1)

#define XATTR_SECURITY_PREFIX	"security."
#define XATTR_SECURITY_PREFIX_LEN (sizeof (XATTR_SECURITY_PREFIX) - 1)

#define XATTR_SYSTEM_PREFIX "system."
#define XATTR_SYSTEM_PREFIX_LEN (sizeof (XATTR_SYSTEM_PREFIX) - 1)

#define XATTR_TRUSTED_PREFIX "trusted."
#define XATTR_TRUSTED_PREFIX_LEN (sizeof (XATTR_TRUSTED_PREFIX) - 1)

#define XATTR_USER_PREFIX "user."
#define XATTR_USER_PREFIX_LEN (sizeof (XATTR_USER_PREFIX) - 1)

/* Security namespace */
#define XATTR_SELINUX_SUFFIX "selinux"
#define XATTR_NAME_SELINUX XATTR_SECURITY_PREFIX XATTR_SELINUX_SUFFIX

#define XATTR_SMACK_SUFFIX "SMACK64"
#define XATTR_SMACK_IPIN "SMACK64IPIN"
#define XATTR_SMACK_IPOUT "SMACK64IPOUT"
#define XATTR_SMACK_EXEC "SMACK64EXEC"
#define XATTR_NAME_SMACK XATTR_SECURITY_PREFIX XATTR_SMACK_SUFFIX
#define XATTR_NAME_SMACKIPIN	XATTR_SECURITY_PREFIX XATTR_SMACK_IPIN
#define XATTR_NAME_SMACKIPOUT	XATTR_SECURITY_PREFIX XATTR_SMACK_IPOUT
#define XATTR_NAME_SMACKEXEC	XATTR_SECURITY_PREFIX XATTR_SMACK_EXEC

#define XATTR_CAPS_SUFFIX "capability"
#define XATTR_NAME_CAPS XATTR_SECURITY_PREFIX XATTR_CAPS_SUFFIX

struct inode;
struct dentry;

struct xattr_handler {
	const char *prefix;
	int flags;	/* fs private flags passed back to the handlers */
	size_t (*list)(struct dentry *dentry, char *list, size_t list_size,
		       const char *name, size_t name_len, int handler_flags);
	int (*get)(struct dentry *dentry, const char *name, void *buffer,
		   size_t size, int handler_flags);
	int (*set)(struct dentry *dentry, const char *name, const void *buffer,
		   size_t size, int flags, int handler_flags);
};

ssize_t xattr_getsecurity(struct inode *, const char *, void *, size_t);
ssize_t vfs_getxattr(struct dentry *, const char *, void *, size_t);
ssize_t vfs_listxattr(struct dentry *d, char *list, size_t size);
int __vfs_setxattr_noperm(struct dentry *, const char *, const void *, size_t, int);
int vfs_setxattr(struct dentry *, const char *, const void *, size_t, int);
int vfs_removexattr(struct dentry *, const char *);

ssize_t generic_getxattr(struct dentry *dentry, const char *name, void *buffer, size_t size);
ssize_t generic_listxattr(struct dentry *dentry, char *buffer, size_t buffer_size);
int generic_setxattr(struct dentry *dentry, const char *name, const void *value, size_t size, int flags);
int generic_removexattr(struct dentry *dentry, const char *name);

#endif  /*  __KERNEL__  */

#endif	/* _LINUX_XATTR_H */
