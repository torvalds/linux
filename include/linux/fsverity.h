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
#include <linux/mm.h>
#include <crypto/hash_info.h>
#include <crypto/sha2.h>
#include <uapi/linux/fsverity.h>

/*
 * Largest digest size among all hash algorithms supported by fs-verity.
 * Currently assumed to be <= size of fsverity_descriptor::root_hash.
 */
#define FS_VERITY_MAX_DIGEST_SIZE	SHA512_DIGEST_SIZE

/* Arbitrary limit to bound the kmalloc() size.  Can be changed. */
#define FS_VERITY_MAX_DESCRIPTOR_SIZE	16384

struct fsverity_info;

/* Verity operations for filesystems */
struct fsverity_operations {
	/**
	 * Begin enabling verity on the given file.
	 *
	 * @filp: a readonly file descriptor for the file
	 *
	 * The filesystem must do any needed filesystem-specific preparations
	 * for enabling verity, e.g. evicting inline data.  It also must return
	 * -EBUSY if verity is already being enabled on the given file.
	 *
	 * i_rwsem is held for write.
	 *
	 * Return: 0 on success, -errno on failure
	 */
	int (*begin_enable_verity)(struct file *filp);

	/**
	 * End enabling verity on the given file.
	 *
	 * @filp: a readonly file descriptor for the file
	 * @desc: the verity descriptor to write, or NULL on failure
	 * @desc_size: size of verity descriptor, or 0 on failure
	 * @merkle_tree_size: total bytes the Merkle tree took up
	 *
	 * If desc == NULL, then enabling verity failed and the filesystem only
	 * must do any necessary cleanups.  Else, it must also store the given
	 * verity descriptor to a fs-specific location associated with the inode
	 * and do any fs-specific actions needed to mark the inode as a verity
	 * inode, e.g. setting a bit in the on-disk inode.  The filesystem is
	 * also responsible for setting the S_VERITY flag in the VFS inode.
	 *
	 * i_rwsem is held for write, but it may have been dropped between
	 * ->begin_enable_verity() and ->end_enable_verity().
	 *
	 * Return: 0 on success, -errno on failure
	 */
	int (*end_enable_verity)(struct file *filp, const void *desc,
				 size_t desc_size, u64 merkle_tree_size);

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
	 * This can be called at any time on an open verity file.  It may be
	 * called by multiple processes concurrently, even with the same page.
	 *
	 * Note that this must retrieve a *page*, not necessarily a *block*.
	 *
	 * Return: the page on success, ERR_PTR() on failure
	 */
	struct page *(*read_merkle_tree_page)(struct inode *inode,
					      pgoff_t index);

	/**
	 * Perform readahead of a Merkle tree for the given inode.
	 *
	 * @inode: the inode
	 * @index: 0-based index of the first page within the Merkle tree
	 * @nr_pages: number of pages to be read ahead.
	 *
	 * This can be called at any time on an open verity file.  It may be
	 * called by multiple processes concurrently, even with the same range.
	 *
	 * Optional method so that ->read_merkle_tree_page preferably finds
	 * cached data instead of issuing dependent I/O.
	 */
	void (*readahead_merkle_tree)(struct inode *inode, pgoff_t index,
				      unsigned long nr_pages);

	/**
	 * Write a Merkle tree block to the given file.
	 *
	 * @file: the file for which the Merkle tree is being built
	 * @buf: the Merkle tree block to write
	 * @pos: the position of the block in the Merkle tree (in bytes)
	 * @size: the Merkle tree block size (in bytes)
	 *
	 * This is only called between ->begin_enable_verity() and
	 * ->end_enable_verity().
	 *
	 * Return: 0 on success, -errno on failure
	 */
	int (*write_merkle_tree_block)(struct file *file, const void *buf,
				       u64 pos, unsigned int size);
};

#ifdef CONFIG_FS_VERITY
/**
 * fsverity_active() - do reads from the inode need to go through fs-verity?
 * @inode: inode to check
 *
 * This checks whether the inode's verity info has been set, and reads need
 * to verify the file data.
 *
 * Return: true if reads need to go through fs-verity, otherwise false
 */
static inline bool fsverity_active(const struct inode *inode)
{
	if (IS_VERITY(inode)) {
		/*
		 * This pairs with the try_cmpxchg in set_mask_bits()
		 * used to set the S_VERITY bit in i_flags.
		 */
		smp_mb();
		return true;
	}

	return false;
}

struct fsverity_info *__fsverity_get_info(const struct inode *inode);
/**
 * fsverity_get_info - get fsverity information for an inode
 * @inode: inode to operate on.
 *
 * This gets the fsverity_info for @inode if it exists.  Safe to call without
 * knowin that a fsverity_info exist for @inode, including on file systems that
 * do not support fsverity.
 */
static inline struct fsverity_info *fsverity_get_info(const struct inode *inode)
{
	if (!fsverity_active(inode))
		return NULL;
	return __fsverity_get_info(inode);
}

/* enable.c */

int fsverity_ioctl_enable(struct file *filp, const void __user *arg);

/* measure.c */

int fsverity_ioctl_measure(struct file *filp, void __user *arg);
int fsverity_get_digest(struct inode *inode,
			u8 raw_digest[FS_VERITY_MAX_DIGEST_SIZE],
			u8 *alg, enum hash_algo *halg);

/* open.c */

int __fsverity_file_open(struct inode *inode, struct file *filp);

/* read_metadata.c */

int fsverity_ioctl_read_metadata(struct file *filp, const void __user *uarg);

/* verify.c */

bool fsverity_verify_blocks(struct fsverity_info *vi, struct folio *folio,
			    size_t len, size_t offset);
void fsverity_verify_bio(struct fsverity_info *vi, struct bio *bio);
void fsverity_enqueue_verify_work(struct work_struct *work);

#else /* !CONFIG_FS_VERITY */

static inline bool fsverity_active(const struct inode *inode)
{
	return false;
}

static inline struct fsverity_info *fsverity_get_info(const struct inode *inode)
{
	return NULL;
}

/* enable.c */

static inline int fsverity_ioctl_enable(struct file *filp,
					const void __user *arg)
{
	return -EOPNOTSUPP;
}

/* measure.c */

static inline int fsverity_ioctl_measure(struct file *filp, void __user *arg)
{
	return -EOPNOTSUPP;
}

static inline int fsverity_get_digest(struct inode *inode,
				      u8 raw_digest[FS_VERITY_MAX_DIGEST_SIZE],
				      u8 *alg, enum hash_algo *halg)
{
	/*
	 * fsverity is not enabled in the kernel configuration, so always report
	 * that the file doesn't have fsverity enabled (digest size 0).
	 */
	return 0;
}

/* open.c */

static inline int __fsverity_file_open(struct inode *inode, struct file *filp)
{
	return -EOPNOTSUPP;
}

/* read_metadata.c */

static inline int fsverity_ioctl_read_metadata(struct file *filp,
					       const void __user *uarg)
{
	return -EOPNOTSUPP;
}

/* verify.c */

static inline bool fsverity_verify_blocks(struct fsverity_info *vi,
					  struct folio *folio, size_t len,
					  size_t offset)
{
	WARN_ON_ONCE(1);
	return false;
}

static inline void fsverity_verify_bio(struct fsverity_info *vi,
				       struct bio *bio)
{
	WARN_ON_ONCE(1);
}

static inline void fsverity_enqueue_verify_work(struct work_struct *work)
{
	WARN_ON_ONCE(1);
}

#endif	/* !CONFIG_FS_VERITY */

static inline bool fsverity_verify_folio(struct fsverity_info *vi,
					 struct folio *folio)
{
	return fsverity_verify_blocks(vi, folio, folio_size(folio), 0);
}

static inline bool fsverity_verify_page(struct fsverity_info *vi,
					struct page *page)
{
	return fsverity_verify_blocks(vi, page_folio(page), PAGE_SIZE, 0);
}

/**
 * fsverity_file_open() - prepare to open a verity file
 * @inode: the inode being opened
 * @filp: the struct file being set up
 *
 * When opening a verity file, deny the open if it is for writing.  Otherwise,
 * set up the inode's verity info if not already done.
 *
 * When combined with fscrypt, this must be called after fscrypt_file_open().
 * Otherwise, we won't have the key set up to decrypt the verity metadata.
 *
 * Return: 0 on success, -errno on failure
 */
static inline int fsverity_file_open(struct inode *inode, struct file *filp)
{
	if (IS_VERITY(inode))
		return __fsverity_file_open(inode, filp);
	return 0;
}

void fsverity_cleanup_inode(struct inode *inode);
void fsverity_readahead(struct fsverity_info *vi, pgoff_t index,
			unsigned long nr_pages);

struct page *generic_read_merkle_tree_page(struct inode *inode, pgoff_t index);
void generic_readahead_merkle_tree(struct inode *inode, pgoff_t index,
				   unsigned long nr_pages);

#endif	/* _LINUX_FSVERITY_H */
