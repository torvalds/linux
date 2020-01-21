/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2019 Google LLC
 */
#ifndef __LINUX_BIO_CRYPT_CTX_H
#define __LINUX_BIO_CRYPT_CTX_H

enum blk_crypto_mode_num {
	BLK_ENCRYPTION_MODE_INVALID,
	BLK_ENCRYPTION_MODE_AES_256_XTS,
	BLK_ENCRYPTION_MODE_AES_128_CBC_ESSIV,
	BLK_ENCRYPTION_MODE_ADIANTUM,
	BLK_ENCRYPTION_MODE_MAX,
};

#ifdef CONFIG_BLOCK
#include <linux/blk_types.h>

#ifdef CONFIG_BLK_INLINE_ENCRYPTION

#define BLK_CRYPTO_MAX_KEY_SIZE		64
#define BLK_CRYPTO_MAX_WRAPPED_KEY_SIZE		128

/**
 * struct blk_crypto_key - an inline encryption key
 * @crypto_mode: encryption algorithm this key is for
 * @data_unit_size: the data unit size for all encryption/decryptions with this
 *	key.  This is the size in bytes of each individual plaintext and
 *	ciphertext.  This is always a power of 2.  It might be e.g. the
 *	filesystem block size or the disk sector size.
 * @data_unit_size_bits: log2 of data_unit_size
 * @size: size of this key in bytes (determined by @crypto_mode)
 * @hash: hash of this key, for keyslot manager use only
 * @raw: the raw bytes of this key.  Only the first @size bytes are used.
 *
 * A blk_crypto_key is immutable once created, and many bios can reference it at
 * the same time.  It must not be freed until all bios using it have completed.
 */
struct blk_crypto_key {
	enum blk_crypto_mode_num crypto_mode;
	unsigned int data_unit_size;
	unsigned int data_unit_size_bits;
	unsigned int size;
	unsigned int hash;
	u8 raw[BLK_CRYPTO_MAX_WRAPPED_KEY_SIZE];
};

#define BLK_CRYPTO_MAX_IV_SIZE		32
#define BLK_CRYPTO_DUN_ARRAY_SIZE	(BLK_CRYPTO_MAX_IV_SIZE/sizeof(u64))

/**
 * struct bio_crypt_ctx - an inline encryption context
 * @bc_key: the key, algorithm, and data unit size to use
 * @bc_keyslot: the keyslot that has been assigned for this key in @bc_ksm,
 *		or -1 if no keyslot has been assigned yet.
 * @bc_dun: the data unit number (starting IV) to use
 * @bc_ksm: the keyslot manager into which the key has been programmed with
 *	    @bc_keyslot, or NULL if this key hasn't yet been programmed.
 *
 * A bio_crypt_ctx specifies that the contents of the bio will be encrypted (for
 * write requests) or decrypted (for read requests) inline by the storage device
 * or controller, or by the crypto API fallback.
 */
struct bio_crypt_ctx {
	const struct blk_crypto_key	*bc_key;
	int				bc_keyslot;

	/* Data unit number */
	u64				bc_dun[BLK_CRYPTO_DUN_ARRAY_SIZE];

	/*
	 * The keyslot manager where the key has been programmed
	 * with keyslot.
	 */
	struct keyslot_manager		*bc_ksm;
};

int bio_crypt_ctx_init(void);

struct bio_crypt_ctx *bio_crypt_alloc_ctx(gfp_t gfp_mask);

void bio_crypt_free_ctx(struct bio *bio);

static inline bool bio_has_crypt_ctx(struct bio *bio)
{
	return bio->bi_crypt_context;
}

void bio_crypt_clone(struct bio *dst, struct bio *src, gfp_t gfp_mask);

static inline void bio_crypt_set_ctx(struct bio *bio,
				     const struct blk_crypto_key *key,
				     u64 dun[BLK_CRYPTO_DUN_ARRAY_SIZE],
				     gfp_t gfp_mask)
{
	struct bio_crypt_ctx *bc = bio_crypt_alloc_ctx(gfp_mask);

	bc->bc_key = key;
	memcpy(bc->bc_dun, dun, sizeof(bc->bc_dun));
	bc->bc_ksm = NULL;
	bc->bc_keyslot = -1;

	bio->bi_crypt_context = bc;
}

void bio_crypt_ctx_release_keyslot(struct bio_crypt_ctx *bc);

int bio_crypt_ctx_acquire_keyslot(struct bio_crypt_ctx *bc,
				  struct keyslot_manager *ksm);

struct request;
bool bio_crypt_should_process(struct request *rq);

static inline bool bio_crypt_dun_is_contiguous(const struct bio_crypt_ctx *bc,
					       unsigned int bytes,
					u64 next_dun[BLK_CRYPTO_DUN_ARRAY_SIZE])
{
	int i = 0;
	unsigned int inc = bytes >> bc->bc_key->data_unit_size_bits;

	while (i < BLK_CRYPTO_DUN_ARRAY_SIZE) {
		if (bc->bc_dun[i] + inc != next_dun[i])
			return false;
		inc = ((bc->bc_dun[i] + inc)  < inc);
		i++;
	}

	return true;
}


static inline void bio_crypt_dun_increment(u64 dun[BLK_CRYPTO_DUN_ARRAY_SIZE],
					   unsigned int inc)
{
	int i = 0;

	while (inc && i < BLK_CRYPTO_DUN_ARRAY_SIZE) {
		dun[i] += inc;
		inc = (dun[i] < inc);
		i++;
	}
}

static inline void bio_crypt_advance(struct bio *bio, unsigned int bytes)
{
	struct bio_crypt_ctx *bc = bio->bi_crypt_context;

	if (!bc)
		return;

	bio_crypt_dun_increment(bc->bc_dun,
				bytes >> bc->bc_key->data_unit_size_bits);
}

bool bio_crypt_ctx_compatible(struct bio *b_1, struct bio *b_2);

bool bio_crypt_ctx_mergeable(struct bio *b_1, unsigned int b1_bytes,
			     struct bio *b_2);

#else /* CONFIG_BLK_INLINE_ENCRYPTION */
static inline int bio_crypt_ctx_init(void)
{
	return 0;
}

static inline bool bio_has_crypt_ctx(struct bio *bio)
{
	return false;
}

static inline void bio_crypt_clone(struct bio *dst, struct bio *src,
				   gfp_t gfp_mask) { }

static inline void bio_crypt_free_ctx(struct bio *bio) { }

static inline void bio_crypt_advance(struct bio *bio, unsigned int bytes) { }

static inline bool bio_crypt_ctx_compatible(struct bio *b_1, struct bio *b_2)
{
	return true;
}

static inline bool bio_crypt_ctx_mergeable(struct bio *b_1,
					   unsigned int b1_bytes,
					   struct bio *b_2)
{
	return true;
}

#endif /* CONFIG_BLK_INLINE_ENCRYPTION */

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

#endif /* CONFIG_BLOCK */

#endif /* __LINUX_BIO_CRYPT_CTX_H */
