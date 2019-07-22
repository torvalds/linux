/* SPDX-License-Identifier: GPL-2.0 */
/*
 * fs-verity: read-only file-based authenticity protection
 *
 * This header declares the interface between the fs/verity/ support layer and
 * filesystems that support fs-verity.
 *
 * Copyright 2019 Google LLC
 */

#ifndef _LINUX_FSVERITY_H
#define _LINUX_FSVERITY_H

#include <linux/fs.h>
#include <uapi/linux/fsverity.h>

/* Verity operations for filesystems */
struct fsverity_operations {

	/**
	 * Get the verity descriptor of the given inode.
	 *
	 * @inode: an inode with the S_VERITY flag set
	 * @buf: buffer in which to place the verity descriptor
	 * @bufsize: size of @buf, or 0 to retrieve the size only
	 *
	 * If bufsize == 0, then the size of the verity descriptor is returned.
	 * Otherwise the verity descriptor is written to 'buf' and its actual
	 * size is returned; -ERANGE is returned if it's too large.  This may be
	 * called by multiple processes concurrently on the same inode.
	 *
	 * Return: the size on success, -errno on failure
	 */
	int (*get_verity_descriptor)(struct inode *inode, void *buf,
				     size_t bufsize);
};

#ifdef CONFIG_FS_VERITY

static inline struct fsverity_info *fsverity_get_info(const struct inode *inode)
{
	/* pairs with the cmpxchg() in fsverity_set_info() */
	return READ_ONCE(inode->i_verity_info);
}

/* open.c */

extern int fsverity_file_open(struct inode *inode, struct file *filp);
extern void fsverity_cleanup_inode(struct inode *inode);

#else /* !CONFIG_FS_VERITY */

static inline struct fsverity_info *fsverity_get_info(const struct inode *inode)
{
	return NULL;
}

/* open.c */

static inline int fsverity_file_open(struct inode *inode, struct file *filp)
{
	return IS_VERITY(inode) ? -EOPNOTSUPP : 0;
}

static inline void fsverity_cleanup_inode(struct inode *inode)
{
}

#endif	/* !CONFIG_FS_VERITY */

#endif	/* _LINUX_FSVERITY_H */
