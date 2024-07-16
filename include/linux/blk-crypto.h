/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2019 Google LLC
 */

#ifndef __LINUX_BLK_CRYPTO_H
#define __LINUX_BLK_CRYPTO_H

#include <linux/types.h>

enum blk_crypto_mode_num {
	BLK_ENCRYPTION_MODE_INVALID,
	BLK_ENCRYPTION_MODE_AES_256_XTS,
	BLK_ENCRYPTION_MODE_AES_128_CBC_ESSIV,
	BLK_ENCRYPTION_MODE_ADIANTUM,
	BLK_ENCRYPTION_MODE_SM4_XTS,
	BLK_ENCRYPTION_MODE_MAX,
};

/*
 * Supported types of keys.  Must be bitflags due to their use in
 * blk_crypto_profile::key_types_supported.
 */
enum blk_crypto_key_type {
	/*
	 * Standard keys (i.e. "software keys").  These keys are simply kept in
	 * raw, plaintext form in kernel memory.
	 */
	BLK_CRYPTO_KEY_TYPE_STANDARD = 1 << 0,

	/*
	 * Hardware-wrapped keys.  These keys are only present in kernel memory
	 * in ephemerally-wrapped form, and they can only be unwrapped by
	 * dedicated hardware.  For details, see the "Hardware-wrapped keys"
	 * section of Documentation/block/inline-encryption.rst.
	 */
	BLK_CRYPTO_KEY_TYPE_HW_WRAPPED = 1 << 1,
};

/*
 * Currently the maximum standard key size is 64 bytes, as that is the key size
 * of BLK_ENCRYPTION_MODE_AES_256_XTS which takes the longest key.
 *
 * The maximum hardware-wrapped key size depends on the hardware's key wrapping
 * algorithm, which is a hardware implementation detail, so it isn't precisely
 * specified.  But currently 128 bytes is plenty in practice.  Implementations
 * are recommended to wrap a 32-byte key for the hardware KDF with AES-256-GCM,
 * which should result in a size closer to 64 bytes than 128.
 *
 * Both of these values can trivially be increased if ever needed.
 */
#define BLK_CRYPTO_MAX_STANDARD_KEY_SIZE	64
#define BLK_CRYPTO_MAX_HW_WRAPPED_KEY_SIZE	128

/* This should use max(), but max() doesn't work in a struct definition. */
#define BLK_CRYPTO_MAX_ANY_KEY_SIZE \
	(BLK_CRYPTO_MAX_HW_WRAPPED_KEY_SIZE > \
	 BLK_CRYPTO_MAX_STANDARD_KEY_SIZE ? \
	 BLK_CRYPTO_MAX_HW_WRAPPED_KEY_SIZE : BLK_CRYPTO_MAX_STANDARD_KEY_SIZE)

/*
 * Size of the "software secret" which can be derived from a hardware-wrapped
 * key.  This is currently always 32 bytes.  Note, the choice of 32 bytes
 * assumes that the software secret is only used directly for algorithms that
 * don't require more than a 256-bit key to get the desired security strength.
 * If it were to be used e.g. directly as an AES-256-XTS key, then this would
 * need to be increased (which is possible if hardware supports it, but care
 * would need to be taken to avoid breaking users who need exactly 32 bytes).
 */
#define BLK_CRYPTO_SW_SECRET_SIZE	32

/**
 * struct blk_crypto_config - an inline encryption key's crypto configuration
 * @crypto_mode: encryption algorithm this key is for
 * @data_unit_size: the data unit size for all encryption/decryptions with this
 *	key.  This is the size in bytes of each individual plaintext and
 *	ciphertext.  This is always a power of 2.  It might be e.g. the
 *	filesystem block size or the disk sector size.
 * @dun_bytes: the maximum number of bytes of DUN used when using this key
 * @key_type: the type of this key -- either standard or hardware-wrapped
 */
struct blk_crypto_config {
	enum blk_crypto_mode_num crypto_mode;
	unsigned int data_unit_size;
	unsigned int dun_bytes;
	enum blk_crypto_key_type key_type;
};

/**
 * struct blk_crypto_key - an inline encryption key
 * @crypto_cfg: the crypto mode, data unit size, key type, and other
 *		characteristics of this key and how it will be used
 * @data_unit_size_bits: log2 of data_unit_size
 * @size: size of this key in bytes.  The size of a standard key is fixed for a
 *	  given crypto mode, but the size of a hardware-wrapped key can vary.
 * @raw: the bytes of this key.  Only the first @size bytes are significant.
 *
 * A blk_crypto_key is immutable once created, and many bios can reference it at
 * the same time.  It must not be freed until all bios using it have completed
 * and it has been evicted from all devices on which it may have been used.
 */
struct blk_crypto_key {
	struct blk_crypto_config crypto_cfg;
	unsigned int data_unit_size_bits;
	unsigned int size;
	u8 raw[BLK_CRYPTO_MAX_ANY_KEY_SIZE];
};

#define BLK_CRYPTO_MAX_IV_SIZE		32
#define BLK_CRYPTO_DUN_ARRAY_SIZE	(BLK_CRYPTO_MAX_IV_SIZE / sizeof(u64))

/**
 * struct bio_crypt_ctx - an inline encryption context
 * @bc_key: the key, algorithm, and data unit size to use
 * @bc_dun: the data unit number (starting IV) to use
 *
 * A bio_crypt_ctx specifies that the contents of the bio will be encrypted (for
 * write requests) or decrypted (for read requests) inline by the storage device
 * or controller, or by the crypto API fallback.
 */
struct bio_crypt_ctx {
	const struct blk_crypto_key	*bc_key;
	u64				bc_dun[BLK_CRYPTO_DUN_ARRAY_SIZE];
};

#include <linux/blk_types.h>
#include <linux/blkdev.h>

#ifdef CONFIG_BLK_INLINE_ENCRYPTION

static inline bool bio_has_crypt_ctx(struct bio *bio)
{
	return bio->bi_crypt_context;
}

void bio_crypt_set_ctx(struct bio *bio, const struct blk_crypto_key *key,
		       const u64 dun[BLK_CRYPTO_DUN_ARRAY_SIZE],
		       gfp_t gfp_mask);

bool bio_crypt_dun_is_contiguous(const struct bio_crypt_ctx *bc,
				 unsigned int bytes,
				 const u64 next_dun[BLK_CRYPTO_DUN_ARRAY_SIZE]);

int blk_crypto_init_key(struct blk_crypto_key *blk_key,
			const u8 *raw_key, size_t raw_key_size,
			enum blk_crypto_key_type key_type,
			enum blk_crypto_mode_num crypto_mode,
			unsigned int dun_bytes,
			unsigned int data_unit_size);

int blk_crypto_start_using_key(struct block_device *bdev,
			       const struct blk_crypto_key *key);

void blk_crypto_evict_key(struct block_device *bdev,
			  const struct blk_crypto_key *key);

bool blk_crypto_config_supported_natively(struct block_device *bdev,
					  const struct blk_crypto_config *cfg);
bool blk_crypto_config_supported(struct block_device *bdev,
				 const struct blk_crypto_config *cfg);

int blk_crypto_derive_sw_secret(struct block_device *bdev,
				const u8 *eph_key, size_t eph_key_size,
				u8 sw_secret[BLK_CRYPTO_SW_SECRET_SIZE]);

#else /* CONFIG_BLK_INLINE_ENCRYPTION */

static inline bool bio_has_crypt_ctx(struct bio *bio)
{
	return false;
}

#endif /* CONFIG_BLK_INLINE_ENCRYPTION */

static inline void bio_clone_skip_dm_default_key(struct bio *dst,
						 const struct bio *src);

int __bio_crypt_clone(struct bio *dst, struct bio *src, gfp_t gfp_mask);
/**
 * bio_crypt_clone - clone bio encryption context
 * @dst: destination bio
 * @src: source bio
 * @gfp_mask: memory allocation flags
 *
 * If @src has an encryption context, clone it to @dst.
 *
 * Return: 0 on success, -ENOMEM if out of memory.  -ENOMEM is only possible if
 *	   @gfp_mask doesn't include %__GFP_DIRECT_RECLAIM.
 */
static inline int bio_crypt_clone(struct bio *dst, struct bio *src,
				  gfp_t gfp_mask)
{
	bio_clone_skip_dm_default_key(dst, src);
	if (bio_has_crypt_ctx(src))
		return __bio_crypt_clone(dst, src, gfp_mask);
	return 0;
}

#if IS_ENABLED(CONFIG_DM_DEFAULT_KEY)
static inline void bio_set_skip_dm_default_key(struct bio *bio)
{
	bio->bi_skip_dm_default_key = true;
}

static inline bool bio_should_skip_dm_default_key(const struct bio *bio)
{
	return bio->bi_skip_dm_default_key;
}

static inline void bio_clone_skip_dm_default_key(struct bio *dst,
						 const struct bio *src)
{
	dst->bi_skip_dm_default_key = src->bi_skip_dm_default_key;
}
#else /* CONFIG_DM_DEFAULT_KEY */
static inline void bio_set_skip_dm_default_key(struct bio *bio)
{
}

static inline bool bio_should_skip_dm_default_key(const struct bio *bio)
{
	return false;
}

static inline void bio_clone_skip_dm_default_key(struct bio *dst,
						 const struct bio *src)
{
}
#endif /* !CONFIG_DM_DEFAULT_KEY */

#endif /* __LINUX_BLK_CRYPTO_H */
