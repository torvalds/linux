// SPDX-License-Identifier: MIT OR GPL-2.0
/*
 * Test cases for glob functions.
 */

#include <kunit/test.h>
#include <linux/glob.h>
#include <linux/module.h>

/**
 * struct glob_test_case - Test case for glob matching.
 * @pat: Pattern to match.
 * @str: String to match against.
 * @expected: Expected glob_match result, true if matched.
 */
struct glob_test_case {
	const char *pat;
	const char *str;
	bool expected;
};

static const struct glob_test_case glob_test_cases[] = {
	/* Some basic tests */
	{ .pat = "a", .str = "a", .expected = true },
	{ .pat = "a", .str = "b", .expected = false },
	{ .pat = "a", .str = "aa", .expected = false },
	{ .pat = "a", .str = "", .expected = false },
	{ .pat = "", .str = "", .expected = true },
	{ .pat = "", .str = "a", .expected = false },
	/* Simple character class tests */
	{ .pat = "[a]", .str = "a", .expected = true },
	{ .pat = "[a]", .str = "b", .expected = false },
	{ .pat = "[!a]", .str = "a", .expected = false },
	{ .pat = "[!a]", .str = "b", .expected = true },
	{ .pat = "[ab]", .str = "a", .expected = true },
	{ .pat = "[ab]", .str = "b", .expected = true },
	{ .pat = "[ab]", .str = "c", .expected = false },
	{ .pat = "[!ab]", .str = "c", .expected = true },
	{ .pat = "[a-c]", .str = "b", .expected = true },
	{ .pat = "[a-c]", .str = "d", .expected = false },
	/* Corner cases in character class parsing */
	{ .pat = "[a-c-e-g]", .str = "-", .expected = true },
	{ .pat = "[a-c-e-g]", .str = "d", .expected = false },
	{ .pat = "[a-c-e-g]", .str = "f", .expected = true },
	{ .pat = "[]a-ceg-ik[]", .str = "a", .expected = true },
	{ .pat = "[]a-ceg-ik[]", .str = "]", .expected = true },
	{ .pat = "[]a-ceg-ik[]", .str = "[", .expected = true },
	{ .pat = "[]a-ceg-ik[]", .str = "h", .expected = true },
	{ .pat = "[]a-ceg-ik[]", .str = "f", .expected = false },
	{ .pat = "[!]a-ceg-ik[]", .str = "h", .expected = false },
	{ .pat = "[!]a-ceg-ik[]", .str = "]", .expected = false },
	{ .pat = "[!]a-ceg-ik[]", .str = "f", .expected = true },
	/* Simple wild cards */
	{ .pat = "?", .str = "a", .expected = true },
	{ .pat = "?", .str = "aa", .expected = false },
	{ .pat = "??", .str = "a", .expected = false },
	{ .pat = "?x?", .str = "axb", .expected = true },
	{ .pat = "?x?", .str = "abx", .expected = false },
	{ .pat = "?x?", .str = "xab", .expected = false },
	/* Asterisk wild cards (backtracking) */
	{ .pat = "*??", .str = "a", .expected = false },
	{ .pat = "*??", .str = "ab", .expected = true },
	{ .pat = "*??", .str = "abc", .expected = true },
	{ .pat = "*??", .str = "abcd", .expected = true },
	{ .pat = "??*", .str = "a", .expected = false },
	{ .pat = "??*", .str = "ab", .expected = true },
	{ .pat = "??*", .str = "abc", .expected = true },
	{ .pat = "??*", .str = "abcd", .expected = true },
	{ .pat = "?*?", .str = "a", .expected = false },
	{ .pat = "?*?", .str = "ab", .expected = true },
	{ .pat = "?*?", .str = "abc", .expected = true },
	{ .pat = "?*?", .str = "abcd", .expected = true },
	{ .pat = "*b", .str = "b", .expected = true },
	{ .pat = "*b", .str = "ab", .expected = true },
	{ .pat = "*b", .str = "ba", .expected = false },
	{ .pat = "*b", .str = "bb", .expected = true },
	{ .pat = "*b", .str = "abb", .expected = true },
	{ .pat = "*b", .str = "bab", .expected = true },
	{ .pat = "*bc", .str = "abbc", .expected = true },
	{ .pat = "*bc", .str = "bc", .expected = true },
	{ .pat = "*bc", .str = "bbc", .expected = true },
	{ .pat = "*bc", .str = "bcbc", .expected = true },
	/* Multiple asterisks (complex backtracking) */
	{ .pat = "*ac*", .str = "abacadaeafag", .expected = true },
	{ .pat = "*ac*ae*ag*", .str = "abacadaeafag", .expected = true },
	{ .pat = "*a*b*[bc]*[ef]*g*", .str = "abacadaeafag", .expected = true },
	{ .pat = "*a*b*[ef]*[cd]*g*", .str = "abacadaeafag", .expected = false },
	{ .pat = "*abcd*", .str = "abcabcabcabcdefg", .expected = true },
	{ .pat = "*ab*cd*", .str = "abcabcabcabcdefg", .expected = true },
	{ .pat = "*abcd*abcdef*", .str = "abcabcdabcdeabcdefg", .expected = true },
	{ .pat = "*abcd*", .str = "abcabcabcabcefg", .expected = false },
	{ .pat = "*ab*cd*", .str = "abcabcabcabcefg", .expected = false },
};

static void glob_case_to_desc(const struct glob_test_case *t, char *desc)
{
	snprintf(desc, KUNIT_PARAM_DESC_SIZE, "pat:\"%s\" str:\"%s\"", t->pat, t->str);
}

KUNIT_ARRAY_PARAM(glob, glob_test_cases, glob_case_to_desc);

static void glob_test_match(struct kunit *test)
{
	const struct glob_test_case *params = test->param_value;

	KUNIT_EXPECT_EQ_MSG(test,
			    glob_match(params->pat, params->str),
			    params->expected,
			    "Pattern: \"%s\", String: \"%s\", Expected: %d",
			    params->pat, params->str, params->expected);
}

static struct kunit_case glob_kunit_test_cases[] = {
	KUNIT_CASE_PARAM(glob_test_match, glob_gen_params),
	{}
};

static struct kunit_suite glob_test_suite = {
	.name = "glob",
	.test_cases = glob_kunit_test_cases,
};

kunit_test_suite(glob_test_suite);
MODULE_DESCRIPTION("Test cases for glob functions");
MODULE_LICENSE("Dual MIT/GPL");
