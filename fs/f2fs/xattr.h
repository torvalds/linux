/*
 * fs/f2fs/xattr.h
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *             http://www.samsung.com/
 *
 * Portions of this code from linux/fs/ext2/xattr.h
 *
 * On-disk format of extended attributes for the ext2 filesystem.
 *
 * (C) 2001 Andreas Gruenbacher, <a.gruenbacher@computer.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __F2FS_XATTR_H__
#define __F2FS_XATTR_H__

#include <linux/init.h>
#include <linux/xattr.h>

/* Magic value in attribute blocks */
#define F2FS_XATTR_MAGIC                0xF2F52011

/* Maximum number of references to one attribute block */
#define F2FS_XATTR_REFCOUNT_MAX         1024

/* Name indexes */
#define F2FS_SYSTEM_ADVISE_PREFIX		"system.advise"
#define F2FS_XATTR_INDEX_USER			1
#define F2FS_XATTR_INDEX_POSIX_ACL_ACCESS	2
#define F2FS_XATTR_INDEX_POSIX_ACL_DEFAULT	3
#define F2FS_XATTR_INDEX_TRUSTED		4
#define F2FS_XATTR_INDEX_LUSTRE			5
#define F2FS_XATTR_INDEX_SECURITY		6
#define F2FS_XATTR_INDEX_ADVISE			7

struct f2fs_xattr_header {
	__le32  h_magic;        /* magic number for identification */
	__le32  h_refcount;     /* reference count */
	__u32   h_reserved[4];  /* zero right now */
};

struct f2fs_xattr_entry {
	__u8    e_name_index;
	__u8    e_name_len;
	__le16  e_value_size;   /* size of attribute value */
	char    e_name[0];      /* attribute name */
};

#define XATTR_HDR(ptr)		((struct f2fs_xattr_header *)(ptr))
#define XATTR_ENTRY(ptr)	((struct f2fs_xattr_entry *)(ptr))
#define XATTR_FIRST_ENTRY(ptr)	(XATTR_ENTRY(XATTR_HDR(ptr) + 1))
#define XATTR_ROUND		(3)

#define XATTR_ALIGN(size)	((size + XATTR_ROUND) & ~XATTR_ROUND)

#define ENTRY_SIZE(entry) (XATTR_ALIGN(sizeof(struct f2fs_xattr_entry) + \
			entry->e_name_len + le16_to_cpu(entry->e_value_size)))

#define XATTR_NEXT_ENTRY(entry)	((struct f2fs_xattr_entry *)((char *)(entry) +\
			ENTRY_SIZE(entry)))

#define IS_XATTR_LAST_ENTRY(entry) (*(__u32 *)(entry) == 0)

#define list_for_each_xattr(entry, addr) \
		for (entry = XATTR_FIRST_ENTRY(addr);\
				!IS_XATTR_LAST_ENTRY(entry);\
				entry = XATTR_NEXT_ENTRY(entry))

#define MIN_OFFSET(i)	XATTR_ALIGN(inline_xattr_size(i) + PAGE_SIZE -	\
				sizeof(struct node_footer) - sizeof(__u32))

#define MAX_VALUE_LEN(i)	(MIN_OFFSET(i) -			\
				sizeof(struct f2fs_xattr_header) -	\
				sizeof(struct f2fs_xattr_entry))

/*
 * On-disk structure of f2fs_xattr
 * We use inline xattrs space + 1 block for xattr.
 *
 * +--------------------+
 * | f2fs_xattr_header  |
 * |                    |
 * +--------------------+
 * | f2fs_xattr_entry   |
 * | .e_name_index = 1  |
 * | .e_name_len = 3    |
 * | .e_value_size = 14 |
 * | .e_name = "foo"    |
 * | "value_of_xattr"   |<- value_offs = e_name + e_name_len
 * +--------------------+
 * | f2fs_xattr_entry   |
 * | .e_name_index = 4  |
 * | .e_name = "bar"    |
 * +--------------------+
 * |                    |
 * |        Free        |
 * |                    |
 * +--------------------+<- MIN_OFFSET
 * |   node_footer      |
 * | (nid, ino, offset) |
 * +--------------------+
 *
 **/

#ifdef CONFIG_F2FS_FS_XATTR
extern const struct xattr_handler f2fs_xattr_user_handler;
extern const struct xattr_handler f2fs_xattr_trusted_handler;
extern const struct xattr_handler f2fs_xattr_advise_handler;
extern const struct xattr_handler f2fs_xattr_security_handler;

extern const struct xattr_handler *f2fs_xattr_handlers[];

extern int f2fs_setxattr(struct inode *, int, const char *,
				const void *, size_t, struct page *, int);
extern int f2fs_getxattr(struct inode *, int, const char *, void *, size_t);
extern ssize_t f2fs_listxattr(struct dentry *, char *, size_t);
#else

#define f2fs_xattr_handlers	NULL
static inline int f2fs_setxattr(struct inode *inode, int index,
		const char *name, const void *value, size_t size, int flags)
{
	return -EOPNOTSUPP;
}
static inline int f2fs_getxattr(struct inode *inode, int index,
		const char *name, void *buffer, size_t buffer_size)
{
	return -EOPNOTSUPP;
}
static inline ssize_t f2fs_listxattr(struct dentry *dentry, char *buffer,
		size_t buffer_size)
{
	return -EOPNOTSUPP;
}
#endif

#ifdef CONFIG_F2FS_FS_SECURITY
extern int f2fs_init_security(struct inode *, struct inode *,
				const struct qstr *, struct page *);
#else
static inline int f2fs_init_security(struct inode *inode, struct inode *dir,
				const struct qstr *qstr, struct page *ipage)
{
	return 0;
}
#endif
#endif /* __F2FS_XATTR_H__ */
