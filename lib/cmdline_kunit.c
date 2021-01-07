// SPDX-License-Identifier: GPL-2.0+
/*
 * Test cases for API provided by cmdline.c
 */

#include <kunit/test.h>
#include <linux/kernel.h>
#include <linux/random.h>
#include <linux/string.h>

static const char *cmdline_test_strings[] = {
	"\"\"", ""  , "=" , "\"-", ","    , "-,"   , ",-"   , "-" ,
	"+,"  , "--", ",,", "''" , "\"\",", "\",\"", "-\"\"", "\"",
};

static const int cmdline_test_values[] = {
	1, 1, 1, 1, 2, 3, 2, 3,
	1, 3, 2, 1, 1, 1, 3, 1,
};

static void cmdline_do_one_test(struct kunit *test, const char *in, int rc, int offset)
{
	const char *fmt = "Pattern: %s";
	const char *out = in;
	int dummy;
	int ret;

	ret = get_option((char **)&out, &dummy);

	KUNIT_EXPECT_EQ_MSG(test, ret, rc, fmt, in);
	KUNIT_EXPECT_PTR_EQ_MSG(test, out, in + offset, fmt, in);
}

static void cmdline_test_noint(struct kunit *test)
{
	unsigned int i = 0;

	do {
		const char *str = cmdline_test_strings[i];
		int rc = 0;
		int offset;

		/* Only first and leading '-' will advance the pointer */
		offset = !!(*str == '-');
		cmdline_do_one_test(test, str, rc, offset);
	} while (++i < ARRAY_SIZE(cmdline_test_strings));
}

static void cmdline_test_lead_int(struct kunit *test)
{
	unsigned int i = 0;
	char in[32];

	do {
		const char *str = cmdline_test_strings[i];
		int rc = cmdline_test_values[i];
		int offset;

		sprintf(in, "%u%s", get_random_int() % 256, str);
		/* Only first '-' after the number will advance the pointer */
		offset = strlen(in) - strlen(str) + !!(rc == 2);
		cmdline_do_one_test(test, in, rc, offset);
	} while (++i < ARRAY_SIZE(cmdline_test_strings));
}

static void cmdline_test_tail_int(struct kunit *test)
{
	unsigned int i = 0;
	char in[32];

	do {
		const char *str = cmdline_test_strings[i];
		/* When "" or "-" the result will be valid integer */
		int rc = strcmp(str, "") ? (strcmp(str, "-") ? 0 : 1) : 1;
		int offset;

		sprintf(in, "%s%u", str, get_random_int() % 256);
		/*
		 * Only first and leading '-' not followed by integer
		 * will advance the pointer.
		 */
		offset = rc ? strlen(in) : !!(*str == '-');
		cmdline_do_one_test(test, in, rc, offset);
	} while (++i < ARRAY_SIZE(cmdline_test_strings));
}

static struct kunit_case cmdline_test_cases[] = {
	KUNIT_CASE(cmdline_test_noint),
	KUNIT_CASE(cmdline_test_lead_int),
	KUNIT_CASE(cmdline_test_tail_int),
	{}
};

static struct kunit_suite cmdline_test_suite = {
	.name = "cmdline",
	.test_cases = cmdline_test_cases,
};
kunit_test_suite(cmdline_test_suite);

MODULE_LICENSE("GPL");
