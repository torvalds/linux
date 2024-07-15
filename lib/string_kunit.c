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

static struct kunit_case string_test_cases[] = {
	KUNIT_CASE(test_memset16),
	KUNIT_CASE(test_memset32),
	KUNIT_CASE(test_memset64),
	KUNIT_CASE(test_strchr),
	KUNIT_CASE(test_strnchr),
	KUNIT_CASE(test_strspn),
	{}
};

static struct kunit_suite string_test_suite = {
	.name = "string",
	.test_cases = string_test_cases,
};

kunit_test_suites(&string_test_suite);

MODULE_LICENSE("GPL v2");
