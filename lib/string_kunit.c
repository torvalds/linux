// SPDX-License-Identifier: GPL-2.0-only
/*
 * Test cases for string functions.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <kunit/test.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/string.h>

#define STRCMP_LARGE_BUF_LEN 2048
#define STRCMP_CHANGE_POINT 1337
#define STRCMP_TEST_EXPECT_EQUAL(test, fn, ...) KUNIT_EXPECT_EQ(test, fn(__VA_ARGS__), 0)
#define STRCMP_TEST_EXPECT_LOWER(test, fn, ...) KUNIT_EXPECT_LT(test, fn(__VA_ARGS__), 0)
#define STRCMP_TEST_EXPECT_GREATER(test, fn, ...) KUNIT_EXPECT_GT(test, fn(__VA_ARGS__), 0)

static void test_memset16(struct kunit *test)
{
	unsigned i, j, k;
	u16 v, *p;

	p = kunit_kzalloc(test, 256 * 2 * 2, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, p);

	for (i = 0; i < 256; i++) {
		for (j = 0; j < 256; j++) {
			memset(p, 0xa1, 256 * 2 * sizeof(v));
			memset16(p + i, 0xb1b2, j);
			for (k = 0; k < 512; k++) {
				v = p[k];
				if (k < i) {
					KUNIT_ASSERT_EQ_MSG(test, v, 0xa1a1,
						"i:%d j:%d k:%d", i, j, k);
				} else if (k < i + j) {
					KUNIT_ASSERT_EQ_MSG(test, v, 0xb1b2,
						"i:%d j:%d k:%d", i, j, k);
				} else {
					KUNIT_ASSERT_EQ_MSG(test, v, 0xa1a1,
						"i:%d j:%d k:%d", i, j, k);
				}
			}
		}
	}
}

static void test_memset32(struct kunit *test)
{
	unsigned i, j, k;
	u32 v, *p;

	p = kunit_kzalloc(test, 256 * 2 * 4, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, p);

	for (i = 0; i < 256; i++) {
		for (j = 0; j < 256; j++) {
			memset(p, 0xa1, 256 * 2 * sizeof(v));
			memset32(p + i, 0xb1b2b3b4, j);
			for (k = 0; k < 512; k++) {
				v = p[k];
				if (k < i) {
					KUNIT_ASSERT_EQ_MSG(test, v, 0xa1a1a1a1,
						"i:%d j:%d k:%d", i, j, k);
				} else if (k < i + j) {
					KUNIT_ASSERT_EQ_MSG(test, v, 0xb1b2b3b4,
						"i:%d j:%d k:%d", i, j, k);
				} else {
					KUNIT_ASSERT_EQ_MSG(test, v, 0xa1a1a1a1,
						"i:%d j:%d k:%d", i, j, k);
				}
			}
		}
	}
}

static void test_memset64(struct kunit *test)
{
	unsigned i, j, k;
	u64 v, *p;

	p = kunit_kzalloc(test, 256 * 2 * 8, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, p);

	for (i = 0; i < 256; i++) {
		for (j = 0; j < 256; j++) {
			memset(p, 0xa1, 256 * 2 * sizeof(v));
			memset64(p + i, 0xb1b2b3b4b5b6b7b8ULL, j);
			for (k = 0; k < 512; k++) {
				v = p[k];
				if (k < i) {
					KUNIT_ASSERT_EQ_MSG(test, v, 0xa1a1a1a1a1a1a1a1ULL,
						"i:%d j:%d k:%d", i, j, k);
				} else if (k < i + j) {
					KUNIT_ASSERT_EQ_MSG(test, v, 0xb1b2b3b4b5b6b7b8ULL,
						"i:%d j:%d k:%d", i, j, k);
				} else {
					KUNIT_ASSERT_EQ_MSG(test, v, 0xa1a1a1a1a1a1a1a1ULL,
						"i:%d j:%d k:%d", i, j, k);
				}
			}
		}
	}
}

static void test_strchr(struct kunit *test)
{
	const char *test_string = "abcdefghijkl";
	const char *empty_string = "";
	char *result;
	int i;

	for (i = 0; i < strlen(test_string) + 1; i++) {
		result = strchr(test_string, test_string[i]);
		KUNIT_ASSERT_EQ_MSG(test, result - test_string, i,
				    "char:%c", 'a' + i);
	}

	result = strchr(empty_string, '\0');
	KUNIT_ASSERT_PTR_EQ(test, result, empty_string);

	result = strchr(empty_string, 'a');
	KUNIT_ASSERT_NULL(test, result);

	result = strchr(test_string, 'z');
	KUNIT_ASSERT_NULL(test, result);
}

static void test_strnchr(struct kunit *test)
{
	const char *test_string = "abcdefghijkl";
	const char *empty_string = "";
	char *result;
	int i, j;

	for (i = 0; i < strlen(test_string) + 1; i++) {
		for (j = 0; j < strlen(test_string) + 2; j++) {
			result = strnchr(test_string, j, test_string[i]);
			if (j <= i) {
				KUNIT_ASSERT_NULL_MSG(test, result,
					"char:%c i:%d j:%d", 'a' + i, i, j);
			} else {
				KUNIT_ASSERT_EQ_MSG(test, result - test_string, i,
					"char:%c i:%d j:%d", 'a' + i, i, j);
			}
		}
	}

	result = strnchr(empty_string, 0, '\0');
	KUNIT_ASSERT_NULL(test, result);

	result = strnchr(empty_string, 1, '\0');
	KUNIT_ASSERT_PTR_EQ(test, result, empty_string);

	result = strnchr(empty_string, 1, 'a');
	KUNIT_ASSERT_NULL(test, result);

	result = strnchr(NULL, 0, '\0');
	KUNIT_ASSERT_NULL(test, result);
}

static void test_strspn(struct kunit *test)
{
	static const struct strspn_test {
		const char str[16];
		const char accept[16];
		const char reject[16];
		unsigned a;
		unsigned r;
	} tests[] = {
		{ "foobar", "", "", 0, 6 },
		{ "abba", "abc", "ABBA", 4, 4 },
		{ "abba", "a", "b", 1, 1 },
		{ "", "abc", "abc", 0, 0},
	};
	const struct strspn_test *s = tests;
	size_t i;

	for (i = 0; i < ARRAY_SIZE(tests); ++i, ++s) {
		KUNIT_ASSERT_EQ_MSG(test, s->a, strspn(s->str, s->accept),
			"i:%zu", i);
		KUNIT_ASSERT_EQ_MSG(test, s->r, strcspn(s->str, s->reject),
			"i:%zu", i);
	}
}

static char strcmp_buffer1[STRCMP_LARGE_BUF_LEN];
static char strcmp_buffer2[STRCMP_LARGE_BUF_LEN];

static void strcmp_fill_buffers(char fill1, char fill2)
{
	memset(strcmp_buffer1, fill1, STRCMP_LARGE_BUF_LEN);
	memset(strcmp_buffer2, fill2, STRCMP_LARGE_BUF_LEN);
	strcmp_buffer1[STRCMP_LARGE_BUF_LEN - 1] = 0;
	strcmp_buffer2[STRCMP_LARGE_BUF_LEN - 1] = 0;
}

static void test_strcmp(struct kunit *test)
{
	/* Equal strings */
	STRCMP_TEST_EXPECT_EQUAL(test, strcmp, "Hello, Kernel!", "Hello, Kernel!");
	/* First string is lexicographically less than the second */
	STRCMP_TEST_EXPECT_LOWER(test, strcmp, "Hello, KUnit!", "Hello, Kernel!");
	/* First string is lexicographically larger than the second */
	STRCMP_TEST_EXPECT_GREATER(test, strcmp, "Hello, Kernel!", "Hello, KUnit!");
	/* Empty string is always lexicographically less than any non-empty string */
	STRCMP_TEST_EXPECT_LOWER(test, strcmp, "", "Non-empty string");
	/* Two empty strings should be equal */
	STRCMP_TEST_EXPECT_EQUAL(test, strcmp, "", "");
	/* Compare two strings which have only one char difference */
	STRCMP_TEST_EXPECT_LOWER(test, strcmp, "Abacaba", "Abadaba");
	/* Compare two strings which have the same prefix*/
	STRCMP_TEST_EXPECT_LOWER(test, strcmp, "Just a string", "Just a string and something else");
}

static void test_strcmp_long_strings(struct kunit *test)
{
	strcmp_fill_buffers('B', 'B');
	STRCMP_TEST_EXPECT_EQUAL(test, strcmp, strcmp_buffer1, strcmp_buffer2);

	strcmp_buffer1[STRCMP_CHANGE_POINT] = 'A';
	STRCMP_TEST_EXPECT_LOWER(test, strcmp, strcmp_buffer1, strcmp_buffer2);

	strcmp_buffer1[STRCMP_CHANGE_POINT] = 'C';
	STRCMP_TEST_EXPECT_GREATER(test, strcmp, strcmp_buffer1, strcmp_buffer2);
}

static void test_strncmp(struct kunit *test)
{
	/* Equal strings */
	STRCMP_TEST_EXPECT_EQUAL(test, strncmp, "Hello, KUnit!", "Hello, KUnit!", 13);
	/* First string is lexicographically less than the second */
	STRCMP_TEST_EXPECT_LOWER(test, strncmp, "Hello, KUnit!", "Hello, Kernel!", 13);
	/* Result is always 'equal' when count = 0 */
	STRCMP_TEST_EXPECT_EQUAL(test, strncmp, "Hello, Kernel!", "Hello, KUnit!", 0);
	/* Strings with common prefix are equal if count = length of prefix */
	STRCMP_TEST_EXPECT_EQUAL(test, strncmp, "Abacaba", "Abadaba", 3);
	/* Strings with common prefix are not equal when count = length of prefix + 1 */
	STRCMP_TEST_EXPECT_LOWER(test, strncmp, "Abacaba", "Abadaba", 4);
	/* If one string is a prefix of another, the shorter string is lexicographically smaller */
	STRCMP_TEST_EXPECT_LOWER(test, strncmp, "Just a string", "Just a string and something else",
				 strlen("Just a string and something else"));
	/*
	 * If one string is a prefix of another, and we check first length
	 * of prefix chars, the result is 'equal'
	 */
	STRCMP_TEST_EXPECT_EQUAL(test, strncmp, "Just a string", "Just a string and something else",
				 strlen("Just a string"));
}

static void test_strncmp_long_strings(struct kunit *test)
{
	strcmp_fill_buffers('B', 'B');
	STRCMP_TEST_EXPECT_EQUAL(test, strncmp, strcmp_buffer1,
				 strcmp_buffer2, STRCMP_LARGE_BUF_LEN);

	strcmp_buffer1[STRCMP_CHANGE_POINT] = 'A';
	STRCMP_TEST_EXPECT_LOWER(test, strncmp, strcmp_buffer1,
				 strcmp_buffer2, STRCMP_LARGE_BUF_LEN);

	strcmp_buffer1[STRCMP_CHANGE_POINT] = 'C';
	STRCMP_TEST_EXPECT_GREATER(test, strncmp, strcmp_buffer1,
				   strcmp_buffer2, STRCMP_LARGE_BUF_LEN);
	/* the strings are equal up to STRCMP_CHANGE_POINT */
	STRCMP_TEST_EXPECT_EQUAL(test, strncmp, strcmp_buffer1,
				 strcmp_buffer2, STRCMP_CHANGE_POINT);
	STRCMP_TEST_EXPECT_GREATER(test, strncmp, strcmp_buffer1,
				   strcmp_buffer2, STRCMP_CHANGE_POINT + 1);
}

static void test_strcasecmp(struct kunit *test)
{
	/* Same strings in different case should be equal */
	STRCMP_TEST_EXPECT_EQUAL(test, strcasecmp, "Hello, Kernel!", "HeLLO, KErNeL!");
	/* Empty strings should be equal */
	STRCMP_TEST_EXPECT_EQUAL(test, strcasecmp, "", "");
	/* Despite ascii code for 'a' is larger than ascii code for 'B', 'a' < 'B' */
	STRCMP_TEST_EXPECT_LOWER(test, strcasecmp, "a", "B");
	STRCMP_TEST_EXPECT_GREATER(test, strcasecmp, "B", "a");
	/* Special symbols and numbers should be processed correctly */
	STRCMP_TEST_EXPECT_EQUAL(test, strcasecmp, "-+**.1230ghTTT~^", "-+**.1230Ghttt~^");
}

static void test_strcasecmp_long_strings(struct kunit *test)
{
	strcmp_fill_buffers('b', 'B');
	STRCMP_TEST_EXPECT_EQUAL(test, strcasecmp, strcmp_buffer1, strcmp_buffer2);

	strcmp_buffer1[STRCMP_CHANGE_POINT] = 'a';
	STRCMP_TEST_EXPECT_LOWER(test, strcasecmp, strcmp_buffer1, strcmp_buffer2);

	strcmp_buffer1[STRCMP_CHANGE_POINT] = 'C';
	STRCMP_TEST_EXPECT_GREATER(test, strcasecmp, strcmp_buffer1, strcmp_buffer2);
}

static void test_strncasecmp(struct kunit *test)
{
	/* Same strings in different case should be equal */
	STRCMP_TEST_EXPECT_EQUAL(test, strncasecmp, "AbAcAbA", "Abacaba", strlen("Abacaba"));
	/* strncasecmp should check 'count' chars only */
	STRCMP_TEST_EXPECT_EQUAL(test, strncasecmp, "AbaCaBa", "abaCaDa", 5);
	STRCMP_TEST_EXPECT_LOWER(test, strncasecmp, "a", "B", 1);
	STRCMP_TEST_EXPECT_GREATER(test, strncasecmp, "B", "a", 1);
	/* Result is always 'equal' when count = 0 */
	STRCMP_TEST_EXPECT_EQUAL(test, strncasecmp, "Abacaba", "Not abacaba", 0);
}

static void test_strncasecmp_long_strings(struct kunit *test)
{
	strcmp_fill_buffers('b', 'B');
	STRCMP_TEST_EXPECT_EQUAL(test, strncasecmp, strcmp_buffer1,
				 strcmp_buffer2, STRCMP_LARGE_BUF_LEN);

	strcmp_buffer1[STRCMP_CHANGE_POINT] = 'a';
	STRCMP_TEST_EXPECT_LOWER(test, strncasecmp, strcmp_buffer1,
				 strcmp_buffer2, STRCMP_LARGE_BUF_LEN);

	strcmp_buffer1[STRCMP_CHANGE_POINT] = 'C';
	STRCMP_TEST_EXPECT_GREATER(test, strncasecmp, strcmp_buffer1,
				   strcmp_buffer2, STRCMP_LARGE_BUF_LEN);

	STRCMP_TEST_EXPECT_EQUAL(test, strncasecmp, strcmp_buffer1,
				 strcmp_buffer2, STRCMP_CHANGE_POINT);
	STRCMP_TEST_EXPECT_GREATER(test, strncasecmp, strcmp_buffer1,
				   strcmp_buffer2, STRCMP_CHANGE_POINT + 1);
}

static struct kunit_case string_test_cases[] = {
	KUNIT_CASE(test_memset16),
	KUNIT_CASE(test_memset32),
	KUNIT_CASE(test_memset64),
	KUNIT_CASE(test_strchr),
	KUNIT_CASE(test_strnchr),
	KUNIT_CASE(test_strspn),
	KUNIT_CASE(test_strcmp),
	KUNIT_CASE(test_strcmp_long_strings),
	KUNIT_CASE(test_strncmp),
	KUNIT_CASE(test_strncmp_long_strings),
	KUNIT_CASE(test_strcasecmp),
	KUNIT_CASE(test_strcasecmp_long_strings),
	KUNIT_CASE(test_strncasecmp),
	KUNIT_CASE(test_strncasecmp_long_strings),
	{}
};

static struct kunit_suite string_test_suite = {
	.name = "string",
	.test_cases = string_test_cases,
};

kunit_test_suites(&string_test_suite);

MODULE_LICENSE("GPL v2");
