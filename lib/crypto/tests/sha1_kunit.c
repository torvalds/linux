// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2025 Google LLC
 */
#include <crypto/sha1.h>
#include "sha1-testvecs.h"

#define HASH sha1
#define HASH_CTX sha1_ctx
#define HASH_SIZE SHA1_DIGEST_SIZE
#define HASH_INIT sha1_init
#define HASH_UPDATE sha1_update
#define HASH_FINAL sha1_final
#define HMAC_KEY hmac_sha1_key
#define HMAC_CTX hmac_sha1_ctx
#define HMAC_PREPAREKEY hmac_sha1_preparekey
#define HMAC_INIT hmac_sha1_init
#define HMAC_UPDATE hmac_sha1_update
#define HMAC_FINAL hmac_sha1_final
#define HMAC hmac_sha1
#define HMAC_USINGRAWKEY hmac_sha1_usingrawkey
#include "hash-test-template.h"

static struct kunit_case hash_test_cases[] = {
	HASH_KUNIT_CASES,
	KUNIT_CASE(benchmark_hash),
	{},
};

static struct kunit_suite hash_test_suite = {
	.name = "sha1",
	.test_cases = hash_test_cases,
	.suite_init = hash_suite_init,
	.suite_exit = hash_suite_exit,
};
kunit_test_suite(hash_test_suite);

MODULE_DESCRIPTION("KUnit tests and benchmark for SHA-1 and HMAC-SHA1");
MODULE_LICENSE("GPL");
