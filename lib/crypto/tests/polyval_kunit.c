// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2025 Google LLC
 */
#include <crypto/polyval.h>
#include "polyval-testvecs.h"

/*
 * A fixed key used when presenting POLYVAL as an unkeyed hash function in order
 * to reuse hash-test-template.h.  At the beginning of the test suite, this is
 * initialized to a key prepared from bytes generated from a fixed seed.
 */
static struct polyval_key test_key;

static void polyval_init_withtestkey(struct polyval_ctx *ctx)
{
	polyval_init(ctx, &test_key);
}

static void polyval_withtestkey(const u8 *data, size_t len,
				u8 out[POLYVAL_BLOCK_SIZE])
{
	polyval(&test_key, data, len, out);
}

/* Generate the HASH_KUNIT_CASES using hash-test-template.h. */
#define HASH polyval_withtestkey
#define HASH_CTX polyval_ctx
#define HASH_SIZE POLYVAL_BLOCK_SIZE
#define HASH_INIT polyval_init_withtestkey
#define HASH_UPDATE polyval_update
#define HASH_FINAL polyval_final
#include "hash-test-template.h"

/*
 * Test an example from RFC8452 ("AES-GCM-SIV: Nonce Misuse-Resistant
 * Authenticated Encryption") to ensure compatibility with that.
 */
static void test_polyval_rfc8452_testvec(struct kunit *test)
{
	static const u8 raw_key[POLYVAL_BLOCK_SIZE] =
		"\x31\x07\x28\xd9\x91\x1f\x1f\x38"
		"\x37\xb2\x43\x16\xc3\xfa\xb9\xa0";
	static const u8 data[48] =
		"\x65\x78\x61\x6d\x70\x6c\x65\x00"
		"\x00\x00\x00\x00\x00\x00\x00\x00"
		"\x48\x65\x6c\x6c\x6f\x20\x77\x6f"
		"\x72\x6c\x64\x00\x00\x00\x00\x00"
		"\x38\x00\x00\x00\x00\x00\x00\x00"
		"\x58\x00\x00\x00\x00\x00\x00\x00";
	static const u8 expected_hash[POLYVAL_BLOCK_SIZE] =
		"\xad\x7f\xcf\x0b\x51\x69\x85\x16"
		"\x62\x67\x2f\x3c\x5f\x95\x13\x8f";
	u8 hash[POLYVAL_BLOCK_SIZE];
	struct polyval_key key;

	polyval_preparekey(&key, raw_key);
	polyval(&key, data, sizeof(data), hash);
	KUNIT_ASSERT_MEMEQ(test, hash, expected_hash, sizeof(hash));
}

/*
 * Test a key and messages containing all one bits.  This is useful to detect
 * overflow bugs in implementations that emulate carryless multiplication using
 * a series of standard multiplications with the bits spread out.
 */
static void test_polyval_allones_key_and_message(struct kunit *test)
{
	struct polyval_key key;
	struct polyval_ctx hashofhashes_ctx;
	u8 hash[POLYVAL_BLOCK_SIZE];

	static_assert(TEST_BUF_LEN >= 4096);
	memset(test_buf, 0xff, 4096);

	polyval_preparekey(&key, test_buf);
	polyval_init(&hashofhashes_ctx, &key);
	for (size_t len = 0; len <= 4096; len += 16) {
		polyval(&key, test_buf, len, hash);
		polyval_update(&hashofhashes_ctx, hash, sizeof(hash));
	}
	polyval_final(&hashofhashes_ctx, hash);
	KUNIT_ASSERT_MEMEQ(test, hash, polyval_allones_hashofhashes,
			   sizeof(hash));
}

#define MAX_LEN_FOR_KEY_CHECK 1024

/*
 * Given two prepared keys which should be identical (but may differ in
 * alignment and/or whether they are followed by a guard page or not), verify
 * that they produce consistent results on various data lengths.
 */
static void check_key_consistency(struct kunit *test,
				  const struct polyval_key *key1,
				  const struct polyval_key *key2)
{
	u8 *data = test_buf;
	u8 hash1[POLYVAL_BLOCK_SIZE];
	u8 hash2[POLYVAL_BLOCK_SIZE];

	rand_bytes(data, MAX_LEN_FOR_KEY_CHECK);
	KUNIT_ASSERT_MEMEQ(test, key1, key2, sizeof(*key1));

	for (int i = 0; i < 100; i++) {
		size_t len = rand_length(MAX_LEN_FOR_KEY_CHECK);

		polyval(key1, data, len, hash1);
		polyval(key2, data, len, hash2);
		KUNIT_ASSERT_MEMEQ(test, hash1, hash2, sizeof(hash1));
	}
}

/* Test that no buffer overreads occur on either raw_key or polyval_key. */
static void test_polyval_with_guarded_key(struct kunit *test)
{
	u8 raw_key[POLYVAL_BLOCK_SIZE];
	u8 *guarded_raw_key = &test_buf[TEST_BUF_LEN - sizeof(raw_key)];
	struct polyval_key key1, key2;
	struct polyval_key *guarded_key =
		(struct polyval_key *)&test_buf[TEST_BUF_LEN - sizeof(key1)];

	/* Prepare with regular buffers. */
	rand_bytes(raw_key, sizeof(raw_key));
	polyval_preparekey(&key1, raw_key);

	/* Prepare with guarded raw_key, then check that it works. */
	memcpy(guarded_raw_key, raw_key, sizeof(raw_key));
	polyval_preparekey(&key2, guarded_raw_key);
	check_key_consistency(test, &key1, &key2);

	/* Prepare guarded polyval_key, then check that it works. */
	polyval_preparekey(guarded_key, raw_key);
	check_key_consistency(test, &key1, guarded_key);
}

/*
 * Test that polyval_key only needs to be aligned to
 * __alignof__(struct polyval_key), i.e. 8 bytes.  The assembly code may prefer
 * 16-byte or higher alignment, but it musn't require it.
 */
static void test_polyval_with_minimally_aligned_key(struct kunit *test)
{
	u8 raw_key[POLYVAL_BLOCK_SIZE];
	struct polyval_key key;
	struct polyval_key *minaligned_key =
		(struct polyval_key *)&test_buf[MAX_LEN_FOR_KEY_CHECK +
						__alignof__(struct polyval_key)];

	KUNIT_ASSERT_TRUE(test, IS_ALIGNED((uintptr_t)minaligned_key,
					   __alignof__(struct polyval_key)));
	KUNIT_ASSERT_TRUE(test,
			  !IS_ALIGNED((uintptr_t)minaligned_key,
				      2 * __alignof__(struct polyval_key)));

	rand_bytes(raw_key, sizeof(raw_key));
	polyval_preparekey(&key, raw_key);
	polyval_preparekey(minaligned_key, raw_key);
	check_key_consistency(test, &key, minaligned_key);
}

struct polyval_irq_test_state {
	struct polyval_key expected_key;
	u8 raw_key[POLYVAL_BLOCK_SIZE];
};

static bool polyval_irq_test_func(void *state_)
{
	struct polyval_irq_test_state *state = state_;
	struct polyval_key key;

	polyval_preparekey(&key, state->raw_key);
	return memcmp(&key, &state->expected_key, sizeof(key)) == 0;
}

/*
 * Test that polyval_preparekey() produces the same output regardless of whether
 * FPU or vector registers are usable when it is called.
 */
static void test_polyval_preparekey_in_irqs(struct kunit *test)
{
	struct polyval_irq_test_state state;

	rand_bytes(state.raw_key, sizeof(state.raw_key));
	polyval_preparekey(&state.expected_key, state.raw_key);
	kunit_run_irq_test(test, polyval_irq_test_func, 200000, &state);
}

static int polyval_suite_init(struct kunit_suite *suite)
{
	u8 raw_key[POLYVAL_BLOCK_SIZE];

	rand_bytes_seeded_from_len(raw_key, sizeof(raw_key));
	polyval_preparekey(&test_key, raw_key);
	return hash_suite_init(suite);
}

static void polyval_suite_exit(struct kunit_suite *suite)
{
	hash_suite_exit(suite);
}

static struct kunit_case polyval_test_cases[] = {
	HASH_KUNIT_CASES,
	KUNIT_CASE(test_polyval_rfc8452_testvec),
	KUNIT_CASE(test_polyval_allones_key_and_message),
	KUNIT_CASE(test_polyval_with_guarded_key),
	KUNIT_CASE(test_polyval_with_minimally_aligned_key),
	KUNIT_CASE(test_polyval_preparekey_in_irqs),
	KUNIT_CASE(benchmark_hash),
	{},
};

static struct kunit_suite polyval_test_suite = {
	.name = "polyval",
	.test_cases = polyval_test_cases,
	.suite_init = polyval_suite_init,
	.suite_exit = polyval_suite_exit,
};
kunit_test_suite(polyval_test_suite);

MODULE_DESCRIPTION("KUnit tests and benchmark for POLYVAL");
MODULE_LICENSE("GPL");
