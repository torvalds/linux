// SPDX-License-Identifier: GPL-2.0
#include "tests.h"
#include "util/debug.h"
#include "util/sha1.h"

#include <linux/compiler.h>
#include <stdlib.h>
#include <string2.h>

static int test_strreplace(char needle, const char *haystack,
			   const char *replace, const char *expected)
{
	char *new = strreplace_chars(needle, haystack, replace);
	int ret = strcmp(new, expected);

	free(new);
	return ret == 0;
}

#define MAX_LEN 512

/* Test sha1() for all lengths from 0 to MAX_LEN inclusively. */
static int test_sha1(void)
{
	u8 data[MAX_LEN];
	size_t digests_size = (MAX_LEN + 1) * SHA1_DIGEST_SIZE;
	u8 *digests;
	u8 digest_of_digests[SHA1_DIGEST_SIZE];
	/*
	 * The correctness of this value was verified by running this test with
	 * sha1() replaced by OpenSSL's SHA1().
	 */
	static const u8 expected_digest_of_digests[SHA1_DIGEST_SIZE] = {
		0x74, 0xcd, 0x4c, 0xb9, 0xd8, 0xa6, 0xd5, 0x95, 0x22, 0x8b,
		0x7e, 0xd6, 0x8b, 0x7e, 0x46, 0x95, 0x31, 0x9b, 0xa2, 0x43,
	};
	size_t i;

	digests = malloc(digests_size);
	TEST_ASSERT_VAL("failed to allocate digests", digests != NULL);

	/* Generate MAX_LEN bytes of data. */
	for (i = 0; i < MAX_LEN; i++)
		data[i] = i;

	/* Calculate a SHA-1 for each length 0 through MAX_LEN inclusively. */
	for (i = 0; i <= MAX_LEN; i++)
		sha1(data, i, &digests[i * SHA1_DIGEST_SIZE]);

	/* Calculate digest of all digests calculated above. */
	sha1(digests, digests_size, digest_of_digests);

	free(digests);

	/* Check for the expected result. */
	TEST_ASSERT_VAL("wrong output from sha1()",
			memcmp(digest_of_digests, expected_digest_of_digests,
			       SHA1_DIGEST_SIZE) == 0);
	return 0;
}

static int test__util(struct test_suite *t __maybe_unused, int subtest __maybe_unused)
{
	TEST_ASSERT_VAL("empty string", test_strreplace(' ', "", "123", ""));
	TEST_ASSERT_VAL("no match", test_strreplace('5', "123", "4", "123"));
	TEST_ASSERT_VAL("replace 1", test_strreplace('3', "123", "4", "124"));
	TEST_ASSERT_VAL("replace 2", test_strreplace('a', "abcabc", "ef", "efbcefbc"));
	TEST_ASSERT_VAL("replace long", test_strreplace('a', "abcabc", "longlong",
							"longlongbclonglongbc"));

	return test_sha1();
}

DEFINE_SUITE("util", util);
