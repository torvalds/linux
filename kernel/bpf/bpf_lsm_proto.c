// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2025 Google LLC.
 */

#include <linux/fs.h>
#include <linux/xattr.h>
#include <linux/bpf_lsm.h>

/*
 * Strong definition of the mmap_file() BPF LSM hook. The __nullable suffix on
 * the struct file pointer parameter name marks it as PTR_MAYBE_NULL. This
 * explicitly enforces that BPF LSM programs check for NULL before attempting to
 * dereference it.
 */
noinline int bpf_lsm_mmap_file(struct file *file__nullable, unsigned long reqprot,
			       unsigned long prot, unsigned long flags)
{
	return 0;
}

/*
 * Strong definition of the inode_init_security() BPF LSM hook. Both the
 * qstr and the xattr array are NULL for some callers, so the __nullable
 * suffix marks it as PTR_MAYBE_NULL. BPF LSM programs have to check before
 * dereferencing them or handing them to bpf_inode_init_xattr().
 */
noinline int bpf_lsm_inode_init_security(struct inode *inode, struct inode *dir,
					 const struct qstr *qstr__nullable,
					 struct xattr *xattrs__nullable,
					 int *xattr_count)
{
	return -EOPNOTSUPP;
}
