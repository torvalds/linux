/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright (C) 2011 Lawrence Livermore National Security, LLC.
 */

#ifndef _ZFS_XATTR_H
#define	_ZFS_XATTR_H

#include <linux/posix_acl_xattr.h>

/*
 * 2.6.35 API change,
 * The const keyword was added to the 'struct xattr_handler' in the
 * generic Linux super_block structure.  To handle this we define an
 * appropriate xattr_handler_t typedef which can be used.  This was
 * the preferred solution because it keeps the code clean and readable.
 */
#ifdef HAVE_CONST_XATTR_HANDLER
typedef const struct xattr_handler	xattr_handler_t;
#else
typedef struct xattr_handler		xattr_handler_t;
#endif

/*
 * 3.7 API change,
 * Preferred XATTR_NAME_* definitions introduced, these are mapped to
 * the previous definitions for older kernels.
 */
#ifndef XATTR_NAME_POSIX_ACL_DEFAULT
#define	XATTR_NAME_POSIX_ACL_DEFAULT	POSIX_ACL_XATTR_DEFAULT
#endif

#ifndef XATTR_NAME_POSIX_ACL_ACCESS
#define	XATTR_NAME_POSIX_ACL_ACCESS	POSIX_ACL_XATTR_ACCESS
#endif

/*
 * 4.5 API change,
 */
#if defined(HAVE_XATTR_LIST_SIMPLE)
#define	ZPL_XATTR_LIST_WRAPPER(fn)					\
static bool								\
fn(struct dentry *dentry)						\
{									\
	return (!!__ ## fn(dentry->d_inode, NULL, 0, NULL, 0));		\
}
/*
 * 4.4 API change,
 */
#elif defined(HAVE_XATTR_LIST_DENTRY)
#define	ZPL_XATTR_LIST_WRAPPER(fn)					\
static size_t								\
fn(struct dentry *dentry, char *list, size_t list_size,			\
    const char *name, size_t name_len, int type)			\
{									\
	return (__ ## fn(dentry->d_inode,				\
	    list, list_size, name, name_len));				\
}
/*
 * 2.6.33 API change,
 */
#elif defined(HAVE_XATTR_LIST_HANDLER)
#define	ZPL_XATTR_LIST_WRAPPER(fn)					\
static size_t								\
fn(const struct xattr_handler *handler, struct dentry *dentry,		\
    char *list, size_t list_size, const char *name, size_t name_len)	\
{									\
	return (__ ## fn(dentry->d_inode,				\
	    list, list_size, name, name_len));				\
}
/*
 * 2.6.32 API
 */
#elif defined(HAVE_XATTR_LIST_INODE)
#define	ZPL_XATTR_LIST_WRAPPER(fn)					\
static size_t								\
fn(struct inode *ip, char *list, size_t list_size,			\
    const char *name, size_t name_len)					\
{									\
	return (__ ## fn(ip, list, list_size, name, name_len));		\
}
#endif

/*
 * 4.7 API change,
 * The xattr_handler->get() callback was changed to take a both dentry and
 * inode, because the dentry might not be attached to an inode yet.
 */
#if defined(HAVE_XATTR_GET_DENTRY_INODE)
#define	ZPL_XATTR_GET_WRAPPER(fn)					\
static int								\
fn(const struct xattr_handler *handler, struct dentry *dentry,		\
    struct inode *inode, const char *name, void *buffer, size_t size)	\
{									\
	return (__ ## fn(inode, name, buffer, size));			\
}
/*
 * 4.4 API change,
 * The xattr_handler->get() callback was changed to take a xattr_handler,
 * and handler_flags argument was removed and should be accessed by
 * handler->flags.
 */
#elif defined(HAVE_XATTR_GET_HANDLER)
#define	ZPL_XATTR_GET_WRAPPER(fn)					\
static int								\
fn(const struct xattr_handler *handler, struct dentry *dentry,		\
    const char *name, void *buffer, size_t size)			\
{									\
	return (__ ## fn(dentry->d_inode, name, buffer, size));		\
}
/*
 * 2.6.33 API change,
 * The xattr_handler->get() callback was changed to take a dentry
 * instead of an inode, and a handler_flags argument was added.
 */
#elif defined(HAVE_XATTR_GET_DENTRY)
#define	ZPL_XATTR_GET_WRAPPER(fn)					\
static int								\
fn(struct dentry *dentry, const char *name, void *buffer, size_t size,	\
    int unused_handler_flags)						\
{									\
	return (__ ## fn(dentry->d_inode, name, buffer, size));		\
}
/*
 * 2.6.32 API
 */
#elif defined(HAVE_XATTR_GET_INODE)
#define	ZPL_XATTR_GET_WRAPPER(fn)					\
static int								\
fn(struct inode *ip, const char *name, void *buffer, size_t size)	\
{									\
	return (__ ## fn(ip, name, buffer, size));			\
}
#endif

/*
 * 4.7 API change,
 * The xattr_handler->set() callback was changed to take a both dentry and
 * inode, because the dentry might not be attached to an inode yet.
 */
#if defined(HAVE_XATTR_SET_DENTRY_INODE)
#define	ZPL_XATTR_SET_WRAPPER(fn)					\
static int								\
fn(const struct xattr_handler *handler, struct dentry *dentry,		\
    struct inode *inode, const char *name, const void *buffer,		\
    size_t size, int flags)						\
{									\
	return (__ ## fn(inode, name, buffer, size, flags));		\
}
/*
 * 4.4 API change,
 * The xattr_handler->set() callback was changed to take a xattr_handler,
 * and handler_flags argument was removed and should be accessed by
 * handler->flags.
 */
#elif defined(HAVE_XATTR_SET_HANDLER)
#define	ZPL_XATTR_SET_WRAPPER(fn)					\
static int								\
fn(const struct xattr_handler *handler, struct dentry *dentry,		\
    const char *name, const void *buffer, size_t size, int flags)	\
{									\
	return (__ ## fn(dentry->d_inode, name, buffer, size, flags));	\
}
/*
 * 2.6.33 API change,
 * The xattr_handler->set() callback was changed to take a dentry
 * instead of an inode, and a handler_flags argument was added.
 */
#elif defined(HAVE_XATTR_SET_DENTRY)
#define	ZPL_XATTR_SET_WRAPPER(fn)					\
static int								\
fn(struct dentry *dentry, const char *name, const void *buffer,		\
    size_t size, int flags, int unused_handler_flags)			\
{									\
	return (__ ## fn(dentry->d_inode, name, buffer, size, flags));	\
}
/*
 * 2.6.32 API
 */
#elif defined(HAVE_XATTR_SET_INODE)
#define	ZPL_XATTR_SET_WRAPPER(fn)					\
static int								\
fn(struct inode *ip, const char *name, const void *buffer,		\
    size_t size, int flags)						\
{									\
	return (__ ## fn(ip, name, buffer, size, flags));		\
}
#endif

#ifdef HAVE_6ARGS_SECURITY_INODE_INIT_SECURITY
#define	zpl_security_inode_init_security(ip, dip, qstr, nm, val, len)	\
	security_inode_init_security(ip, dip, qstr, nm, val, len)
#else
#define	zpl_security_inode_init_security(ip, dip, qstr, nm, val, len)	\
	security_inode_init_security(ip, dip, nm, val, len)
#endif /* HAVE_6ARGS_SECURITY_INODE_INIT_SECURITY */

/*
 * Linux 3.7 API change. posix_acl_{from,to}_xattr gained the user_ns
 * parameter.  All callers are expected to pass the &init_user_ns which
 * is available through the init credential (kcred).
 */
#ifdef HAVE_POSIX_ACL_FROM_XATTR_USERNS
static inline struct posix_acl *
zpl_acl_from_xattr(const void *value, int size)
{
	return (posix_acl_from_xattr(kcred->user_ns, value, size));
}

static inline int
zpl_acl_to_xattr(struct posix_acl *acl, void *value, int size)
{
	return (posix_acl_to_xattr(kcred->user_ns, acl, value, size));
}

#else

static inline struct posix_acl *
zpl_acl_from_xattr(const void *value, int size)
{
	return (posix_acl_from_xattr(value, size));
}

static inline int
zpl_acl_to_xattr(struct posix_acl *acl, void *value, int size)
{
	return (posix_acl_to_xattr(acl, value, size));
}
#endif /* HAVE_POSIX_ACL_FROM_XATTR_USERNS */

#endif /* _ZFS_XATTR_H */
