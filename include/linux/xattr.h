/* SPDX-License-Identifier: GPL-2.0 */
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
#include <linux/mm.h>
#include <linux/rhashtable-types.h>
#include <linux/user_namespace.h>
#include <uapi/linux/xattr.h>

/* List of all open_how "versions". */
#define XATTR_ARGS_SIZE_VER0	16 /* sizeof first published struct */
#define XATTR_ARGS_SIZE_LATEST	XATTR_ARGS_SIZE_VER0

struct inode;
struct dentry;

static inline bool is_posix_acl_xattr(const char *name)
{
	return (strcmp(name, XATTR_NAME_POSIX_ACL_ACCESS) == 0) ||
	       (strcmp(name, XATTR_NAME_POSIX_ACL_DEFAULT) == 0);
}

/*
 * struct xattr_handler: When @name is set, match attributes with exactly that
 * name.  When @prefix is set instead, match attributes with that prefix and
 * with a non-empty suffix.
 */
struct xattr_handler {
	const char *name;
	const char *prefix;
	int flags;      /* fs private flags */
	bool (*list)(struct dentry *dentry);
	int (*get)(const struct xattr_handler *, struct dentry *dentry,
		   struct inode *inode, const char *name, void *buffer,
		   size_t size);
	int (*set)(const struct xattr_handler *,
		   struct mnt_idmap *idmap, struct dentry *dentry,
		   struct inode *inode, const char *name, const void *buffer,
		   size_t size, int flags);
};

/**
 * xattr_handler_can_list - check whether xattr can be listed
 * @handler: handler for this type of xattr
 * @dentry: dentry whose inode xattr to list
 *
 * Determine whether the xattr associated with @dentry can be listed given
 * @handler.
 *
 * Return: true if xattr can be listed, false if not.
 */
static inline bool xattr_handler_can_list(const struct xattr_handler *handler,
					  struct dentry *dentry)
{
	return handler && (!handler->list || handler->list(dentry));
}

const char *xattr_full_name(const struct xattr_handler *, const char *);

struct xattr {
	const char *name;
	void *value;
	size_t value_len;
};

ssize_t __vfs_getxattr(struct dentry *, struct inode *, const char *, void *, size_t);
ssize_t vfs_getxattr(struct mnt_idmap *, struct dentry *, const char *,
		     void *, size_t);
ssize_t vfs_listxattr(struct dentry *d, char *list, size_t size);
int __vfs_setxattr(struct mnt_idmap *, struct dentry *, struct inode *,
		   const char *, const void *, size_t, int);
int __vfs_setxattr_noperm(struct mnt_idmap *, struct dentry *,
			  const char *, const void *, size_t, int);
int __vfs_setxattr_locked(struct mnt_idmap *, struct dentry *,
			  const char *, const void *, size_t, int,
			  struct delegated_inode *);
int vfs_setxattr(struct mnt_idmap *, struct dentry *, const char *,
		 const void *, size_t, int);
int __vfs_removexattr(struct mnt_idmap *, struct dentry *, const char *);
int __vfs_removexattr_locked(struct mnt_idmap *, struct dentry *,
			     const char *, struct delegated_inode *);
int vfs_removexattr(struct mnt_idmap *, struct dentry *, const char *);

ssize_t generic_listxattr(struct dentry *dentry, char *buffer, size_t buffer_size);
int vfs_getxattr_alloc(struct mnt_idmap *idmap,
		       struct dentry *dentry, const char *name,
		       char **xattr_value, size_t size, gfp_t flags);

int xattr_supports_user_prefix(struct inode *inode);

static inline const char *xattr_prefix(const struct xattr_handler *handler)
{
	return handler->prefix ?: handler->name;
}

struct simple_xattrs {
	struct rhashtable ht;
};

struct simple_xattr {
	struct rhash_head hash_node;
	struct rcu_head rcu;
	char *name;
	size_t size;
	char value[] __counted_by(size);
};

#define SIMPLE_XATTR_MAX_NR		128
#define SIMPLE_XATTR_MAX_SIZE		(128 << 10)

struct simple_xattr_limits {
	atomic_t	nr_xattrs;	/* current user.* xattr count */
	atomic_t	xattr_size;	/* current total user.* value bytes */
};

static inline void simple_xattr_limits_init(struct simple_xattr_limits *limits)
{
	atomic_set(&limits->nr_xattrs, 0);
	atomic_set(&limits->xattr_size, 0);
}

int simple_xattrs_init(struct simple_xattrs *xattrs);
struct simple_xattrs *simple_xattrs_alloc(void);
struct simple_xattrs *simple_xattrs_lazy_alloc(struct simple_xattrs **xattrsp,
					       const void *value, int flags);
void simple_xattrs_free(struct simple_xattrs *xattrs, size_t *freed_space);
size_t simple_xattr_space(const char *name, size_t size);
struct simple_xattr *simple_xattr_alloc(const void *value, size_t size);
void simple_xattr_free(struct simple_xattr *xattr);
void simple_xattr_free_rcu(struct simple_xattr *xattr);
int simple_xattr_get(struct simple_xattrs *xattrs, const char *name,
		     void *buffer, size_t size);
struct simple_xattr *simple_xattr_set(struct simple_xattrs *xattrs,
				      const char *name, const void *value,
				      size_t size, int flags);
int simple_xattr_set_limited(struct simple_xattrs *xattrs,
			     struct simple_xattr_limits *limits,
			     const char *name, const void *value,
			     size_t size, int flags);
ssize_t simple_xattr_list(struct inode *inode, struct simple_xattrs *xattrs,
			  char *buffer, size_t size);
int simple_xattr_add(struct simple_xattrs *xattrs,
		     struct simple_xattr *new_xattr);
int xattr_list_one(char **buffer, ssize_t *remaining_size, const char *name);

DEFINE_CLASS(simple_xattr,
	     struct simple_xattr *,
	     if (!IS_ERR_OR_NULL(_T)) simple_xattr_free(_T),
	     simple_xattr_alloc(value, size),
	     const void *value, size_t size)

DEFINE_CLASS(simple_xattrs,
            struct simple_xattrs *,
            if (!IS_ERR_OR_NULL(_T)) { simple_xattrs_free(_T, NULL); kfree(_T); },
            simple_xattrs_alloc(),
            void)

#endif	/* _LINUX_XATTR_H */
