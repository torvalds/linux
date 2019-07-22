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

	/**
	 * Read a Merkle tree page of the given inode.
	 *
	 * @inode: the inode
	 * @index: 0-based index of the page within the Merkle tree
	 *
	 * This can be called at any time on an open verity file, as well as
	 * between ->begin_enable_verity() and ->end_enable_verity().  It may be
	 * called by multiple processes concurrently, even with the same page.
	 *
	 * Note that this must retrieve a *page*, not necessarily a *block*.
	 *
	 * Return: the page on success, ERR_PTR() on failure
	 */
	struct page *(*read_merkle_tree_page)(struct inode *inode,
					      pgoff_t index);
};

#ifdef CONFIG_FS_VERITY

static inline struct fsverity_info *fsverity_get_info(const struct inode *inode)
{
	/* pairs with the cmpxchg() in fsverity_set_info() */
	return READ_ONCE(inode->i_verity_info);
}

/* open.c */

extern int fsverity_file_open(struct inode *inode, struct file *filp);
extern int fsverity_prepare_setattr(struct dentry *dentry, struct iattr *attr);
extern void fsverity_cleanup_inode(struct inode *inode);

/* verify.c */

extern bool fsverity_verify_page(struct page *page);
extern void fsverity_verify_bio(struct bio *bio);
extern void fsverity_enqueue_verify_work(struct work_struct *work);

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

static inline int fsverity_prepare_setattr(struct dentry *dentry,
					   struct iattr *attr)
{
	return IS_VERITY(d_inode(dentry)) ? -EOPNOTSUPP : 0;
}

static inline void fsverity_cleanup_inode(struct inode *inode)
{
}

/* verify.c */

static inline bool fsverity_verify_page(struct page *page)
{
	WARN_ON(1);
	return false;
}

static inline void fsverity_verify_bio(struct bio *bio)
{
	WARN_ON(1);
}

static inline void fsverity_enqueue_verify_work(struct work_struct *work)
{
	WARN_ON(1);
}

#endif	/* !CONFIG_FS_VERITY */

/**
 * fsverity_active() - do reads from the inode need to go through fs-verity?
 *
 * This checks whether ->i_verity_info has been set.
 *
 * Filesystems call this from ->readpages() to check whether the pages need to
 * be verified or not.  Don't use IS_VERITY() for this purpose; it's subject to
 * a race condition where the file is being read concurrently with
 * FS_IOC_ENABLE_VERITY completing.  (S_VERITY is set before ->i_verity_info.)
 */
static inline bool fsverity_active(const struct inode *inode)
{
	return fsverity_get_info(inode) != NULL;
}

#endif	/* _LINUX_FSVERITY_H */
