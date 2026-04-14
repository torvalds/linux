// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2026 Google LLC
 */
#include <crypto/sm3.h>
#include "sm3-testvecs.h"

#define HASH sm3
#define HASH_CTX sm3_ctx
#define HASH_SIZE SM3_DIGEST_SIZE
#define HASH_INIT sm3_init
#define HASH_UPDATE sm3_update
#define HASH_FINAL sm3_final
#include "hash-test-template.h"

static struct kunit_case sm3_test_cases[] = {
	HASH_KUNIT_CASES,
	KUNIT_CASE(benchmark_hash),
	{},
};

static struct kunit_suite sm3_test_suite = {
	.name = "sm3",
	.test_cases = sm3_test_cases,
	.suite_init = hash_suite_init,
	.suite_exit = hash_suite_exit,
};
kunit_test_suite(sm3_test_suite);

MODULE_DESCRIPTION("KUnit tests and benchmark for SM3");
MODULE_LICENSE("GPL");
