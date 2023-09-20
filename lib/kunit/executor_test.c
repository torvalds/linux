// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit test for the KUnit executor.
 *
 * Copyright (C) 2021, Google LLC.
 * Author: Daniel Latypov <dlatypov@google.com>
 */

#include <kunit/test.h>

static void kfree_at_end(struct kunit *test, const void *to_free);
static struct kunit_suite *alloc_fake_suite(struct kunit *test,
					    const char *suite_name,
					    struct kunit_case *test_cases);

static void dummy_test(struct kunit *test) {}

static struct kunit_case dummy_test_cases[] = {
	/* .run_case is not important, just needs to be non-NULL */
	{ .name = "test1", .run_case = dummy_test },
	{ .name = "test2", .run_case = dummy_test },
	{},
};

static void parse_filter_test(struct kunit *test)
{
	struct kunit_test_filter filter = {NULL, NULL};

	kunit_parse_filter_glob(&filter, "suite");
	KUNIT_EXPECT_STREQ(test, filter.suite_glob, "suite");
	KUNIT_EXPECT_FALSE(test, filter.test_glob);
	kfree(filter.suite_glob);
	kfree(filter.test_glob);

	kunit_parse_filter_glob(&filter, "suite.test");
	KUNIT_EXPECT_STREQ(test, filter.suite_glob, "suite");
	KUNIT_EXPECT_STREQ(test, filter.test_glob, "test");
	kfree(filter.suite_glob);
	kfree(filter.test_glob);
}

static void filter_suites_test(struct kunit *test)
{
	struct kunit_suite *subsuite[3] = {NULL, NULL};
	struct suite_set suite_set = {.start = subsuite, .end = &subsuite[2]};
	struct suite_set got;
	int err = 0;

	subsuite[0] = alloc_fake_suite(test, "suite1", dummy_test_cases);
	subsuite[1] = alloc_fake_suite(test, "suite2", dummy_test_cases);

	/* Want: suite1, suite2, NULL -> suite2, NULL */
	got = kunit_filter_suites(&suite_set, "suite2", &err);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, got.start);
	KUNIT_ASSERT_EQ(test, err, 0);
	kfree_at_end(test, got.start);

	/* Validate we just have suite2 */
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, got.start[0]);
	KUNIT_EXPECT_STREQ(test, (const char *)got.start[0]->name, "suite2");

	/* Contains one element (end is 1 past end) */
	KUNIT_ASSERT_EQ(test, got.end - got.start, 1);
}

static void filter_suites_test_glob_test(struct kunit *test)
{
	struct kunit_suite *subsuite[3] = {NULL, NULL};
	struct suite_set suite_set = {.start = subsuite, .end = &subsuite[2]};
	struct suite_set got;
	int err = 0;

	subsuite[0] = alloc_fake_suite(test, "suite1", dummy_test_cases);
	subsuite[1] = alloc_fake_suite(test, "suite2", dummy_test_cases);

	/* Want: suite1, suite2, NULL -> suite2 (just test1), NULL */
	got = kunit_filter_suites(&suite_set, "suite2.test2", &err);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, got.start);
	KUNIT_ASSERT_EQ(test, err, 0);
	kfree_at_end(test, got.start);

	/* Validate we just have suite2 */
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, got.start[0]);
	KUNIT_EXPECT_STREQ(test, (const char *)got.start[0]->name, "suite2");
	KUNIT_ASSERT_EQ(test, got.end - got.start, 1);

	/* Now validate we just have test2 */
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, got.start[0]->test_cases);
	KUNIT_EXPECT_STREQ(test, (const char *)got.start[0]->test_cases[0].name, "test2");
	KUNIT_EXPECT_FALSE(test, got.start[0]->test_cases[1].name);
}

static void filter_suites_to_empty_test(struct kunit *test)
{
	struct kunit_suite *subsuite[3] = {NULL, NULL};
	struct suite_set suite_set = {.start = subsuite, .end = &subsuite[2]};
	struct suite_set got;
	int err = 0;

	subsuite[0] = alloc_fake_suite(test, "suite1", dummy_test_cases);
	subsuite[1] = alloc_fake_suite(test, "suite2", dummy_test_cases);

	got = kunit_filter_suites(&suite_set, "not_found", &err);
	KUNIT_ASSERT_EQ(test, err, 0);
	kfree_at_end(test, got.start); /* just in case */

	KUNIT_EXPECT_PTR_EQ_MSG(test, got.start, got.end,
				"should be empty to indicate no match");
}

static struct kunit_case executor_test_cases[] = {
	KUNIT_CASE(parse_filter_test),
	KUNIT_CASE(filter_suites_test),
	KUNIT_CASE(filter_suites_test_glob_test),
	KUNIT_CASE(filter_suites_to_empty_test),
	{}
};

static struct kunit_suite executor_test_suite = {
	.name = "kunit_executor_test",
	.test_cases = executor_test_cases,
};

kunit_test_suites(&executor_test_suite);

/* Test helpers */

/* Use the resource API to register a call to kfree(to_free).
 * Since we never actually use the resource, it's safe to use on const data.
 */
static void kfree_at_end(struct kunit *test, const void *to_free)
{
	/* kfree() handles NULL already, but avoid allocating a no-op cleanup. */
	if (IS_ERR_OR_NULL(to_free))
		return;

	kunit_add_action(test,
			(kunit_action_t *)kfree,
			(void *)to_free);
}

static struct kunit_suite *alloc_fake_suite(struct kunit *test,
					    const char *suite_name,
					    struct kunit_case *test_cases)
{
	struct kunit_suite *suite;

	/* We normally never expect to allocate suites, hence the non-const cast. */
	suite = kunit_kzalloc(test, sizeof(*suite), GFP_KERNEL);
	strncpy((char *)suite->name, suite_name, sizeof(suite->name) - 1);
	suite->test_cases = test_cases;

	return suite;
}
