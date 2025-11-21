// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2025 Google LLC
 */
#include <crypto/md5.h>
#include "md5-testvecs.h"

#define HASH md5
#define HASH_CTX md5_ctx
#define HASH_SIZE MD5_DIGEST_SIZE
#define HASH_INIT md5_init
#define HASH_UPDATE md5_update
#define HASH_FINAL md5_final
#define HMAC_KEY hmac_md5_key
#define HMAC_CTX hmac_md5_ctx
#define HMAC_PREPAREKEY hmac_md5_preparekey
#define HMAC_INIT hmac_md5_init
#define HMAC_UPDATE hmac_md5_update
#define HMAC_FINAL hmac_md5_final
#define HMAC hmac_md5
#define HMAC_USINGRAWKEY hmac_md5_usingrawkey
#include "hash-test-template.h"

static struct kunit_case hash_test_cases[] = {
	HASH_KUNIT_CASES,
	KUNIT_CASE(benchmark_hash),
	{},
};

static struct kunit_suite hash_test_suite = {
	.name = "md5",
	.test_cases = hash_test_cases,
	.suite_init = hash_suite_init,
	.suite_exit = hash_suite_exit,
};
kunit_test_suite(hash_test_suite);

MODULE_DESCRIPTION("KUnit tests and benchmark for MD5 and HMAC-MD5");
MODULE_LICENSE("GPL");
