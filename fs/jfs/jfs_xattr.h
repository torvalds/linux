/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *   Copyright (C) International Business Machines Corp., 2000-2002
 */

#ifndef H_JFS_XATTR
#define H_JFS_XATTR

#include <linux/xattr.h>

/*
 * jfs_ea_list describe the on-disk format of the extended attributes.
 * I know the null-terminator is redundant since namelen is stored, but
 * I am maintaining compatibility with OS/2 where possible.
 */
struct jfs_ea {
	u8 flag;	/* Unused? */
	u8 namelen;	/* Length of name */
	__le16 valuelen;	/* Length of value */
	char name[];	/* Attribute name (includes null-terminator) */
};			/* Value immediately follows name */

struct jfs_ea_list {
	__le32 size;		/* overall size */
	struct jfs_ea ea[];	/* Variable length list */
};

/* Macros for defining maximum number of bytes supported for EAs */
#define MAXEASIZE	65535
#define MAXEALISTSIZE	MAXEASIZE

/*
 * some macros for dealing with variable length EA lists.
 */
#define EA_SIZE(ea) \
	(sizeof (struct jfs_ea) + (ea)->namelen + 1 + \
	 le16_to_cpu((ea)->valuelen))
#define	NEXT_EA(ea) ((struct jfs_ea *) (((char *) (ea)) + (EA_SIZE (ea))))
#define	FIRST_EA(ealist) ((ealist)->ea)
#define	EALIST_SIZE(ealist) le32_to_cpu((ealist)->size)
#define	END_EALIST(ealist) \
	((struct jfs_ea *) (((char *) (ealist)) + EALIST_SIZE(ealist)))

extern int __jfs_setxattr(tid_t, struct inode *, const char *, const void *,
			  size_t, int);
extern ssize_t __jfs_getxattr(struct inode *, const char *, void *, size_t);
extern ssize_t jfs_listxattr(struct dentry *, char *, size_t);

extern const struct xattr_handler *jfs_xattr_handlers[];

#ifdef CONFIG_JFS_SECURITY
extern int jfs_init_security(tid_t, struct inode *, struct inode *,
			     const struct qstr *);
#else
static inline int jfs_init_security(tid_t tid, struct inode *inode,
				    struct inode *dir, const struct qstr *qstr)
{
	return 0;
}
#endif

#endif	/* H_JFS_XATTR */
