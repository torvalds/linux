// SPDX-License-Identifier: GPL-2.0+
/*
 * Kernel module for testing 'strscpy' family of functions.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <kunit/test.h>
#include <linux/string.h>

/**
 * strscpy_check() - Run a specific test case.
 * @test: KUnit test context pointer
 * @src: Source string, argument to strscpy_pad()
 * @count: Size of destination buffer, argument to strscpy_pad()
 * @expected: Expected return value from call to strscpy_pad()
 * @chars: Number of characters from the src string expected to be
 *         written to the dst buffer.
 * @terminator: 1 if there should be a terminating null byte 0 otherwise.
 * @pad: Number of pad characters expected (in the tail of dst buffer).
 *       (@pad does not include the null terminator byte.)
 *
 * Calls strscpy_pad() and verifies the return value and state of the
 * destination buffer after the call returns.
 */
static void strscpy_check(struct kunit *test, char *src, int count,
			  int expected, int chars, int terminator, int pad)
{
	int nr_bytes_poison;
	int max_expected;
	int max_count;
	int written;
	char buf[6];
	int index, i;
	const char POISON = 'z';

	KUNIT_ASSERT_TRUE_MSG(test, src != NULL,
			      "null source string not supported");

	memset(buf, POISON, sizeof(buf));
	/* Future proofing test suite, validate args */
	max_count = sizeof(buf) - 2; /* Space for null and to verify overflow */
	max_expected = count - 1;    /* Space for the null */

	KUNIT_ASSERT_LE_MSG(test, count, max_count,
		"count (%d) is too big (%d) ... aborting", count, max_count);
	KUNIT_EXPECT_LE_MSG(test, expected, max_expected,
		"expected (%d) is bigger than can possibly be returned (%d)",
		expected, max_expected);

	written = strscpy_pad(buf, src, count);
	KUNIT_ASSERT_EQ(test, written, expected);

	if (count && written == -E2BIG) {
		KUNIT_ASSERT_EQ_MSG(test, 0, strncmp(buf, src, count - 1),
			"buffer state invalid for -E2BIG");
		KUNIT_ASSERT_EQ_MSG(test, buf[count - 1], '\0',
			"too big string is not null terminated correctly");
	}

	for (i = 0; i < chars; i++)
		KUNIT_ASSERT_EQ_MSG(test, buf[i], src[i],
			"buf[i]==%c != src[i]==%c", buf[i], src[i]);

	if (terminator)
		KUNIT_ASSERT_EQ_MSG(test, buf[count - 1], '\0',
			"string is not null terminated correctly");

	for (i = 0; i < pad; i++) {
		index = chars + terminator + i;
		KUNIT_ASSERT_EQ_MSG(test, buf[index], '\0',
			"padding missing at index: %d", i);
	}

	nr_bytes_poison = sizeof(buf) - chars - terminator - pad;
	for (i = 0; i < nr_bytes_poison; i++) {
		index = sizeof(buf) - 1 - i; /* Check from the end back */
		KUNIT_ASSERT_EQ_MSG(test, buf[index], POISON,
			"poison value missing at index: %d", i);
	}
}

static void test_strscpy(struct kunit *test)
{
	char dest[8];

	/*
	 * strscpy_check() uses a destination buffer of size 6 and needs at
	 * least 2 characters spare (one for null and one to check for
	 * overflow).  This means we should only call tc() with
	 * strings up to a maximum of 4 characters long and 'count'
	 * should not exceed 4.  To test with longer strings increase
	 * the buffer size in tc().
	 */

	/* strscpy_check(test, src, count, expected, chars, terminator, pad) */
	strscpy_check(test, "a", 0, -E2BIG, 0, 0, 0);
	strscpy_check(test, "",  0, -E2BIG, 0, 0, 0);

	strscpy_check(test, "a", 1, -E2BIG, 0, 1, 0);
	strscpy_check(test, "",  1, 0,	 0, 1, 0);

	strscpy_check(test, "ab", 2, -E2BIG, 1, 1, 0);
	strscpy_check(test, "a",  2, 1,	  1, 1, 0);
	strscpy_check(test, "",   2, 0,	  0, 1, 1);

	strscpy_check(test, "abc", 3, -E2BIG, 2, 1, 0);
	strscpy_check(test, "ab",  3, 2,	   2, 1, 0);
	strscpy_check(test, "a",   3, 1,	   1, 1, 1);
	strscpy_check(test, "",    3, 0,	   0, 1, 2);

	strscpy_check(test, "abcd", 4, -E2BIG, 3, 1, 0);
	strscpy_check(test, "abc",  4, 3,	    3, 1, 0);
	strscpy_check(test, "ab",   4, 2,	    2, 1, 1);
	strscpy_check(test, "a",    4, 1,	    1, 1, 2);
	strscpy_check(test, "",     4, 0,	    0, 1, 3);

	/* Compile-time-known source strings. */
	KUNIT_EXPECT_EQ(test, strscpy(dest, "", ARRAY_SIZE(dest)), 0);
	KUNIT_EXPECT_EQ(test, strscpy(dest, "", 3), 0);
	KUNIT_EXPECT_EQ(test, strscpy(dest, "", 1), 0);
	KUNIT_EXPECT_EQ(test, strscpy(dest, "", 0), -E2BIG);
	KUNIT_EXPECT_EQ(test, strscpy(dest, "Fixed", ARRAY_SIZE(dest)), 5);
	KUNIT_EXPECT_EQ(test, strscpy(dest, "Fixed", 3), -E2BIG);
	KUNIT_EXPECT_EQ(test, strscpy(dest, "Fixed", 1), -E2BIG);
	KUNIT_EXPECT_EQ(test, strscpy(dest, "Fixed", 0), -E2BIG);
	KUNIT_EXPECT_EQ(test, strscpy(dest, "This is too long", ARRAY_SIZE(dest)), -E2BIG);
}

static struct kunit_case strscpy_test_cases[] = {
	KUNIT_CASE(test_strscpy),
	{}
};

static struct kunit_suite strscpy_test_suite = {
	.name = "strscpy",
	.test_cases = strscpy_test_cases,
};

kunit_test_suite(strscpy_test_suite);

MODULE_AUTHOR("Tobin C. Harding <tobin@kernel.org>");
MODULE_LICENSE("GPL");
