// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2025 Google LLC
 */
#include <crypto/sha2.h>
#include "sha384-testvecs.h"

#define HASH sha384
#define HASH_CTX sha384_ctx
#define HASH_SIZE SHA384_DIGEST_SIZE
#define HASH_INIT sha384_init
#define HASH_UPDATE sha384_update
#define HASH_FINAL sha384_final
#define HMAC_KEY hmac_sha384_key
#define HMAC_CTX hmac_sha384_ctx
#define HMAC_PREPAREKEY hmac_sha384_preparekey
#define HMAC_INIT hmac_sha384_init
#define HMAC_UPDATE hmac_sha384_update
#define HMAC_FINAL hmac_sha384_final
#define HMAC hmac_sha384
#define HMAC_USINGRAWKEY hmac_sha384_usingrawkey
#include "hash-test-template.h"

static struct kunit_case hash_test_cases[] = {
	HASH_KUNIT_CASES,
	KUNIT_CASE(benchmark_hash),
	{},
};

static struct kunit_suite hash_test_suite = {
	.name = "sha384",
	.test_cases = hash_test_cases,
	.suite_init = hash_suite_init,
	.suite_exit = hash_suite_exit,
};
kunit_test_suite(hash_test_suite);

MODULE_DESCRIPTION("KUnit tests and benchmark for SHA-384 and HMAC-SHA384");
MODULE_LICENSE("GPL");
