// SPDX-License-Identifier: GPL-2.0
/*
 * Utility functions for file contents encryption/decryption on
 * block device-based filesystems.
 *
 * Copyright (C) 2015, Google, Inc.
 * Copyright (C) 2015, Motorola Mobility
 */

#include <linux/bio.h>
#include <linux/export.h>
#include <linux/module.h>
#include <linux/namei.h>
#include <linux/pagemap.h>

#include "fscrypt_private.h"

/**
 * fscrypt_decrypt_bio() - decrypt the contents of a bio
 * @bio: the bio to decrypt
 *
 * Decrypt the contents of a "read" bio following successful completion of the
 * underlying disk read.  The bio must be reading a whole number of blocks of an
 * encrypted file directly into the page cache.  If the bio is reading the
 * ciphertext into bounce pages instead of the page cache (for example, because
 * the file is also compressed, so decompression is required after decryption),
 * then this function isn't applicable.  This function may sleep, so it must be
 * called from a workqueue rather than from the bio's bi_end_io callback.
 *
 * Return: %true on success; %false on failure.  On failure, bio->bi_status is
 *	   also set to an error status.
 */
bool fscrypt_decrypt_bio(struct bio *bio)
{
	struct folio_iter fi;

	bio_for_each_folio_all(fi, bio) {
		int err = fscrypt_decrypt_pagecache_blocks(fi.folio, fi.length,
							   fi.offset);

		if (err) {
			bio->bi_status = errno_to_blk_status(err);
			return false;
		}
	}
	return true;
}
EXPORT_SYMBOL(fscrypt_decrypt_bio);

static int fscrypt_zeroout_range_inline_crypt(const struct inode *inode,
					      pgoff_t lblk, sector_t pblk,
					      unsigned int len)
{
	const unsigned int blockbits = inode->i_blkbits;
	const unsigned int blocks_per_page = 1 << (PAGE_SHIFT - blockbits);
	struct bio *bio;
	int ret, err = 0;
	int num_pages = 0;

	/* This always succeeds since __GFP_DIRECT_RECLAIM is set. */
	bio = bio_alloc(inode->i_sb->s_bdev, BIO_MAX_VECS, REQ_OP_WRITE,
			GFP_NOFS);

	while (len) {
		unsigned int blocks_this_page = min(len, blocks_per_page);
		unsigned int bytes_this_page = blocks_this_page << blockbits;

		if (num_pages == 0) {
			fscrypt_set_bio_crypt_ctx(bio, inode, lblk, GFP_NOFS);
			bio->bi_iter.bi_sector =
					pblk << (blockbits - SECTOR_SHIFT);
		}
		ret = bio_add_page(bio, ZERO_PAGE(0), bytes_this_page, 0);
		if (WARN_ON_ONCE(ret != bytes_this_page)) {
			err = -EIO;
			goto out;
		}
		num_pages++;
		len -= blocks_this_page;
		lblk += blocks_this_page;
		pblk += blocks_this_page;
		if (num_pages == BIO_MAX_VECS || !len ||
		    !fscrypt_mergeable_bio(bio, inode, lblk)) {
			err = submit_bio_wait(bio);
			if (err)
				goto out;
			bio_reset(bio, inode->i_sb->s_bdev, REQ_OP_WRITE);
			num_pages = 0;
		}
	}
out:
	bio_put(bio);
	return err;
}

/**
 * fscrypt_zeroout_range() - zero out a range of blocks in an encrypted file
 * @inode: the file's inode
 * @lblk: the first file logical block to zero out
 * @pblk: the first filesystem physical block to zero out
 * @len: number of blocks to zero out
 *
 * Zero out filesystem blocks in an encrypted regular file on-disk, i.e. write
 * ciphertext blocks which decrypt to the all-zeroes block.  The blocks must be
 * both logically and physically contiguous.  It's also assumed that the
 * filesystem only uses a single block device, ->s_bdev.
 *
 * Note that since each block uses a different IV, this involves writing a
 * different ciphertext to each block; we can't simply reuse the same one.
 *
 * Return: 0 on success; -errno on failure.
 */
int fscrypt_zeroout_range(const struct inode *inode, pgoff_t lblk,
			  sector_t pblk, unsigned int len)
{
	const struct fscrypt_inode_info *ci = fscrypt_get_inode_info_raw(inode);
	const unsigned int du_bits = ci->ci_data_unit_bits;
	const unsigned int du_size = 1U << du_bits;
	const unsigned int du_per_page_bits = PAGE_SHIFT - du_bits;
	const unsigned int du_per_page = 1U << du_per_page_bits;
	u64 du_index = (u64)lblk << (inode->i_blkbits - du_bits);
	u64 du_remaining = (u64)len << (inode->i_blkbits - du_bits);
	sector_t sector = pblk << (inode->i_blkbits - SECTOR_SHIFT);
	struct page *pages[16]; /* write up to 16 pages at a time */
	unsigned int nr_pages;
	unsigned int i;
	unsigned int offset;
	struct bio *bio;
	int ret, err;

	if (len == 0)
		return 0;

	if (fscrypt_inode_uses_inline_crypto(inode))
		return fscrypt_zeroout_range_inline_crypt(inode, lblk, pblk,
							  len);

	BUILD_BUG_ON(ARRAY_SIZE(pages) > BIO_MAX_VECS);
	nr_pages = min_t(u64, ARRAY_SIZE(pages),
			 (du_remaining + du_per_page - 1) >> du_per_page_bits);

	/*
	 * We need at least one page for ciphertext.  Allocate the first one
	 * from a mempool, with __GFP_DIRECT_RECLAIM set so that it can't fail.
	 *
	 * Any additional page allocations are allowed to fail, as they only
	 * help performance, and waiting on the mempool for them could deadlock.
	 */
	for (i = 0; i < nr_pages; i++) {
		pages[i] = fscrypt_alloc_bounce_page(i == 0 ? GFP_NOFS :
						     GFP_NOWAIT);
		if (!pages[i])
			break;
	}
	nr_pages = i;
	if (WARN_ON_ONCE(nr_pages <= 0))
		return -EINVAL;

	/* This always succeeds since __GFP_DIRECT_RECLAIM is set. */
	bio = bio_alloc(inode->i_sb->s_bdev, nr_pages, REQ_OP_WRITE, GFP_NOFS);

	do {
		bio->bi_iter.bi_sector = sector;

		i = 0;
		offset = 0;
		do {
			err = fscrypt_crypt_data_unit(ci, FS_ENCRYPT, du_index,
						      ZERO_PAGE(0), pages[i],
						      du_size, offset);
			if (err)
				goto out;
			du_index++;
			sector += 1U << (du_bits - SECTOR_SHIFT);
			du_remaining--;
			offset += du_size;
			if (offset == PAGE_SIZE || du_remaining == 0) {
				ret = bio_add_page(bio, pages[i++], offset, 0);
				if (WARN_ON_ONCE(ret != offset)) {
					err = -EIO;
					goto out;
				}
				offset = 0;
			}
		} while (i != nr_pages && du_remaining != 0);

		err = submit_bio_wait(bio);
		if (err)
			goto out;
		bio_reset(bio, inode->i_sb->s_bdev, REQ_OP_WRITE);
	} while (du_remaining != 0);
	err = 0;
out:
	bio_put(bio);
	for (i = 0; i < nr_pages; i++)
		fscrypt_free_bounce_page(pages[i]);
	return err;
}
EXPORT_SYMBOL(fscrypt_zeroout_range);
