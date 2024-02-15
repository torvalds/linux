/* SPDX-License-Identifier: GPL-2.0 */
/*
 * evm.h
 *
 * Copyright (c) 2009 IBM Corporation
 * Author: Mimi Zohar <zohar@us.ibm.com>
 */

#ifndef _LINUX_EVM_H
#define _LINUX_EVM_H

#include <linux/integrity.h>
#include <linux/xattr.h>

struct integrity_iint_cache;

#ifdef CONFIG_EVM
extern int evm_set_key(void *key, size_t keylen);
extern enum integrity_status evm_verifyxattr(struct dentry *dentry,
					     const char *xattr_name,
					     void *xattr_value,
					     size_t xattr_value_len,
					     struct integrity_iint_cache *iint);
int evm_inode_init_security(struct inode *inode, struct inode *dir,
			    const struct qstr *qstr, struct xattr *xattrs,
			    int *xattr_count);
extern bool evm_revalidate_status(const char *xattr_name);
extern int evm_protected_xattr_if_enabled(const char *req_xattr_name);
extern int evm_read_protected_xattrs(struct dentry *dentry, u8 *buffer,
				     int buffer_size, char type,
				     bool canonical_fmt);
#ifdef CONFIG_FS_POSIX_ACL
extern int posix_xattr_acl(const char *xattrname);
#else
static inline int posix_xattr_acl(const char *xattrname)
{
	return 0;
}
#endif
#else

static inline int evm_set_key(void *key, size_t keylen)
{
	return -EOPNOTSUPP;
}

#ifdef CONFIG_INTEGRITY
static inline enum integrity_status evm_verifyxattr(struct dentry *dentry,
						    const char *xattr_name,
						    void *xattr_value,
						    size_t xattr_value_len,
					struct integrity_iint_cache *iint)
{
	return INTEGRITY_UNKNOWN;
}
#endif

static inline int evm_inode_init_security(struct inode *inode, struct inode *dir,
					  const struct qstr *qstr,
					  struct xattr *xattrs,
					  int *xattr_count)
{
	return 0;
}

static inline bool evm_revalidate_status(const char *xattr_name)
{
	return false;
}

static inline int evm_protected_xattr_if_enabled(const char *req_xattr_name)
{
	return false;
}

static inline int evm_read_protected_xattrs(struct dentry *dentry, u8 *buffer,
					    int buffer_size, char type,
					    bool canonical_fmt)
{
	return -EOPNOTSUPP;
}

#endif /* CONFIG_EVM */
#endif /* LINUX_EVM_H */
