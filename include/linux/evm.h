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
extern int evm_inode_setattr(struct dentry *dentry, struct iattr *attr);
extern void evm_inode_post_setattr(struct dentry *dentry, int ia_valid);
extern int evm_inode_setxattr(struct user_namespace *mnt_userns,
			      struct dentry *dentry, const char *name,
			      const void *value, size_t size);
extern void evm_inode_post_setxattr(struct dentry *dentry,
				    const char *xattr_name,
				    const void *xattr_value,
				    size_t xattr_value_len);
extern int evm_inode_removexattr(struct user_namespace *mnt_userns,
				 struct dentry *dentry, const char *xattr_name);
extern void evm_inode_post_removexattr(struct dentry *dentry,
				       const char *xattr_name);
extern int evm_inode_init_security(struct inode *inode,
				   const struct xattr *xattr_array,
				   struct xattr *evm);
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

static inline int evm_inode_setattr(struct dentry *dentry, struct iattr *attr)
{
	return 0;
}

static inline void evm_inode_post_setattr(struct dentry *dentry, int ia_valid)
{
	return;
}

static inline int evm_inode_setxattr(struct user_namespace *mnt_userns,
				     struct dentry *dentry, const char *name,
				     const void *value, size_t size)
{
	return 0;
}

static inline void evm_inode_post_setxattr(struct dentry *dentry,
					   const char *xattr_name,
					   const void *xattr_value,
					   size_t xattr_value_len)
{
	return;
}

static inline int evm_inode_removexattr(struct user_namespace *mnt_userns,
					struct dentry *dentry,
					const char *xattr_name)
{
	return 0;
}

static inline void evm_inode_post_removexattr(struct dentry *dentry,
					      const char *xattr_name)
{
	return;
}

static inline int evm_inode_init_security(struct inode *inode,
					  const struct xattr *xattr_array,
					  struct xattr *evm)
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
