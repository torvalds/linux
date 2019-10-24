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

/* Return true iff inline encryption should be used for this file */
bool fscrypt_should_use_inline_encryption(const struct fscrypt_info *ci)
{
	const struct inode *inode = ci->ci_inode;
	struct super_block *sb = inode->i_sb;

	/* The file must need contents encryption, not filenames encryption */
	if (!S_ISREG(inode->i_mode))
		return false;

	/* blk-crypto must implement the needed encryption algorithm */
	if (ci->ci_mode->blk_crypto_mode == BLK_ENCRYPTION_MODE_INVALID)
		return false;

	/* DIRECT_KEY needs a 24+ byte IV, so it can't work with 8-byte DUNs */
	if (fscrypt_is_direct_key_policy(&ci->ci_policy))
		return false;

	/* The filesystem must be mounted with -o inlinecrypt */
	if (!sb->s_cop->inline_crypt_enabled ||
	    !sb->s_cop->inline_crypt_enabled(sb))
		return false;

	return true;
}

/* Set a per-file inline encryption key (for passing to blk-crypto) */
int fscrypt_set_inline_crypt_key(struct fscrypt_info *ci, const u8 *derived_key)
{
	const struct fscrypt_mode *mode = ci->ci_mode;
	const struct super_block *sb = ci->ci_inode->i_sb;

	ci->ci_inline_crypt_key = kmemdup(derived_key, mode->keysize, GFP_NOFS);
	if (!ci->ci_inline_crypt_key)
		return -ENOMEM;
	ci->ci_owns_key = true;

	return blk_crypto_start_using_mode(mode->blk_crypto_mode,
					   sb->s_blocksize,
					   sb->s_bdev->bd_queue);
}

/* Free a per-file inline encryption key and evict it from blk-crypto */
void fscrypt_free_inline_crypt_key(struct fscrypt_info *ci)
{
	if (ci->ci_inline_crypt_key != NULL) {
		const struct fscrypt_mode *mode = ci->ci_mode;
		const struct super_block *sb = ci->ci_inode->i_sb;

		blk_crypto_evict_key(sb->s_bdev->bd_queue,
				     ci->ci_inline_crypt_key,
				     mode->blk_crypto_mode, sb->s_blocksize);
		kzfree(ci->ci_inline_crypt_key);
	}
}

/*
 * Set up ->inline_crypt_key (for passing to blk-crypto) for inodes which use an
 * IV_INO_LBLK_64 encryption policy.
 *
 * Return: 0 on success, -errno on failure
 */
int fscrypt_setup_per_mode_inline_crypt_key(struct fscrypt_info *ci,
					    struct fscrypt_master_key *mk)
{
	static DEFINE_MUTEX(inline_crypt_setup_mutex);
	const struct super_block *sb = ci->ci_inode->i_sb;
	struct block_device *bdev = sb->s_bdev;
	const struct fscrypt_mode *mode = ci->ci_mode;
	const u8 mode_num = mode - fscrypt_modes;
	u8 *raw_key;
	u8 hkdf_info[sizeof(mode_num) + sizeof(sb->s_uuid)];
	int err;

	if (WARN_ON(mode_num > __FSCRYPT_MODE_MAX))
		return -EINVAL;

	/* pairs with smp_store_release() below */
	raw_key = smp_load_acquire(&mk->mk_iv_ino_lblk_64_raw_keys[mode_num]);
	if (raw_key) {
		err = 0;
		goto out;
	}

	mutex_lock(&inline_crypt_setup_mutex);

	raw_key = mk->mk_iv_ino_lblk_64_raw_keys[mode_num];
	if (raw_key) {
		err = 0;
		goto out_unlock;
	}

	raw_key = kmalloc(mode->keysize, GFP_NOFS);
	if (!raw_key) {
		err = -ENOMEM;
		goto out_unlock;
	}

	BUILD_BUG_ON(sizeof(mode_num) != 1);
	BUILD_BUG_ON(sizeof(sb->s_uuid) != 16);
	BUILD_BUG_ON(sizeof(hkdf_info) != 17);
	hkdf_info[0] = mode_num;
	memcpy(&hkdf_info[1], &sb->s_uuid, sizeof(sb->s_uuid));

	err = fscrypt_hkdf_expand(&mk->mk_secret.hkdf,
				  HKDF_CONTEXT_IV_INO_LBLK_64_KEY,
				  hkdf_info, sizeof(hkdf_info),
				  raw_key, mode->keysize);
	if (err)
		goto out_unlock;

	err = blk_crypto_start_using_mode(mode->blk_crypto_mode,
					  sb->s_blocksize, bdev->bd_queue);
	if (err)
		goto out_unlock;

	/*
	 * When a master key's first inline encryption key is set up, save a
	 * reference to the filesystem's block device so that the inline
	 * encryption keys can be evicted when the master key is destroyed.
	 */
	if (!mk->mk_bdev) {
		mk->mk_bdev = bdgrab(bdev);
		mk->mk_data_unit_size = sb->s_blocksize;
	}

	/* pairs with smp_load_acquire() above */
	smp_store_release(&mk->mk_iv_ino_lblk_64_raw_keys[mode_num], raw_key);
	err = 0;
out_unlock:
	mutex_unlock(&inline_crypt_setup_mutex);
out:
	if (err == 0) {
		ci->ci_inline_crypt_key = raw_key;
		/*
		 * Since each struct fscrypt_master_key belongs to a particular
		 * filesystem (a struct super_block), there should be only one
		 * block device, and only one data unit size as it should equal
		 * the filesystem's blocksize (i.e. s_blocksize).
		 */
		if (WARN_ON(mk->mk_bdev != bdev))
			err = -EINVAL;
		if (WARN_ON(mk->mk_data_unit_size != sb->s_blocksize))
			err = -EINVAL;
	} else {
		kzfree(raw_key);
	}
	return err;
}

/*
 * Evict per-mode inline encryption keys from blk-crypto when a master key is
 * destroyed.
 */
void fscrypt_evict_inline_crypt_keys(struct fscrypt_master_key *mk)
{
	struct block_device *bdev = mk->mk_bdev;
	size_t i;

	if (!bdev) /* No inline encryption keys? */
		return;

	for (i = 0; i < ARRAY_SIZE(mk->mk_iv_ino_lblk_64_raw_keys); i++) {
		u8 *raw_key = mk->mk_iv_ino_lblk_64_raw_keys[i];

		if (raw_key != NULL) {
			blk_crypto_evict_key(bdev->bd_queue, raw_key,
					     fscrypt_modes[i].blk_crypto_mode,
					     mk->mk_data_unit_size);
			kzfree(raw_key);
		}
	}
	bdput(bdev);
}

/**
 * fscrypt_inode_uses_inline_crypto - test whether an inode uses inline encryption
 * @inode: an inode
 *
 * Return: true if the inode requires file contents encryption and if the
 *	   encryption should be done in the block layer via blk-crypto rather
 *	   than in the filesystem layer.
 */
bool fscrypt_inode_uses_inline_crypto(const struct inode *inode)
{
	return IS_ENCRYPTED(inode) && S_ISREG(inode->i_mode) &&
		inode->i_crypt_info->ci_inline_crypt_key != NULL;
}
EXPORT_SYMBOL_GPL(fscrypt_inode_uses_inline_crypto);

/**
 * fscrypt_inode_uses_fs_layer_crypto - test whether an inode uses fs-layer encryption
 * @inode: an inode
 *
 * Return: true if the inode requires file contents encryption and if the
 *	   encryption should be done in the filesystem layer rather than in the
 *	   block layer via blk-crypto.
 */
bool fscrypt_inode_uses_fs_layer_crypto(const struct inode *inode)
{
	return IS_ENCRYPTED(inode) && S_ISREG(inode->i_mode) &&
		inode->i_crypt_info->ci_inline_crypt_key == NULL;
}
EXPORT_SYMBOL_GPL(fscrypt_inode_uses_fs_layer_crypto);

static inline u64 fscrypt_generate_dun(const struct fscrypt_info *ci,
				       u64 lblk_num)
{
	union fscrypt_iv iv;

	fscrypt_generate_iv(&iv, lblk_num, ci);
	/*
	 * fscrypt_should_use_inline_encryption() ensures we never get here if
	 * more than the first 8 bytes of the IV are nonzero.
	 */
	BUG_ON(memchr_inv(&iv.raw[8], 0, ci->ci_mode->ivsize - 8));
	return le64_to_cpu(iv.lblk_num);
}

/**
 * fscrypt_set_bio_crypt_ctx - prepare a file contents bio for inline encryption
 * @bio: a bio which will eventually be submitted to the file
 * @inode: the file's inode
 * @first_lblk: the first file logical block number in the I/O
 * @gfp_mask: memory allocation flags
 *
 * If the contents of the file should be encrypted (or decrypted) with inline
 * encryption, then assign the appropriate encryption context to the bio.
 *
 * Normally the bio should be newly allocated (i.e. no pages added yet), as
 * otherwise fscrypt_mergeable_bio() won't work as intended.
 *
 * The encryption context will be freed automatically when the bio is freed.
 *
 * Return: 0 on success, -errno on failure.  If __GFP_NOFAIL is specified, this
 *	   is guaranteed to succeed.
 */
int fscrypt_set_bio_crypt_ctx(struct bio *bio, const struct inode *inode,
			      u64 first_lblk, gfp_t gfp_mask)
{
	const struct fscrypt_info *ci = inode->i_crypt_info;
	u64 dun;

	if (!fscrypt_inode_uses_inline_crypto(inode))
		return 0;

	dun = fscrypt_generate_dun(ci, first_lblk);

	return bio_crypt_set_ctx(bio, ci->ci_inline_crypt_key,
				 ci->ci_mode->blk_crypto_mode,
				 dun, inode->i_blkbits, gfp_mask);
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
 * fscrypt_set_bio_crypt_ctx_bh - prepare a file contents bio for inline encryption
 * @bio: a bio which will eventually be submitted to the file
 * @first_bh: the first buffer_head for which I/O will be submitted
 * @gfp_mask: memory allocation flags
 *
 * Same as fscrypt_set_bio_crypt_ctx(), except this takes a buffer_head instead
 * of an inode and block number directly.
 *
 * Return: 0 on success, -errno on failure
 */
int fscrypt_set_bio_crypt_ctx_bh(struct bio *bio,
				 const struct buffer_head *first_bh,
				 gfp_t gfp_mask)
{
	const struct inode *inode;
	u64 first_lblk;

	if (!bh_get_inode_and_lblk_num(first_bh, &inode, &first_lblk))
		return 0;

	return fscrypt_set_bio_crypt_ctx(bio, inode, first_lblk, gfp_mask);
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
	const struct bio_crypt_ctx *bc;
	const u8 *next_key;
	u64 next_dun;

	if (bio_has_crypt_ctx(bio) != fscrypt_inode_uses_inline_crypto(inode))
		return false;
	if (!bio_has_crypt_ctx(bio))
		return true;
	bc = bio->bi_crypt_context;
	next_key = inode->i_crypt_info->ci_inline_crypt_key;
	next_dun = fscrypt_generate_dun(inode->i_crypt_info, next_lblk);

	/*
	 * Comparing the key pointers is good enough, as all I/O for each key
	 * uses the same pointer.  I.e., there's currently no need to support
	 * merging requests where the keys are the same but the pointers differ.
	 */
	return next_key == bc->raw_key &&
		next_dun == bc->data_unit_num +
			    (bio_sectors(bio) >>
			     (bc->data_unit_size_bits - SECTOR_SHIFT));
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
		return !bio_has_crypt_ctx(bio);

	return fscrypt_mergeable_bio(bio, inode, next_lblk);
}
EXPORT_SYMBOL_GPL(fscrypt_mergeable_bio_bh);
