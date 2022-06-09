// SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)

#include "test_progs.h"
#include "testing_helpers.h"

static void init_test_filter_set(struct test_filter_set *set)
{
	set->cnt = 0;
	set->tests = NULL;
}

static void free_test_filter_set(struct test_filter_set *set)
{
	int i, j;

	for (i = 0; i < set->cnt; i++) {
		for (j = 0; j < set->tests[i].subtest_cnt; j++)
			free((void *)set->tests[i].subtests[j]);
		free(set->tests[i].subtests);
		free(set->tests[i].name);
	}

	free(set->tests);
	init_test_filter_set(set);
}

static void test_parse_test_list(void)
{
	struct test_filter_set set;

	init_test_filter_set(&set);

	ASSERT_OK(parse_test_list("arg_parsing", &set, true), "parsing");
	if (!ASSERT_EQ(set.cnt, 1, "test filters count"))
		goto error;
	if (!ASSERT_OK_PTR(set.tests, "test filters initialized"))
		goto error;
	ASSERT_EQ(set.tests[0].subtest_cnt, 0, "subtest filters count");
	ASSERT_OK(strcmp("arg_parsing", set.tests[0].name), "subtest name");
	free_test_filter_set(&set);

	ASSERT_OK(parse_test_list("arg_parsing,bpf_cookie", &set, true),
		  "parsing");
	if (!ASSERT_EQ(set.cnt, 2, "count of test filters"))
		goto error;
	if (!ASSERT_OK_PTR(set.tests, "test filters initialized"))
		goto error;
	ASSERT_EQ(set.tests[0].subtest_cnt, 0, "subtest filters count");
	ASSERT_EQ(set.tests[1].subtest_cnt, 0, "subtest filters count");
	ASSERT_OK(strcmp("arg_parsing", set.tests[0].name), "test name");
	ASSERT_OK(strcmp("bpf_cookie", set.tests[1].name), "test name");
	free_test_filter_set(&set);

	ASSERT_OK(parse_test_list("arg_parsing/arg_parsing,bpf_cookie",
				  &set,
				  true),
		  "parsing");
	if (!ASSERT_EQ(set.cnt, 2, "count of test filters"))
		goto error;
	if (!ASSERT_OK_PTR(set.tests, "test filters initialized"))
		goto error;
	if (!ASSERT_EQ(set.tests[0].subtest_cnt, 1, "subtest filters count"))
		goto error;
	ASSERT_EQ(set.tests[1].subtest_cnt, 0, "subtest filters count");
	ASSERT_OK(strcmp("arg_parsing", set.tests[0].name), "test name");
	ASSERT_OK(strcmp("arg_parsing", set.tests[0].subtests[0]),
		  "subtest name");
	ASSERT_OK(strcmp("bpf_cookie", set.tests[1].name), "test name");
	free_test_filter_set(&set);

	ASSERT_OK(parse_test_list("arg_parsing/arg_parsing", &set, true),
		  "parsing");
	ASSERT_OK(parse_test_list("bpf_cookie", &set, true), "parsing");
	ASSERT_OK(parse_test_list("send_signal", &set, true), "parsing");
	if (!ASSERT_EQ(set.cnt, 3, "count of test filters"))
		goto error;
	if (!ASSERT_OK_PTR(set.tests, "test filters initialized"))
		goto error;
	if (!ASSERT_EQ(set.tests[0].subtest_cnt, 1, "subtest filters count"))
		goto error;
	ASSERT_EQ(set.tests[1].subtest_cnt, 0, "subtest filters count");
	ASSERT_EQ(set.tests[2].subtest_cnt, 0, "subtest filters count");
	ASSERT_OK(strcmp("arg_parsing", set.tests[0].name), "test name");
	ASSERT_OK(strcmp("arg_parsing", set.tests[0].subtests[0]),
		  "subtest name");
	ASSERT_OK(strcmp("bpf_cookie", set.tests[1].name), "test name");
	ASSERT_OK(strcmp("send_signal", set.tests[2].name), "test name");
	free_test_filter_set(&set);

	ASSERT_OK(parse_test_list("bpf_cookie/trace", &set, false), "parsing");
	if (!ASSERT_EQ(set.cnt, 1, "count of test filters"))
		goto error;
	if (!ASSERT_OK_PTR(set.tests, "test filters initialized"))
		goto error;
	if (!ASSERT_EQ(set.tests[0].subtest_cnt, 1, "subtest filters count"))
		goto error;
	ASSERT_OK(strcmp("*bpf_cookie*", set.tests[0].name), "test name");
	ASSERT_OK(strcmp("*trace*", set.tests[0].subtests[0]), "subtest name");
error:
	free_test_filter_set(&set);
}

void test_arg_parsing(void)
{
	if (test__start_subtest("test_parse_test_list"))
		test_parse_test_list();
}
