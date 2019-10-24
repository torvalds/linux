/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2019 Google LLC
 */
#ifndef __LINUX_BIO_CRYPT_CTX_H
#define __LINUX_BIO_CRYPT_CTX_H

enum blk_crypto_mode_num {
	BLK_ENCRYPTION_MODE_INVALID	= 0,
	BLK_ENCRYPTION_MODE_AES_256_XTS	= 1,
};

#ifdef CONFIG_BLOCK
#include <linux/blk_types.h>

#ifdef CONFIG_BLK_INLINE_ENCRYPTION
struct bio_crypt_ctx {
	int keyslot;
	const u8 *raw_key;
	enum blk_crypto_mode_num crypto_mode;
	u64 data_unit_num;
	unsigned int data_unit_size_bits;

	/*
	 * The keyslot manager where the key has been programmed
	 * with keyslot.
	 */
	struct keyslot_manager *processing_ksm;

	/*
	 * Copy of the bvec_iter when this bio was submitted.
	 * We only want to en/decrypt the part of the bio
	 * as described by the bvec_iter upon submission because
	 * bio might be split before being resubmitted
	 */
	struct bvec_iter crypt_iter;
	u64 sw_data_unit_num;
};

extern int bio_crypt_clone(struct bio *dst, struct bio *src,
			   gfp_t gfp_mask);

static inline bool bio_has_crypt_ctx(struct bio *bio)
{
	return bio->bi_crypt_context;
}

static inline void bio_crypt_advance(struct bio *bio, unsigned int bytes)
{
	if (bio_has_crypt_ctx(bio)) {
		bio->bi_crypt_context->data_unit_num +=
			bytes >> bio->bi_crypt_context->data_unit_size_bits;
	}
}

static inline bool bio_crypt_has_keyslot(struct bio *bio)
{
	return bio->bi_crypt_context->keyslot >= 0;
}

extern int bio_crypt_ctx_init(void);

extern struct bio_crypt_ctx *bio_crypt_alloc_ctx(gfp_t gfp_mask);

extern void bio_crypt_free_ctx(struct bio *bio);

static inline int bio_crypt_set_ctx(struct bio *bio,
				    const u8 *raw_key,
				    enum blk_crypto_mode_num crypto_mode,
				    u64 dun,
				    unsigned int dun_bits,
				    gfp_t gfp_mask)
{
	struct bio_crypt_ctx *crypt_ctx;

	crypt_ctx = bio_crypt_alloc_ctx(gfp_mask);
	if (!crypt_ctx)
		return -ENOMEM;

	crypt_ctx->raw_key = raw_key;
	crypt_ctx->data_unit_num = dun;
	crypt_ctx->data_unit_size_bits = dun_bits;
	crypt_ctx->crypto_mode = crypto_mode;
	crypt_ctx->processing_ksm = NULL;
	crypt_ctx->keyslot = -1;
	bio->bi_crypt_context = crypt_ctx;

	return 0;
}

static inline void bio_set_data_unit_num(struct bio *bio, u64 dun)
{
	bio->bi_crypt_context->data_unit_num = dun;
}

static inline int bio_crypt_get_keyslot(struct bio *bio)
{
	return bio->bi_crypt_context->keyslot;
}

static inline void bio_crypt_set_keyslot(struct bio *bio,
					 unsigned int keyslot,
					 struct keyslot_manager *ksm)
{
	bio->bi_crypt_context->keyslot = keyslot;
	bio->bi_crypt_context->processing_ksm = ksm;
}

extern void bio_crypt_ctx_release_keyslot(struct bio *bio);

extern int bio_crypt_ctx_acquire_keyslot(struct bio *bio,
					 struct keyslot_manager *ksm);

static inline const u8 *bio_crypt_raw_key(struct bio *bio)
{
	return bio->bi_crypt_context->raw_key;
}

static inline enum blk_crypto_mode_num bio_crypto_mode(struct bio *bio)
{
	return bio->bi_crypt_context->crypto_mode;
}

static inline u64 bio_crypt_data_unit_num(struct bio *bio)
{
	return bio->bi_crypt_context->data_unit_num;
}

static inline u64 bio_crypt_sw_data_unit_num(struct bio *bio)
{
	return bio->bi_crypt_context->sw_data_unit_num;
}

extern bool bio_crypt_should_process(struct bio *bio, struct request_queue *q);

extern bool bio_crypt_ctx_compatible(struct bio *b_1, struct bio *b_2);

extern bool bio_crypt_ctx_back_mergeable(struct bio *b_1,
					 unsigned int b1_sectors,
					 struct bio *b_2);

#else /* CONFIG_BLK_INLINE_ENCRYPTION */
struct keyslot_manager;

static inline int bio_crypt_ctx_init(void)
{
	return 0;
}

static inline int bio_crypt_clone(struct bio *dst, struct bio *src,
				  gfp_t gfp_mask)
{
	return 0;
}

static inline void bio_crypt_advance(struct bio *bio,
				     unsigned int bytes) { }

static inline bool bio_has_crypt_ctx(struct bio *bio)
{
	return false;
}

static inline void bio_crypt_free_ctx(struct bio *bio) { }

static inline void bio_crypt_set_ctx(struct bio *bio,
				     u8 *raw_key,
				     enum blk_crypto_mode_num crypto_mode,
				     u64 dun,
				     unsigned int dun_bits,
				     gfp_t gfp_mask) { }

static inline void bio_set_data_unit_num(struct bio *bio, u64 dun) { }

static inline bool bio_crypt_has_keyslot(struct bio *bio)
{
	return false;
}

static inline void bio_crypt_set_keyslot(struct bio *bio,
					 unsigned int keyslot,
					 struct keyslot_manager *ksm) { }

static inline int bio_crypt_get_keyslot(struct bio *bio)
{
	return -1;
}

static inline u8 *bio_crypt_raw_key(struct bio *bio)
{
	return NULL;
}

static inline u64 bio_crypt_data_unit_num(struct bio *bio)
{
	return 0;
}

static inline bool bio_crypt_should_process(struct bio *bio,
					    struct request_queue *q)
{
	return false;
}

static inline bool bio_crypt_ctx_compatible(struct bio *b_1, struct bio *b_2)
{
	return true;
}

static inline bool bio_crypt_ctx_back_mergeable(struct bio *b_1,
						unsigned int b1_sectors,
						struct bio *b_2)
{
	return true;
}

#endif /* CONFIG_BLK_INLINE_ENCRYPTION */
#endif /* CONFIG_BLOCK */
#endif /* __LINUX_BIO_CRYPT_CTX_H */
