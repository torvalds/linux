// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2026 Google LLC
 */
#include <crypto/gf128hash.h>
#include "ghash-testvecs.h"

/*
 * A fixed key used when presenting GHASH as an unkeyed hash function in order
 * to reuse hash-test-template.h.  At the beginning of the test suite, this is
 * initialized to a key prepared from bytes generated from a fixed seed.
 */
static struct ghash_key test_key;

static void ghash_init_withtestkey(struct ghash_ctx *ctx)
{
	ghash_init(ctx, &test_key);
}

static void ghash_withtestkey(const u8 *data, size_t len,
			      u8 out[GHASH_BLOCK_SIZE])
{
	ghash(&test_key, data, len, out);
}

/* Generate the HASH_KUNIT_CASES using hash-test-template.h. */
#define HASH ghash_withtestkey
#define HASH_CTX ghash_ctx
#define HASH_SIZE GHASH_BLOCK_SIZE
#define HASH_INIT ghash_init_withtestkey
#define HASH_UPDATE ghash_update
#define HASH_FINAL ghash_final
#include "hash-test-template.h"

/*
 * Test a key and messages containing all one bits.  This is useful to detect
 * overflow bugs in implementations that emulate carryless multiplication using
 * a series of standard multiplications with the bits spread out.
 */
static void test_ghash_allones_key_and_message(struct kunit *test)
{
	struct ghash_key key;
	struct ghash_ctx hashofhashes_ctx;
	u8 hash[GHASH_BLOCK_SIZE];

	static_assert(TEST_BUF_LEN >= 4096);
	memset(test_buf, 0xff, 4096);

	ghash_preparekey(&key, test_buf);
	ghash_init(&hashofhashes_ctx, &key);
	for (size_t len = 0; len <= 4096; len += 16) {
		ghash(&key, test_buf, len, hash);
		ghash_update(&hashofhashes_ctx, hash, sizeof(hash));
	}
	ghash_final(&hashofhashes_ctx, hash);
	KUNIT_ASSERT_MEMEQ(test, hash, ghash_allones_hashofhashes,
			   sizeof(hash));
}

#define MAX_LEN_FOR_KEY_CHECK 1024

/*
 * Given two prepared keys which should be identical (but may differ in
 * alignment and/or whether they are followed by a guard page or not), verify
 * that they produce consistent results on various data lengths.
 */
static void check_key_consistency(struct kunit *test,
				  const struct ghash_key *key1,
				  const struct ghash_key *key2)
{
	u8 *data = test_buf;
	u8 hash1[GHASH_BLOCK_SIZE];
	u8 hash2[GHASH_BLOCK_SIZE];

	rand_bytes(data, MAX_LEN_FOR_KEY_CHECK);
	KUNIT_ASSERT_MEMEQ(test, key1, key2, sizeof(*key1));

	for (int i = 0; i < 100; i++) {
		size_t len = rand_length(MAX_LEN_FOR_KEY_CHECK);

		ghash(key1, data, len, hash1);
		ghash(key2, data, len, hash2);
		KUNIT_ASSERT_MEMEQ(test, hash1, hash2, sizeof(hash1));
	}
}

/* Test that no buffer overreads occur on either raw_key or ghash_key. */
static void test_ghash_with_guarded_key(struct kunit *test)
{
	u8 raw_key[GHASH_BLOCK_SIZE];
	u8 *guarded_raw_key = &test_buf[TEST_BUF_LEN - sizeof(raw_key)];
	struct ghash_key key1, key2;
	struct ghash_key *guarded_key =
		(struct ghash_key *)&test_buf[TEST_BUF_LEN - sizeof(key1)];

	/* Prepare with regular buffers. */
	rand_bytes(raw_key, sizeof(raw_key));
	ghash_preparekey(&key1, raw_key);

	/* Prepare with guarded raw_key, then check that it works. */
	memcpy(guarded_raw_key, raw_key, sizeof(raw_key));
	ghash_preparekey(&key2, guarded_raw_key);
	check_key_consistency(test, &key1, &key2);

	/* Prepare guarded ghash_key, then check that it works. */
	ghash_preparekey(guarded_key, raw_key);
	check_key_consistency(test, &key1, guarded_key);
}

/*
 * Test that ghash_key only needs to be aligned to
 * __alignof__(struct ghash_key), i.e. 8 bytes.  The assembly code may prefer
 * 16-byte or higher alignment, but it mustn't require it.
 */
static void test_ghash_with_minimally_aligned_key(struct kunit *test)
{
	u8 raw_key[GHASH_BLOCK_SIZE];
	struct ghash_key key;
	struct ghash_key *minaligned_key =
		(struct ghash_key *)&test_buf[MAX_LEN_FOR_KEY_CHECK +
					      __alignof__(struct ghash_key)];

	KUNIT_ASSERT_TRUE(test, IS_ALIGNED((uintptr_t)minaligned_key,
					   __alignof__(struct ghash_key)));
	KUNIT_ASSERT_TRUE(test, !IS_ALIGNED((uintptr_t)minaligned_key,
					    2 * __alignof__(struct ghash_key)));

	rand_bytes(raw_key, sizeof(raw_key));
	ghash_preparekey(&key, raw_key);
	ghash_preparekey(minaligned_key, raw_key);
	check_key_consistency(test, &key, minaligned_key);
}

struct ghash_irq_test_state {
	struct ghash_key expected_key;
	u8 raw_key[GHASH_BLOCK_SIZE];
};

static bool ghash_irq_test_func(void *state_)
{
	struct ghash_irq_test_state *state = state_;
	struct ghash_key key;

	ghash_preparekey(&key, state->raw_key);
	return memcmp(&key, &state->expected_key, sizeof(key)) == 0;
}

/*
 * Test that ghash_preparekey() produces the same output regardless of whether
 * FPU or vector registers are usable when it is called.
 */
static void test_ghash_preparekey_in_irqs(struct kunit *test)
{
	struct ghash_irq_test_state state;

	rand_bytes(state.raw_key, sizeof(state.raw_key));
	ghash_preparekey(&state.expected_key, state.raw_key);
	kunit_run_irq_test(test, ghash_irq_test_func, 200000, &state);
}

static int ghash_suite_init(struct kunit_suite *suite)
{
	u8 raw_key[GHASH_BLOCK_SIZE];

	rand_bytes_seeded_from_len(raw_key, sizeof(raw_key));
	ghash_preparekey(&test_key, raw_key);
	return hash_suite_init(suite);
}

static void ghash_suite_exit(struct kunit_suite *suite)
{
	hash_suite_exit(suite);
}

static struct kunit_case ghash_test_cases[] = {
	HASH_KUNIT_CASES,
	KUNIT_CASE(test_ghash_allones_key_and_message),
	KUNIT_CASE(test_ghash_with_guarded_key),
	KUNIT_CASE(test_ghash_with_minimally_aligned_key),
	KUNIT_CASE(test_ghash_preparekey_in_irqs),
	KUNIT_CASE(benchmark_hash),
	{},
};

static struct kunit_suite ghash_test_suite = {
	.name = "ghash",
	.test_cases = ghash_test_cases,
	.suite_init = ghash_suite_init,
	.suite_exit = ghash_suite_exit,
};
kunit_test_suite(ghash_test_suite);

MODULE_DESCRIPTION("KUnit tests and benchmark for GHASH");
MODULE_LICENSE("GPL");
