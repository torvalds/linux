// SPDX-License-Identifier: GPL-2.0
/*
 * This contains encryption functions for per-file encryption.
 *
 * Copyright (C) 2015, Google, Inc.
 * Copyright (C) 2015, Motorola Mobility
 *
 * Written by Michael Halcrow, 2014.
 *
 * Filename encryption additions
 *	Uday Savagaonkar, 2014
 * Encryption policy handling additions
 *	Ildar Muslukhov, 2014
 * Add fscrypt_pullback_bio_page()
 *	Jaegeuk Kim, 2015.
 *
 * This has not yet undergone a rigorous security audit.
 *
 * The usage of AES-XTS should conform to recommendations in NIST
 * Special Publication 800-38E and IEEE P1619/D16.
 */

#include <linux/pagemap.h>
#include <linux/module.h>
#include <linux/bio.h>
#include <linux/namei.h>
#include "fscrypt_private.h"

void fscrypt_decrypt_bio(struct bio *bio)
{
	struct bio_vec *bv;
	int i;

	bio_for_each_segment_all(bv, bio, i) {
		struct page *page = bv->bv_page;
		int ret = fscrypt_decrypt_pagecache_blocks(page, bv->bv_len,
							   bv->bv_offset);
		if (ret)
			SetPageError(page);
	}
}
EXPORT_SYMBOL(fscrypt_decrypt_bio);

int fscrypt_zeroout_range(const struct inode *inode, pgoff_t lblk,
				sector_t pblk, unsigned int len)
{
	const unsigned int blockbits = inode->i_blkbits;
	const unsigned int blocksize = 1 << blockbits;
	struct page *ciphertext_page;
	struct bio *bio;
	int ret, err = 0;

	ciphertext_page = fscrypt_alloc_bounce_page(GFP_NOWAIT);
	if (!ciphertext_page)
		return -ENOMEM;

	while (len--) {
		err = fscrypt_crypt_block(inode, FS_ENCRYPT, lblk,
					  ZERO_PAGE(0), ciphertext_page,
					  blocksize, 0, GFP_NOFS);
		if (err)
			goto errout;

		bio = bio_alloc(GFP_NOWAIT, 1);
		if (!bio) {
			err = -ENOMEM;
			goto errout;
		}
		bio_set_dev(bio, inode->i_sb->s_bdev);
		bio->bi_iter.bi_sector = pblk << (blockbits - 9);
		bio_set_op_attrs(bio, REQ_OP_WRITE, 0);
		ret = bio_add_page(bio, ciphertext_page, blocksize, 0);
		if (WARN_ON(ret != blocksize)) {
			/* should never happen! */
			bio_put(bio);
			err = -EIO;
			goto errout;
		}
		err = submit_bio_wait(bio);
		if (err == 0 && bio->bi_status)
			err = -EIO;
		bio_put(bio);
		if (err)
			goto errout;
		lblk++;
		pblk++;
	}
	err = 0;
errout:
	fscrypt_free_bounce_page(ciphertext_page);
	return err;
}
EXPORT_SYMBOL(fscrypt_zeroout_range);
