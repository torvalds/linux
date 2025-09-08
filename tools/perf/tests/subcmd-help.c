// SPDX-License-Identifier: GPL-2.0
#include "tests.h"
#include <linux/compiler.h>
#include <subcmd/help.h>

static int test__load_cmdnames(struct test_suite *test __maybe_unused,
			       int subtest __maybe_unused)
{
	struct cmdnames cmds = {};

	add_cmdname(&cmds, "aaa", 3);
	add_cmdname(&cmds, "foo", 3);
	add_cmdname(&cmds, "xyz", 3);

	TEST_ASSERT_VAL("cannot find cmd", is_in_cmdlist(&cmds, "aaa") == 1);
	TEST_ASSERT_VAL("wrong cmd", is_in_cmdlist(&cmds, "bar") == 0);
	TEST_ASSERT_VAL("case sensitive", is_in_cmdlist(&cmds, "XYZ") == 0);

	clean_cmdnames(&cmds);
	return TEST_OK;
}

static int test__uniq_cmdnames(struct test_suite *test __maybe_unused,
			       int subtest __maybe_unused)
{
	struct cmdnames cmds = {};

	/* uniq() assumes it's sorted */
	add_cmdname(&cmds, "aaa", 3);
	add_cmdname(&cmds, "aaa", 3);
	add_cmdname(&cmds, "bbb", 3);

	TEST_ASSERT_VAL("invalid original size", cmds.cnt == 3);
	/* uniquify command names (to remove second 'aaa') */
	uniq(&cmds);
	TEST_ASSERT_VAL("invalid final size", cmds.cnt == 2);

	TEST_ASSERT_VAL("cannot find cmd", is_in_cmdlist(&cmds, "aaa") == 1);
	TEST_ASSERT_VAL("cannot find cmd", is_in_cmdlist(&cmds, "bbb") == 1);
	TEST_ASSERT_VAL("wrong cmd", is_in_cmdlist(&cmds, "ccc") == 0);

	clean_cmdnames(&cmds);
	return TEST_OK;
}

static int test__exclude_cmdnames(struct test_suite *test __maybe_unused,
				  int subtest __maybe_unused)
{
	struct cmdnames cmds1 = {};
	struct cmdnames cmds2 = {};

	add_cmdname(&cmds1, "aaa", 3);
	add_cmdname(&cmds1, "bbb", 3);
	add_cmdname(&cmds1, "ccc", 3);
	add_cmdname(&cmds1, "ddd", 3);
	add_cmdname(&cmds1, "eee", 3);
	add_cmdname(&cmds1, "fff", 3);
	add_cmdname(&cmds1, "ggg", 3);
	add_cmdname(&cmds1, "hhh", 3);
	add_cmdname(&cmds1, "iii", 3);
	add_cmdname(&cmds1, "jjj", 3);

	add_cmdname(&cmds2, "bbb", 3);
	add_cmdname(&cmds2, "eee", 3);
	add_cmdname(&cmds2, "jjj", 3);

	TEST_ASSERT_VAL("invalid original size", cmds1.cnt == 10);
	TEST_ASSERT_VAL("invalid original size", cmds2.cnt == 3);

	/* remove duplicate command names in cmds1 */
	exclude_cmds(&cmds1, &cmds2);

	TEST_ASSERT_VAL("invalid excluded size", cmds1.cnt == 7);
	TEST_ASSERT_VAL("invalid excluded size", cmds2.cnt == 3);

	/* excluded commands should not belong to cmds1 */
	TEST_ASSERT_VAL("cannot find cmd", is_in_cmdlist(&cmds1, "aaa") == 1);
	TEST_ASSERT_VAL("wrong cmd", is_in_cmdlist(&cmds1, "bbb") == 0);
	TEST_ASSERT_VAL("cannot find cmd", is_in_cmdlist(&cmds1, "ccc") == 1);
	TEST_ASSERT_VAL("cannot find cmd", is_in_cmdlist(&cmds1, "ddd") == 1);
	TEST_ASSERT_VAL("wrong cmd", is_in_cmdlist(&cmds1, "eee") == 0);
	TEST_ASSERT_VAL("cannot find cmd", is_in_cmdlist(&cmds1, "fff") == 1);
	TEST_ASSERT_VAL("cannot find cmd", is_in_cmdlist(&cmds1, "ggg") == 1);
	TEST_ASSERT_VAL("cannot find cmd", is_in_cmdlist(&cmds1, "hhh") == 1);
	TEST_ASSERT_VAL("cannot find cmd", is_in_cmdlist(&cmds1, "iii") == 1);
	TEST_ASSERT_VAL("wrong cmd", is_in_cmdlist(&cmds1, "jjj") == 0);

	/* they should be only in cmds2 */
	TEST_ASSERT_VAL("cannot find cmd", is_in_cmdlist(&cmds2, "bbb") == 1);
	TEST_ASSERT_VAL("cannot find cmd", is_in_cmdlist(&cmds2, "eee") == 1);
	TEST_ASSERT_VAL("cannot find cmd", is_in_cmdlist(&cmds2, "jjj") == 1);

	clean_cmdnames(&cmds1);
	clean_cmdnames(&cmds2);
	return TEST_OK;
}

static struct test_case tests__subcmd_help[] = {
	TEST_CASE("Load subcmd names", load_cmdnames),
	TEST_CASE("Uniquify subcmd names", uniq_cmdnames),
	TEST_CASE("Exclude duplicate subcmd names", exclude_cmdnames),
	{	.name = NULL, }
};

struct test_suite suite__subcmd_help = {
	.desc = "libsubcmd help tests",
	.test_cases = tests__subcmd_help,
};
