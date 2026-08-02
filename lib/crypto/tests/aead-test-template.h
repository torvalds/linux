/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Shared KUnit test cases for AEAD algorithms, including a benchmark
 *
 * Copyright 2026 Google LLC
 */

/*
 * This file implements KUnit test cases shared by the different KUnit test
 * suites for Authenticated Encryption with Associated Data (AEAD) algorithms.
 *
 * Test suites including this file must #define the following:
 *
 * Data structs:
 * - AEAD_KEY: name of key struct
 * - AEAD_CTX: name of context for incremental computation
 *
 * Constants:
 * - AEAD_VALID_KEY_LENS: array of all valid key lengths in bytes
 * - AEAD_VALID_NONCE_LENS: array of all valid nonce lengths in bytes
 * - AEAD_VALID_TAG_LENS: array of all valid authtag lengths in bytes
 * - AEAD_MAX_KEY_LEN: max key length in bytes (assumed to fit on stack)
 * - AEAD_MAX_NONCE_LEN: max nonce length in bytes (assumed to fit on stack)
 * - AEAD_MAX_TAG_LEN: max authtag length in bytes (assumed to fit on stack)
 * - AEAD_MONTE_CARLO_CHECKSUM: checksum of a deterministically generated series
 *   of (ciphertext, authtag) pairs (see test_aead_monte_carlo())
 *
 * Functions:
 * - AEAD_PREPAREKEY: key preparation
 * - AEAD_ENCRYPT and AEAD_DECRYPT: one-shot encryption and decryption
 * - AEAD_INIT, AEAD_AUTH_UPDATE, AEAD_ENCRYPT_UPDATE, AEAD_ENCRYPT_FINAL,
 *   AEAD_DECRYPT_UPDATE, AEAD_DECRYPT_FINAL: functions for incremental
 *   encryption and decryption
 *
 * Function prototypes and their behavior must match the AES-CCM API.
 */

#include <crypto/blake2s.h>
#include <kunit/run-in-irq-context.h>
#include <kunit/test.h>
#include <linux/ktime.h>
#include <linux/preempt.h>
#include "test-utils.h"

/*
 * Allocate a KUnit-managed struct AEAD_KEY and prepare it with a random key,
 * using a random key length and random authentication tag length.
 */
static struct AEAD_KEY *aead_alloc_random_key(struct kunit *test,
					      size_t *tag_len_ret)
{
	size_t key_len =
		AEAD_VALID_KEY_LENS[rand32() % ARRAY_SIZE(AEAD_VALID_KEY_LENS)];
	size_t tag_len =
		AEAD_VALID_TAG_LENS[rand32() % ARRAY_SIZE(AEAD_VALID_TAG_LENS)];
	u8 raw_key[AEAD_MAX_KEY_LEN];
	struct AEAD_KEY *key = alloc_buf(test, sizeof(*key));
	int err;

	rand_bytes(raw_key, key_len);
	err = AEAD_PREPAREKEY(key, raw_key, key_len, tag_len);
	KUNIT_ASSERT_EQ(test, 0, err);
	*tag_len_ret = tag_len;
	return key;
}

/*
 * Allocate a KUnit-managed slab buffer of length @len bytes and initialize it
 * with random data.
 */
static u8 *aead_alloc_random_data(struct kunit *test, size_t len)
{
	u8 *buf = alloc_buf(test, len);

	rand_bytes(buf, len);
	return buf;
}

/*
 * Allocate a KUnit-managed guarded buffer of length @len bytes and initialize
 * it with random data.
 */
static u8 *aead_alloc_random_data_guarded(struct kunit *test, size_t len)
{
	u8 *buf = alloc_guarded_buf(test, len);

	rand_bytes(buf, len);
	return buf;
}

/* Process the given associated data using a random incremental strategy. */
static size_t aead_auth_incrementally(struct AEAD_CTX *ctx, const u8 *ad,
				      size_t ad_len)
{
	size_t num_parts = 0;
	size_t pos = 0;

	while (rand_bool()) {
		size_t part_len = rand_length(ad_len - pos);

		AEAD_AUTH_UPDATE(ctx, &ad[pos], part_len);
		pos += part_len;
		num_parts++;
	}
	if (pos < ad_len || rand_bool()) {
		AEAD_AUTH_UPDATE(ctx, &ad[pos], ad_len - pos);
		num_parts++;
	}
	return num_parts;
}

/* Process the given en/decrypted data using a random incremental strategy. */
static size_t aead_crypt_incrementally(struct AEAD_CTX *ctx, u8 *dst,
				       const u8 *src, size_t data_len, bool enc)
{
	size_t num_parts = 0;
	size_t pos = 0;

	while (rand_bool()) {
		size_t part_len = rand_length(data_len - pos);

		if (enc)
			AEAD_ENCRYPT_UPDATE(ctx, &dst[pos], &src[pos],
					    part_len);
		else
			AEAD_DECRYPT_UPDATE(ctx, &dst[pos], &src[pos],
					    part_len);
		pos += part_len;
		num_parts++;
	}
	if (pos < data_len || rand_bool()) {
		if (enc)
			AEAD_ENCRYPT_UPDATE(ctx, &dst[pos], &src[pos],
					    data_len - pos);
		else
			AEAD_DECRYPT_UPDATE(ctx, &dst[pos], &src[pos],
					    data_len - pos);
		num_parts++;
	}
	return num_parts;
}

struct aead_incremental_info {
	size_t num_data_parts;
	size_t num_ad_parts;
};

static const char *aead_incr_info_str(struct kunit *test,
				      const struct aead_incremental_info *info)
{
	const size_t max_str_len = 64;
	char *str = alloc_buf(test, max_str_len);

	snprintf(str, max_str_len, "num_data_parts=%zu num_ad_parts=%zu",
		 info->num_data_parts, info->num_ad_parts);
	return str;
}

/*
 * Encrypt data using a random incremental strategy.
 * Return information about the incremental strategy used.
 */
static struct aead_incremental_info
aead_encrypt_incrementally(struct kunit *test, struct AEAD_CTX *ctx, u8 *dst,
			   const u8 *src, size_t data_len, u8 *tag,
			   const u8 *ad, size_t ad_len, const u8 *nonce,
			   size_t nonce_len, const struct AEAD_KEY *key)
{
	struct aead_incremental_info info;
	int err;

	err = AEAD_INIT(ctx, data_len, ad_len, nonce, nonce_len, key);
	KUNIT_ASSERT_EQ(test, 0, err);
	info.num_ad_parts = aead_auth_incrementally(ctx, ad, ad_len);
	info.num_data_parts = aead_crypt_incrementally(ctx, dst, src, data_len,
						       /* enc= */ true);
	AEAD_ENCRYPT_FINAL(ctx, tag);
	KUNIT_ASSERT_TRUE_MSG(test, mem_is_zero(ctx, sizeof(*ctx)),
			      "encrypt_final didn't zeroize context");
	return info;
}

/*
 * Decrypt authentic data using a random incremental strategy.
 * Return information about the incremental strategy used.
 */
static struct aead_incremental_info
aead_decrypt_incrementally(struct kunit *test, struct AEAD_CTX *ctx, u8 *dst,
			   const u8 *src, size_t data_len, const u8 *tag,
			   const u8 *ad, size_t ad_len, const u8 *nonce,
			   size_t nonce_len, const struct AEAD_KEY *key)
{
	struct aead_incremental_info info;
	int err;

	err = AEAD_INIT(ctx, data_len, ad_len, nonce, nonce_len, key);
	KUNIT_ASSERT_EQ(test, 0, err);
	info.num_ad_parts = aead_auth_incrementally(ctx, ad, ad_len);
	info.num_data_parts = aead_crypt_incrementally(ctx, dst, src, data_len,
						       /* enc= */ false);
	err = AEAD_DECRYPT_FINAL(ctx, tag);
	KUNIT_ASSERT_EQ(test, 0, err);
	KUNIT_ASSERT_TRUE_MSG(test, mem_is_zero(ctx, sizeof(*ctx)),
			      "decrypt_final didn't zeroize context");
	return info;
}

/* Return true if key_len is declared to be a valid key length. */
static bool aead_is_key_len_expected_valid(size_t key_len)
{
	for (size_t i = 0; i < ARRAY_SIZE(AEAD_VALID_KEY_LENS); i++) {
		if (AEAD_VALID_KEY_LENS[i] == key_len)
			return true;
	}
	return false;
}

/* Return true if nonce_len is declared to be a valid nonce length. */
static bool aead_is_nonce_len_expected_valid(size_t nonce_len)
{
	for (size_t i = 0; i < ARRAY_SIZE(AEAD_VALID_NONCE_LENS); i++) {
		if (AEAD_VALID_NONCE_LENS[i] == nonce_len)
			return true;
	}
	return false;
}

/* Return true if tag_len is declared to be a valid tag length. */
static bool aead_is_tag_len_expected_valid(size_t tag_len)
{
	for (size_t i = 0; i < ARRAY_SIZE(AEAD_VALID_TAG_LENS); i++) {
		if (AEAD_VALID_TAG_LENS[i] == tag_len)
			return true;
	}
	return false;
}

struct aead_basic_validation_test_ctx {
	struct AEAD_KEY key;
	struct AEAD_CTX ctx;
	u8 *raw_key_buf_end;
	u8 *nonce_buf_end;
	u8 *tag_buf_end;
	u8 pt[64]; /* plaintext */
	u8 ct[64]; /* ciphertext */
	u8 decrypted[64];
	u8 ad[16]; /* associated data */
	u8 *unused_buf;
	size_t data_len;
	size_t ad_len;
};

static struct aead_basic_validation_test_ctx *
aead_alloc_basic_validation_test_ctx(struct kunit *test)
{
	struct aead_basic_validation_test_ctx *ctx =
		alloc_buf(test, sizeof(*ctx));

	memset(ctx, 0, sizeof(*ctx));
	ctx->raw_key_buf_end =
		aead_alloc_random_data_guarded(test, AEAD_MAX_KEY_LEN) +
		AEAD_MAX_KEY_LEN;
	ctx->nonce_buf_end =
		aead_alloc_random_data_guarded(test, AEAD_MAX_NONCE_LEN) +
		AEAD_MAX_NONCE_LEN;
	ctx->tag_buf_end =
		aead_alloc_random_data_guarded(test, AEAD_MAX_TAG_LEN) +
		AEAD_MAX_TAG_LEN;

	/*
	 * A pointer to this buffer is passed when passing a length that is
	 * expected to be invalid.  It should never actually be accessed.
	 */
	ctx->unused_buf =
		alloc_buf(test, max3(AEAD_MAX_KEY_LEN, AEAD_MAX_NONCE_LEN,
				     AEAD_MAX_TAG_LEN));

	ctx->data_len = sizeof(ctx->pt);
	ctx->ad_len = sizeof(ctx->ad);
	return ctx;
}

/*
 * Given an expected-valid key_len, nonce_len, and tag_len, verify round-trip
 * encryption and decryption with them.  Use guarded buffers for each of the raw
 * key, nonce, and tag to detect any buffer overruns in them.  Also, verify that
 * every byte of the tag is actually checked.
 */
static void aead_do_basic_checks(struct kunit *test,
				 struct aead_basic_validation_test_ctx *ctx,
				 size_t key_len, size_t nonce_len,
				 size_t tag_len)
{
	/* Set up exact-size guarded buffers for (raw_key, nonce, tag). */
	const u8 *raw_key = ctx->raw_key_buf_end - key_len;
	const u8 *nonce = ctx->nonce_buf_end - nonce_len;
	u8 *tag = ctx->tag_buf_end - tag_len;
	int err;

	/* Key preparation should succeed. */
	err = AEAD_PREPAREKEY(&ctx->key, raw_key, key_len, tag_len);
	KUNIT_ASSERT_EQ_MSG(test, 0, err,
			    "key_len=%zu, tag_len=%zu wasn't accepted", key_len,
			    tag_len);

	/* Encryption should succeed. */
	err = AEAD_ENCRYPT(ctx->ct, ctx->pt, ctx->data_len, tag, ctx->ad,
			   ctx->ad_len, nonce, nonce_len, &ctx->key);
	KUNIT_ASSERT_EQ_MSG(
		test, 0, err,
		"Encryption failed with key_len=%zu, nonce_len=%zu, tag_len=%zu",
		key_len, nonce_len, tag_len);

	/* Decryption should succeed and give the original data. */
	err = AEAD_DECRYPT(ctx->decrypted, ctx->ct, ctx->data_len, tag, ctx->ad,
			   ctx->ad_len, nonce, nonce_len, &ctx->key);
	KUNIT_ASSERT_EQ_MSG(
		test, 0, err,
		"Decryption failed with key_len=%zu, nonce_len=%zu, tag_len=%zu",
		key_len, nonce_len, tag_len);
	KUNIT_ASSERT_MEMEQ_MSG(
		test, ctx->pt, ctx->decrypted, ctx->data_len,
		"Decryption gave wrong output with key_len=%zu, nonce_len=%zu, tag_len=%zu",
		key_len, nonce_len, tag_len);

	/*
	 * Every byte of the tag should actually be checked.
	 * And on authentication failure, the dst buffer should be cleared.
	 */
	for (size_t i = 0; i < tag_len; i++) {
		memset(ctx->decrypted, 0xff, ctx->data_len);
		tag[i] ^= 1;
		err = AEAD_DECRYPT(ctx->decrypted, ctx->ct, ctx->data_len, tag,
				   ctx->ad, ctx->ad_len, nonce, nonce_len,
				   &ctx->key);
		KUNIT_ASSERT_EQ_MSG(
			test, -EBADMSG, err,
			"Decryption with bad auth tag with key_len=%zu, nonce_len=%zu, tag_len=%zu didn't fail with -EBADMSG",
			key_len, nonce_len, tag_len);
		KUNIT_ASSERT_TRUE_MSG(
			test, mem_is_zero(ctx->decrypted, ctx->data_len),
			"dst wasn't cleared on authentication failure");
		tag[i] ^= 1;
	}
}

/* Verify that the given expected-invalid key_len is actually rejected. */
static void
aead_verify_invalid_key_len(struct kunit *test,
			    struct aead_basic_validation_test_ctx *ctx,
			    size_t key_len)
{
	int err;

	/*
	 * The preparekey function should reject the key_len.  It should do so
	 * before writing to the key struct.
	 */
	memset(&ctx->key, 0, sizeof(ctx->key));
	err = AEAD_PREPAREKEY(&ctx->key, ctx->unused_buf, key_len,
			      AEAD_MAX_TAG_LEN);
	KUNIT_ASSERT_EQ_MSG(test, -EINVAL, err,
			    "key_len=%zu wasn't rejected with -EINVAL",
			    key_len);
	KUNIT_ASSERT_TRUE_MSG(
		test, mem_is_zero(&ctx->key, sizeof(ctx->key)),
		"Key struct was written to before length validation");
}

/*
 * Test that every valid key length is accepted and basic checks pass with it,
 * and test that invalid key lengths are rejected.
 */
static void test_aead_all_key_lens(struct kunit *test)
{
	struct aead_basic_validation_test_ctx *ctx =
		aead_alloc_basic_validation_test_ctx(test);

	for (size_t key_len = 0; key_len <= AEAD_MAX_KEY_LEN; key_len++) {
		if (aead_is_key_len_expected_valid(key_len))
			aead_do_basic_checks(test, ctx, key_len,
					     AEAD_MAX_NONCE_LEN,
					     AEAD_MAX_TAG_LEN);
		else
			aead_verify_invalid_key_len(test, ctx, key_len);
	}
	aead_verify_invalid_key_len(test, ctx, AEAD_MAX_KEY_LEN + 1);
	aead_verify_invalid_key_len(test, ctx, AEAD_MAX_KEY_LEN * 2);
	aead_verify_invalid_key_len(test, ctx, U32_MAX);
	aead_verify_invalid_key_len(test, ctx, SIZE_MAX);
}

/* Verify that the given expected-invalid nonce_len is actually rejected. */
static void
aead_verify_invalid_nonce_len(struct kunit *test,
			      struct aead_basic_validation_test_ctx *ctx,
			      size_t nonce_len)
{
	static const u8 raw_key[AEAD_MAX_KEY_LEN];
	int err;

	/* Key preparation should succeed, as nonce_len isn't given yet. */
	err = AEAD_PREPAREKEY(&ctx->key, raw_key, sizeof(raw_key),
			      AEAD_MAX_TAG_LEN);
	KUNIT_ASSERT_EQ(test, 0, err);

	/* The init function should reject the nonce_len. */
	memset(&ctx->ctx, 0, sizeof(ctx->ctx));
	err = AEAD_INIT(&ctx->ctx, ctx->data_len, ctx->ad_len, ctx->unused_buf,
			nonce_len, &ctx->key);
	KUNIT_ASSERT_EQ_MSG(test, -EINVAL, err,
			    "nonce_len=%zu wasn't rejected with -EINVAL (init)",
			    nonce_len);
	KUNIT_ASSERT_TRUE_MSG(
		test, mem_is_zero(&ctx->ctx, sizeof(ctx->ctx)),
		"Context struct was written to before length validation");

	/* The encrypt function should reject the nonce_len. */
	err = AEAD_ENCRYPT(ctx->ct, ctx->pt, ctx->data_len, ctx->unused_buf,
			   ctx->ad, ctx->ad_len, ctx->unused_buf, nonce_len,
			   &ctx->key);
	KUNIT_ASSERT_EQ_MSG(
		test, -EINVAL, err,
		"nonce_len=%zu wasn't rejected with -EINVAL (encrypt)",
		nonce_len);

	/* The decrypt function should reject the nonce_len. */
	err = AEAD_DECRYPT(ctx->pt, ctx->ct, ctx->data_len, ctx->unused_buf,
			   ctx->ad, ctx->ad_len, ctx->unused_buf, nonce_len,
			   &ctx->key);
	KUNIT_ASSERT_EQ_MSG(
		test, -EINVAL, err,
		"nonce_len=%zu wasn't rejected with -EINVAL (decrypt)",
		nonce_len);
}

/*
 * Test that every valid nonce length is accepted and basic checks pass with it,
 * and test that invalid nonce lengths are rejected.
 */
static void test_aead_all_nonce_lens(struct kunit *test)
{
	struct aead_basic_validation_test_ctx *ctx =
		aead_alloc_basic_validation_test_ctx(test);

	for (size_t nonce_len = 0; nonce_len <= AEAD_MAX_NONCE_LEN;
	     nonce_len++) {
		if (aead_is_nonce_len_expected_valid(nonce_len))
			aead_do_basic_checks(test, ctx, AEAD_MAX_KEY_LEN,
					     nonce_len, AEAD_MAX_TAG_LEN);
		else
			aead_verify_invalid_nonce_len(test, ctx, nonce_len);
	}
	aead_verify_invalid_nonce_len(test, ctx, AEAD_MAX_NONCE_LEN + 1);
	aead_verify_invalid_nonce_len(test, ctx, AEAD_MAX_NONCE_LEN * 2);
	aead_verify_invalid_nonce_len(test, ctx, U32_MAX);
	aead_verify_invalid_nonce_len(test, ctx, SIZE_MAX);
}

/* Verify that the given expected-invalid tag_len is actually rejected. */
static void
aead_verify_invalid_tag_len(struct kunit *test,
			    struct aead_basic_validation_test_ctx *ctx,
			    size_t tag_len)
{
	static const u8 raw_key[AEAD_MAX_KEY_LEN];
	int err;

	/*
	 * The preparekey function should reject the tag_len.  It should do so
	 * before writing to the key struct.
	 */
	memset(&ctx->key, 0, sizeof(ctx->key));
	err = AEAD_PREPAREKEY(&ctx->key, raw_key, sizeof(raw_key), tag_len);
	KUNIT_ASSERT_EQ_MSG(test, -EINVAL, err,
			    "tag_len=%zu wasn't rejected with -EINVAL",
			    tag_len);
	KUNIT_ASSERT_TRUE_MSG(
		test, mem_is_zero(&ctx->key, sizeof(ctx->key)),
		"Key struct was written to before length validation");
}

/*
 * Test that every valid authentication tag length is accepted and basic checks
 * pass with it, and test that invalid authentication tag lengths are rejected.
 */
static void test_aead_all_tag_lens(struct kunit *test)
{
	struct aead_basic_validation_test_ctx *ctx =
		aead_alloc_basic_validation_test_ctx(test);

	for (size_t tag_len = 0; tag_len <= AEAD_MAX_TAG_LEN; tag_len++) {
		if (aead_is_tag_len_expected_valid(tag_len))
			aead_do_basic_checks(test, ctx, AEAD_MAX_KEY_LEN,
					     AEAD_MAX_NONCE_LEN, tag_len);
		else
			aead_verify_invalid_tag_len(test, ctx, tag_len);
	}
	aead_verify_invalid_tag_len(test, ctx, AEAD_MAX_TAG_LEN + 1);
	aead_verify_invalid_tag_len(test, ctx, AEAD_MAX_TAG_LEN * 2);
	aead_verify_invalid_tag_len(test, ctx, U32_MAX);
	aead_verify_invalid_tag_len(test, ctx, SIZE_MAX);
}

/*
 * Test that one-shot encryption and decryption are consistent with each other
 * and with incremental encryption and decryption.
 */
static void test_aead_incremental_updates(struct kunit *test)
{
	const size_t max_data_len = 1024;
	const size_t max_ad_len = 512;
	const size_t nonce_len = AEAD_MAX_NONCE_LEN;
	size_t tag_len;
	struct AEAD_KEY *key = aead_alloc_random_key(test, &tag_len);
	struct AEAD_CTX *ctx = alloc_buf(test, sizeof(*ctx));
	u8 *pt = aead_alloc_random_data(test, max_data_len);
	u8 *ad = aead_alloc_random_data(test, max_ad_len);
	u8 *nonce = aead_alloc_random_data(test, nonce_len);
	u8 *ct = alloc_buf(test, max_data_len);
	u8 *ct2 = alloc_buf(test, max_data_len);
	u8 *decrypted = alloc_buf(test, max_data_len);
	u8 *tag = alloc_buf(test, tag_len);
	u8 *tag2 = alloc_buf(test, tag_len);
	int err;

	for (int i = 0; i < 500; i++) {
		/* Select the lengths to test. */
		const size_t data_len = rand_length(max_data_len);
		const size_t ad_len = rand_length(max_ad_len);
		struct aead_incremental_info incr_info;

		/* Try one-shot encryption and decryption. */
		err = AEAD_ENCRYPT(ct, pt, data_len, tag, ad, ad_len, nonce,
				   nonce_len, key);
		KUNIT_ASSERT_EQ(test, 0, err);
		err = AEAD_DECRYPT(decrypted, ct, data_len, tag, ad, ad_len,
				   nonce, nonce_len, key);
		KUNIT_ASSERT_EQ(test, 0, err);
		KUNIT_ASSERT_MEMEQ_MSG(
			test, pt, decrypted, data_len,
			"Decryption didn't invert encryption; data_len=%zu, ad_len=%zu",
			data_len, ad_len);

		/* Try incremental encryption and decryption. */
		incr_info = aead_encrypt_incrementally(test, ctx, ct2, pt,
						       data_len, tag2, ad,
						       ad_len, nonce, nonce_len,
						       key);
		KUNIT_ASSERT_MEMEQ_MSG(
			test, ct, ct2, data_len,
			"One-shot and incremental encryption gave different ciphertexts; data_len=%zu ad_len=%zu %s",
			data_len, ad_len, aead_incr_info_str(test, &incr_info));
		KUNIT_ASSERT_MEMEQ_MSG(
			test, tag, tag2, tag_len,
			"One-shot and incremental encryption gave different auth tags; data_len=%zu ad_len=%zu %s",
			data_len, ad_len, aead_incr_info_str(test, &incr_info));
		incr_info = aead_decrypt_incrementally(test, ctx, decrypted,
						       ct2, data_len, tag2, ad,
						       ad_len, nonce, nonce_len,
						       key);
		KUNIT_ASSERT_MEMEQ_MSG(
			test, pt, decrypted, data_len,
			"One-shot and incremental decryption gave different plaintexts; data_len=%zu ad_len=%zu %s",
			data_len, ad_len, aead_incr_info_str(test, &incr_info));
	}
}

/*
 * Test using guarded buffers for the plaintext, ciphertext, and associated
 * data.  This detects out-of-bounds accesses, even in assembly code.
 *
 * Note: other test cases cover overrun of raw_key, nonce, and tag.
 */
static void test_aead_data_buffer_overruns(struct kunit *test)
{
	const size_t max_data_len = 1024;
	const size_t max_ad_len = 512;
	const size_t nonce_len = AEAD_MAX_NONCE_LEN;
	size_t tag_len;
	struct AEAD_KEY *key = aead_alloc_random_key(test, &tag_len);
	const u8 *nonce = aead_alloc_random_data(test, nonce_len);
	const u8 *pt_end = aead_alloc_random_data_guarded(test, max_data_len) +
			   max_data_len;
	const u8 *ad_end =
		aead_alloc_random_data_guarded(test, max_ad_len) + max_ad_len;
	u8 *ct_end = alloc_guarded_buf(test, max_data_len) + max_data_len;
	u8 *decrypted_end =
		alloc_guarded_buf(test, max_data_len) + max_data_len;
	u8 *tag = alloc_buf(test, tag_len);

	for (int i = 0; i < 200; i++) {
		/* Select the lengths to test. */
		const size_t data_len = rand_length(max_data_len);
		const size_t ad_len = rand_length(max_ad_len);
		/* Set up exact-size guarded buffers. */
		const u8 *pt = pt_end - data_len;
		const u8 *ad = ad_end - ad_len;
		u8 *ct = ct_end - data_len;
		u8 *decrypted = decrypted_end - data_len;
		int err;

		/* Encrypt and decrypt. */
		err = AEAD_ENCRYPT(ct, pt, data_len, tag, ad, ad_len, nonce,
				   nonce_len, key);
		KUNIT_ASSERT_EQ(test, 0, err);
		err = AEAD_DECRYPT(decrypted, ct, data_len, tag, ad, ad_len,
				   nonce, nonce_len, key);
		KUNIT_ASSERT_EQ(test, 0, err);
		KUNIT_ASSERT_MEMEQ_MSG(
			test, pt, decrypted, data_len,
			"Decryption didn't invert encryption; data_len=%zu, ad_len=%zu",
			data_len, ad_len);
	}
}

/*
 * Test that encryption and decryption produce the same results regardless of
 * how the buffers are aligned in memory.
 */
static void test_aead_alignment_consistency(struct kunit *test)
{
	const size_t max_data_len = 4096;
	const size_t max_ad_len = 4096;
	const size_t max_offset = 128;
	const size_t nonce_len = AEAD_MAX_NONCE_LEN;
	const size_t key_len = AEAD_MAX_KEY_LEN;
	const size_t tag_len = AEAD_MAX_TAG_LEN;
	u8 *raw_key1_buf = alloc_buf(test, key_len + max_offset);
	u8 *raw_key2_buf = alloc_buf(test, key_len + max_offset);
	u8 *nonce1_buf = alloc_buf(test, nonce_len + max_offset);
	u8 *nonce2_buf = alloc_buf(test, nonce_len + max_offset);
	u8 *pt1_buf = alloc_buf(test, max_data_len);
	u8 *pt2_buf = alloc_buf(test, max_data_len);
	u8 *ct1_buf = alloc_buf(test, max_data_len);
	u8 *ct2_buf = alloc_buf(test, max_data_len);
	u8 *ad1_buf = alloc_buf(test, max_ad_len);
	u8 *ad2_buf = alloc_buf(test, max_ad_len);
	u8 *tag1_buf = alloc_buf(test, tag_len + max_offset);
	u8 *tag2_buf = alloc_buf(test, tag_len + max_offset);
	struct AEAD_KEY *key = alloc_buf(test, sizeof(*key));
	int err;

	for (int i = 0; i < 100; i++) {
		/* Generate lengths. */
		size_t data_len = rand_length(max_data_len);
		size_t ad_len = rand_length(max_ad_len);

		/* Generate two sets of alignments. */
		u8 *raw_key1 = raw_key1_buf + rand_offset(max_offset);
		u8 *raw_key2 = raw_key2_buf + rand_offset(max_offset);
		u8 *nonce1 = nonce1_buf + rand_offset(max_offset);
		u8 *nonce2 = nonce2_buf + rand_offset(max_offset);
		u8 *pt1 = pt1_buf + rand_offset(max_data_len - data_len);
		u8 *pt2 = pt2_buf + rand_offset(max_data_len - data_len);
		u8 *ct1 = ct1_buf + rand_offset(max_data_len - data_len);
		u8 *ct2 = ct2_buf + rand_offset(max_data_len - data_len);
		u8 *ad1 = ad1_buf + rand_offset(max_ad_len - ad_len);
		u8 *ad2 = ad2_buf + rand_offset(max_ad_len - ad_len);
		u8 *tag1 = tag1_buf + rand_offset(max_offset);
		u8 *tag2 = tag2_buf + rand_offset(max_offset);

		/*
		 * Generate inputs in the first set of buffers using the first
		 * set of alignments.
		 */
		rand_bytes(raw_key1, key_len);
		rand_bytes(nonce1, nonce_len);
		rand_bytes(pt1, data_len);
		rand_bytes(ad1, ad_len);

		/*
		 * Copy the inputs to the second set of buffers using the second
		 * set of alignments.
		 */
		memcpy(raw_key2, raw_key1, key_len);
		memcpy(nonce2, nonce1, nonce_len);
		memcpy(pt2, pt1, data_len);
		memcpy(ad2, ad1, ad_len);

		/* Verify encryption consistency. */

		err = AEAD_PREPAREKEY(key, raw_key1, key_len, tag_len);
		KUNIT_ASSERT_EQ(test, 0, err);
		err = AEAD_ENCRYPT(ct1, pt1, data_len, tag1, ad1, ad_len,
				   nonce1, nonce_len, key);
		KUNIT_ASSERT_EQ(test, 0, err);

		err = AEAD_PREPAREKEY(key, raw_key2, key_len, tag_len);
		KUNIT_ASSERT_EQ(test, 0, err);
		err = AEAD_ENCRYPT(ct2, pt2, data_len, tag2, ad2, ad_len,
				   nonce2, nonce_len, key);
		KUNIT_ASSERT_EQ(test, 0, err);

		KUNIT_ASSERT_MEMEQ(test, ct1, ct2, data_len);
		KUNIT_ASSERT_MEMEQ(test, tag1, tag2, tag_len);

		/* Verify decryption consistency. */

		err = AEAD_PREPAREKEY(key, raw_key1, key_len, tag_len);
		KUNIT_ASSERT_EQ(test, 0, err);
		err = AEAD_DECRYPT(pt1, ct1, data_len, tag1, ad1, ad_len,
				   nonce1, nonce_len, key);
		KUNIT_ASSERT_EQ(test, 0, err);

		err = AEAD_PREPAREKEY(key, raw_key2, key_len, tag_len);
		KUNIT_ASSERT_EQ(test, 0, err);
		err = AEAD_DECRYPT(pt2, ct2, data_len, tag2, ad2, ad_len,
				   nonce2, nonce_len, key);
		KUNIT_ASSERT_EQ(test, 0, err);

		KUNIT_ASSERT_MEMEQ(test, pt1, pt2, data_len);
	}
}

static void test_aead_inplace(struct kunit *test)
{
	const size_t max_data_len = 1024;
	const size_t max_ad_len = 512;
	const size_t nonce_len = AEAD_MAX_NONCE_LEN;
	size_t tag_len;
	struct AEAD_KEY *key = aead_alloc_random_key(test, &tag_len);
	u8 *data = aead_alloc_random_data(test, max_data_len + tag_len);
	u8 *data2 = alloc_buf(test, max_data_len + tag_len);
	u8 *ad = aead_alloc_random_data(test, max_ad_len);
	const u8 *nonce = aead_alloc_random_data(test, nonce_len);

	for (int i = 0; i < 100; i++) {
		size_t data_len = rand_length(max_data_len);
		size_t ad_len = rand_length(max_ad_len);
		int err;

		/* Encrypt out-of-place. */
		err = AEAD_ENCRYPT(data2, data, data_len, data2 + data_len, ad,
				   ad_len, nonce, nonce_len, key);
		KUNIT_ASSERT_EQ(test, 0, err);

		/* Encrypt in-place. */
		err = AEAD_ENCRYPT(data, data, data_len, data + data_len, ad,
				   ad_len, nonce, nonce_len, key);
		KUNIT_ASSERT_EQ(test, 0, err);

		/* Compare the results. */
		KUNIT_ASSERT_MEMEQ(test, data2, data, data_len + tag_len);

		/* Decrypt out-of-place. */
		err = AEAD_DECRYPT(data2, data, data_len, data + data_len, ad,
				   ad_len, nonce, nonce_len, key);
		KUNIT_ASSERT_EQ(test, 0, err);

		/* Decrypt in-place. */
		err = AEAD_DECRYPT(data, data, data_len, data + data_len, ad,
				   ad_len, nonce, nonce_len, key);
		KUNIT_ASSERT_EQ(test, 0, err);

		/* Compare the results. */
		KUNIT_ASSERT_MEMEQ(test, data2, data, data_len);
	}
}

/*
 * Monte-Carlo test for AEAD algorithms.  This deterministically generates
 * random AEAD inputs, encrypts them, verifies decryption, and computes and
 * verifies the checksum of all computed (ciphertext, tag) pairs.
 */
static void test_aead_monte_carlo(struct kunit *test)
{
	const size_t max_data_len = 1024;
	const size_t max_ad_len = 293;
	u8 raw_key[AEAD_MAX_KEY_LEN];
	u8 nonce[AEAD_MAX_NONCE_LEN];
	u8 tag[AEAD_MAX_TAG_LEN];
	u8 *pt = alloc_buf(test, max_data_len);
	u8 *ct = alloc_buf(test, max_data_len);
	u8 *decrypted = alloc_buf(test, max_data_len);
	u8 *ad = alloc_buf(test, max_ad_len);
	struct AEAD_KEY *key = alloc_buf(test, sizeof(*key));
	struct blake2s_ctx checksum_ctx;
	u8 actual_checksum[BLAKE2S_HASH_SIZE];
	int err;

	blake2s_init(&checksum_ctx, BLAKE2S_HASH_SIZE);

	for (size_t data_len = 0; data_len <= max_data_len; data_len++) {
		size_t ad_len = data_len % max_ad_len;
		size_t key_len =
			AEAD_VALID_KEY_LENS[data_len %
					    ARRAY_SIZE(AEAD_VALID_KEY_LENS)];
		size_t nonce_len =
			AEAD_VALID_NONCE_LENS[data_len %
					      ARRAY_SIZE(AEAD_VALID_NONCE_LENS)];
		size_t tag_len =
			AEAD_VALID_TAG_LENS[data_len %
					    ARRAY_SIZE(AEAD_VALID_TAG_LENS)];

		rand_bytes_seeded_from_len(pt, data_len);
		rand_bytes_seeded_from_len(ad, ad_len);
		rand_bytes_seeded_from_len(raw_key, key_len);
		rand_bytes_seeded_from_len(nonce, nonce_len);

		err = AEAD_PREPAREKEY(key, raw_key, key_len, tag_len);
		KUNIT_ASSERT_EQ(test, 0, err);

		err = AEAD_ENCRYPT(ct, pt, data_len, tag, ad, ad_len, nonce,
				   nonce_len, key);
		KUNIT_ASSERT_EQ_MSG(
			test, 0, err,
			"Encryption failed with data_len=%zu, ad_len=%zu",
			data_len, ad_len);
		err = AEAD_DECRYPT(decrypted, ct, data_len, tag, ad, ad_len,
				   nonce, nonce_len, key);
		KUNIT_ASSERT_EQ_MSG(
			test, 0, err,
			"Decryption failed with data_len=%zu, ad_len=%zu",
			data_len, ad_len);
		KUNIT_ASSERT_MEMEQ_MSG(
			test, pt, decrypted, data_len,
			"Decryption didn't invert encryption; data_len=%zu, ad_len=%zu",
			data_len, ad_len);

		blake2s_update(&checksum_ctx, ct, data_len);
		blake2s_update(&checksum_ctx, tag, tag_len);
	}

	blake2s_final(&checksum_ctx, actual_checksum);
	KUNIT_EXPECT_MEMEQ_MSG(test, actual_checksum, AEAD_MONTE_CARLO_CHECKSUM,
			       BLAKE2S_HASH_SIZE,
			       "Monte-Carlo checksum mismatch");
}

#define IRQ_TEST_DATA_LEN 256
#define IRQ_TEST_NUM_BUFFERS 3 /* matches max concurrency level */

struct aead_irq_test_slot {
	/* Fields written only at test case initialization time */
	u8 raw_key[AEAD_MAX_KEY_LEN];
	u8 nonce[AEAD_MAX_NONCE_LEN];
	u8 pt[IRQ_TEST_DATA_LEN];
	u8 ct[IRQ_TEST_DATA_LEN + AEAD_MAX_TAG_LEN];
	u8 ad[IRQ_TEST_DATA_LEN];

	/* Fields written throughout the test case */
	struct AEAD_KEY key;
	u8 scratch_buf[IRQ_TEST_DATA_LEN + AEAD_MAX_TAG_LEN];
	int phase;
	atomic_t in_use;
};

struct aead_irq_test_state {
	struct aead_irq_test_slot slots[IRQ_TEST_NUM_BUFFERS];
};

static bool aead_irq_test_func(void *state_)
{
	struct aead_irq_test_state *state = state_;
	struct aead_irq_test_slot *slot;
	size_t data_len;
	bool ok = true;

	/*
	 * Find a free slot.  This should always succeed, since the number of
	 * slots is equal to the max concurrency level of kunit_run_irq_test().
	 */
	for (slot = &state->slots[0];
	     slot < &state->slots[ARRAY_SIZE(state->slots)]; slot++) {
		if (atomic_cmpxchg(&slot->in_use, 0, 1) == 0)
			break;
	}
	if (WARN_ON_ONCE(slot == &state->slots[ARRAY_SIZE(state->slots)]))
		return false;
	/*
	 * This execution context now has exclusive access to 'slot'.
	 * Next, execute the next operation that the slot is set to perform.
	 */

	data_len = sizeof(slot->pt);
	if (slot->phase == 0) {
		/* Phase 0: Prepare slot's key in current context. */
		ok = ok && AEAD_PREPAREKEY(&slot->key, slot->raw_key,
					   sizeof(slot->raw_key),
					   AEAD_MAX_TAG_LEN) == 0;
	} else if (slot->phase == 1) {
		/*
		 * Phase 1: Encrypt plaintext using key that may have been
		 * prepared in a different context.
		 */
		ok = ok && AEAD_ENCRYPT(slot->scratch_buf, slot->pt, data_len,
					&slot->scratch_buf[data_len], slot->ad,
					sizeof(slot->ad), slot->nonce,
					sizeof(slot->nonce), &slot->key) == 0;
		/* Verify the ciphertext (with concatenated auth tag) matches */
		ok = ok &&
		     memcmp(slot->scratch_buf, slot->ct, sizeof(slot->ct)) == 0;
	} else {
		/*
		 * Phase 2: Decrypt ciphertext using key that may have been
		 * prepared in a different context.
		 */
		ok = ok && AEAD_DECRYPT(slot->scratch_buf, slot->ct, data_len,
					&slot->ct[data_len], slot->ad,
					sizeof(slot->ad), slot->nonce,
					sizeof(slot->nonce), &slot->key) == 0;
		/* Verify the plaintext matches. */
		ok = ok && memcmp(slot->scratch_buf, slot->pt, data_len) == 0;
	}
	slot->phase = (slot->phase + 1) % 3;
	atomic_set_release(&slot->in_use, 0);
	return ok;
}

/*
 * Test that encryption and decryption produce the correct results in task,
 * softirq, and hardirq contexts running concurrently -- including with keys
 * prepared in other contexts.  This is needed to cover fallback code paths that
 * execute in contexts where FPU or vector registers cannot be used.
 */
static void test_aead_interrupt_context(struct kunit *test)
{
	struct aead_irq_test_state *state = alloc_buf(test, sizeof(*state));

	memset(state, 0, sizeof(*state));

	/*
	 * For each slot, generate a set of AEAD inputs: a key, a nonce, a
	 * plaintext, and some associated data.  Then generate the corresponding
	 * ciphertext with concatenated auth tag.
	 */
	for (int i = 0; i < IRQ_TEST_NUM_BUFFERS; i++) {
		struct aead_irq_test_slot *slot = &state->slots[i];
		int err;

		rand_bytes(slot->raw_key, sizeof(slot->raw_key));
		rand_bytes(slot->nonce, sizeof(slot->nonce));
		rand_bytes(slot->pt, sizeof(slot->pt));
		rand_bytes(slot->ad, sizeof(slot->ad));
		err = AEAD_PREPAREKEY(&slot->key, slot->raw_key,
				      sizeof(slot->raw_key), AEAD_MAX_TAG_LEN);
		KUNIT_ASSERT_EQ(test, 0, err);
		err = AEAD_ENCRYPT(slot->ct, slot->pt, sizeof(slot->pt),
				   &slot->ct[sizeof(slot->pt)], slot->ad,
				   sizeof(slot->ad), slot->nonce,
				   sizeof(slot->nonce), &slot->key);
		KUNIT_ASSERT_EQ(test, 0, err);
	}

	kunit_run_irq_test(test, aead_irq_test_func, 100000, state);
}

/* Benchmark AEAD encryption and decryption on various data lengths. */
static void benchmark_aead(struct kunit *test)
{
	static const size_t data_lens_to_test[] = {
		16, 64, 128, 256, 512, 1024, 1420, 4096, 16384,
	};
	const size_t max_data_len = 16384;
	const size_t ad_len = 16;
	const size_t key_len = AEAD_MAX_KEY_LEN;
	const size_t nonce_len = AEAD_MAX_NONCE_LEN;
	const size_t tag_len = AEAD_MAX_TAG_LEN;
	const u8 *raw_key, *nonce, *ad;
	u8 *pt, *ct, *tag;
	struct AEAD_KEY *key;
	int err;

	if (!IS_ENABLED(CONFIG_CRYPTO_LIB_BENCHMARK))
		kunit_skip(test, "not enabled");

	raw_key = aead_alloc_random_data(test, key_len);
	nonce = aead_alloc_random_data(test, nonce_len);
	ad = aead_alloc_random_data(test, ad_len);
	pt = aead_alloc_random_data(test, max_data_len);
	ct = alloc_buf(test, max_data_len);
	tag = alloc_buf(test, tag_len);

	key = alloc_buf(test, sizeof(*key));
	err = AEAD_PREPAREKEY(key, raw_key, key_len, tag_len);
	KUNIT_ASSERT_EQ(test, 0, err);

	/* Warm-up */
	for (size_t i = 0; i < 10000000; i += max_data_len) {
		err = AEAD_ENCRYPT(ct, pt, max_data_len, tag, ad, ad_len, nonce,
				   nonce_len, key);
		KUNIT_ASSERT_EQ(test, 0, err);
		err = AEAD_DECRYPT(pt, ct, max_data_len, tag, ad, ad_len, nonce,
				   nonce_len, key);
		KUNIT_ASSERT_EQ(test, 0, err);
	}

	for (size_t i = 0; i < ARRAY_SIZE(data_lens_to_test); i++) {
		size_t data_len = data_lens_to_test[i];
		size_t num_iters = 10000000 / (data_len + 128);
		u64 t_enc, t_dec;
		bool ok = true;

		KUNIT_ASSERT_LE(test, data_len, max_data_len);

		preempt_disable();

		t_enc = ktime_get_ns();
		for (size_t j = 0; j < num_iters; j++) {
			err = AEAD_ENCRYPT(ct, pt, data_len, tag, ad, ad_len,
					   nonce, nonce_len, key);
			ok &= (err == 0);
		}
		t_enc = ktime_get_ns() - t_enc;

		t_dec = ktime_get_ns();
		for (size_t j = 0; j < num_iters; j++) {
			err = AEAD_DECRYPT(pt, ct, data_len, tag, ad, ad_len,
					   nonce, nonce_len, key);
			ok &= (err == 0);
		}
		t_dec = ktime_get_ns() - t_dec;

		preempt_enable();

		KUNIT_ASSERT_TRUE_MSG(test, ok, "data_len=%zu", data_len);

		kunit_info(test, "data_len=%zu: enc %llu MB/s, dec %llu MB/s",
			   data_len,
			   div64_u64((u64)data_len * num_iters * 1000,
				     t_enc ?: 1),
			   div64_u64((u64)data_len * num_iters * 1000,
				     t_dec ?: 1));
	}
}

/* clang-format off */
#define AEAD_KUNIT_CASES				\
	KUNIT_CASE(test_aead_all_key_lens),		\
	KUNIT_CASE(test_aead_all_nonce_lens),		\
	KUNIT_CASE(test_aead_all_tag_lens),		\
	KUNIT_CASE(test_aead_incremental_updates),	\
	KUNIT_CASE(test_aead_data_buffer_overruns),	\
	KUNIT_CASE(test_aead_alignment_consistency),	\
	KUNIT_CASE(test_aead_inplace),			\
	KUNIT_CASE(test_aead_monte_carlo),		\
	KUNIT_CASE(test_aead_interrupt_context),	\
	KUNIT_CASE(benchmark_aead)
