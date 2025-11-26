// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2025 Google LLC
 */
#include <crypto/sha2.h>
#include "sha256-testvecs.h"

/* Generate the HASH_KUNIT_CASES using hash-test-template.h. */
#define HASH sha256
#define HASH_CTX sha256_ctx
#define HASH_SIZE SHA256_DIGEST_SIZE
#define HASH_INIT sha256_init
#define HASH_UPDATE sha256_update
#define HASH_FINAL sha256_final
#define HMAC_KEY hmac_sha256_key
#define HMAC_CTX hmac_sha256_ctx
#define HMAC_PREPAREKEY hmac_sha256_preparekey
#define HMAC_INIT hmac_sha256_init
#define HMAC_UPDATE hmac_sha256_update
#define HMAC_FINAL hmac_sha256_final
#define HMAC hmac_sha256
#define HMAC_USINGRAWKEY hmac_sha256_usingrawkey
#include "hash-test-template.h"

static void free_guarded_buf(void *buf)
{
	vfree(buf);
}

/*
 * Allocate a KUnit-managed buffer that has length @len bytes immediately
 * followed by an unmapped page, and assert that the allocation succeeds.
 */
static void *alloc_guarded_buf(struct kunit *test, size_t len)
{
	size_t full_len = round_up(len, PAGE_SIZE);
	void *buf = vmalloc(full_len);

	KUNIT_ASSERT_NOT_NULL(test, buf);
	KUNIT_ASSERT_EQ(test, 0,
			kunit_add_action_or_reset(test, free_guarded_buf, buf));
	return buf + full_len - len;
}

/*
 * Test for sha256_finup_2x().  Specifically, choose various data lengths and
 * salt lengths, and for each one, verify that sha256_finup_2x() produces the
 * same results as sha256_update() and sha256_final().
 *
 * Use guarded buffers for all inputs and outputs to reliably detect any
 * out-of-bounds reads or writes, even if they occur in assembly code.
 */
static void test_sha256_finup_2x(struct kunit *test)
{
	const size_t max_data_len = 16384;
	u8 *data1_buf, *data2_buf, *hash1, *hash2;
	u8 expected_hash1[SHA256_DIGEST_SIZE];
	u8 expected_hash2[SHA256_DIGEST_SIZE];
	u8 salt[SHA256_BLOCK_SIZE];
	struct sha256_ctx *ctx;

	data1_buf = alloc_guarded_buf(test, max_data_len);
	data2_buf = alloc_guarded_buf(test, max_data_len);
	hash1 = alloc_guarded_buf(test, SHA256_DIGEST_SIZE);
	hash2 = alloc_guarded_buf(test, SHA256_DIGEST_SIZE);
	ctx = alloc_guarded_buf(test, sizeof(*ctx));

	rand_bytes(data1_buf, max_data_len);
	rand_bytes(data2_buf, max_data_len);
	rand_bytes(salt, sizeof(salt));
	memset(ctx, 0, sizeof(*ctx));

	for (size_t i = 0; i < 500; i++) {
		size_t salt_len = rand_length(sizeof(salt));
		size_t data_len = rand_length(max_data_len);
		const u8 *data1 = data1_buf + max_data_len - data_len;
		const u8 *data2 = data2_buf + max_data_len - data_len;
		struct sha256_ctx orig_ctx;

		sha256_init(ctx);
		sha256_update(ctx, salt, salt_len);
		orig_ctx = *ctx;

		sha256_finup_2x(ctx, data1, data2, data_len, hash1, hash2);
		KUNIT_ASSERT_MEMEQ_MSG(
			test, ctx, &orig_ctx, sizeof(*ctx),
			"sha256_finup_2x() modified its ctx argument");

		sha256_update(ctx, data1, data_len);
		sha256_final(ctx, expected_hash1);
		sha256_update(&orig_ctx, data2, data_len);
		sha256_final(&orig_ctx, expected_hash2);
		KUNIT_ASSERT_MEMEQ_MSG(
			test, hash1, expected_hash1, SHA256_DIGEST_SIZE,
			"Wrong hash1 with salt_len=%zu data_len=%zu", salt_len,
			data_len);
		KUNIT_ASSERT_MEMEQ_MSG(
			test, hash2, expected_hash2, SHA256_DIGEST_SIZE,
			"Wrong hash2 with salt_len=%zu data_len=%zu", salt_len,
			data_len);
	}
}

/* Test sha256_finup_2x() with ctx == NULL */
static void test_sha256_finup_2x_defaultctx(struct kunit *test)
{
	const size_t data_len = 128;
	struct sha256_ctx ctx;
	u8 hash1_a[SHA256_DIGEST_SIZE];
	u8 hash2_a[SHA256_DIGEST_SIZE];
	u8 hash1_b[SHA256_DIGEST_SIZE];
	u8 hash2_b[SHA256_DIGEST_SIZE];

	rand_bytes(test_buf, 2 * data_len);

	sha256_init(&ctx);
	sha256_finup_2x(&ctx, test_buf, &test_buf[data_len], data_len, hash1_a,
			hash2_a);

	sha256_finup_2x(NULL, test_buf, &test_buf[data_len], data_len, hash1_b,
			hash2_b);

	KUNIT_ASSERT_MEMEQ(test, hash1_a, hash1_b, SHA256_DIGEST_SIZE);
	KUNIT_ASSERT_MEMEQ(test, hash2_a, hash2_b, SHA256_DIGEST_SIZE);
}

/*
 * Test that sha256_finup_2x() and sha256_update/final() produce consistent
 * results with total message lengths that require more than 32 bits.
 */
static void test_sha256_finup_2x_hugelen(struct kunit *test)
{
	const size_t data_len = 4 * SHA256_BLOCK_SIZE;
	struct sha256_ctx ctx = {};
	u8 expected_hash[SHA256_DIGEST_SIZE];
	u8 hash[SHA256_DIGEST_SIZE];

	rand_bytes(test_buf, data_len);
	for (size_t align = 0; align < SHA256_BLOCK_SIZE; align++) {
		sha256_init(&ctx);
		ctx.ctx.bytecount = 0x123456789abcd00 + align;

		sha256_finup_2x(&ctx, test_buf, test_buf, data_len, hash, hash);

		sha256_update(&ctx, test_buf, data_len);
		sha256_final(&ctx, expected_hash);

		KUNIT_ASSERT_MEMEQ(test, hash, expected_hash,
				   SHA256_DIGEST_SIZE);
	}
}

/* Benchmark for sha256_finup_2x() */
static void benchmark_sha256_finup_2x(struct kunit *test)
{
	/*
	 * Try a few different salt lengths, since sha256_finup_2x() performance
	 * may vary slightly for the same data_len depending on how many bytes
	 * were already processed in the initial context.
	 */
	static const size_t salt_lens_to_test[] = { 0, 32, 64 };
	const size_t data_len = 4096;
	const size_t num_iters = 4096;
	struct sha256_ctx ctx;
	u8 hash1[SHA256_DIGEST_SIZE];
	u8 hash2[SHA256_DIGEST_SIZE];

	if (!IS_ENABLED(CONFIG_CRYPTO_LIB_BENCHMARK))
		kunit_skip(test, "not enabled");
	if (!sha256_finup_2x_is_optimized())
		kunit_skip(test, "not relevant");

	rand_bytes(test_buf, data_len * 2);

	/* Warm-up */
	for (size_t i = 0; i < num_iters; i++)
		sha256_finup_2x(NULL, &test_buf[0], &test_buf[data_len],
				data_len, hash1, hash2);

	for (size_t i = 0; i < ARRAY_SIZE(salt_lens_to_test); i++) {
		size_t salt_len = salt_lens_to_test[i];
		u64 t0, t1;

		/*
		 * Prepare the initial context.  The time to process the salt is
		 * not measured; we're just interested in sha256_finup_2x().
		 */
		sha256_init(&ctx);
		sha256_update(&ctx, test_buf, salt_len);

		preempt_disable();
		t0 = ktime_get_ns();
		for (size_t j = 0; j < num_iters; j++)
			sha256_finup_2x(&ctx, &test_buf[0], &test_buf[data_len],
					data_len, hash1, hash2);
		t1 = ktime_get_ns();
		preempt_enable();
		kunit_info(test, "data_len=%zu salt_len=%zu: %llu MB/s",
			   data_len, salt_len,
			   div64_u64((u64)data_len * 2 * num_iters * 1000,
				     t1 - t0 ?: 1));
	}
}

static struct kunit_case hash_test_cases[] = {
	HASH_KUNIT_CASES,
	KUNIT_CASE(test_sha256_finup_2x),
	KUNIT_CASE(test_sha256_finup_2x_defaultctx),
	KUNIT_CASE(test_sha256_finup_2x_hugelen),
	KUNIT_CASE(benchmark_hash),
	KUNIT_CASE(benchmark_sha256_finup_2x),
	{},
};

static struct kunit_suite hash_test_suite = {
	.name = "sha256",
	.test_cases = hash_test_cases,
	.suite_init = hash_suite_init,
	.suite_exit = hash_suite_exit,
};
kunit_test_suite(hash_test_suite);

MODULE_DESCRIPTION("KUnit tests and benchmark for SHA-256 and HMAC-SHA256");
MODULE_LICENSE("GPL");
