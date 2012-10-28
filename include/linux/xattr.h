/*
  File: linux/xattr.h

  Extended attributes handling.

  Copyright (C) 2001 by Andreas Gruenbacher <a.gruenbacher@computer.org>
  Copyright (c) 2001-2002 Silicon Graphics, Inc.  All Rights Reserved.
  Copyright (c) 2004 Red Hat, Inc., James Morris <jmorris@redhat.com>
*/
#ifndef _LINUX_XATTR_H
#define _LINUX_XATTR_H


#include <linux/slab.h>
#include <linux/types.h>
#include <linux/spinlock.h>
#include <uapi/linux/xattr.h>

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

struct xattr {
	char *name;
	void *value;
	size_t value_len;
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
ssize_t vfs_getxattr_alloc(struct dentry *dentry, const char *name,
			   char **xattr_value, size_t size, gfp_t flags);
int vfs_xattr_cmp(struct dentry *dentry, const char *xattr_name,
		  const char *value, size_t size, gfp_t flags);

struct simple_xattrs {
	struct list_head head;
	spinlock_t lock;
};

struct simple_xattr {
	struct list_head list;
	char *name;
	size_t size;
	char value[0];
};

/*
 * initialize the simple_xattrs structure
 */
static inline void simple_xattrs_init(struct simple_xattrs *xattrs)
{
	INIT_LIST_HEAD(&xattrs->head);
	spin_lock_init(&xattrs->lock);
}

/*
 * free all the xattrs
 */
static inline void simple_xattrs_free(struct simple_xattrs *xattrs)
{
	struct simple_xattr *xattr, *node;

	list_for_each_entry_safe(xattr, node, &xattrs->head, list) {
		kfree(xattr->name);
		kfree(xattr);
	}
}

struct simple_xattr *simple_xattr_alloc(const void *value, size_t size);
int simple_xattr_get(struct simple_xattrs *xattrs, const char *name,
		     void *buffer, size_t size);
int simple_xattr_set(struct simple_xattrs *xattrs, const char *name,
		     const void *value, size_t size, int flags);
int simple_xattr_remove(struct simple_xattrs *xattrs, const char *name);
ssize_t simple_xattr_list(struct simple_xattrs *xattrs, char *buffer,
			  size_t size);
void simple_xattr_list_add(struct simple_xattrs *xattrs,
			   struct simple_xattr *new_xattr);

#endif	/* _LINUX_XATTR_H */
