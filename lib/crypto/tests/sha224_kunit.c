// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2025 Google LLC
 */
#include <crypto/sha2.h>
#include "sha224-testvecs.h"

#define HASH sha224
#define HASH_CTX sha224_ctx
#define HASH_SIZE SHA224_DIGEST_SIZE
#define HASH_INIT sha224_init
#define HASH_UPDATE sha224_update
#define HASH_FINAL sha224_final
#define HMAC_KEY hmac_sha224_key
#define HMAC_CTX hmac_sha224_ctx
#define HMAC_PREPAREKEY hmac_sha224_preparekey
#define HMAC_INIT hmac_sha224_init
#define HMAC_UPDATE hmac_sha224_update
#define HMAC_FINAL hmac_sha224_final
#define HMAC hmac_sha224
#define HMAC_USINGRAWKEY hmac_sha224_usingrawkey
#include "hash-test-template.h"

static struct kunit_case hash_test_cases[] = {
	HASH_KUNIT_CASES,
	KUNIT_CASE(benchmark_hash),
	{},
};

static struct kunit_suite hash_test_suite = {
	.name = "sha224",
	.test_cases = hash_test_cases,
	.suite_init = hash_suite_init,
	.suite_exit = hash_suite_exit,
};
kunit_test_suite(hash_test_suite);

MODULE_DESCRIPTION("KUnit tests and benchmark for SHA-224 and HMAC-SHA224");
MODULE_LICENSE("GPL");
