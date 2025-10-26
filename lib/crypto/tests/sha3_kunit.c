// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2025 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */
#include <crypto/sha3.h>
#include "sha3-testvecs.h"

#define HASH		sha3_256
#define HASH_CTX	sha3_ctx
#define HASH_SIZE	SHA3_256_DIGEST_SIZE
#define HASH_INIT	sha3_256_init
#define HASH_UPDATE	sha3_update
#define HASH_FINAL	sha3_final
#include "hash-test-template.h"

/*
 * Sample message and the output generated for various algorithms by passing it
 * into "openssl sha3-224" etc..
 */
static const u8 test_sha3_sample[] =
	"The quick red fox jumped over the lazy brown dog!\n"
	"The quick red fox jumped over the lazy brown dog!\n"
	"The quick red fox jumped over the lazy brown dog!\n"
	"The quick red fox jumped over the lazy brown dog!\n";

static const u8 test_sha3_224[8 + SHA3_224_DIGEST_SIZE + 8] = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* Write-before guard */
	0xd6, 0xe8, 0xd8, 0x80, 0xfa, 0x42, 0x80, 0x70,
	0x7e, 0x7f, 0xd7, 0xd2, 0xd7, 0x7a, 0x35, 0x65,
	0xf0, 0x0b, 0x4f, 0x9f, 0x2a, 0x33, 0xca, 0x0a,
	0xef, 0xa6, 0x4c, 0xb8,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* Write-after guard */
};

static const u8 test_sha3_256[8 + SHA3_256_DIGEST_SIZE + 8] = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* Write-before guard */
	0xdb, 0x3b, 0xb0, 0xb8, 0x8d, 0x15, 0x78, 0xe5,
	0x78, 0x76, 0x8e, 0x39, 0x7e, 0x89, 0x86, 0xb9,
	0x14, 0x3a, 0x1e, 0xe7, 0x96, 0x7c, 0xf3, 0x25,
	0x70, 0xbd, 0xc3, 0xa9, 0xae, 0x63, 0x71, 0x1d,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* Write-after guard */
};

static const u8 test_sha3_384[8 + SHA3_384_DIGEST_SIZE + 8] = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* Write-before guard */
	0x2d, 0x4b, 0x29, 0x85, 0x19, 0x94, 0xaa, 0x31,
	0x9b, 0x04, 0x9d, 0x6e, 0x79, 0x66, 0xc7, 0x56,
	0x8a, 0x2e, 0x99, 0x84, 0x06, 0xcf, 0x10, 0x2d,
	0xec, 0xf0, 0x03, 0x04, 0x1f, 0xd5, 0x99, 0x63,
	0x2f, 0xc3, 0x2b, 0x0d, 0xd9, 0x45, 0xf7, 0xbb,
	0x0a, 0xc3, 0x46, 0xab, 0xfe, 0x4d, 0x94, 0xc2,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* Write-after guard */
};

static const u8 test_sha3_512[8 + SHA3_512_DIGEST_SIZE + 8] = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* Write-before guard */
	0xdd, 0x71, 0x3b, 0x44, 0xb6, 0x6c, 0xd7, 0x78,
	0xe7, 0x93, 0xa1, 0x4c, 0xd7, 0x24, 0x16, 0xf1,
	0xfd, 0xa2, 0x82, 0x4e, 0xed, 0x59, 0xe9, 0x83,
	0x15, 0x38, 0x89, 0x7d, 0x39, 0x17, 0x0c, 0xb2,
	0xcf, 0x12, 0x80, 0x78, 0xa1, 0x78, 0x41, 0xeb,
	0xed, 0x21, 0x4c, 0xa4, 0x4a, 0x5f, 0x30, 0x1a,
	0x70, 0x98, 0x4f, 0x14, 0xa2, 0xd1, 0x64, 0x1b,
	0xc2, 0x0a, 0xff, 0x3b, 0xe8, 0x26, 0x41, 0x8f,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* Write-after guard */
};

static const u8 test_shake128[8 + SHAKE128_DEFAULT_SIZE + 8] = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* Write-before guard */
	0x41, 0xd6, 0xb8, 0x9c, 0xf8, 0xe8, 0x54, 0xf2,
	0x5c, 0xde, 0x51, 0x12, 0xaf, 0x9e, 0x0d, 0x91,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* Write-after guard */
};

static const u8 test_shake256[8 + SHAKE256_DEFAULT_SIZE + 8] = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* Write-before guard */
	0xab, 0x06, 0xd4, 0xf9, 0x8b, 0xfd, 0xb2, 0xc4,
	0xfe, 0xf1, 0xcc, 0xe2, 0x40, 0x45, 0xdd, 0x15,
	0xcb, 0xdd, 0x02, 0x8d, 0xb7, 0x9f, 0x1e, 0x67,
	0xd6, 0x7f, 0x98, 0x5e, 0x1b, 0x19, 0xf8, 0x01,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* Write-after guard */
};

static void test_sha3_224_basic(struct kunit *test)
{
	u8 out[8 + SHA3_224_DIGEST_SIZE + 8];

	BUILD_BUG_ON(sizeof(out) != sizeof(test_sha3_224));

	memset(out, 0, sizeof(out));
	sha3_224(test_sha3_sample, sizeof(test_sha3_sample) - 1, out + 8);

	KUNIT_ASSERT_MEMEQ_MSG(test, out, test_sha3_224, sizeof(test_sha3_224),
			       "SHA3-224 gives wrong output");
}

static void test_sha3_256_basic(struct kunit *test)
{
	u8 out[8 + SHA3_256_DIGEST_SIZE + 8];

	BUILD_BUG_ON(sizeof(out) != sizeof(test_sha3_256));

	memset(out, 0, sizeof(out));
	sha3_256(test_sha3_sample, sizeof(test_sha3_sample) - 1, out + 8);

	KUNIT_ASSERT_MEMEQ_MSG(test, out, test_sha3_256, sizeof(test_sha3_256),
			       "SHA3-256 gives wrong output");
}

static void test_sha3_384_basic(struct kunit *test)
{
	u8 out[8 + SHA3_384_DIGEST_SIZE + 8];

	BUILD_BUG_ON(sizeof(out) != sizeof(test_sha3_384));

	memset(out, 0, sizeof(out));
	sha3_384(test_sha3_sample, sizeof(test_sha3_sample) - 1, out + 8);

	KUNIT_ASSERT_MEMEQ_MSG(test, out, test_sha3_384, sizeof(test_sha3_384),
			       "SHA3-384 gives wrong output");
}

static void test_sha3_512_basic(struct kunit *test)
{
	u8 out[8 + SHA3_512_DIGEST_SIZE + 8];

	BUILD_BUG_ON(sizeof(out) != sizeof(test_sha3_512));

	memset(out, 0, sizeof(out));
	sha3_512(test_sha3_sample, sizeof(test_sha3_sample) - 1, out + 8);

	KUNIT_ASSERT_MEMEQ_MSG(test, out, test_sha3_512, sizeof(test_sha3_512),
			       "SHA3-512 gives wrong output");
}

static void test_shake128_basic(struct kunit *test)
{
	u8 out[8 + SHAKE128_DEFAULT_SIZE + 8];

	BUILD_BUG_ON(sizeof(out) != sizeof(test_shake128));

	memset(out, 0, sizeof(out));
	shake128(test_sha3_sample, sizeof(test_sha3_sample) - 1,
		 out + 8, sizeof(out) - 16);

	KUNIT_ASSERT_MEMEQ_MSG(test, out, test_shake128, sizeof(test_shake128),
			       "SHAKE128 gives wrong output");
}

static void test_shake256_basic(struct kunit *test)
{
	u8 out[8 + SHAKE256_DEFAULT_SIZE + 8];

	BUILD_BUG_ON(sizeof(out) != sizeof(test_shake256));

	memset(out, 0, sizeof(out));
	shake256(test_sha3_sample, sizeof(test_sha3_sample) - 1,
		 out + 8, sizeof(out) - 16);

	KUNIT_ASSERT_MEMEQ_MSG(test, out, test_shake256, sizeof(test_shake256),
			       "SHAKE256 gives wrong output");
}

/*
 * Usable NIST tests.
 *
 * From: https://csrc.nist.gov/projects/cryptographic-standards-and-guidelines/example-values
 */
static const u8 test_nist_1600_sample[] = {
	0xa3, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3,
	0xa3, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3,
	0xa3, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3,
	0xa3, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3,
	0xa3, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3,
	0xa3, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3,
	0xa3, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3,
	0xa3, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3,
	0xa3, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3,
	0xa3, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3,
	0xa3, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3,
	0xa3, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3,
	0xa3, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3,
	0xa3, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3,
	0xa3, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3,
	0xa3, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3,
	0xa3, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3,
	0xa3, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3,
	0xa3, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3,
	0xa3, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3,
	0xa3, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3,
	0xa3, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3,
	0xa3, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3,
	0xa3, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3,
	0xa3, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3
};

static const u8 test_shake128_nist_0[] = {
	0x7f, 0x9c, 0x2b, 0xa4, 0xe8, 0x8f, 0x82, 0x7d,
	0x61, 0x60, 0x45, 0x50, 0x76, 0x05, 0x85, 0x3e
};

static const u8 test_shake128_nist_1600[] = {
	0x13, 0x1a, 0xb8, 0xd2, 0xb5, 0x94, 0x94, 0x6b,
	0x9c, 0x81, 0x33, 0x3f, 0x9b, 0xb6, 0xe0, 0xce,
};

static const u8 test_shake256_nist_0[] = {
	0x46, 0xb9, 0xdd, 0x2b, 0x0b, 0xa8, 0x8d, 0x13,
	0x23, 0x3b, 0x3f, 0xeb, 0x74, 0x3e, 0xeb, 0x24,
	0x3f, 0xcd, 0x52, 0xea, 0x62, 0xb8, 0x1b, 0x82,
	0xb5, 0x0c, 0x27, 0x64, 0x6e, 0xd5, 0x76, 0x2f
};

static const u8 test_shake256_nist_1600[] = {
	0xcd, 0x8a, 0x92, 0x0e, 0xd1, 0x41, 0xaa, 0x04,
	0x07, 0xa2, 0x2d, 0x59, 0x28, 0x86, 0x52, 0xe9,
	0xd9, 0xf1, 0xa7, 0xee, 0x0c, 0x1e, 0x7c, 0x1c,
	0xa6, 0x99, 0x42, 0x4d, 0xa8, 0x4a, 0x90, 0x4d,
};

static void test_shake128_nist(struct kunit *test)
{
	u8 out[SHAKE128_DEFAULT_SIZE];

	shake128("", 0, out, sizeof(out));
	KUNIT_ASSERT_MEMEQ_MSG(test, out, test_shake128_nist_0, sizeof(out),
			       "SHAKE128 gives wrong output for NIST.0");

	shake128(test_nist_1600_sample, sizeof(test_nist_1600_sample),
		 out, sizeof(out));
	KUNIT_ASSERT_MEMEQ_MSG(test, out, test_shake128_nist_1600, sizeof(out),
			       "SHAKE128 gives wrong output for NIST.1600");
}

static void test_shake256_nist(struct kunit *test)
{
	u8 out[SHAKE256_DEFAULT_SIZE];

	shake256("", 0, out, sizeof(out));
	KUNIT_ASSERT_MEMEQ_MSG(test, out, test_shake256_nist_0, sizeof(out),
			       "SHAKE256 gives wrong output for NIST.0");

	shake256(test_nist_1600_sample, sizeof(test_nist_1600_sample),
		 out, sizeof(out));
	KUNIT_ASSERT_MEMEQ_MSG(test, out, test_shake256_nist_1600, sizeof(out),
			       "SHAKE256 gives wrong output for NIST.1600");
}

static void shake(int alg, const u8 *in, size_t in_len, u8 *out, size_t out_len)
{
	if (alg == 0)
		shake128(in, in_len, out, out_len);
	else
		shake256(in, in_len, out, out_len);
}

static void shake_init(struct shake_ctx *ctx, int alg)
{
	if (alg == 0)
		shake128_init(ctx);
	else
		shake256_init(ctx);
}

/*
 * Test each of SHAKE128 and SHAKE256 with all input lengths 0 through 4096, for
 * both input and output.  The input and output lengths cycle through the values
 * together, so we do 4096 tests total.  To verify all the SHAKE outputs,
 * compute and verify the SHA3-256 digest of all of them concatenated together.
 */
static void test_shake_all_lens_up_to_4096(struct kunit *test)
{
	struct sha3_ctx main_ctx;
	const size_t max_len = 4096;
	u8 *const in = test_buf;
	u8 *const out = &test_buf[TEST_BUF_LEN - max_len];
	u8 main_hash[SHA3_256_DIGEST_SIZE];

	KUNIT_ASSERT_LE(test, 2 * max_len, TEST_BUF_LEN);

	rand_bytes_seeded_from_len(in, max_len);
	for (int alg = 0; alg < 2; alg++) {
		sha3_256_init(&main_ctx);
		for (size_t in_len = 0; in_len <= max_len; in_len++) {
			size_t out_len = (in_len * 293) % (max_len + 1);

			shake(alg, in, in_len, out, out_len);
			sha3_update(&main_ctx, out, out_len);
		}
		sha3_final(&main_ctx, main_hash);
		if (alg == 0)
			KUNIT_ASSERT_MEMEQ_MSG(test, main_hash,
					       shake128_testvec_consolidated,
					       sizeof(main_hash),
					       "shake128() gives wrong output");
		else
			KUNIT_ASSERT_MEMEQ_MSG(test, main_hash,
					       shake256_testvec_consolidated,
					       sizeof(main_hash),
					       "shake256() gives wrong output");
	}
}

/*
 * Test that a sequence of SHAKE squeezes gives the same output as a single
 * squeeze of the same total length.
 */
static void test_shake_multiple_squeezes(struct kunit *test)
{
	const size_t max_len = 512;
	u8 *ref_out;

	KUNIT_ASSERT_GE(test, TEST_BUF_LEN, 2 * max_len);

	ref_out = kunit_kzalloc(test, max_len, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ref_out);

	for (int i = 0; i < 2000; i++) {
		const int alg = rand32() % 2;
		const size_t in_len = rand_length(max_len);
		const size_t out_len = rand_length(max_len);
		const size_t in_offs = rand_offset(max_len - in_len);
		const size_t out_offs = rand_offset(max_len - out_len);
		u8 *const in = &test_buf[in_offs];
		u8 *const out = &test_buf[out_offs];
		struct shake_ctx ctx;
		size_t remaining_len, j, num_parts;

		rand_bytes(in, in_len);
		rand_bytes(out, out_len);

		/* Compute the output using the one-shot function. */
		shake(alg, in, in_len, ref_out, out_len);

		/* Compute the output using a random sequence of squeezes. */
		shake_init(&ctx, alg);
		shake_update(&ctx, in, in_len);
		remaining_len = out_len;
		j = 0;
		num_parts = 0;
		while (rand_bool()) {
			size_t part_len = rand_length(remaining_len);

			shake_squeeze(&ctx, &out[j], part_len);
			num_parts++;
			j += part_len;
			remaining_len -= part_len;
		}
		if (remaining_len != 0 || rand_bool()) {
			shake_squeeze(&ctx, &out[j], remaining_len);
			num_parts++;
		}

		/* Verify that the outputs are the same. */
		KUNIT_ASSERT_MEMEQ_MSG(
			test, out, ref_out, out_len,
			"Multi-squeeze test failed with in_len=%zu in_offs=%zu out_len=%zu out_offs=%zu num_parts=%zu alg=%d",
			in_len, in_offs, out_len, out_offs, num_parts, alg);
	}
}

/*
 * Test that SHAKE operations on buffers immediately followed by an unmapped
 * page work as expected.  This catches out-of-bounds memory accesses even if
 * they occur in assembly code.
 */
static void test_shake_with_guarded_bufs(struct kunit *test)
{
	const size_t max_len = 512;
	u8 *reg_buf;

	KUNIT_ASSERT_GE(test, TEST_BUF_LEN, max_len);

	reg_buf = kunit_kzalloc(test, max_len, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, reg_buf);

	for (int alg = 0; alg < 2; alg++) {
		for (size_t len = 0; len <= max_len; len++) {
			u8 *guarded_buf = &test_buf[TEST_BUF_LEN - len];

			rand_bytes(reg_buf, len);
			memcpy(guarded_buf, reg_buf, len);

			shake(alg, reg_buf, len, reg_buf, len);
			shake(alg, guarded_buf, len, guarded_buf, len);

			KUNIT_ASSERT_MEMEQ_MSG(
				test, reg_buf, guarded_buf, len,
				"Guard page test failed with len=%zu alg=%d",
				len, alg);
		}
	}
}

static struct kunit_case sha3_test_cases[] = {
	HASH_KUNIT_CASES,
	KUNIT_CASE(test_sha3_224_basic),
	KUNIT_CASE(test_sha3_256_basic),
	KUNIT_CASE(test_sha3_384_basic),
	KUNIT_CASE(test_sha3_512_basic),
	KUNIT_CASE(test_shake128_basic),
	KUNIT_CASE(test_shake256_basic),
	KUNIT_CASE(test_shake128_nist),
	KUNIT_CASE(test_shake256_nist),
	KUNIT_CASE(test_shake_all_lens_up_to_4096),
	KUNIT_CASE(test_shake_multiple_squeezes),
	KUNIT_CASE(test_shake_with_guarded_bufs),
	KUNIT_CASE(benchmark_hash),
	{},
};

static struct kunit_suite sha3_test_suite = {
	.name = "sha3",
	.test_cases = sha3_test_cases,
	.suite_init = hash_suite_init,
	.suite_exit = hash_suite_exit,
};
kunit_test_suite(sha3_test_suite);

MODULE_DESCRIPTION("KUnit tests and benchmark for SHA3");
MODULE_LICENSE("GPL");
