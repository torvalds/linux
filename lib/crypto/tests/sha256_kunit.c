// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2025 Google LLC
 */
#include <crypto/sha2.h>
#include "sha256-testvecs.h"

#define HASH sha256
#define HASH_CTX sha256_ctx
#define HASH_SIZE SHA256_DIGEST_SIZE
#define HASH_INIT sha256_init
#define HASH_UPDATE sha256_update
#define HASH_FINAL sha256_final
#define HMAC_KEY hmac_sha256_key
#define HMAC_CTX hmac_sha256_ctx
#define HMAC_PREPAREKEY hmac_sha256_preparekey
#define HMAC_INIT hmac_sha256_init
#define HMAC_UPDATE hmac_sha256_update
#define HMAC_FINAL hmac_sha256_final
#define HMAC hmac_sha256
#define HMAC_USINGRAWKEY hmac_sha256_usingrawkey
#include "hash-test-template.h"

static struct kunit_case hash_test_cases[] = {
	HASH_KUNIT_CASES,
	KUNIT_CASE(benchmark_hash),
	{},
};

static struct kunit_suite hash_test_suite = {
	.name = "sha256",
	.test_cases = hash_test_cases,
	.suite_init = hash_suite_init,
	.suite_exit = hash_suite_exit,
};
kunit_test_suite(hash_test_suite);

MODULE_DESCRIPTION("KUnit tests and benchmark for SHA-256 and HMAC-SHA256");
MODULE_LICENSE("GPL");
