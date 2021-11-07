/* SPDX-License-Identifier: GPL-2.0 */
/*
 * DAMON Debugfs Interface Unit Tests
 *
 * Author: SeongJae Park <sjpark@amazon.de>
 */

#ifdef CONFIG_DAMON_DBGFS_KUNIT_TEST

#ifndef _DAMON_DBGFS_TEST_H
#define _DAMON_DBGFS_TEST_H

#include <kunit/test.h>

static void damon_dbgfs_test_str_to_target_ids(struct kunit *test)
{
	char *question;
	unsigned long *answers;
	unsigned long expected[] = {12, 35, 46};
	ssize_t nr_integers = 0, i;

	question = "123";
	answers = str_to_target_ids(question, strlen(question),
			&nr_integers);
	KUNIT_EXPECT_EQ(test, (ssize_t)1, nr_integers);
	KUNIT_EXPECT_EQ(test, 123ul, answers[0]);
	kfree(answers);

	question = "123abc";
	answers = str_to_target_ids(question, strlen(question),
			&nr_integers);
	KUNIT_EXPECT_EQ(test, (ssize_t)1, nr_integers);
	KUNIT_EXPECT_EQ(test, 123ul, answers[0]);
	kfree(answers);

	question = "a123";
	answers = str_to_target_ids(question, strlen(question),
			&nr_integers);
	KUNIT_EXPECT_EQ(test, (ssize_t)0, nr_integers);
	kfree(answers);

	question = "12 35";
	answers = str_to_target_ids(question, strlen(question),
			&nr_integers);
	KUNIT_EXPECT_EQ(test, (ssize_t)2, nr_integers);
	for (i = 0; i < nr_integers; i++)
		KUNIT_EXPECT_EQ(test, expected[i], answers[i]);
	kfree(answers);

	question = "12 35 46";
	answers = str_to_target_ids(question, strlen(question),
			&nr_integers);
	KUNIT_EXPECT_EQ(test, (ssize_t)3, nr_integers);
	for (i = 0; i < nr_integers; i++)
		KUNIT_EXPECT_EQ(test, expected[i], answers[i]);
	kfree(answers);

	question = "12 35 abc 46";
	answers = str_to_target_ids(question, strlen(question),
			&nr_integers);
	KUNIT_EXPECT_EQ(test, (ssize_t)2, nr_integers);
	for (i = 0; i < 2; i++)
		KUNIT_EXPECT_EQ(test, expected[i], answers[i]);
	kfree(answers);

	question = "";
	answers = str_to_target_ids(question, strlen(question),
			&nr_integers);
	KUNIT_EXPECT_EQ(test, (ssize_t)0, nr_integers);
	kfree(answers);

	question = "\n";
	answers = str_to_target_ids(question, strlen(question),
			&nr_integers);
	KUNIT_EXPECT_EQ(test, (ssize_t)0, nr_integers);
	kfree(answers);
}

static void damon_dbgfs_test_set_targets(struct kunit *test)
{
	struct damon_ctx *ctx = dbgfs_new_ctx();
	unsigned long ids[] = {1, 2, 3};
	char buf[64];

	/* Make DAMON consider target id as plain number */
	ctx->primitive.target_valid = NULL;
	ctx->primitive.cleanup = NULL;

	damon_set_targets(ctx, ids, 3);
	sprint_target_ids(ctx, buf, 64);
	KUNIT_EXPECT_STREQ(test, (char *)buf, "1 2 3\n");

	damon_set_targets(ctx, NULL, 0);
	sprint_target_ids(ctx, buf, 64);
	KUNIT_EXPECT_STREQ(test, (char *)buf, "\n");

	damon_set_targets(ctx, (unsigned long []){1, 2}, 2);
	sprint_target_ids(ctx, buf, 64);
	KUNIT_EXPECT_STREQ(test, (char *)buf, "1 2\n");

	damon_set_targets(ctx, (unsigned long []){2}, 1);
	sprint_target_ids(ctx, buf, 64);
	KUNIT_EXPECT_STREQ(test, (char *)buf, "2\n");

	damon_set_targets(ctx, NULL, 0);
	sprint_target_ids(ctx, buf, 64);
	KUNIT_EXPECT_STREQ(test, (char *)buf, "\n");

	dbgfs_destroy_ctx(ctx);
}

static struct kunit_case damon_test_cases[] = {
	KUNIT_CASE(damon_dbgfs_test_str_to_target_ids),
	KUNIT_CASE(damon_dbgfs_test_set_targets),
	{},
};

static struct kunit_suite damon_test_suite = {
	.name = "damon-dbgfs",
	.test_cases = damon_test_cases,
};
kunit_test_suite(damon_test_suite);

#endif /* _DAMON_TEST_H */

#endif	/* CONFIG_DAMON_KUNIT_TEST */
