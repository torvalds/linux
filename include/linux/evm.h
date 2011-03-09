/*
 * evm.h
 *
 * Copyright (c) 2009 IBM Corporation
 * Author: Mimi Zohar <zohar@us.ibm.com>
 */

#ifndef _LINUX_EVM_H
#define _LINUX_EVM_H

#include <linux/integrity.h>

#ifdef CONFIG_EVM
extern enum integrity_status evm_verifyxattr(struct dentry *dentry,
					     const char *xattr_name,
					     void *xattr_value,
					     size_t xattr_value_len);
extern int evm_inode_setxattr(struct dentry *dentry, const char *name,
			      const void *value, size_t size);
extern void evm_inode_post_setxattr(struct dentry *dentry,
				    const char *xattr_name,
				    const void *xattr_value,
				    size_t xattr_value_len);
extern int evm_inode_removexattr(struct dentry *dentry, const char *xattr_name);
#else
#ifdef CONFIG_INTEGRITY
static inline enum integrity_status evm_verifyxattr(struct dentry *dentry,
						    const char *xattr_name,
						    void *xattr_value,
						    size_t xattr_value_len)
{
	return INTEGRITY_UNKNOWN;
}
#endif

static inline int evm_inode_setxattr(struct dentry *dentry, const char *name,
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

static inline int evm_inode_removexattr(struct dentry *dentry,
					const char *xattr_name)
{
	return 0;
}
#endif /* CONFIG_EVM_H */
#endif /* LINUX_EVM_H */
