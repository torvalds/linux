// SPDX-License-Identifier: GPL-2.0
/*
 * File contents en/decryption on block-based filesystems
 *
 * Copyright 2019 Google LLC
 */

/*
 * This file implements fscrypt's file contents en/decryption using blk-crypto
 * (Documentation/block/inline-encryption.rst).  fscrypt assigns a bio_crypt_ctx
 * with a key and IV to each bio, and the block layer does the en/decryption.
 *
 * This file's exported functions are called only by block-based filesystems.
 */

#include <linux/blk-crypto.h>
#include <linux/blkdev.h>
#include <linux/export.h>
#include <linux/sched/mm.h>
#include <linux/slab.h>
#include <linux/uio.h>

#include "fscrypt_private.h"

static unsigned int
fscrypt_get_devices(struct super_block *sb,
		    struct block_device *devs[FSCRYPT_MAX_DEVICES])
{
	if (sb->s_cop->get_devices)
		return sb->s_cop->get_devices(sb, devs);
	devs[0] = sb->s_bdev;
	return 1;
}

static unsigned int fscrypt_get_dun_bytes(const struct fscrypt_inode_info *ci)
{
	const struct super_block *sb = ci->ci_inode->i_sb;
	unsigned int flags = fscrypt_policy_flags(&ci->ci_policy);
	int dun_bits;

	if (flags & FSCRYPT_POLICY_FLAG_DIRECT_KEY)
		return offsetofend(union fscrypt_iv, nonce);

	if (flags & FSCRYPT_POLICY_FLAG_IV_INO_LBLK_64)
		return sizeof(__le64);

	if (flags & FSCRYPT_POLICY_FLAG_IV_INO_LBLK_32)
		return sizeof(__le32);

	/* Default case: IVs are just the file data unit index */
	dun_bits = fscrypt_max_file_dun_bits(sb, ci->ci_data_unit_bits);
	return DIV_ROUND_UP(dun_bits, 8);
}

/*
 * Log a message when starting to use blk-crypto (native) or blk-crypto-fallback
 * for an encryption mode for the first time.  This is the blk-crypto
 * counterpart to the message logged when starting to use the crypto API for the
 * first time.  A limitation is that these messages don't convey which specific
 * filesystems or files are using each implementation.  However, *usually*
 * systems use just one implementation per mode, which makes these messages
 * helpful for debugging problems where the "wrong" implementation is used.
 */
static void fscrypt_log_blk_crypto_impl(struct fscrypt_mode *mode,
					struct block_device *dev,
					const struct blk_crypto_key *blk_key)
{
	if (blk_crypto_config_supported_natively(dev, &blk_key->crypto_cfg)) {
		if (!xchg(&mode->logged_blk_crypto_native, 1))
			pr_info("fscrypt: %s using blk-crypto (native)\n",
				mode->friendly_name);
	} else if (!xchg(&mode->logged_blk_crypto_fallback, 1)) {
		pr_info("fscrypt: %s using blk-crypto-fallback\n",
			mode->friendly_name);
	}
}

int fscrypt_prepare_inline_crypt_key(struct fscrypt_prepared_key *prep_key,
				     const u8 *key_bytes, size_t key_size,
				     bool is_hw_wrapped,
				     const struct fscrypt_inode_info *ci)
{
	const struct inode *inode = ci->ci_inode;
	struct super_block *sb = inode->i_sb;
	bool inlinecrypt = sb->s_flags & SB_INLINECRYPT;
	struct fscrypt_mode *mode = ci->ci_mode;
	enum blk_crypto_key_type key_type = is_hw_wrapped ?
		BLK_CRYPTO_KEY_TYPE_HW_WRAPPED : BLK_CRYPTO_KEY_TYPE_RAW;
	struct blk_crypto_key *blk_key;
	struct block_device *devs[FSCRYPT_MAX_DEVICES];
	unsigned int num_devs;
	unsigned int i;
	int err;

	if (is_hw_wrapped && !inlinecrypt) {
		/*
		 * blk_crypto_init_key() would catch this anyway, but this
		 * provides a clearer error message.
		 */
		fscrypt_err(
			inode,
			"Hardware-wrapped keys require inline encryption (-o inlinecrypt)");
		return -EINVAL;
	}

	blk_key = kmalloc_obj(*blk_key);
	if (!blk_key)
		return -ENOMEM;

	err = blk_crypto_init_key(blk_key, key_bytes, key_size, key_type,
				  mode->blk_crypto_mode,
				  fscrypt_get_dun_bytes(ci),
				  1U << ci->ci_data_unit_bits,
				  inlinecrypt ? BLK_CRYPTO_CFG_ALLOW_HW : 0);
	if (err) {
		fscrypt_err(inode, "Error %d initializing blk-crypto key", err);
		goto fail;
	}

	/* Start using blk-crypto on all the filesystem's block devices. */
	num_devs = fscrypt_get_devices(sb, devs);
	for (i = 0; i < num_devs; i++) {
		err = blk_crypto_start_using_key(devs[i], blk_key);
		if (err)
			break;
		fscrypt_log_blk_crypto_impl(mode, devs[i], blk_key);
	}
	if (err) {
		if (err == -EOPNOTSUPP && is_hw_wrapped)
			fscrypt_err(
				inode,
				"Hardware-wrapped key required, but no suitable inline encryption capabilities are available");
		else
			fscrypt_err(inode,
				    "Error %d starting to use blk-crypto", err);
		goto fail;
	}

	prep_key->blk_key = blk_key;
	return 0;

fail:
	kfree_sensitive(blk_key);
	return err;
}

void fscrypt_destroy_inline_crypt_key(struct super_block *sb,
				      struct fscrypt_prepared_key *prep_key)
{
	struct blk_crypto_key *blk_key = prep_key->blk_key;
	struct block_device *devs[FSCRYPT_MAX_DEVICES];
	unsigned int num_devs;
	unsigned int i;

	if (!blk_key)
		return;

	/*
	 * Evict the key from all the filesystem's block devices.
	 * This *must* be done before the key is freed.
	 */
	num_devs = fscrypt_get_devices(sb, devs);
	for (i = 0; i < num_devs; i++)
		blk_crypto_evict_key(devs[i], blk_key);

	kfree_sensitive(blk_key);
}

/*
 * Ask the inline encryption hardware to derive the software secret from a
 * hardware-wrapped key.  Returns -EOPNOTSUPP if hardware-wrapped keys aren't
 * supported on this filesystem or hardware.
 */
int fscrypt_derive_sw_secret(struct super_block *sb,
			     const u8 *wrapped_key, size_t wrapped_key_size,
			     u8 sw_secret[BLK_CRYPTO_SW_SECRET_SIZE])
{
	int err;

	/* The filesystem must be mounted with -o inlinecrypt. */
	if (!(sb->s_flags & SB_INLINECRYPT)) {
		fscrypt_warn(NULL,
			     "%s: filesystem not mounted with inlinecrypt\n",
			     sb->s_id);
		return -EOPNOTSUPP;
	}

	err = blk_crypto_derive_sw_secret(sb->s_bdev, wrapped_key,
					  wrapped_key_size, sw_secret);
	if (err == -EOPNOTSUPP)
		fscrypt_warn(NULL,
			     "%s: block device doesn't support hardware-wrapped keys\n",
			     sb->s_id);
	return err;
}

static void fscrypt_generate_dun(const struct fscrypt_inode_info *ci,
				 loff_t pos, u64 dun[BLK_CRYPTO_DUN_ARRAY_SIZE])
{
	union fscrypt_iv iv;
	int i;

	fscrypt_generate_iv(&iv, pos >> ci->ci_data_unit_bits, ci);

	BUILD_BUG_ON(FSCRYPT_MAX_IV_SIZE > BLK_CRYPTO_MAX_IV_SIZE);
	memset(dun, 0, BLK_CRYPTO_MAX_IV_SIZE);
	for (i = 0; i < ci->ci_mode->ivsize/sizeof(dun[0]); i++)
		dun[i] = le64_to_cpu(iv.dun[i]);
}

/**
 * fscrypt_set_bio_crypt_ctx() - prepare a file contents bio for inline crypto
 * @bio: a bio which will eventually be submitted to the file
 * @inode: the file's inode
 * @pos: the first file position (in bytes) in the I/O
 * @gfp_mask: memory allocation flags - these must be a waiting mask so that
 *					bio_crypt_set_ctx can't fail.
 *
 * If the contents of the file should be encrypted (or decrypted), then assign
 * the appropriate encryption context to the bio.
 *
 * Normally the bio should be newly allocated (i.e. no pages added yet), as
 * otherwise fscrypt_mergeable_bio() won't work as intended.
 *
 * The encryption context will be freed automatically when the bio is freed.
 */
void fscrypt_set_bio_crypt_ctx(struct bio *bio, const struct inode *inode,
			       loff_t pos, gfp_t gfp_mask)
{
	const struct fscrypt_inode_info *ci;
	u64 dun[BLK_CRYPTO_DUN_ARRAY_SIZE];

	if (!fscrypt_needs_contents_encryption(inode))
		return;
	ci = fscrypt_get_inode_info_raw(inode);

	fscrypt_generate_dun(ci, pos, dun);
	bio_crypt_set_ctx(bio, ci->ci_enc_key.blk_key, dun, gfp_mask);
}
EXPORT_SYMBOL_GPL(fscrypt_set_bio_crypt_ctx);

/**
 * fscrypt_mergeable_bio() - test whether data can be added to a bio
 * @bio: the bio being built up
 * @inode: the inode for the next part of the I/O
 * @pos: the next file position (in bytes) in the I/O
 *
 * When building a bio which may contain data which should undergo encryption
 * (or decryption) via fscrypt, filesystems should call this function to ensure
 * that the resulting bio contains only contiguous data unit numbers.  This will
 * return false if the next part of the I/O cannot be merged with the bio
 * because either the encryption key would be different or the encryption data
 * unit numbers would be discontiguous.
 *
 * fscrypt_set_bio_crypt_ctx() must have already been called on the bio.
 *
 * This function isn't required in cases where crypto-mergeability is ensured in
 * another way, such as I/O targeting only a single file (and thus a single key)
 * combined with fscrypt_limit_io_blocks() to ensure DUN contiguity.
 *
 * Return: true iff the I/O is mergeable
 */
bool fscrypt_mergeable_bio(struct bio *bio, const struct inode *inode,
			   loff_t pos)
{
	const struct bio_crypt_ctx *bc = bio->bi_crypt_context;
	const struct fscrypt_inode_info *ci;
	u64 next_dun[BLK_CRYPTO_DUN_ARRAY_SIZE];

	if (!!bc != fscrypt_needs_contents_encryption(inode))
		return false;
	if (!bc)
		return true;
	ci = fscrypt_get_inode_info_raw(inode);

	/*
	 * Comparing the key pointers is good enough, as all I/O for each key
	 * uses the same pointer.  I.e., there's currently no need to support
	 * merging requests where the keys are the same but the pointers differ.
	 */
	if (bc->bc_key != ci->ci_enc_key.blk_key)
		return false;

	fscrypt_generate_dun(ci, pos, next_dun);
	return bio_crypt_dun_is_contiguous(bc, bio->bi_iter.bi_size, next_dun);
}
EXPORT_SYMBOL_GPL(fscrypt_mergeable_bio);

/**
 * fscrypt_limit_io_blocks() - limit I/O blocks to avoid discontiguous DUNs
 * @inode: the file on which I/O is being done
 * @lblk: the block at which the I/O is being started from
 * @nr_blocks: the number of blocks we want to submit starting at @lblk
 *
 * Determine the limit to the number of blocks that can be submitted in a bio
 * targeting @lblk without causing a data unit number (DUN) discontiguity.
 *
 * This is normally just @nr_blocks, as normally the DUNs just increment along
 * with the logical blocks.  (Or the file is not encrypted.)
 *
 * In rare cases, fscrypt can be using an IV generation method that allows the
 * DUN to wrap around within logically contiguous blocks, and that wraparound
 * will occur.  If this happens, a value less than @nr_blocks will be returned
 * so that the wraparound doesn't occur in the middle of a bio, which would
 * cause encryption/decryption to produce wrong results.
 *
 * Return: the actual number of blocks that can be submitted
 */
u64 fscrypt_limit_io_blocks(const struct inode *inode, u64 lblk, u64 nr_blocks)
{
	const struct fscrypt_inode_info *ci;
	u32 dun;

	if (!fscrypt_needs_contents_encryption(inode))
		return nr_blocks;

	if (nr_blocks <= 1)
		return nr_blocks;

	ci = fscrypt_get_inode_info_raw(inode);
	if (!(fscrypt_policy_flags(&ci->ci_policy) &
	      FSCRYPT_POLICY_FLAG_IV_INO_LBLK_32))
		return nr_blocks;

	/* With IV_INO_LBLK_32, the DUN can wrap around from U32_MAX to 0. */

	dun = ci->ci_hashed_ino + lblk;

	return min_t(u64, nr_blocks, (u64)U32_MAX + 1 - dun);
}
EXPORT_SYMBOL_GPL(fscrypt_limit_io_blocks);

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
