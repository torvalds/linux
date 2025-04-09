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
#include <linux/sched/mm.h>
#include <linux/slab.h>
#include <linux/uio.h>

#include "fscrypt_private.h"

static struct block_device **fscrypt_get_devices(struct super_block *sb,
						 unsigned int *num_devs)
{
	struct block_device **devs;

	if (sb->s_cop->get_devices) {
		devs = sb->s_cop->get_devices(sb, num_devs);
		if (devs)
			return devs;
	}
	devs = kmalloc(sizeof(*devs), GFP_KERNEL);
	if (!devs)
		return ERR_PTR(-ENOMEM);
	devs[0] = sb->s_bdev;
	*num_devs = 1;
	return devs;
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
					struct block_device **devs,
					unsigned int num_devs,
					const struct blk_crypto_config *cfg)
{
	unsigned int i;

	for (i = 0; i < num_devs; i++) {
		if (!IS_ENABLED(CONFIG_BLK_INLINE_ENCRYPTION_FALLBACK) ||
		    blk_crypto_config_supported_natively(devs[i], cfg)) {
			if (!xchg(&mode->logged_blk_crypto_native, 1))
				pr_info("fscrypt: %s using blk-crypto (native)\n",
					mode->friendly_name);
		} else if (!xchg(&mode->logged_blk_crypto_fallback, 1)) {
			pr_info("fscrypt: %s using blk-crypto-fallback\n",
				mode->friendly_name);
		}
	}
}

/* Enable inline encryption for this file if supported. */
int fscrypt_select_encryption_impl(struct fscrypt_inode_info *ci)
{
	const struct inode *inode = ci->ci_inode;
	struct super_block *sb = inode->i_sb;
	struct blk_crypto_config crypto_cfg;
	struct block_device **devs;
	unsigned int num_devs;
	unsigned int i;

	/* The file must need contents encryption, not filenames encryption */
	if (!S_ISREG(inode->i_mode))
		return 0;

	/* The crypto mode must have a blk-crypto counterpart */
	if (ci->ci_mode->blk_crypto_mode == BLK_ENCRYPTION_MODE_INVALID)
		return 0;

	/* The filesystem must be mounted with -o inlinecrypt */
	if (!(sb->s_flags & SB_INLINECRYPT))
		return 0;

	/*
	 * When a page contains multiple logically contiguous filesystem blocks,
	 * some filesystem code only calls fscrypt_mergeable_bio() for the first
	 * block in the page. This is fine for most of fscrypt's IV generation
	 * strategies, where contiguous blocks imply contiguous IVs. But it
	 * doesn't work with IV_INO_LBLK_32. For now, simply exclude
	 * IV_INO_LBLK_32 with blocksize != PAGE_SIZE from inline encryption.
	 */
	if ((fscrypt_policy_flags(&ci->ci_policy) &
	     FSCRYPT_POLICY_FLAG_IV_INO_LBLK_32) &&
	    sb->s_blocksize != PAGE_SIZE)
		return 0;

	/*
	 * On all the filesystem's block devices, blk-crypto must support the
	 * crypto configuration that the file would use.
	 */
	crypto_cfg.crypto_mode = ci->ci_mode->blk_crypto_mode;
	crypto_cfg.data_unit_size = 1U << ci->ci_data_unit_bits;
	crypto_cfg.dun_bytes = fscrypt_get_dun_bytes(ci);
	crypto_cfg.key_type = BLK_CRYPTO_KEY_TYPE_RAW;

	devs = fscrypt_get_devices(sb, &num_devs);
	if (IS_ERR(devs))
		return PTR_ERR(devs);

	for (i = 0; i < num_devs; i++) {
		if (!blk_crypto_config_supported(devs[i], &crypto_cfg))
			goto out_free_devs;
	}

	fscrypt_log_blk_crypto_impl(ci->ci_mode, devs, num_devs, &crypto_cfg);

	ci->ci_inlinecrypt = true;
out_free_devs:
	kfree(devs);

	return 0;
}

int fscrypt_prepare_inline_crypt_key(struct fscrypt_prepared_key *prep_key,
				     const u8 *raw_key,
				     const struct fscrypt_inode_info *ci)
{
	const struct inode *inode = ci->ci_inode;
	struct super_block *sb = inode->i_sb;
	enum blk_crypto_mode_num crypto_mode = ci->ci_mode->blk_crypto_mode;
	struct blk_crypto_key *blk_key;
	struct block_device **devs;
	unsigned int num_devs;
	unsigned int i;
	int err;

	blk_key = kmalloc(sizeof(*blk_key), GFP_KERNEL);
	if (!blk_key)
		return -ENOMEM;

	err = blk_crypto_init_key(blk_key, raw_key, ci->ci_mode->keysize,
				  BLK_CRYPTO_KEY_TYPE_RAW, crypto_mode,
				  fscrypt_get_dun_bytes(ci),
				  1U << ci->ci_data_unit_bits);
	if (err) {
		fscrypt_err(inode, "error %d initializing blk-crypto key", err);
		goto fail;
	}

	/* Start using blk-crypto on all the filesystem's block devices. */
	devs = fscrypt_get_devices(sb, &num_devs);
	if (IS_ERR(devs)) {
		err = PTR_ERR(devs);
		goto fail;
	}
	for (i = 0; i < num_devs; i++) {
		err = blk_crypto_start_using_key(devs[i], blk_key);
		if (err)
			break;
	}
	kfree(devs);
	if (err) {
		fscrypt_err(inode, "error %d starting to use blk-crypto", err);
		goto fail;
	}

	/*
	 * Pairs with the smp_load_acquire() in fscrypt_is_key_prepared().
	 * I.e., here we publish ->blk_key with a RELEASE barrier so that
	 * concurrent tasks can ACQUIRE it.  Note that this concurrency is only
	 * possible for per-mode keys, not for per-file keys.
	 */
	smp_store_release(&prep_key->blk_key, blk_key);
	return 0;

fail:
	kfree_sensitive(blk_key);
	return err;
}

void fscrypt_destroy_inline_crypt_key(struct super_block *sb,
				      struct fscrypt_prepared_key *prep_key)
{
	struct blk_crypto_key *blk_key = prep_key->blk_key;
	struct block_device **devs;
	unsigned int num_devs;
	unsigned int i;

	if (!blk_key)
		return;

	/* Evict the key from all the filesystem's block devices. */
	devs = fscrypt_get_devices(sb, &num_devs);
	if (!IS_ERR(devs)) {
		for (i = 0; i < num_devs; i++)
			blk_crypto_evict_key(devs[i], blk_key);
		kfree(devs);
	}
	kfree_sensitive(blk_key);
}

bool __fscrypt_inode_uses_inline_crypto(const struct inode *inode)
{
	return inode->i_crypt_info->ci_inlinecrypt;
}
EXPORT_SYMBOL_GPL(__fscrypt_inode_uses_inline_crypto);

static void fscrypt_generate_dun(const struct fscrypt_inode_info *ci,
				 u64 lblk_num,
				 u64 dun[BLK_CRYPTO_DUN_ARRAY_SIZE])
{
	u64 index = lblk_num << ci->ci_data_units_per_block_bits;
	union fscrypt_iv iv;
	int i;

	fscrypt_generate_iv(&iv, index, ci);

	BUILD_BUG_ON(FSCRYPT_MAX_IV_SIZE > BLK_CRYPTO_MAX_IV_SIZE);
	memset(dun, 0, BLK_CRYPTO_MAX_IV_SIZE);
	for (i = 0; i < ci->ci_mode->ivsize/sizeof(dun[0]); i++)
		dun[i] = le64_to_cpu(iv.dun[i]);
}

/**
 * fscrypt_set_bio_crypt_ctx() - prepare a file contents bio for inline crypto
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
	const struct fscrypt_inode_info *ci;
	u64 dun[BLK_CRYPTO_DUN_ARRAY_SIZE];

	if (!fscrypt_inode_uses_inline_crypto(inode))
		return;
	ci = inode->i_crypt_info;

	fscrypt_generate_dun(ci, first_lblk, dun);
	bio_crypt_set_ctx(bio, ci->ci_enc_key.blk_key, dun, gfp_mask);
}
EXPORT_SYMBOL_GPL(fscrypt_set_bio_crypt_ctx);

/* Extract the inode and logical block number from a buffer_head. */
static bool bh_get_inode_and_lblk_num(const struct buffer_head *bh,
				      const struct inode **inode_ret,
				      u64 *lblk_num_ret)
{
	struct folio *folio = bh->b_folio;
	const struct address_space *mapping;
	const struct inode *inode;

	/*
	 * The ext4 journal (jbd2) can submit a buffer_head it directly created
	 * for a non-pagecache page.  fscrypt doesn't care about these.
	 */
	mapping = folio_mapping(folio);
	if (!mapping)
		return false;
	inode = mapping->host;

	*inode_ret = inode;
	*lblk_num_ret = ((u64)folio->index << (PAGE_SHIFT - inode->i_blkbits)) +
			(bh_offset(bh) >> inode->i_blkbits);
	return true;
}

/**
 * fscrypt_set_bio_crypt_ctx_bh() - prepare a file contents bio for inline
 *				    crypto
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
 * fscrypt_mergeable_bio() - test whether data can be added to a bio
 * @bio: the bio being built up
 * @inode: the inode for the next part of the I/O
 * @next_lblk: the next file logical block number in the I/O
 *
 * When building a bio which may contain data which should undergo inline
 * encryption (or decryption) via fscrypt, filesystems should call this function
 * to ensure that the resulting bio contains only contiguous data unit numbers.
 * This will return false if the next part of the I/O cannot be merged with the
 * bio because either the encryption key would be different or the encryption
 * data unit numbers would be discontiguous.
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
	if (bc->bc_key != inode->i_crypt_info->ci_enc_key.blk_key)
		return false;

	fscrypt_generate_dun(inode->i_crypt_info, next_lblk, next_dun);
	return bio_crypt_dun_is_contiguous(bc, bio->bi_iter.bi_size, next_dun);
}
EXPORT_SYMBOL_GPL(fscrypt_mergeable_bio);

/**
 * fscrypt_mergeable_bio_bh() - test whether data can be added to a bio
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

/**
 * fscrypt_dio_supported() - check whether DIO (direct I/O) is supported on an
 *			     inode, as far as encryption is concerned
 * @inode: the inode in question
 *
 * Return: %true if there are no encryption constraints that prevent DIO from
 *	   being supported; %false if DIO is unsupported.  (Note that in the
 *	   %true case, the filesystem might have other, non-encryption-related
 *	   constraints that prevent DIO from actually being supported.  Also, on
 *	   encrypted files the filesystem is still responsible for only allowing
 *	   DIO when requests are filesystem-block-aligned.)
 */
bool fscrypt_dio_supported(struct inode *inode)
{
	int err;

	/* If the file is unencrypted, no veto from us. */
	if (!fscrypt_needs_contents_encryption(inode))
		return true;

	/*
	 * We only support DIO with inline crypto, not fs-layer crypto.
	 *
	 * To determine whether the inode is using inline crypto, we have to set
	 * up the key if it wasn't already done.  This is because in the current
	 * design of fscrypt, the decision of whether to use inline crypto or
	 * not isn't made until the inode's encryption key is being set up.  In
	 * the DIO read/write case, the key will always be set up already, since
	 * the file will be open.  But in the case of statx(), the key might not
	 * be set up yet, as the file might not have been opened yet.
	 */
	err = fscrypt_require_key(inode);
	if (err) {
		/*
		 * Key unavailable or couldn't be set up.  This edge case isn't
		 * worth worrying about; just report that DIO is unsupported.
		 */
		return false;
	}
	return fscrypt_inode_uses_inline_crypto(inode);
}
EXPORT_SYMBOL_GPL(fscrypt_dio_supported);

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

	if (!fscrypt_inode_uses_inline_crypto(inode))
		return nr_blocks;

	if (nr_blocks <= 1)
		return nr_blocks;

	ci = inode->i_crypt_info;
	if (!(fscrypt_policy_flags(&ci->ci_policy) &
	      FSCRYPT_POLICY_FLAG_IV_INO_LBLK_32))
		return nr_blocks;

	/* With IV_INO_LBLK_32, the DUN can wrap around from U32_MAX to 0. */

	dun = ci->ci_hashed_ino + lblk;

	return min_t(u64, nr_blocks, (u64)U32_MAX + 1 - dun);
}
EXPORT_SYMBOL_GPL(fscrypt_limit_io_blocks);
