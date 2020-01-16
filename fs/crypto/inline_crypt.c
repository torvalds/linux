// SPDX-License-Identifier: GPL-2.0
/*
 * Inline encryption support for fscrypt
 *
 * Copyright 2019 Google LLC
 */

/*
 * With "inline encryption", the block layer handles the decryption/encryption
 * as part of the bio, instead of the filesystem doing the crypto itself via
 * crypto API.  See Documentation/block/inline-encryption.rst.  fscrypt still
 * provides the key and IV to use.
 */

#include <linux/blk-crypto.h>
#include <linux/blkdev.h>
#include <linux/buffer_head.h>
#include <linux/keyslot-manager.h>

#include "fscrypt_private.h"

struct fscrypt_blk_crypto_key {
	struct blk_crypto_key base;
	int num_devs;
	struct request_queue *devs[];
};

/* Enable inline encryption for this file if supported. */
void fscrypt_select_encryption_impl(struct fscrypt_info *ci)
{
	const struct inode *inode = ci->ci_inode;
	struct super_block *sb = inode->i_sb;

	/* The file must need contents encryption, not filenames encryption */
	if (!S_ISREG(inode->i_mode))
		return;

	/* blk-crypto must implement the needed encryption algorithm */
	if (ci->ci_mode->blk_crypto_mode == BLK_ENCRYPTION_MODE_INVALID)
		return;

	/* The filesystem must be mounted with -o inlinecrypt */
	if (!sb->s_cop->inline_crypt_enabled ||
	    !sb->s_cop->inline_crypt_enabled(sb))
		return;

	ci->ci_inlinecrypt = true;
}

int fscrypt_prepare_inline_crypt_key(struct fscrypt_prepared_key *prep_key,
				     const u8 *raw_key,
				     unsigned int raw_key_size,
				     const struct fscrypt_info *ci)
{
	const struct inode *inode = ci->ci_inode;
	struct super_block *sb = inode->i_sb;
	enum blk_crypto_mode_num crypto_mode = ci->ci_mode->blk_crypto_mode;
	int num_devs = 1;
	int queue_refs = 0;
	struct fscrypt_blk_crypto_key *blk_key;
	int err;
	int i;

	if (sb->s_cop->get_num_devices)
		num_devs = sb->s_cop->get_num_devices(sb);
	if (WARN_ON(num_devs < 1))
		return -EINVAL;

	blk_key = kzalloc(struct_size(blk_key, devs, num_devs), GFP_NOFS);
	if (!blk_key)
		return -ENOMEM;

	blk_key->num_devs = num_devs;
	if (num_devs == 1)
		blk_key->devs[0] = bdev_get_queue(sb->s_bdev);
	else
		sb->s_cop->get_devices(sb, blk_key->devs);

	BUILD_BUG_ON(FSCRYPT_MAX_HW_WRAPPED_KEY_SIZE >
		     BLK_CRYPTO_MAX_WRAPPED_KEY_SIZE);

	err = blk_crypto_init_key(&blk_key->base, raw_key, raw_key_size,
				  crypto_mode, sb->s_blocksize);
	if (err) {
		fscrypt_err(inode, "error %d initializing blk-crypto key", err);
		goto fail;
	}

	/*
	 * We have to start using blk-crypto on all the filesystem's devices.
	 * We also have to save all the request_queue's for later so that the
	 * key can be evicted from them.  This is needed because some keys
	 * aren't destroyed until after the filesystem was already unmounted
	 * (namely, the per-mode keys in struct fscrypt_master_key).
	 */
	for (i = 0; i < num_devs; i++) {
		if (!blk_get_queue(blk_key->devs[i])) {
			fscrypt_err(inode, "couldn't get request_queue");
			err = -EAGAIN;
			goto fail;
		}
		queue_refs++;

		err = blk_crypto_start_using_mode(crypto_mode, sb->s_blocksize,
						  blk_key->devs[i]);
		if (err) {
			fscrypt_err(inode,
				    "error %d starting to use blk-crypto", err);
			goto fail;
		}
	}
	/*
	 * Pairs with READ_ONCE() in fscrypt_is_key_prepared().  (Only matters
	 * for the per-mode keys, which are shared by multiple inodes.)
	 */
	smp_store_release(&prep_key->blk_key, blk_key);
	return 0;

fail:
	for (i = 0; i < queue_refs; i++)
		blk_put_queue(blk_key->devs[i]);
	kzfree(blk_key);
	return err;
}

void fscrypt_destroy_inline_crypt_key(struct fscrypt_prepared_key *prep_key)
{
	struct fscrypt_blk_crypto_key *blk_key = prep_key->blk_key;
	int i;

	if (blk_key) {
		for (i = 0; i < blk_key->num_devs; i++) {
			blk_crypto_evict_key(blk_key->devs[i], &blk_key->base);
			blk_put_queue(blk_key->devs[i]);
		}
		kzfree(blk_key);
	}
}

int fscrypt_derive_raw_secret(struct super_block *sb,
			      const u8 *wrapped_key,
			      unsigned int wrapped_key_size,
			      u8 *raw_secret, unsigned int raw_secret_size)
{
	struct request_queue *q;

	q = sb->s_bdev->bd_queue;
	if (!q->ksm)
		return -EOPNOTSUPP;

	return keyslot_manager_derive_raw_secret(q->ksm,
						 wrapped_key, wrapped_key_size,
						 raw_secret, raw_secret_size);
}

/**
 * fscrypt_inode_uses_inline_crypto - test whether an inode uses inline
 *				      encryption
 * @inode: an inode
 *
 * Return: true if the inode requires file contents encryption and if the
 *	   encryption should be done in the block layer via blk-crypto rather
 *	   than in the filesystem layer.
 */
bool fscrypt_inode_uses_inline_crypto(const struct inode *inode)
{
	return IS_ENCRYPTED(inode) && S_ISREG(inode->i_mode) &&
		inode->i_crypt_info->ci_inlinecrypt;
}
EXPORT_SYMBOL_GPL(fscrypt_inode_uses_inline_crypto);

/**
 * fscrypt_inode_uses_fs_layer_crypto - test whether an inode uses fs-layer
 *					encryption
 * @inode: an inode
 *
 * Return: true if the inode requires file contents encryption and if the
 *	   encryption should be done in the filesystem layer rather than in the
 *	   block layer via blk-crypto.
 */
bool fscrypt_inode_uses_fs_layer_crypto(const struct inode *inode)
{
	return IS_ENCRYPTED(inode) && S_ISREG(inode->i_mode) &&
		!inode->i_crypt_info->ci_inlinecrypt;
}
EXPORT_SYMBOL_GPL(fscrypt_inode_uses_fs_layer_crypto);

static void fscrypt_generate_dun(const struct fscrypt_info *ci, u64 lblk_num,
				 u64 dun[BLK_CRYPTO_DUN_ARRAY_SIZE])
{
	union fscrypt_iv iv;
	int i;

	fscrypt_generate_iv(&iv, lblk_num, ci);

	BUILD_BUG_ON(FSCRYPT_MAX_IV_SIZE > BLK_CRYPTO_MAX_IV_SIZE);
	memset(dun, 0, BLK_CRYPTO_MAX_IV_SIZE);
	for (i = 0; i < ci->ci_mode->ivsize/sizeof(dun[0]); i++)
		dun[i] = le64_to_cpu(iv.dun[i]);
}

/**
 * fscrypt_set_bio_crypt_ctx - prepare a file contents bio for inline encryption
 * @bio: a bio which will eventually be submitted to the file
 * @inode: the file's inode
 * @first_lblk: the first file logical block number in the I/O
 * @gfp_mask: memory allocation flags - these must be a waiting mask so that
 *					bio_crypt_set_ctx can't fail.
 *
 * If the contents of the file should be encrypted (or decrypted) with inline
 * encryption, then assign the appropriate encryption context to the bio.
 *
 * Normally the bio should be newly allocated (i.e. no pages added yet), as
 * otherwise fscrypt_mergeable_bio() won't work as intended.
 *
 * The encryption context will be freed automatically when the bio is freed.
 */
void fscrypt_set_bio_crypt_ctx(struct bio *bio, const struct inode *inode,
			       u64 first_lblk, gfp_t gfp_mask)
{
	const struct fscrypt_info *ci = inode->i_crypt_info;
	u64 dun[BLK_CRYPTO_DUN_ARRAY_SIZE];

	if (!fscrypt_inode_uses_inline_crypto(inode))
		return;

	fscrypt_generate_dun(ci, first_lblk, dun);
	bio_crypt_set_ctx(bio, &ci->ci_key.blk_key->base, dun, gfp_mask);
}
EXPORT_SYMBOL_GPL(fscrypt_set_bio_crypt_ctx);

/* Extract the inode and logical block number from a buffer_head. */
static bool bh_get_inode_and_lblk_num(const struct buffer_head *bh,
				      const struct inode **inode_ret,
				      u64 *lblk_num_ret)
{
	struct page *page = bh->b_page;
	const struct address_space *mapping;
	const struct inode *inode;

	/*
	 * The ext4 journal (jbd2) can submit a buffer_head it directly created
	 * for a non-pagecache page.  fscrypt doesn't care about these.
	 */
	mapping = page_mapping(page);
	if (!mapping)
		return false;
	inode = mapping->host;

	*inode_ret = inode;
	*lblk_num_ret = ((u64)page->index << (PAGE_SHIFT - inode->i_blkbits)) +
			(bh_offset(bh) >> inode->i_blkbits);
	return true;
}

/**
 * fscrypt_set_bio_crypt_ctx_bh - prepare a file contents bio for inline
 *				  encryption
 * @bio: a bio which will eventually be submitted to the file
 * @first_bh: the first buffer_head for which I/O will be submitted
 * @gfp_mask: memory allocation flags
 *
 * Same as fscrypt_set_bio_crypt_ctx(), except this takes a buffer_head instead
 * of an inode and block number directly.
 */
void fscrypt_set_bio_crypt_ctx_bh(struct bio *bio,
				 const struct buffer_head *first_bh,
				 gfp_t gfp_mask)
{
	const struct inode *inode;
	u64 first_lblk;

	if (bh_get_inode_and_lblk_num(first_bh, &inode, &first_lblk))
		fscrypt_set_bio_crypt_ctx(bio, inode, first_lblk, gfp_mask);
}
EXPORT_SYMBOL_GPL(fscrypt_set_bio_crypt_ctx_bh);

/**
 * fscrypt_mergeable_bio - test whether data can be added to a bio
 * @bio: the bio being built up
 * @inode: the inode for the next part of the I/O
 * @next_lblk: the next file logical block number in the I/O
 *
 * When building a bio which may contain data which should undergo inline
 * encryption (or decryption) via fscrypt, filesystems should call this function
 * to ensure that the resulting bio contains only logically contiguous data.
 * This will return false if the next part of the I/O cannot be merged with the
 * bio because either the encryption key would be different or the encryption
 * data unit numbers would be discontiguous.
 *
 * fscrypt_set_bio_crypt_ctx() must have already been called on the bio.
 *
 * Return: true iff the I/O is mergeable
 */
bool fscrypt_mergeable_bio(struct bio *bio, const struct inode *inode,
			   u64 next_lblk)
{
	const struct bio_crypt_ctx *bc = bio->bi_crypt_context;
	u64 next_dun[BLK_CRYPTO_DUN_ARRAY_SIZE];

	if (!!bc != fscrypt_inode_uses_inline_crypto(inode))
		return false;
	if (!bc)
		return true;

	/*
	 * Comparing the key pointers is good enough, as all I/O for each key
	 * uses the same pointer.  I.e., there's currently no need to support
	 * merging requests where the keys are the same but the pointers differ.
	 */
	if (bc->bc_key != &inode->i_crypt_info->ci_key.blk_key->base)
		return false;

	fscrypt_generate_dun(inode->i_crypt_info, next_lblk, next_dun);
	return bio_crypt_dun_is_contiguous(bc, bio->bi_iter.bi_size, next_dun);
}
EXPORT_SYMBOL_GPL(fscrypt_mergeable_bio);

/**
 * fscrypt_mergeable_bio_bh - test whether data can be added to a bio
 * @bio: the bio being built up
 * @next_bh: the next buffer_head for which I/O will be submitted
 *
 * Same as fscrypt_mergeable_bio(), except this takes a buffer_head instead of
 * an inode and block number directly.
 *
 * Return: true iff the I/O is mergeable
 */
bool fscrypt_mergeable_bio_bh(struct bio *bio,
			      const struct buffer_head *next_bh)
{
	const struct inode *inode;
	u64 next_lblk;

	if (!bh_get_inode_and_lblk_num(next_bh, &inode, &next_lblk))
		return !bio->bi_crypt_context;

	return fscrypt_mergeable_bio(bio, inode, next_lblk);
}
EXPORT_SYMBOL_GPL(fscrypt_mergeable_bio_bh);
