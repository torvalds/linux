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
	return 0;
}

/*
 * Poly1305 test case which uses a key and message consisting only of one bits:
 *
 * - Using an all-one-bits r_key tests the key clamping.
 * - Using an all-one-bits s_key tests carries in implementations of the
 *   addition mod 2**128 during finalization.
 * - Using all-one-bits message and r_key results in large values for the
 *   internal products of r_key by (accumulator + message block).  This
 *   increases the chance of detecting bugs that occur only in rare cases where
 *   the sum of these internal products is very large, for example the bug fixed
 *   by commit 678cce4019d7 ("crypto: x86/poly1305 - fix overflow during partial
 *   reduction").
 *
 * Bugs with the handling of large internal product sums may be specific to
 * particular update lengths (in blocks) and/or require that the accumulator
 * also have a large value.  Note that the accumulator starts at 0.  Thus, a
 * single all-one-bits test vector may be insufficient.
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
	const size_t max_len = 4096;
	u8 *data = alloc_buf(test, max_len);
	struct poly1305_desc_ctx mac_ctx, macofmacs_ctx;
	u8 mac[POLY1305_DIGEST_SIZE];

	memset(data, 0xff, max_len);

	poly1305_init(&mac_ctx, data);
	poly1305_init(&macofmacs_ctx, data);
	for (int i = 0; i < 32; i++) {
		for (size_t len = 0; len <= max_len; len += 16) {
			struct poly1305_desc_ctx tmp_ctx;

			poly1305_update(&mac_ctx, data, len);
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

/*
 * Test that Poly1305 MACs are computed correctly when r_key=1, s_key=0, and the
 * message consists of all-ones blocks.
 *
 * With r_key=1 and s_key=0, Poly1305 degrades to the sum of the padded blocks
 * mod 2**130 - 5, then reduced mod 2**128.  A padded all-ones block maps to
 * 2**128 + (2**128 - 1) = 2**129 - 1.  Two such blocks sum to 2**130 - 2, which
 * is congruent to 3 mod 2**130 - 5.  Thus, the expected MAC is just 3 *
 * floor(nblocks / 2), minus 1 if nblocks is odd, then reduced mod 2**128.  If
 * nblocks == 1 this is 2**128 - 1, otherwise it's just a small integer.
 *
 * Inside the Poly1305 implementation in this case, the multiplication does
 * nothing and each block just adds 2**129 - 1 to the accumulator.  Without the
 * multiplication to mix things up, this results in some interesting values
 * being reached where the limbs are at or near their maximum values, even after
 * (lazy) reduction.  This can reproduce bugs that happen only in such cases.
 *
 * For example, this test case reproduces the bug fixed by commit 24f07e8c07a0
 * ("lib/crypto: arm64: Fix lost Poly1305 carry when resuming NEON state").
 *
 * However, reaching that bug required two consecutive updates: one with >= 8
 * blocks to cause NEON code to be used and leave the accumulator in base 2**26
 * with its limbs in a certain pattern, then one with an odd number of blocks >=
 * 9 to cause NEON to be used again, but first processing one block using scalar
 * code as a special case, triggering a conversion to base 2**64 where the carry
 * into the 2**128 bit was lost.  To cover this and other similar edge cases
 * that may exist in other Poly1305 implementations, just test all possible
 * block-aligned splits between three poly1305_update() calls.
 */
static void test_poly1305_split_update_carry(struct kunit *test)
{
	static const u8 key[POLY1305_KEY_SIZE] = { 1 }; /* r_key=1, s_key=0 */
	/*
	 * Use max_nblocks=66 so that it's a bit more than twice the AVX-512
	 * threshold of 32 blocks.
	 */
	const int max_nblocks = 66;
	u8 *data = alloc_buf(test, max_nblocks * POLY1305_BLOCK_SIZE);
	u8 expected_mac[POLY1305_DIGEST_SIZE];
	u8 actual_mac[POLY1305_DIGEST_SIZE];
	struct poly1305_desc_ctx ctx;

	KUNIT_ASSERT_LE(test, 3 * (max_nblocks / 2), U8_MAX);

	memset(data, 0xff, max_nblocks * POLY1305_BLOCK_SIZE);

	for (int nblocks = 0; nblocks <= max_nblocks; nblocks++) {
		size_t len = nblocks * POLY1305_BLOCK_SIZE;

		expected_mac[0] = 3 * (nblocks / 2) - (nblocks % 2);
		memset(&expected_mac[1], nblocks == 1 ? 0xff : 0,
		       POLY1305_DIGEST_SIZE - 1);
		for (size_t part1_len = 0; part1_len <= len;
		     part1_len += POLY1305_BLOCK_SIZE) {
			for (size_t part2_len = 0; part2_len <= len - part1_len;
			     part2_len += POLY1305_BLOCK_SIZE) {
				size_t part3_len = len - part1_len - part2_len;

				poly1305_init(&ctx, key);
				poly1305_update(&ctx, data, part1_len);
				poly1305_update(&ctx, &data[part1_len],
						part2_len);
				poly1305_update(&ctx,
						&data[part1_len + part2_len],
						part3_len);
				poly1305_final(&ctx, actual_mac);
				KUNIT_ASSERT_MEMEQ_MSG(
					test, actual_mac, expected_mac,
					POLY1305_DIGEST_SIZE,
					"Failed with nblocks=%d, part1_len=%zu, part2_len=%zu, part3_len=%zu",
					nblocks, part1_len, part2_len,
					part3_len);
			}
		}
	}
}

static struct kunit_case poly1305_test_cases[] = {
	HASH_KUNIT_CASES,
	KUNIT_CASE(test_poly1305_allones_keys_and_message),
	KUNIT_CASE(test_poly1305_reduction_edge_cases),
	KUNIT_CASE(test_poly1305_split_update_carry),
	KUNIT_CASE(benchmark_hash),
	{},
};

static struct kunit_suite poly1305_test_suite = {
	.name = "poly1305",
	.test_cases = poly1305_test_cases,
	.suite_init = poly1305_suite_init,
};
kunit_test_suite(poly1305_test_suite);

MODULE_DESCRIPTION("KUnit tests and benchmark for Poly1305");
MODULE_LICENSE("GPL");
