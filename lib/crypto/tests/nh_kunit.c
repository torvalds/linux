// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2025 Google LLC
 */
#include <crypto/nh.h>
#include <kunit/test.h>
#include "nh-testvecs.h"
#include "test-utils.h"

static void test_nh(struct kunit *test)
{
	u32 *key = memdup_buf(test, nh_test_key, NH_KEY_BYTES);
	__le64 hash[NH_NUM_PASSES];

	le32_to_cpu_array(key, NH_KEY_WORDS);

	nh(key, nh_test_msg, 16, hash);
	KUNIT_ASSERT_MEMEQ(test, hash, nh_test_val16, NH_HASH_BYTES);

	nh(key, nh_test_msg, 96, hash);
	KUNIT_ASSERT_MEMEQ(test, hash, nh_test_val96, NH_HASH_BYTES);

	nh(key, nh_test_msg, 256, hash);
	KUNIT_ASSERT_MEMEQ(test, hash, nh_test_val256, NH_HASH_BYTES);

	nh(key, nh_test_msg, 1024, hash);
	KUNIT_ASSERT_MEMEQ(test, hash, nh_test_val1024, NH_HASH_BYTES);
}

static struct kunit_case nh_test_cases[] = {
	KUNIT_CASE(test_nh),
	{},
};

static struct kunit_suite nh_test_suite = {
	.name = "nh",
	.test_cases = nh_test_cases,
};
kunit_test_suite(nh_test_suite);

MODULE_DESCRIPTION("KUnit tests for NH");
MODULE_LICENSE("GPL");
