// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * RSA key extract helper KFuzzTest targets
 *
 * Copyright 2025 Google LLC
 */
#include <linux/kfuzztest.h>
#include <crypto/internal/rsa.h>

struct rsa_parse_pub_key_arg {
	const void *key;
	size_t key_len;
};

FUZZ_TEST(test_rsa_parse_pub_key, struct rsa_parse_pub_key_arg)
{
	KFUZZTEST_EXPECT_NOT_NULL(rsa_parse_pub_key_arg, key);
	KFUZZTEST_EXPECT_LE(rsa_parse_pub_key_arg, key_len, 16 * PAGE_SIZE);

	struct rsa_key out;
	rsa_parse_pub_key(&out, arg->key, arg->key_len);
}

struct rsa_parse_priv_key_arg {
	const void *key;
	size_t key_len;
};

FUZZ_TEST(test_rsa_parse_priv_key, struct rsa_parse_priv_key_arg)
{
	KFUZZTEST_EXPECT_NOT_NULL(rsa_parse_priv_key_arg, key);
	KFUZZTEST_EXPECT_LE(rsa_parse_priv_key_arg, key_len, 16 * PAGE_SIZE);

	struct rsa_key out;
	rsa_parse_priv_key(&out, arg->key, arg->key_len);
}
