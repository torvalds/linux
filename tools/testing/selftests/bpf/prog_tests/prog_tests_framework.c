// SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)

#include "test_progs.h"
#include "testing_helpers.h"

static void clear_test_state(struct test_state *state)
{
	state->error_cnt = 0;
	state->sub_succ_cnt = 0;
	state->skip_cnt = 0;
}

void test_prog_tests_framework(void)
{
	struct test_state *state = env.test_state;

	/* in all the ASSERT calls below we need to return on the first
	 * error due to the fact that we are cleaning the test state after
	 * each dummy subtest
	 */

	/* test we properly count skipped tests with subtests */
	if (test__start_subtest("test_good_subtest"))
		test__end_subtest();
	if (!ASSERT_EQ(state->skip_cnt, 0, "skip_cnt_check"))
		return;
	if (!ASSERT_EQ(state->error_cnt, 0, "error_cnt_check"))
		return;
	if (!ASSERT_EQ(state->subtest_num, 1, "subtest_num_check"))
		return;
	clear_test_state(state);

	if (test__start_subtest("test_skip_subtest")) {
		test__skip();
		test__end_subtest();
	}
	if (test__start_subtest("test_skip_subtest")) {
		test__skip();
		test__end_subtest();
	}
	if (!ASSERT_EQ(state->skip_cnt, 2, "skip_cnt_check"))
		return;
	if (!ASSERT_EQ(state->subtest_num, 3, "subtest_num_check"))
		return;
	clear_test_state(state);

	if (test__start_subtest("test_fail_subtest")) {
		test__fail();
		test__end_subtest();
	}
	if (!ASSERT_EQ(state->error_cnt, 1, "error_cnt_check"))
		return;
	if (!ASSERT_EQ(state->subtest_num, 4, "subtest_num_check"))
		return;
	clear_test_state(state);
}

static void dummy_emit(const char *buf, bool force) {}

void test_prog_tests_framework_expected_msgs(void)
{
	struct expected_msgs msgs;
	int i, j, error_cnt;
	const struct {
		const char *name;
		const char *log;
		const char *expected;
		struct expect_msg *pats;
	} cases[] = {
		{
			.name = "simple-ok",
			.log = "aaabbbccc",
			.pats = (struct expect_msg[]) {
				{ .substr = "aaa" },
				{ .substr = "ccc" },
				{}
			}
		},
		{
			.name = "simple-fail",
			.log = "aaabbbddd",
			.expected = "MATCHED    SUBSTR: 'aaa'\n"
				    "EXPECTED   SUBSTR: 'ccc'\n",
			.pats = (struct expect_msg[]) {
				{ .substr = "aaa" },
				{ .substr = "ccc" },
				{}
			}
		},
		{
			.name = "negative-ok-mid",
			.log = "aaabbbccc",
			.pats = (struct expect_msg[]) {
				{ .substr = "aaa" },
				{ .substr = "foo", .negative = true },
				{ .substr = "bar", .negative = true },
				{ .substr = "ccc" },
				{}
			}
		},
		{
			.name = "negative-ok-tail",
			.log = "aaabbbccc",
			.pats = (struct expect_msg[]) {
				{ .substr = "aaa" },
				{ .substr = "foo", .negative = true },
				{}
			}
		},
		{
			.name = "negative-ok-head",
			.log = "aaabbbccc",
			.pats = (struct expect_msg[]) {
				{ .substr = "foo", .negative = true },
				{ .substr = "ccc" },
				{}
			}
		},
		{
			.name = "negative-fail-head",
			.log = "aaabbbccc",
			.expected = "UNEXPECTED SUBSTR: 'aaa'\n",
			.pats = (struct expect_msg[]) {
				{ .substr = "aaa", .negative = true },
				{ .substr = "bbb" },
				{}
			}
		},
		{
			.name = "negative-fail-tail",
			.log = "aaabbbccc",
			.expected = "UNEXPECTED SUBSTR: 'ccc'\n",
			.pats = (struct expect_msg[]) {
				{ .substr = "bbb" },
				{ .substr = "ccc", .negative = true },
				{}
			}
		},
		{
			.name = "negative-fail-mid-1",
			.log = "aaabbbccc",
			.expected = "UNEXPECTED SUBSTR: 'bbb'\n",
			.pats = (struct expect_msg[]) {
				{ .substr = "aaa" },
				{ .substr = "bbb", .negative = true },
				{ .substr = "ccc" },
				{}
			}
		},
		{
			.name = "negative-fail-mid-2",
			.log = "aaabbb222ccc",
			.expected = "UNEXPECTED SUBSTR: '222'\n",
			.pats = (struct expect_msg[]) {
				{ .substr = "aaa" },
				{ .substr = "222", .negative = true },
				{ .substr = "bbb", .negative = true },
				{ .substr = "ccc" },
				{}
			}
		}
	};

	for (i = 0; i < ARRAY_SIZE(cases); i++) {
		if (test__start_subtest(cases[i].name)) {
			error_cnt = env.subtest_state->error_cnt;
			msgs.patterns = cases[i].pats;
			msgs.cnt = 0;
			for (j = 0; cases[i].pats[j].substr; j++)
				msgs.cnt++;
			validate_msgs(cases[i].log, &msgs, dummy_emit);
			fflush(stderr);
			env.subtest_state->error_cnt = error_cnt;
			if (cases[i].expected)
				ASSERT_HAS_SUBSTR(env.subtest_state->log_buf, cases[i].expected, "expected output");
			else
				ASSERT_STREQ(env.subtest_state->log_buf, "", "expected no output");
			test__end_subtest();
		}
	}
}
