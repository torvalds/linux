// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2025 Google LLC
 */
#include <crypto/sha2.h>
#include "sha224-testvecs.h"

/* TODO: add sha224() to the library itself */
static inline void sha224(const u8 *data, size_t len,
			  u8 out[SHA224_DIGEST_SIZE])
{
	struct sha256_state state;

	sha224_init(&state);
	sha256_update(&state, data, len);
	sha224_final(&state, out);
}

#define HASH sha224
#define HASH_CTX sha256_state
#define HASH_SIZE SHA224_DIGEST_SIZE
#define HASH_INIT sha224_init
#define HASH_UPDATE sha256_update
#define HASH_FINAL sha224_final
#define HASH_TESTVECS sha224_testvecs
/* TODO: add HMAC-SHA224 support to the library, then enable the tests for it */
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
	.name = "sha224",
	.test_cases = hash_test_cases,
	.suite_init = hash_suite_init,
	.suite_exit = hash_suite_exit,
};
kunit_test_suite(hash_test_suite);

MODULE_DESCRIPTION("KUnit tests and benchmark for SHA-224");
MODULE_LICENSE("GPL");
