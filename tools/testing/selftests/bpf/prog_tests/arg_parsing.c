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
	free_test_filter_set(&set);

	ASSERT_OK(parse_test_list("t/subtest1,t/subtest2", &set, true),
		  "parsing");
	if (!ASSERT_EQ(set.cnt, 1, "count of test filters"))
		goto error;
	if (!ASSERT_OK_PTR(set.tests, "test filters initialized"))
		goto error;
	if (!ASSERT_EQ(set.tests[0].subtest_cnt, 2, "subtest filters count"))
		goto error;
	ASSERT_OK(strcmp("t", set.tests[0].name), "test name");
	ASSERT_OK(strcmp("subtest1", set.tests[0].subtests[0]), "subtest name");
	ASSERT_OK(strcmp("subtest2", set.tests[0].subtests[1]), "subtest name");
error:
	free_test_filter_set(&set);
}

static void test_parse_test_list_file(void)
{
	struct test_filter_set set;
	char tmpfile[80];
	FILE *fp;
	int fd;

	snprintf(tmpfile, sizeof(tmpfile), "/tmp/bpf_arg_parsing_test.XXXXXX");
	fd = mkstemp(tmpfile);
	if (!ASSERT_GE(fd, 0, "create tmp"))
		return;

	fp = fdopen(fd, "w");
	if (!ASSERT_NEQ(fp, NULL, "fdopen tmp")) {
		close(fd);
		goto out_remove;
	}

	fprintf(fp, "# comment\n");
	fprintf(fp, "  test_with_spaces    \n");
	fprintf(fp, "testA/subtest    # comment\n");
	fprintf(fp, "testB#comment with no space\n");
	fprintf(fp, "testB # duplicate\n");
	fprintf(fp, "testA/subtest # subtest duplicate\n");
	fprintf(fp, "testA/subtest2\n");
	fprintf(fp, "testC_no_eof_newline");
	fflush(fp);

	if (!ASSERT_OK(ferror(fp), "prepare tmp"))
		goto out_fclose;

	if (!ASSERT_OK(fsync(fileno(fp)), "fsync tmp"))
		goto out_fclose;

	init_test_filter_set(&set);

	if (!ASSERT_OK(parse_test_list_file(tmpfile, &set, true), "parse file"))
		goto out_fclose;

	if (!ASSERT_EQ(set.cnt, 4, "test  count"))
		goto out_free_set;

	ASSERT_OK(strcmp("test_with_spaces", set.tests[0].name), "test 0 name");
	ASSERT_EQ(set.tests[0].subtest_cnt, 0, "test 0 subtest count");
	ASSERT_OK(strcmp("testA", set.tests[1].name), "test 1 name");
	ASSERT_EQ(set.tests[1].subtest_cnt, 2, "test 1 subtest count");
	ASSERT_OK(strcmp("subtest", set.tests[1].subtests[0]), "test 1 subtest 0");
	ASSERT_OK(strcmp("subtest2", set.tests[1].subtests[1]), "test 1 subtest 1");
	ASSERT_OK(strcmp("testB", set.tests[2].name), "test 2 name");
	ASSERT_OK(strcmp("testC_no_eof_newline", set.tests[3].name), "test 3 name");

out_free_set:
	free_test_filter_set(&set);
out_fclose:
	fclose(fp);
out_remove:
	remove(tmpfile);
}

void test_arg_parsing(void)
{
	if (test__start_subtest("test_parse_test_list"))
		test_parse_test_list();
	if (test__start_subtest("test_parse_test_list_file"))
		test_parse_test_list_file();
}
