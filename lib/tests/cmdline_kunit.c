// SPDX-License-Identifier: GPL-2.0+
/*
 * Test cases for API provided by cmdline.c
 */

#include <kunit/test.h>
#include <linux/kernel.h>
#include <linux/random.h>
#include <linux/sizes.h>
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
	KUNIT_EXPECT_PTR_EQ_MSG(test, p, NULL, "in test %u at %td out of bound", n, p - r);
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

static void cmdline_test_next_arg_quoted_value(struct kunit *test)
{
	char in[] = "foo=\"bar baz\" qux=1";
	char *next, *param, *val;

	next = next_arg(in, &param, &val);
	KUNIT_EXPECT_STREQ(test, param, "foo");
	KUNIT_ASSERT_NOT_NULL(test, val);
	KUNIT_EXPECT_STREQ(test, val, "bar baz");
	KUNIT_EXPECT_STREQ(test, next, "qux=1");

	next = next_arg(next, &param, &val);
	KUNIT_EXPECT_STREQ(test, param, "qux");
	KUNIT_ASSERT_NOT_NULL(test, val);
	KUNIT_EXPECT_STREQ(test, val, "1");
	KUNIT_EXPECT_STREQ(test, next, "");
}

static void cmdline_test_next_arg_bare_quote_regression(struct kunit *test)
{
	char in[] = "foo=bar \"";
	char *next, *param, *val;

	next = next_arg(in, &param, &val);
	KUNIT_EXPECT_STREQ(test, param, "foo");
	KUNIT_ASSERT_NOT_NULL(test, val);
	KUNIT_EXPECT_STREQ(test, val, "bar");
	KUNIT_EXPECT_STREQ(test, next, "\"");

	/* This hits the i == 0 quoted-token case fixed by 9847f21225c4. */
	next = next_arg(next, &param, &val);
	KUNIT_EXPECT_STREQ(test, param, "");
	KUNIT_EXPECT_PTR_EQ(test, val, NULL);
	KUNIT_EXPECT_STREQ(test, next, "");
}

static void cmdline_test_next_arg_mixed_tokens(struct kunit *test)
{
	char in[] = "bbb= jjj kkk=\"a=b\"";
	char *next, *param, *val;

	next = next_arg(in, &param, &val);
	KUNIT_EXPECT_STREQ(test, param, "bbb");
	KUNIT_ASSERT_NOT_NULL(test, val);
	KUNIT_EXPECT_STREQ(test, val, "");
	KUNIT_EXPECT_STREQ(test, next, "jjj kkk=\"a=b\"");

	next = next_arg(next, &param, &val);
	KUNIT_EXPECT_STREQ(test, param, "jjj");
	KUNIT_EXPECT_NULL(test, val);
	KUNIT_EXPECT_STREQ(test, next, "kkk=\"a=b\"");

	next = next_arg(next, &param, &val);
	KUNIT_EXPECT_STREQ(test, param, "kkk");
	KUNIT_ASSERT_NOT_NULL(test, val);
	KUNIT_EXPECT_STREQ(test, val, "a=b");
	KUNIT_EXPECT_STREQ(test, next, "");
}

struct cmdline_test_memparse_entry {
	const char *input;
	const char *unrecognized;
	unsigned long long result;
};

static const struct cmdline_test_memparse_entry testdata[] = {
	{ "0",				"",	0ULL },
	{ "1",				"",	1ULL },
	{ "a",				"a",	0ULL },
	{ "k",				"k",	0ULL },
	{ "E",				"E",	0ULL },
	{ "0xb",			"",	11ULL },
	{ "0xz",			"x",	0ULL },
	{ "1234",			"",	1234ULL },
	{ "04567",			"",	2423ULL },
	{ "0x9876",			"",	39030LL },
	{ "05678",			"8",	375ULL },
	{ "0xabcdefz",			"z",	11259375ULL },
	{ "0cdba",			"c",	0ULL },
	{ "4K",				"",	SZ_4K },
	{ "0x10k@0xaaaabbbb",		"@",	SZ_16K },
	{ "32M",			"",	SZ_32M },
	{ "067m:foo",			":",	55 * SZ_1M },
	{ "2G;bar=baz",			";",	SZ_2G },
	{ "07gz",			"z",	7ULL * SZ_1G },
	{ "3T+data",			"+",	3 * SZ_1T },
	{ "04t,ro",			",",	SZ_4T },
	{ "012p",			"",	11258999068426240ULL },
	{ "7P,sync",			",",	7881299347898368ULL },
	{ "0x2e",			"",	46ULL },
	{ "2E and more",		" ",	2305843009213693952ULL },
	{ "18446744073709551615",	"",	ULLONG_MAX },
	{ "0xffffffffffffffff0",	"",	ULLONG_MAX },
	{ "1111111111111111111T",	"",	ULLONG_MAX },
	{ "222222222222222222222G",	"",	ULLONG_MAX },
	{ "3333333333333333333333M",	"",	ULLONG_MAX },
};

static void cmdline_test_memparse(struct kunit *test)
{
	const struct cmdline_test_memparse_entry *e;
	unsigned long long ret;
	char *retptr;

	for (e = testdata; e < testdata + ARRAY_SIZE(testdata); e++) {
		ret = memparse(e->input, &retptr);
		KUNIT_EXPECT_EQ_MSG(test, ret, e->result,
				    "    when parsing '%s'", e->input);
		KUNIT_EXPECT_EQ_MSG(test, *retptr, *e->unrecognized,
				    "    when parsing '%s'", e->input);
	}
}

static struct kunit_case cmdline_test_cases[] = {
	KUNIT_CASE(cmdline_test_noint),
	KUNIT_CASE(cmdline_test_lead_int),
	KUNIT_CASE(cmdline_test_tail_int),
	KUNIT_CASE(cmdline_test_range),
	KUNIT_CASE(cmdline_test_next_arg_quoted_value),
	KUNIT_CASE(cmdline_test_next_arg_bare_quote_regression),
	KUNIT_CASE(cmdline_test_next_arg_mixed_tokens),
	KUNIT_CASE(cmdline_test_memparse),
	{}
};

static struct kunit_suite cmdline_test_suite = {
	.name = "cmdline",
	.test_cases = cmdline_test_cases,
};
kunit_test_suite(cmdline_test_suite);

MODULE_DESCRIPTION("Test cases for API provided by cmdline.c");
MODULE_LICENSE("GPL");
