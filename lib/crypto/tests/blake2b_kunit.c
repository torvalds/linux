// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2025 Google LLC
 */
#include <crypto/blake2b.h>
#include "blake2b-testvecs.h"

/*
 * The following are compatibility functions that present BLAKE2b as an unkeyed
 * hash function that produces hashes of fixed length BLAKE2B_HASH_SIZE, so that
 * hash-test-template.h can be reused to test it.
 */

static void blake2b_default(const u8 *data, size_t len,
			    u8 out[BLAKE2B_HASH_SIZE])
{
	blake2b(NULL, 0, data, len, out, BLAKE2B_HASH_SIZE);
}

static void blake2b_init_default(struct blake2b_ctx *ctx)
{
	blake2b_init(ctx, BLAKE2B_HASH_SIZE);
}

/*
 * Generate the HASH_KUNIT_CASES using hash-test-template.h.  These test BLAKE2b
 * with a key length of 0 and a hash length of BLAKE2B_HASH_SIZE.
 */
#define HASH blake2b_default
#define HASH_CTX blake2b_ctx
#define HASH_SIZE BLAKE2B_HASH_SIZE
#define HASH_INIT blake2b_init_default
#define HASH_UPDATE blake2b_update
#define HASH_FINAL blake2b_final
#include "hash-test-template.h"

/*
 * BLAKE2b specific test case which tests all possible combinations of key
 * length and hash length.
 */
static void test_blake2b_all_key_and_hash_lens(struct kunit *test)
{
	const size_t data_len = 100;
	u8 *data = &test_buf[0];
	u8 *key = data + data_len;
	u8 *hash = key + BLAKE2B_KEY_SIZE;
	struct blake2b_ctx main_ctx;
	u8 main_hash[BLAKE2B_HASH_SIZE];

	rand_bytes_seeded_from_len(data, data_len);
	blake2b_init(&main_ctx, BLAKE2B_HASH_SIZE);
	for (int key_len = 0; key_len <= BLAKE2B_KEY_SIZE; key_len++) {
		rand_bytes_seeded_from_len(key, key_len);
		for (int out_len = 1; out_len <= BLAKE2B_HASH_SIZE; out_len++) {
			blake2b(key, key_len, data, data_len, hash, out_len);
			blake2b_update(&main_ctx, hash, out_len);
		}
	}
	blake2b_final(&main_ctx, main_hash);
	KUNIT_ASSERT_MEMEQ(test, main_hash, blake2b_keyed_testvec_consolidated,
			   BLAKE2B_HASH_SIZE);
}

/*
 * BLAKE2b specific test case which tests using a guarded buffer for all allowed
 * key lengths.  Also tests both blake2b() and blake2b_init_key().
 */
static void test_blake2b_with_guarded_key_buf(struct kunit *test)
{
	const size_t data_len = 100;

	rand_bytes(test_buf, data_len);
	for (int key_len = 0; key_len <= BLAKE2B_KEY_SIZE; key_len++) {
		u8 key[BLAKE2B_KEY_SIZE];
		u8 *guarded_key = &test_buf[TEST_BUF_LEN - key_len];
		u8 hash1[BLAKE2B_HASH_SIZE];
		u8 hash2[BLAKE2B_HASH_SIZE];
		struct blake2b_ctx ctx;

		rand_bytes(key, key_len);
		memcpy(guarded_key, key, key_len);

		blake2b(key, key_len, test_buf, data_len,
			hash1, BLAKE2B_HASH_SIZE);
		blake2b(guarded_key, key_len, test_buf, data_len,
			hash2, BLAKE2B_HASH_SIZE);
		KUNIT_ASSERT_MEMEQ(test, hash1, hash2, BLAKE2B_HASH_SIZE);

		blake2b_init_key(&ctx, BLAKE2B_HASH_SIZE, guarded_key, key_len);
		blake2b_update(&ctx, test_buf, data_len);
		blake2b_final(&ctx, hash2);
		KUNIT_ASSERT_MEMEQ(test, hash1, hash2, BLAKE2B_HASH_SIZE);
	}
}

/*
 * BLAKE2b specific test case which tests using a guarded output buffer for all
 * allowed output lengths.
 */
static void test_blake2b_with_guarded_out_buf(struct kunit *test)
{
	const size_t data_len = 100;

	rand_bytes(test_buf, data_len);
	for (int out_len = 1; out_len <= BLAKE2B_HASH_SIZE; out_len++) {
		u8 hash[BLAKE2B_HASH_SIZE];
		u8 *guarded_hash = &test_buf[TEST_BUF_LEN - out_len];

		blake2b(NULL, 0, test_buf, data_len, hash, out_len);
		blake2b(NULL, 0, test_buf, data_len, guarded_hash, out_len);
		KUNIT_ASSERT_MEMEQ(test, hash, guarded_hash, out_len);
	}
}

static struct kunit_case blake2b_test_cases[] = {
	HASH_KUNIT_CASES,
	KUNIT_CASE(test_blake2b_all_key_and_hash_lens),
	KUNIT_CASE(test_blake2b_with_guarded_key_buf),
	KUNIT_CASE(test_blake2b_with_guarded_out_buf),
	KUNIT_CASE(benchmark_hash),
	{},
};

static struct kunit_suite blake2b_test_suite = {
	.name = "blake2b",
	.test_cases = blake2b_test_cases,
	.suite_init = hash_suite_init,
	.suite_exit = hash_suite_exit,
};
kunit_test_suite(blake2b_test_suite);

MODULE_DESCRIPTION("KUnit tests and benchmark for BLAKE2b");
MODULE_LICENSE("GPL");
