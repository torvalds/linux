// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2025 Google LLC
 */
#include <crypto/poly1305.h>
#include "poly1305-testvecs.h"

/*
 * A fixed key used when presenting Poly1305 as an unkeyed hash function in
 * order to reuse hash-test-template.h.  At the beginning of the test suite,
 * this is initialized to bytes generated from a fixed seed.
 */
static u8 test_key[POLY1305_KEY_SIZE];

/* This probably should be in the actual API, but just define it here for now */
static void poly1305(const u8 key[POLY1305_KEY_SIZE], const u8 *data,
		     size_t len, u8 out[POLY1305_DIGEST_SIZE])
{
	struct poly1305_desc_ctx ctx;

	poly1305_init(&ctx, key);
	poly1305_update(&ctx, data, len);
	poly1305_final(&ctx, out);
}

static void poly1305_init_withtestkey(struct poly1305_desc_ctx *ctx)
{
	poly1305_init(ctx, test_key);
}

static void poly1305_withtestkey(const u8 *data, size_t len,
				 u8 out[POLY1305_DIGEST_SIZE])
{
	poly1305(test_key, data, len, out);
}

/* Generate the HASH_KUNIT_CASES using hash-test-template.h. */
#define HASH poly1305_withtestkey
#define HASH_CTX poly1305_desc_ctx
#define HASH_SIZE POLY1305_DIGEST_SIZE
#define HASH_INIT poly1305_init_withtestkey
#define HASH_UPDATE poly1305_update
#define HASH_FINAL poly1305_final
#include "hash-test-template.h"

static int poly1305_suite_init(struct kunit_suite *suite)
{
	rand_bytes_seeded_from_len(test_key, POLY1305_KEY_SIZE);
	return hash_suite_init(suite);
}

static void poly1305_suite_exit(struct kunit_suite *suite)
{
	hash_suite_exit(suite);
}

/*
 * Poly1305 test case which uses a key and message consisting only of one bits:
 *
 * - Using an all-one-bits r_key tests the key clamping.
 * - Using an all-one-bits s_key tests carries in implementations of the
 *   addition mod 2**128 during finalization.
 * - Using all-one-bits message, and to a lesser extent r_key, tends to maximize
 *   any intermediate accumulator values.  This increases the chance of
 *   detecting bugs that occur only in rare cases where the accumulator values
 *   get very large, for example the bug fixed by commit 678cce4019d746da
 *   ("crypto: x86/poly1305 - fix overflow during partial reduction").
 *
 * Accumulator overflow bugs may be specific to particular update lengths (in
 * blocks) and/or particular values of the previous acculumator.  Note that the
 * accumulator starts at 0 which gives the lowest chance of an overflow.  Thus,
 * a single all-one-bits test vector may be insufficient.
 *
 * Considering that, do the following test: continuously update a single
 * Poly1305 context with all-one-bits data of varying lengths (0, 16, 32, ...,
 * 4096 bytes).  After each update, generate the MAC from the current context,
 * and feed that MAC into a separate Poly1305 context.  Repeat that entire
 * sequence of updates 32 times without re-initializing either context,
 * resulting in a total of 8224 MAC computations from a long-running, cumulative
 * context.  Finally, generate and verify the MAC of all the MACs.
 */
static void test_poly1305_allones_keys_and_message(struct kunit *test)
{
	struct poly1305_desc_ctx mac_ctx, macofmacs_ctx;
	u8 mac[POLY1305_DIGEST_SIZE];

	static_assert(TEST_BUF_LEN >= 4096);
	memset(test_buf, 0xff, 4096);

	poly1305_init(&mac_ctx, test_buf);
	poly1305_init(&macofmacs_ctx, test_buf);
	for (int i = 0; i < 32; i++) {
		for (size_t len = 0; len <= 4096; len += 16) {
			struct poly1305_desc_ctx tmp_ctx;

			poly1305_update(&mac_ctx, test_buf, len);
			tmp_ctx = mac_ctx;
			poly1305_final(&tmp_ctx, mac);
			poly1305_update(&macofmacs_ctx, mac,
					POLY1305_DIGEST_SIZE);
		}
	}
	poly1305_final(&macofmacs_ctx, mac);
	KUNIT_ASSERT_MEMEQ(test, mac, poly1305_allones_macofmacs,
			   POLY1305_DIGEST_SIZE);
}

/*
 * Poly1305 test case which uses r_key=1, s_key=0, and a 48-byte message
 * consisting of three blocks with integer values [2**128 - i, 0, 0].  In this
 * case, the result of the polynomial evaluation is 2**130 - i.  For small
 * values of i, this is very close to the modulus 2**130 - 5, which helps catch
 * edge case bugs in the modular reduction logic.
 */
static void test_poly1305_reduction_edge_cases(struct kunit *test)
{
	static const u8 key[POLY1305_KEY_SIZE] = { 1 }; /* r_key=1, s_key=0 */
	u8 data[3 * POLY1305_BLOCK_SIZE] = {};
	u8 expected_mac[POLY1305_DIGEST_SIZE];
	u8 actual_mac[POLY1305_DIGEST_SIZE];

	for (int i = 1; i <= 10; i++) {
		/* Set the first data block to 2**128 - i. */
		data[0] = -i;
		memset(&data[1], 0xff, POLY1305_BLOCK_SIZE - 1);

		/*
		 * Assuming s_key=0, the expected MAC as an integer is
		 * (2**130 - i mod 2**130 - 5) + 0 mod 2**128.  If 1 <= i <= 5,
		 * that's 5 - i.  If 6 <= i <= 10, that's 2**128 - i.
		 */
		if (i <= 5) {
			expected_mac[0] = 5 - i;
			memset(&expected_mac[1], 0, POLY1305_DIGEST_SIZE - 1);
		} else {
			expected_mac[0] = -i;
			memset(&expected_mac[1], 0xff,
			       POLY1305_DIGEST_SIZE - 1);
		}

		/* Compute and verify the MAC. */
		poly1305(key, data, sizeof(data), actual_mac);
		KUNIT_ASSERT_MEMEQ(test, actual_mac, expected_mac,
				   POLY1305_DIGEST_SIZE);
	}
}

static struct kunit_case poly1305_test_cases[] = {
	HASH_KUNIT_CASES,
	KUNIT_CASE(test_poly1305_allones_keys_and_message),
	KUNIT_CASE(test_poly1305_reduction_edge_cases),
	KUNIT_CASE(benchmark_hash),
	{},
};

static struct kunit_suite poly1305_test_suite = {
	.name = "poly1305",
	.test_cases = poly1305_test_cases,
	.suite_init = poly1305_suite_init,
	.suite_exit = poly1305_suite_exit,
};
kunit_test_suite(poly1305_test_suite);

MODULE_DESCRIPTION("KUnit tests and benchmark for Poly1305");
MODULE_LICENSE("GPL");
