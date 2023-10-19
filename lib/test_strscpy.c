// SPDX-License-Identifier: GPL-2.0+

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/string.h>

#include "../tools/testing/selftests/kselftest_module.h"

/*
 * Kernel module for testing 'strscpy' family of functions.
 */

KSTM_MODULE_GLOBALS();

/*
 * tc() - Run a specific test case.
 * @src: Source string, argument to strscpy_pad()
 * @count: Size of destination buffer, argument to strscpy_pad()
 * @expected: Expected return value from call to strscpy_pad()
 * @terminator: 1 if there should be a terminating null byte 0 otherwise.
 * @chars: Number of characters from the src string expected to be
 *         written to the dst buffer.
 * @pad: Number of pad characters expected (in the tail of dst buffer).
 *       (@pad does not include the null terminator byte.)
 *
 * Calls strscpy_pad() and verifies the return value and state of the
 * destination buffer after the call returns.
 */
static int __init tc(char *src, int count, int expected,
		     int chars, int terminator, int pad)
{
	int nr_bytes_poison;
	int max_expected;
	int max_count;
	int written;
	char buf[6];
	int index, i;
	const char POISON = 'z';

	total_tests++;

	if (!src) {
		pr_err("null source string not supported\n");
		return -1;
	}

	memset(buf, POISON, sizeof(buf));
	/* Future proofing test suite, validate args */
	max_count = sizeof(buf) - 2; /* Space for null and to verify overflow */
	max_expected = count - 1;    /* Space for the null */
	if (count > max_count) {
		pr_err("count (%d) is too big (%d) ... aborting", count, max_count);
		return -1;
	}
	if (expected > max_expected) {
		pr_warn("expected (%d) is bigger than can possibly be returned (%d)",
			expected, max_expected);
	}

	written = strscpy_pad(buf, src, count);
	if ((written) != (expected)) {
		pr_err("%d != %d (written, expected)\n", written, expected);
		goto fail;
	}

	if (count && written == -E2BIG) {
		if (strncmp(buf, src, count - 1) != 0) {
			pr_err("buffer state invalid for -E2BIG\n");
			goto fail;
		}
		if (buf[count - 1] != '\0') {
			pr_err("too big string is not null terminated correctly\n");
			goto fail;
		}
	}

	for (i = 0; i < chars; i++) {
		if (buf[i] != src[i]) {
			pr_err("buf[i]==%c != src[i]==%c\n", buf[i], src[i]);
			goto fail;
		}
	}

	if (terminator) {
		if (buf[count - 1] != '\0') {
			pr_err("string is not null terminated correctly\n");
			goto fail;
		}
	}

	for (i = 0; i < pad; i++) {
		index = chars + terminator + i;
		if (buf[index] != '\0') {
			pr_err("padding missing at index: %d\n", i);
			goto fail;
		}
	}

	nr_bytes_poison = sizeof(buf) - chars - terminator - pad;
	for (i = 0; i < nr_bytes_poison; i++) {
		index = sizeof(buf) - 1 - i; /* Check from the end back */
		if (buf[index] != POISON) {
			pr_err("poison value missing at index: %d\n", i);
			goto fail;
		}
	}

	return 0;
fail:
	failed_tests++;
	return -1;
}

static void __init selftest(void)
{
	/*
	 * tc() uses a destination buffer of size 6 and needs at
	 * least 2 characters spare (one for null and one to check for
	 * overflow).  This means we should only call tc() with
	 * strings up to a maximum of 4 characters long and 'count'
	 * should not exceed 4.  To test with longer strings increase
	 * the buffer size in tc().
	 */

	/* tc(src, count, expected, chars, terminator, pad) */
	KSTM_CHECK_ZERO(tc("a", 0, -E2BIG, 0, 0, 0));
	KSTM_CHECK_ZERO(tc("", 0, -E2BIG, 0, 0, 0));

	KSTM_CHECK_ZERO(tc("a", 1, -E2BIG, 0, 1, 0));
	KSTM_CHECK_ZERO(tc("", 1, 0, 0, 1, 0));

	KSTM_CHECK_ZERO(tc("ab", 2, -E2BIG, 1, 1, 0));
	KSTM_CHECK_ZERO(tc("a", 2, 1, 1, 1, 0));
	KSTM_CHECK_ZERO(tc("", 2, 0, 0, 1, 1));

	KSTM_CHECK_ZERO(tc("abc", 3, -E2BIG, 2, 1, 0));
	KSTM_CHECK_ZERO(tc("ab", 3, 2, 2, 1, 0));
	KSTM_CHECK_ZERO(tc("a", 3, 1, 1, 1, 1));
	KSTM_CHECK_ZERO(tc("", 3, 0, 0, 1, 2));

	KSTM_CHECK_ZERO(tc("abcd", 4, -E2BIG, 3, 1, 0));
	KSTM_CHECK_ZERO(tc("abc", 4, 3, 3, 1, 0));
	KSTM_CHECK_ZERO(tc("ab", 4, 2, 2, 1, 1));
	KSTM_CHECK_ZERO(tc("a", 4, 1, 1, 1, 2));
	KSTM_CHECK_ZERO(tc("", 4, 0, 0, 1, 3));
}

KSTM_MODULE_LOADERS(test_strscpy);
MODULE_AUTHOR("Tobin C. Harding <tobin@kernel.org>");
MODULE_LICENSE("GPL");
