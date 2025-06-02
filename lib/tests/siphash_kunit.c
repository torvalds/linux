// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
/* Copyright (C) 2016-2022 Jason A. Donenfeld <Jason@zx2c4.com>. All Rights Reserved.
 *
 * Test cases for siphash.c
 *
 * SipHash: a fast short-input PRF
 * https://131002.net/siphash/
 *
 * This implementation is specifically for SipHash2-4 for a secure PRF
 * and HalfSipHash1-3/SipHash1-3 for an insecure PRF only suitable for
 * hashtables.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <kunit/test.h>
#include <linux/siphash.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/module.h>

/* Test vectors taken from reference source available at:
 *     https://github.com/veorq/SipHash
 */

static const siphash_key_t test_key_siphash =
	{{ 0x0706050403020100ULL, 0x0f0e0d0c0b0a0908ULL }};

static const u64 test_vectors_siphash[64] = {
	0x726fdb47dd0e0e31ULL, 0x74f839c593dc67fdULL, 0x0d6c8009d9a94f5aULL,
	0x85676696d7fb7e2dULL, 0xcf2794e0277187b7ULL, 0x18765564cd99a68dULL,
	0xcbc9466e58fee3ceULL, 0xab0200f58b01d137ULL, 0x93f5f5799a932462ULL,
	0x9e0082df0ba9e4b0ULL, 0x7a5dbbc594ddb9f3ULL, 0xf4b32f46226bada7ULL,
	0x751e8fbc860ee5fbULL, 0x14ea5627c0843d90ULL, 0xf723ca908e7af2eeULL,
	0xa129ca6149be45e5ULL, 0x3f2acc7f57c29bdbULL, 0x699ae9f52cbe4794ULL,
	0x4bc1b3f0968dd39cULL, 0xbb6dc91da77961bdULL, 0xbed65cf21aa2ee98ULL,
	0xd0f2cbb02e3b67c7ULL, 0x93536795e3a33e88ULL, 0xa80c038ccd5ccec8ULL,
	0xb8ad50c6f649af94ULL, 0xbce192de8a85b8eaULL, 0x17d835b85bbb15f3ULL,
	0x2f2e6163076bcfadULL, 0xde4daaaca71dc9a5ULL, 0xa6a2506687956571ULL,
	0xad87a3535c49ef28ULL, 0x32d892fad841c342ULL, 0x7127512f72f27cceULL,
	0xa7f32346f95978e3ULL, 0x12e0b01abb051238ULL, 0x15e034d40fa197aeULL,
	0x314dffbe0815a3b4ULL, 0x027990f029623981ULL, 0xcadcd4e59ef40c4dULL,
	0x9abfd8766a33735cULL, 0x0e3ea96b5304a7d0ULL, 0xad0c42d6fc585992ULL,
	0x187306c89bc215a9ULL, 0xd4a60abcf3792b95ULL, 0xf935451de4f21df2ULL,
	0xa9538f0419755787ULL, 0xdb9acddff56ca510ULL, 0xd06c98cd5c0975ebULL,
	0xe612a3cb9ecba951ULL, 0xc766e62cfcadaf96ULL, 0xee64435a9752fe72ULL,
	0xa192d576b245165aULL, 0x0a8787bf8ecb74b2ULL, 0x81b3e73d20b49b6fULL,
	0x7fa8220ba3b2eceaULL, 0x245731c13ca42499ULL, 0xb78dbfaf3a8d83bdULL,
	0xea1ad565322a1a0bULL, 0x60e61c23a3795013ULL, 0x6606d7e446282b93ULL,
	0x6ca4ecb15c5f91e1ULL, 0x9f626da15c9625f3ULL, 0xe51b38608ef25f57ULL,
	0x958a324ceb064572ULL
};

#if BITS_PER_LONG == 64
static const hsiphash_key_t test_key_hsiphash =
	{{ 0x0706050403020100ULL, 0x0f0e0d0c0b0a0908ULL }};

static const u32 test_vectors_hsiphash[64] = {
	0x050fc4dcU, 0x7d57ca93U, 0x4dc7d44dU,
	0xe7ddf7fbU, 0x88d38328U, 0x49533b67U,
	0xc59f22a7U, 0x9bb11140U, 0x8d299a8eU,
	0x6c063de4U, 0x92ff097fU, 0xf94dc352U,
	0x57b4d9a2U, 0x1229ffa7U, 0xc0f95d34U,
	0x2a519956U, 0x7d908b66U, 0x63dbd80cU,
	0xb473e63eU, 0x8d297d1cU, 0xa6cce040U,
	0x2b45f844U, 0xa320872eU, 0xdae6c123U,
	0x67349c8cU, 0x705b0979U, 0xca9913a5U,
	0x4ade3b35U, 0xef6cd00dU, 0x4ab1e1f4U,
	0x43c5e663U, 0x8c21d1bcU, 0x16a7b60dU,
	0x7a8ff9bfU, 0x1f2a753eU, 0xbf186b91U,
	0xada26206U, 0xa3c33057U, 0xae3a36a1U,
	0x7b108392U, 0x99e41531U, 0x3f1ad944U,
	0xc8138825U, 0xc28949a6U, 0xfaf8876bU,
	0x9f042196U, 0x68b1d623U, 0x8b5114fdU,
	0xdf074c46U, 0x12cc86b3U, 0x0a52098fU,
	0x9d292f9aU, 0xa2f41f12U, 0x43a71ed0U,
	0x73f0bce6U, 0x70a7e980U, 0x243c6d75U,
	0xfdb71513U, 0xa67d8a08U, 0xb7e8f148U,
	0xf7a644eeU, 0x0f1837f2U, 0x4b6694e0U,
	0xb7bbb3a8U
};
#else
static const hsiphash_key_t test_key_hsiphash =
	{{ 0x03020100U, 0x07060504U }};

static const u32 test_vectors_hsiphash[64] = {
	0x5814c896U, 0xe7e864caU, 0xbc4b0e30U,
	0x01539939U, 0x7e059ea6U, 0x88e3d89bU,
	0xa0080b65U, 0x9d38d9d6U, 0x577999b1U,
	0xc839caedU, 0xe4fa32cfU, 0x959246eeU,
	0x6b28096cU, 0x66dd9cd6U, 0x16658a7cU,
	0xd0257b04U, 0x8b31d501U, 0x2b1cd04bU,
	0x06712339U, 0x522aca67U, 0x911bb605U,
	0x90a65f0eU, 0xf826ef7bU, 0x62512debU,
	0x57150ad7U, 0x5d473507U, 0x1ec47442U,
	0xab64afd3U, 0x0a4100d0U, 0x6d2ce652U,
	0x2331b6a3U, 0x08d8791aU, 0xbc6dda8dU,
	0xe0f6c934U, 0xb0652033U, 0x9b9851ccU,
	0x7c46fb7fU, 0x732ba8cbU, 0xf142997aU,
	0xfcc9aa1bU, 0x05327eb2U, 0xe110131cU,
	0xf9e5e7c0U, 0xa7d708a6U, 0x11795ab1U,
	0x65671619U, 0x9f5fff91U, 0xd89c5267U,
	0x007783ebU, 0x95766243U, 0xab639262U,
	0x9c7e1390U, 0xc368dda6U, 0x38ddc455U,
	0xfa13d379U, 0x979ea4e8U, 0x53ecd77eU,
	0x2ee80657U, 0x33dbb66aU, 0xae3f0577U,
	0x88b4c4ccU, 0x3e7f480bU, 0x74c1ebf8U,
	0x87178304U
};
#endif

#define chk(hash, vector, fmt...)			\
	KUNIT_EXPECT_EQ_MSG(test, hash, vector, fmt)

static void siphash_test(struct kunit *test)
{
	u8 in[64] __aligned(SIPHASH_ALIGNMENT);
	u8 in_unaligned[65] __aligned(SIPHASH_ALIGNMENT);
	u8 i;

	for (i = 0; i < 64; ++i) {
		in[i] = i;
		in_unaligned[i + 1] = i;
		chk(siphash(in, i, &test_key_siphash),
		    test_vectors_siphash[i],
		    "siphash self-test aligned %u: FAIL", i + 1);
		chk(siphash(in_unaligned + 1, i, &test_key_siphash),
		    test_vectors_siphash[i],
		    "siphash self-test unaligned %u: FAIL", i + 1);
		chk(hsiphash(in, i, &test_key_hsiphash),
		    test_vectors_hsiphash[i],
		    "hsiphash self-test aligned %u: FAIL", i + 1);
		chk(hsiphash(in_unaligned + 1, i, &test_key_hsiphash),
		    test_vectors_hsiphash[i],
		    "hsiphash self-test unaligned %u: FAIL", i + 1);
	}
	chk(siphash_1u64(0x0706050403020100ULL, &test_key_siphash),
	    test_vectors_siphash[8],
	    "siphash self-test 1u64: FAIL");
	chk(siphash_2u64(0x0706050403020100ULL, 0x0f0e0d0c0b0a0908ULL,
			 &test_key_siphash),
	    test_vectors_siphash[16],
	    "siphash self-test 2u64: FAIL");
	chk(siphash_3u64(0x0706050403020100ULL, 0x0f0e0d0c0b0a0908ULL,
			 0x1716151413121110ULL, &test_key_siphash),
	    test_vectors_siphash[24],
	    "siphash self-test 3u64: FAIL");
	chk(siphash_4u64(0x0706050403020100ULL, 0x0f0e0d0c0b0a0908ULL,
			 0x1716151413121110ULL, 0x1f1e1d1c1b1a1918ULL,
			 &test_key_siphash),
	    test_vectors_siphash[32],
	    "siphash self-test 4u64: FAIL");
	chk(siphash_1u32(0x03020100U, &test_key_siphash),
	    test_vectors_siphash[4],
	    "siphash self-test 1u32: FAIL");
	chk(siphash_2u32(0x03020100U, 0x07060504U, &test_key_siphash),
	    test_vectors_siphash[8],
	    "siphash self-test 2u32: FAIL");
	chk(siphash_3u32(0x03020100U, 0x07060504U,
			 0x0b0a0908U, &test_key_siphash),
	    test_vectors_siphash[12],
	    "siphash self-test 3u32: FAIL");
	chk(siphash_4u32(0x03020100U, 0x07060504U,
			 0x0b0a0908U, 0x0f0e0d0cU, &test_key_siphash),
	    test_vectors_siphash[16],
	    "siphash self-test 4u32: FAIL");
	chk(hsiphash_1u32(0x03020100U, &test_key_hsiphash),
	    test_vectors_hsiphash[4],
	    "hsiphash self-test 1u32: FAIL");
	chk(hsiphash_2u32(0x03020100U, 0x07060504U, &test_key_hsiphash),
	    test_vectors_hsiphash[8],
	    "hsiphash self-test 2u32: FAIL");
	chk(hsiphash_3u32(0x03020100U, 0x07060504U,
			  0x0b0a0908U, &test_key_hsiphash),
	    test_vectors_hsiphash[12],
	    "hsiphash self-test 3u32: FAIL");
	chk(hsiphash_4u32(0x03020100U, 0x07060504U,
			  0x0b0a0908U, 0x0f0e0d0cU, &test_key_hsiphash),
	    test_vectors_hsiphash[16],
	    "hsiphash self-test 4u32: FAIL");
}

static struct kunit_case siphash_test_cases[] = {
	KUNIT_CASE(siphash_test),
	{}
};

static struct kunit_suite siphash_test_suite = {
	.name = "siphash",
	.test_cases = siphash_test_cases,
};

kunit_test_suite(siphash_test_suite);

MODULE_AUTHOR("Jason A. Donenfeld <Jason@zx2c4.com>");
MODULE_DESCRIPTION("Test cases for siphash.c");
MODULE_LICENSE("Dual BSD/GPL");
