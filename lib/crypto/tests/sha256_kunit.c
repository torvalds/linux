// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2025 Google LLC
 */
#include <crypto/sha2.h>
#include "sha256-testvecs.h"

#define HASH sha256
#define HASH_CTX sha256_state
#define HASH_SIZE SHA256_DIGEST_SIZE
#define HASH_INIT sha256_init
#define HASH_UPDATE sha256_update
#define HASH_FINAL sha256_final
#define HASH_TESTVECS sha256_testvecs
/* TODO: add HMAC-SHA256 support to the library, then enable the tests for it */
#include "hash-test-template.h"

static struct kunit_case hash_test_cases[] = {
	KUNIT_CASE(test_hash_test_vectors),
	KUNIT_CASE(test_hash_incremental_updates),
	KUNIT_CASE(test_hash_buffer_overruns),
	KUNIT_CASE(test_hash_overlaps),
	KUNIT_CASE(test_hash_alignment_consistency),
	KUNIT_CASE(test_hash_interrupt_context),
	KUNIT_CASE(test_hash_ctx_zeroization),
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

MODULE_DESCRIPTION("KUnit tests and benchmark for SHA-256");
MODULE_LICENSE("GPL");
