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

static_assert(ARRAY_SIZE(cmdline_test_strings) == ARRAY_SIZE(cmdline_test_values));

static const char *cmdline_test_range_strings[] = {
	"-7" , "--7"  , "-1-2"    , "7--9",
	"7-" , "-7--9", "7-9,"    , "9-7" ,
	"5-a", "a-5"  , "5-8"     , ",8-5",
	"+,1", "-,4"  , "-3,0-1,6", "4,-" ,
	" +2", " -9"  , "0-1,-3,6", "- 9" ,
};

static const int cmdline_test_range_values[][16] = {
	{ 1, -7, }, { 0, -0, }, { 4, -1, 0, +1, 2, }, { 0, 7, },
	{ 0, +7, }, { 0, -7, }, { 3, +7, 8, +9, 0, }, { 0, 9, },
	{ 0, +5, }, { 0, -0, }, { 4, +5, 6, +7, 8, }, { 0, 0, },
	{ 0, +0, }, { 0, -0, }, { 4, -3, 0, +1, 6, }, { 1, 4, },
	{ 0, +0, }, { 0, -0, }, { 4, +0, 1, -3, 6, }, { 0, 0, },
};

static_assert(ARRAY_SIZE(cmdline_test_range_strings) == ARRAY_SIZE(cmdline_test_range_values));

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

		sprintf(in, "%u%s", get_random_u8(), str);
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

		sprintf(in, "%s%u", str, get_random_u8());
		/*
		 * Only first and leading '-' not followed by integer
		 * will advance the pointer.
		 */
		offset = rc ? strlen(in) : !!(*str == '-');
		cmdline_do_one_test(test, in, rc, offset);
	} while (++i < ARRAY_SIZE(cmdline_test_strings));
}

static void cmdline_do_one_range_test(struct kunit *test, const char *in,
				      unsigned int n, const int *e)
{
	unsigned int i;
	int r[16];
	int *p;

	memset(r, 0, sizeof(r));
	get_options(in, ARRAY_SIZE(r), r);
	KUNIT_EXPECT_EQ_MSG(test, r[0], e[0], "in test %u (parsed) expected %d numbers, got %d",
			    n, e[0], r[0]);
	for (i = 1; i < ARRAY_SIZE(r); i++)
		KUNIT_EXPECT_EQ_MSG(test, r[i], e[i], "in test %u at %u", n, i);

	memset(r, 0, sizeof(r));
	get_options(in, 0, r);
	KUNIT_EXPECT_EQ_MSG(test, r[0], e[0], "in test %u (validated) expected %d numbers, got %d",
			    n, e[0], r[0]);

	p = memchr_inv(&r[1], 0, sizeof(r) - sizeof(r[0]));
	KUNIT_EXPECT_PTR_EQ_MSG(test, p, NULL, "in test %u at %u out of bound", n, p - r);
}

static void cmdline_test_range(struct kunit *test)
{
	unsigned int i = 0;

	do {
		const char *str = cmdline_test_range_strings[i];
		const int *e = cmdline_test_range_values[i];

		cmdline_do_one_range_test(test, str, i, e);
	} while (++i < ARRAY_SIZE(cmdline_test_range_strings));
}

static struct kunit_case cmdline_test_cases[] = {
	KUNIT_CASE(cmdline_test_noint),
	KUNIT_CASE(cmdline_test_lead_int),
	KUNIT_CASE(cmdline_test_tail_int),
	KUNIT_CASE(cmdline_test_range),
	{}
};

static struct kunit_suite cmdline_test_suite = {
	.name = "cmdline",
	.test_cases = cmdline_test_cases,
};
kunit_test_suite(cmdline_test_suite);

MODULE_LICENSE("GPL");
