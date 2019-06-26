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

static void __fscrypt_decrypt_bio(struct bio *bio, bool done)
{
	struct bio_vec *bv;
	struct bvec_iter_all iter_all;

	bio_for_each_segment_all(bv, bio, iter_all) {
		struct page *page = bv->bv_page;
		int ret = fscrypt_decrypt_page(page->mapping->host, page,
				PAGE_SIZE, 0, page->index);

		if (ret)
			SetPageError(page);
		else if (done)
			SetPageUptodate(page);
		if (done)
			unlock_page(page);
	}
}

void fscrypt_decrypt_bio(struct bio *bio)
{
	__fscrypt_decrypt_bio(bio, false);
}
EXPORT_SYMBOL(fscrypt_decrypt_bio);

static void completion_pages(struct work_struct *work)
{
	struct fscrypt_ctx *ctx =
		container_of(work, struct fscrypt_ctx, r.work);
	struct bio *bio = ctx->r.bio;

	__fscrypt_decrypt_bio(bio, true);
	fscrypt_release_ctx(ctx);
	bio_put(bio);
}

void fscrypt_enqueue_decrypt_bio(struct fscrypt_ctx *ctx, struct bio *bio)
{
	INIT_WORK(&ctx->r.work, completion_pages);
	ctx->r.bio = bio;
	fscrypt_enqueue_decrypt_work(&ctx->r.work);
}
EXPORT_SYMBOL(fscrypt_enqueue_decrypt_bio);

void fscrypt_pullback_bio_page(struct page **page, bool restore)
{
	struct fscrypt_ctx *ctx;
	struct page *bounce_page;

	/* The bounce data pages are unmapped. */
	if ((*page)->mapping)
		return;

	/* The bounce data page is unmapped. */
	bounce_page = *page;
	ctx = (struct fscrypt_ctx *)page_private(bounce_page);

	/* restore control page */
	*page = ctx->w.control_page;

	if (restore)
		fscrypt_restore_control_page(bounce_page);
}
EXPORT_SYMBOL(fscrypt_pullback_bio_page);

int fscrypt_zeroout_range(const struct inode *inode, pgoff_t lblk,
				sector_t pblk, unsigned int len)
{
	struct fscrypt_ctx *ctx;
	struct page *ciphertext_page = NULL;
	struct bio *bio;
	int ret, err = 0;

	BUG_ON(inode->i_sb->s_blocksize != PAGE_SIZE);

	ctx = fscrypt_get_ctx(GFP_NOFS);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	ciphertext_page = fscrypt_alloc_bounce_page(ctx, GFP_NOWAIT);
	if (IS_ERR(ciphertext_page)) {
		err = PTR_ERR(ciphertext_page);
		goto errout;
	}

	while (len--) {
		err = fscrypt_do_page_crypto(inode, FS_ENCRYPT, lblk,
					     ZERO_PAGE(0), ciphertext_page,
					     PAGE_SIZE, 0, GFP_NOFS);
		if (err)
			goto errout;

		bio = bio_alloc(GFP_NOWAIT, 1);
		if (!bio) {
			err = -ENOMEM;
			goto errout;
		}
		bio_set_dev(bio, inode->i_sb->s_bdev);
		bio->bi_iter.bi_sector =
			pblk << (inode->i_sb->s_blocksize_bits - 9);
		bio_set_op_attrs(bio, REQ_OP_WRITE, 0);
		ret = bio_add_page(bio, ciphertext_page,
					inode->i_sb->s_blocksize, 0);
		if (ret != inode->i_sb->s_blocksize) {
			/* should never happen! */
			WARN_ON(1);
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
	fscrypt_release_ctx(ctx);
	return err;
}
EXPORT_SYMBOL(fscrypt_zeroout_range);
