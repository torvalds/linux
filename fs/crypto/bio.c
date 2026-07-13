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

struct fscrypt_zero_done {
	atomic_t		pending;
	blk_status_t		status;
	struct completion	done;
};

static void fscrypt_zeroout_range_done(struct fscrypt_zero_done *done)
{
	if (atomic_dec_and_test(&done->pending))
		complete(&done->done);
}

static void fscrypt_zeroout_range_end_io(struct bio *bio)
{
	struct fscrypt_zero_done *done = bio->bi_private;

	if (bio->bi_status)
		cmpxchg(&done->status, 0, bio->bi_status);
	fscrypt_zeroout_range_done(done);
	bio_put(bio);
}

/**
 * fscrypt_zeroout_range() - zero out a range of blocks in an encrypted file
 * @inode: the file's inode
 * @pos: the first file position (in bytes) to zero out
 * @sector: the first sector to zero out
 * @len: bytes to zero out
 *
 * Zero out filesystem blocks in an encrypted regular file on-disk, i.e. write
 * ciphertext blocks which decrypt to the all-zeroes block.  The blocks must be
 * both logically and physically contiguous.  It's also assumed that the
 * filesystem only uses a single block device, ->s_bdev.  @len must be a
 * multiple of the file system logical block size.
 *
 * Note that since each block uses a different IV, this involves writing a
 * different ciphertext to each block; we can't simply reuse the same one.
 *
 * Return: 0 on success; -errno on failure.
 */
int fscrypt_zeroout_range(const struct inode *inode, loff_t pos,
			  sector_t sector, u64 len)
{
	struct fscrypt_zero_done done = {
		.pending	= ATOMIC_INIT(1),
		.done		= COMPLETION_INITIALIZER_ONSTACK(done.done),
	};

	if (len == 0)
		return 0;

	do {
		struct bio *bio;
		unsigned int n;

		bio = bio_alloc(inode->i_sb->s_bdev, BIO_MAX_VECS, REQ_OP_WRITE,
				GFP_NOFS);
		bio->bi_iter.bi_sector = sector;
		bio->bi_private = &done;
		bio->bi_end_io = fscrypt_zeroout_range_end_io;
		fscrypt_set_bio_crypt_ctx(bio, inode, pos, GFP_NOFS);

		for (n = 0; n < BIO_MAX_VECS; n++) {
			unsigned int bytes_this_page = min(len, PAGE_SIZE);

			__bio_add_page(bio, ZERO_PAGE(0), bytes_this_page, 0);
			len -= bytes_this_page;
			pos += bytes_this_page;
			sector += (bytes_this_page >> SECTOR_SHIFT);
			if (!len || !fscrypt_mergeable_bio(bio, inode, pos))
				break;
		}

		atomic_inc(&done.pending);
		blk_crypto_submit_bio(bio);
	} while (len);

	fscrypt_zeroout_range_done(&done);

	wait_for_completion(&done.done);
	return blk_status_to_errno(done.status);
}
EXPORT_SYMBOL(fscrypt_zeroout_range);
