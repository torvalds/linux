// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit test for the KUnit executor.
 *
 * Copyright (C) 2021, Google LLC.
 * Author: Daniel Latypov <dlatypov@google.com>
 */

#include <kunit/test.h>

static void kfree_at_end(struct kunit *test, const void *to_free);
static void free_subsuite_at_end(struct kunit *test,
				 struct kunit_suite *const *to_free);
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

static void filter_subsuite_test(struct kunit *test)
{
	struct kunit_suite *subsuite[3] = {NULL, NULL, NULL};
	struct kunit_suite * const *filtered;
	struct kunit_test_filter filter = {
		.suite_glob = "suite2",
		.test_glob = NULL,
	};

	subsuite[0] = alloc_fake_suite(test, "suite1", dummy_test_cases);
	subsuite[1] = alloc_fake_suite(test, "suite2", dummy_test_cases);

	/* Want: suite1, suite2, NULL -> suite2, NULL */
	filtered = kunit_filter_subsuite(subsuite, &filter);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, filtered);
	free_subsuite_at_end(test, filtered);

	/* Validate we just have suite2 */
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, filtered[0]);
	KUNIT_EXPECT_STREQ(test, (const char *)filtered[0]->name, "suite2");
	KUNIT_EXPECT_FALSE(test, filtered[1]);
}

static void filter_subsuite_test_glob_test(struct kunit *test)
{
	struct kunit_suite *subsuite[3] = {NULL, NULL, NULL};
	struct kunit_suite * const *filtered;
	struct kunit_test_filter filter = {
		.suite_glob = "suite2",
		.test_glob = "test2",
	};

	subsuite[0] = alloc_fake_suite(test, "suite1", dummy_test_cases);
	subsuite[1] = alloc_fake_suite(test, "suite2", dummy_test_cases);

	/* Want: suite1, suite2, NULL -> suite2 (just test1), NULL */
	filtered = kunit_filter_subsuite(subsuite, &filter);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, filtered);
	free_subsuite_at_end(test, filtered);

	/* Validate we just have suite2 */
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, filtered[0]);
	KUNIT_EXPECT_STREQ(test, (const char *)filtered[0]->name, "suite2");
	KUNIT_EXPECT_FALSE(test, filtered[1]);

	/* Now validate we just have test2 */
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, filtered[0]->test_cases);
	KUNIT_EXPECT_STREQ(test, (const char *)filtered[0]->test_cases[0].name, "test2");
	KUNIT_EXPECT_FALSE(test, filtered[0]->test_cases[1].name);
}

static void filter_subsuite_to_empty_test(struct kunit *test)
{
	struct kunit_suite *subsuite[3] = {NULL, NULL, NULL};
	struct kunit_suite * const *filtered;
	struct kunit_test_filter filter = {
		.suite_glob = "not_found",
		.test_glob = NULL,
	};

	subsuite[0] = alloc_fake_suite(test, "suite1", dummy_test_cases);
	subsuite[1] = alloc_fake_suite(test, "suite2", dummy_test_cases);

	filtered = kunit_filter_subsuite(subsuite, &filter);
	free_subsuite_at_end(test, filtered); /* just in case */

	KUNIT_EXPECT_FALSE_MSG(test, filtered,
			       "should be NULL to indicate no match");
}

static void kfree_subsuites_at_end(struct kunit *test, struct suite_set *suite_set)
{
	struct kunit_suite * const * const *suites;

	kfree_at_end(test, suite_set->start);
	for (suites = suite_set->start; suites < suite_set->end; suites++)
		free_subsuite_at_end(test, *suites);
}

static void filter_suites_test(struct kunit *test)
{
	/* Suites per-file are stored as a NULL terminated array */
	struct kunit_suite *subsuites[2][2] = {
		{NULL, NULL},
		{NULL, NULL},
	};
	/* Match the memory layout of suite_set */
	struct kunit_suite * const * const suites[2] = {
		subsuites[0], subsuites[1],
	};

	const struct suite_set suite_set = {
		.start = suites,
		.end = suites + 2,
	};
	struct suite_set filtered = {.start = NULL, .end = NULL};
	int err = 0;

	/* Emulate two files, each having one suite */
	subsuites[0][0] = alloc_fake_suite(test, "suite0", dummy_test_cases);
	subsuites[1][0] = alloc_fake_suite(test, "suite1", dummy_test_cases);

	/* Filter out suite1 */
	filtered = kunit_filter_suites(&suite_set, "suite0", &err);
	kfree_subsuites_at_end(test, &filtered); /* let us use ASSERTs without leaking */
	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_ASSERT_EQ(test, filtered.end - filtered.start, (ptrdiff_t)1);

	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, filtered.start);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, filtered.start[0]);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, filtered.start[0][0]);
	KUNIT_EXPECT_STREQ(test, (const char *)filtered.start[0][0]->name, "suite0");
}

static struct kunit_case executor_test_cases[] = {
	KUNIT_CASE(parse_filter_test),
	KUNIT_CASE(filter_subsuite_test),
	KUNIT_CASE(filter_subsuite_test_glob_test),
	KUNIT_CASE(filter_subsuite_to_empty_test),
	KUNIT_CASE(filter_suites_test),
	{}
};

static struct kunit_suite executor_test_suite = {
	.name = "kunit_executor_test",
	.test_cases = executor_test_cases,
};

kunit_test_suites(&executor_test_suite);

/* Test helpers */

static void kfree_res_free(struct kunit_resource *res)
{
	kfree(res->data);
}

/* Use the resource API to register a call to kfree(to_free).
 * Since we never actually use the resource, it's safe to use on const data.
 */
static void kfree_at_end(struct kunit *test, const void *to_free)
{
	/* kfree() handles NULL already, but avoid allocating a no-op cleanup. */
	if (IS_ERR_OR_NULL(to_free))
		return;
	kunit_alloc_resource(test, NULL, kfree_res_free, GFP_KERNEL,
			     (void *)to_free);
}

static void free_subsuite_res_free(struct kunit_resource *res)
{
	kunit_free_subsuite(res->data);
}

static void free_subsuite_at_end(struct kunit *test,
				 struct kunit_suite *const *to_free)
{
	if (IS_ERR_OR_NULL(to_free))
		return;
	kunit_alloc_resource(test, NULL, free_subsuite_res_free,
			     GFP_KERNEL, (void *)to_free);
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
