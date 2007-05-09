/*
 * include/linux/generic_acl.h
 *
 * (C) 2005 Andreas Gruenbacher <agruen@suse.de>
 *
 * This file is released under the GPL.
 */

#ifndef GENERIC_ACL_H
#define GENERIC_ACL_H

#include <linux/posix_acl.h>
#include <linux/posix_acl_xattr.h>

/**
 * struct generic_acl_operations  -  filesystem operations
 *
 * Filesystems must make these operations available to the generic
 * operations.
 */
struct generic_acl_operations {
	struct posix_acl *(*getacl)(struct inode *, int);
	void (*setacl)(struct inode *, int, struct posix_acl *);
};

size_t generic_acl_list(struct inode *, struct generic_acl_operations *, int,
			char *, size_t);
int generic_acl_get(struct inode *, struct generic_acl_operations *, int,
		    void *, size_t);
int generic_acl_set(struct inode *, struct generic_acl_operations *, int,
		    const void *, size_t);
int generic_acl_init(struct inode *, struct inode *,
		     struct generic_acl_operations *);
int generic_acl_chmod(struct inode *, struct generic_acl_operations *);

#endif
