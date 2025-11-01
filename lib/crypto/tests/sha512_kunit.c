// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2025 Google LLC
 */
#include <crypto/sha2.h>
#include "sha512-testvecs.h"

#define HASH sha512
#define HASH_CTX sha512_ctx
#define HASH_SIZE SHA512_DIGEST_SIZE
#define HASH_INIT sha512_init
#define HASH_UPDATE sha512_update
#define HASH_FINAL sha512_final
#define HMAC_KEY hmac_sha512_key
#define HMAC_CTX hmac_sha512_ctx
#define HMAC_PREPAREKEY hmac_sha512_preparekey
#define HMAC_INIT hmac_sha512_init
#define HMAC_UPDATE hmac_sha512_update
#define HMAC_FINAL hmac_sha512_final
#define HMAC hmac_sha512
#define HMAC_USINGRAWKEY hmac_sha512_usingrawkey
#include "hash-test-template.h"

static struct kunit_case hash_test_cases[] = {
	HASH_KUNIT_CASES,
	KUNIT_CASE(benchmark_hash),
	{},
};

static struct kunit_suite hash_test_suite = {
	.name = "sha512",
	.test_cases = hash_test_cases,
	.suite_init = hash_suite_init,
	.suite_exit = hash_suite_exit,
};
kunit_test_suite(hash_test_suite);

MODULE_DESCRIPTION("KUnit tests and benchmark for SHA-512 and HMAC-SHA512");
MODULE_LICENSE("GPL");
