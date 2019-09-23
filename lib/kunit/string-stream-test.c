// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit test for struct string_stream.
 *
 * Copyright (C) 2019, Google LLC.
 * Author: Brendan Higgins <brendanhiggins@google.com>
 */

#include <kunit/string-stream.h>
#include <kunit/test.h>
#include <linux/slab.h>

static void string_stream_test_empty_on_creation(struct kunit *test)
{
	struct string_stream *stream = alloc_string_stream(test, GFP_KERNEL);

	KUNIT_EXPECT_TRUE(test, string_stream_is_empty(stream));
}

static void string_stream_test_not_empty_after_add(struct kunit *test)
{
	struct string_stream *stream = alloc_string_stream(test, GFP_KERNEL);

	string_stream_add(stream, "Foo");

	KUNIT_EXPECT_FALSE(test, string_stream_is_empty(stream));
}

static void string_stream_test_get_string(struct kunit *test)
{
	struct string_stream *stream = alloc_string_stream(test, GFP_KERNEL);
	char *output;

	string_stream_add(stream, "Foo");
	string_stream_add(stream, " %s", "bar");

	output = string_stream_get_string(stream);
	KUNIT_EXPECT_STREQ(test, output, "Foo bar");
}

static struct kunit_case string_stream_test_cases[] = {
	KUNIT_CASE(string_stream_test_empty_on_creation),
	KUNIT_CASE(string_stream_test_not_empty_after_add),
	KUNIT_CASE(string_stream_test_get_string),
	{}
};

static struct kunit_suite string_stream_test_suite = {
	.name = "string-stream-test",
	.test_cases = string_stream_test_cases
};
kunit_test_suite(string_stream_test_suite);
