/* SPDX-License-Identifier: GPL-2.0 */
/*
 * DAMON Debugfs Interface Unit Tests
 *
 * Author: SeongJae Park <sj@kernel.org>
 */

#ifdef CONFIG_DAMON_DBGFS_KUNIT_TEST

#ifndef _DAMON_DBGFS_TEST_H
#define _DAMON_DBGFS_TEST_H

#include <kunit/test.h>

static void damon_dbgfs_test_str_to_ints(struct kunit *test)
{
	char *question;
	int *answers;
	int expected[] = {12, 35, 46};
	ssize_t nr_integers = 0, i;

	question = "123";
	answers = str_to_ints(question, strlen(question), &nr_integers);
	KUNIT_EXPECT_EQ(test, (ssize_t)1, nr_integers);
	KUNIT_EXPECT_EQ(test, 123, answers[0]);
	kfree(answers);

	question = "123abc";
	answers = str_to_ints(question, strlen(question), &nr_integers);
	KUNIT_EXPECT_EQ(test, (ssize_t)1, nr_integers);
	KUNIT_EXPECT_EQ(test, 123, answers[0]);
	kfree(answers);

	question = "a123";
	answers = str_to_ints(question, strlen(question), &nr_integers);
	KUNIT_EXPECT_EQ(test, (ssize_t)0, nr_integers);
	kfree(answers);

	question = "12 35";
	answers = str_to_ints(question, strlen(question), &nr_integers);
	KUNIT_EXPECT_EQ(test, (ssize_t)2, nr_integers);
	for (i = 0; i < nr_integers; i++)
		KUNIT_EXPECT_EQ(test, expected[i], answers[i]);
	kfree(answers);

	question = "12 35 46";
	answers = str_to_ints(question, strlen(question), &nr_integers);
	KUNIT_EXPECT_EQ(test, (ssize_t)3, nr_integers);
	for (i = 0; i < nr_integers; i++)
		KUNIT_EXPECT_EQ(test, expected[i], answers[i]);
	kfree(answers);

	question = "12 35 abc 46";
	answers = str_to_ints(question, strlen(question), &nr_integers);
	KUNIT_EXPECT_EQ(test, (ssize_t)2, nr_integers);
	for (i = 0; i < 2; i++)
		KUNIT_EXPECT_EQ(test, expected[i], answers[i]);
	kfree(answers);

	question = "";
	answers = str_to_ints(question, strlen(question), &nr_integers);
	KUNIT_EXPECT_EQ(test, (ssize_t)0, nr_integers);
	kfree(answers);

	question = "\n";
	answers = str_to_ints(question, strlen(question), &nr_integers);
	KUNIT_EXPECT_EQ(test, (ssize_t)0, nr_integers);
	kfree(answers);
}

static void damon_dbgfs_test_set_targets(struct kunit *test)
{
	struct damon_ctx *ctx = dbgfs_new_ctx();
	char buf[64];

	if (!damon_is_registered_ops(DAMON_OPS_PADDR)) {
		dbgfs_destroy_ctx(ctx);
		kunit_skip(test, "PADDR not registered");
	}

	/* Make DAMON consider target has no pid */
	damon_select_ops(ctx, DAMON_OPS_PADDR);

	dbgfs_set_targets(ctx, 0, NULL);
	sprint_target_ids(ctx, buf, 64);
	KUNIT_EXPECT_STREQ(test, (char *)buf, "\n");

	dbgfs_set_targets(ctx, 1, NULL);
	sprint_target_ids(ctx, buf, 64);
	KUNIT_EXPECT_STREQ(test, (char *)buf, "42\n");

	dbgfs_set_targets(ctx, 0, NULL);
	sprint_target_ids(ctx, buf, 64);
	KUNIT_EXPECT_STREQ(test, (char *)buf, "\n");

	dbgfs_destroy_ctx(ctx);
}

static void damon_dbgfs_test_set_init_regions(struct kunit *test)
{
	struct damon_ctx *ctx = damon_new_ctx();
	/* Each line represents one region in ``<target idx> <start> <end>`` */
	char * const valid_inputs[] = {"1 10 20\n 1   20 30\n1 35 45",
		"1 10 20\n",
		"1 10 20\n0 39 59\n0 70 134\n  1  20 25\n",
		""};
	/* Reading the file again will show sorted, clean output */
	char * const valid_expects[] = {"1 10 20\n1 20 30\n1 35 45\n",
		"1 10 20\n",
		"0 39 59\n0 70 134\n1 10 20\n1 20 25\n",
		""};
	char * const invalid_inputs[] = {"3 10 20\n",	/* target not exists */
		"1 10 20\n 1 14 26\n",		/* regions overlap */
		"0 10 20\n1 30 40\n 0 5 8"};	/* not sorted by address */
	char *input, *expect;
	int i, rc;
	char buf[256];

	if (!damon_is_registered_ops(DAMON_OPS_PADDR)) {
		damon_destroy_ctx(ctx);
		kunit_skip(test, "PADDR not registered");
	}

	damon_select_ops(ctx, DAMON_OPS_PADDR);

	dbgfs_set_targets(ctx, 3, NULL);

	/* Put valid inputs and check the results */
	for (i = 0; i < ARRAY_SIZE(valid_inputs); i++) {
		input = valid_inputs[i];
		expect = valid_expects[i];

		rc = set_init_regions(ctx, input, strnlen(input, 256));
		KUNIT_EXPECT_EQ(test, rc, 0);

		memset(buf, 0, 256);
		sprint_init_regions(ctx, buf, 256);

		KUNIT_EXPECT_STREQ(test, (char *)buf, expect);
	}
	/* Put invalid inputs and check the return error code */
	for (i = 0; i < ARRAY_SIZE(invalid_inputs); i++) {
		input = invalid_inputs[i];
		pr_info("input: %s\n", input);
		rc = set_init_regions(ctx, input, strnlen(input, 256));
		KUNIT_EXPECT_EQ(test, rc, -EINVAL);

		memset(buf, 0, 256);
		sprint_init_regions(ctx, buf, 256);

		KUNIT_EXPECT_STREQ(test, (char *)buf, "");
	}

	dbgfs_set_targets(ctx, 0, NULL);
	damon_destroy_ctx(ctx);
}

static struct kunit_case damon_test_cases[] = {
	KUNIT_CASE(damon_dbgfs_test_str_to_ints),
	KUNIT_CASE(damon_dbgfs_test_set_targets),
	KUNIT_CASE(damon_dbgfs_test_set_init_regions),
	{},
};

static struct kunit_suite damon_test_suite = {
	.name = "damon-dbgfs",
	.test_cases = damon_test_cases,
};
kunit_test_suite(damon_test_suite);

#endif /* _DAMON_TEST_H */

#endif	/* CONFIG_DAMON_KUNIT_TEST */
