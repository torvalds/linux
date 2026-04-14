// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2026 Google LLC
 */
#include <crypto/aes-cbc-macs.h>
#include "aes-cmac-testvecs.h"

/*
 * A fixed key used when presenting AES-CMAC as an unkeyed hash function in
 * order to reuse hash-test-template.h.  At the beginning of the test suite,
 * this is initialized to a key prepared from bytes generated from a fixed seed.
 */
static struct aes_cmac_key test_key;

static void aes_cmac_init_withtestkey(struct aes_cmac_ctx *ctx)
{
	aes_cmac_init(ctx, &test_key);
}

static void aes_cmac_withtestkey(const u8 *data, size_t data_len,
				 u8 out[AES_BLOCK_SIZE])
{
	aes_cmac(&test_key, data, data_len, out);
}

#define HASH aes_cmac_withtestkey
#define HASH_CTX aes_cmac_ctx
#define HASH_SIZE AES_BLOCK_SIZE
#define HASH_INIT aes_cmac_init_withtestkey
#define HASH_UPDATE aes_cmac_update
#define HASH_FINAL aes_cmac_final
#include "hash-test-template.h"

static int aes_cbc_macs_suite_init(struct kunit_suite *suite)
{
	u8 raw_key[AES_KEYSIZE_256];
	int err;

	rand_bytes_seeded_from_len(raw_key, sizeof(raw_key));
	err = aes_cmac_preparekey(&test_key, raw_key, sizeof(raw_key));
	if (err)
		return err;
	return hash_suite_init(suite);
}

static void aes_cbc_macs_suite_exit(struct kunit_suite *suite)
{
	hash_suite_exit(suite);
}

/* Verify compatibility of the AES-CMAC implementation with RFC 4493. */
static void test_aes_cmac_rfc4493(struct kunit *test)
{
	static const u8 raw_key[AES_KEYSIZE_128] = {
		0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
		0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c,
	};
	static const struct {
		size_t data_len;
		const u8 data[40];
		const u8 mac[AES_BLOCK_SIZE];
	} testvecs[] = {
		{
			/* Example 1 from RFC 4493 */
			.data_len = 0,
			.mac = {
				0xbb, 0x1d, 0x69, 0x29, 0xe9, 0x59, 0x37, 0x28,
				0x7f, 0xa3, 0x7d, 0x12, 0x9b, 0x75, 0x67, 0x46,
			},

		},
		{
			/* Example 2 from RFC 4493 */
			.data = {
				0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96,
				0xe9, 0x3d, 0x7e, 0x11, 0x73, 0x93, 0x17, 0x2a,
			},
			.data_len = 16,
			.mac = {
				0x07, 0x0a, 0x16, 0xb4, 0x6b, 0x4d, 0x41, 0x44,
				0xf7, 0x9b, 0xdd, 0x9d, 0xd0, 0x4a, 0x28, 0x7c,
			},
		},
		{
			/* Example 3 from RFC 4493 */
			.data = {
				0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96,
				0xe9, 0x3d, 0x7e, 0x11, 0x73, 0x93, 0x17, 0x2a,
				0xae, 0x2d, 0x8a, 0x57, 0x1e, 0x03, 0xac, 0x9c,
				0x9e, 0xb7, 0x6f, 0xac, 0x45, 0xaf, 0x8e, 0x51,
				0x30, 0xc8, 0x1c, 0x46, 0xa3, 0x5c, 0xe4, 0x11,
			},
			.data_len = 40,
			.mac = {
				0xdf, 0xa6, 0x67, 0x47, 0xde, 0x9a, 0xe6, 0x30,
				0x30, 0xca, 0x32, 0x61, 0x14, 0x97, 0xc8, 0x27,
			},
		},
	};
	struct aes_cmac_key key;
	int err;

	err = aes_cmac_preparekey(&key, raw_key, sizeof(raw_key));
	KUNIT_ASSERT_EQ(test, err, 0);

	for (size_t i = 0; i < ARRAY_SIZE(testvecs); i++) {
		u8 mac[AES_BLOCK_SIZE];

		aes_cmac(&key, testvecs[i].data, testvecs[i].data_len, mac);
		KUNIT_ASSERT_MEMEQ(test, mac, testvecs[i].mac, AES_BLOCK_SIZE);
	}
}

/*
 * Verify compatibility of the AES-XCBC-MAC implementation with RFC 3566.
 *
 * Additional AES-XCBC-MAC tests are not necessary, since the AES-XCBC-MAC
 * implementation is well covered by the AES-CMAC tests already.  Only the key
 * preparation function differs; the rest of the code is shared.
 */
static void test_aes_xcbcmac_rfc3566(struct kunit *test)
{
	struct aes_cmac_key key;
	/* AES-XCBC-MAC Test Case #4 from RFC 3566 */
	static const u8 raw_key[AES_KEYSIZE_128] = {
		0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
		0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
	};
	static const u8 message[20] = {
		0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09,
		0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13,
	};
	static const u8 expected_mac[AES_BLOCK_SIZE] = {
		0x47, 0xf5, 0x1b, 0x45, 0x64, 0x96, 0x62, 0x15,
		0xb8, 0x98, 0x5c, 0x63, 0x05, 0x5e, 0xd3, 0x08,
	};
	u8 actual_mac[AES_BLOCK_SIZE];

	aes_xcbcmac_preparekey(&key, raw_key);
	aes_cmac(&key, message, sizeof(message), actual_mac);
	KUNIT_ASSERT_MEMEQ(test, actual_mac, expected_mac, AES_BLOCK_SIZE);
}

static void test_aes_cbcmac_rfc3610(struct kunit *test)
{
	/*
	 * The following AES-CBC-MAC test vector is extracted from RFC 3610
	 * Packet Vector #11.  It required some rearrangement to get the actual
	 * input to AES-CBC-MAC from the values given.
	 */
	static const u8 raw_key[AES_KEYSIZE_128] = {
		0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7,
		0xc8, 0xc9, 0xca, 0xcb, 0xcc, 0xcd, 0xce, 0xcf,
	};
	const size_t unpadded_data_len = 52;
	static const u8 data[64] = {
		/* clang-format off */
		/* CCM header */
		0x61, 0x00, 0x00, 0x00, 0x0d, 0x0c, 0x0b, 0x0a,
		0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0x00, 0x14,
		/* CCM additional authentication blocks */
		0x00, 0x0c, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
		0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x00, 0x00,
		/* CCM message blocks */
		0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13,
		0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b,
		0x1c, 0x1d, 0x1e, 0x1f, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		/* clang-format on */
	};
	static const u8 expected_mac[AES_BLOCK_SIZE] = {
		0x6b, 0x5e, 0x24, 0x34, 0x12, 0xcc, 0xc2, 0xad,
		0x6f, 0x1b, 0x11, 0xc3, 0xa1, 0xa9, 0xd8, 0xbc,
	};
	struct aes_enckey key;
	struct aes_cbcmac_ctx ctx;
	u8 actual_mac[AES_BLOCK_SIZE];
	int err;

	err = aes_prepareenckey(&key, raw_key, sizeof(raw_key));
	KUNIT_ASSERT_EQ(test, err, 0);

	/*
	 * Trailing zeroes should not affect the CBC-MAC value, up to the next
	 * AES block boundary.
	 */
	for (size_t data_len = unpadded_data_len; data_len <= sizeof(data);
	     data_len++) {
		aes_cbcmac_init(&ctx, &key);
		aes_cbcmac_update(&ctx, data, data_len);
		aes_cbcmac_final(&ctx, actual_mac);
		KUNIT_ASSERT_MEMEQ(test, actual_mac, expected_mac,
				   AES_BLOCK_SIZE);

		/* Incremental computations should produce the same result. */
		for (size_t part1_len = 0; part1_len <= data_len; part1_len++) {
			aes_cbcmac_init(&ctx, &key);
			aes_cbcmac_update(&ctx, data, part1_len);
			aes_cbcmac_update(&ctx, &data[part1_len],
					  data_len - part1_len);
			aes_cbcmac_final(&ctx, actual_mac);
			KUNIT_ASSERT_MEMEQ(test, actual_mac, expected_mac,
					   AES_BLOCK_SIZE);
		}
	}
}

static struct kunit_case aes_cbc_macs_test_cases[] = {
	HASH_KUNIT_CASES,
	KUNIT_CASE(test_aes_cmac_rfc4493),
	KUNIT_CASE(test_aes_xcbcmac_rfc3566),
	KUNIT_CASE(test_aes_cbcmac_rfc3610),
	KUNIT_CASE(benchmark_hash),
	{},
};

static struct kunit_suite aes_cbc_macs_test_suite = {
	.name = "aes_cbc_macs",
	.test_cases = aes_cbc_macs_test_cases,
	.suite_init = aes_cbc_macs_suite_init,
	.suite_exit = aes_cbc_macs_suite_exit,
};
kunit_test_suite(aes_cbc_macs_test_suite);

MODULE_DESCRIPTION(
	"KUnit tests and benchmark for AES-CMAC, AES-XCBC-MAC, and AES-CBC-MAC");
MODULE_IMPORT_NS("CRYPTO_INTERNAL");
MODULE_LICENSE("GPL");
