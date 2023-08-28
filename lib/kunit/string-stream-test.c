// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit test for struct string_stream.
 *
 * Copyright (C) 2019, Google LLC.
 * Author: Brendan Higgins <brendanhiggins@google.com>
 */

#include <kunit/test.h>
#include <linux/slab.h>

#include "string-stream.h"

static char *get_concatenated_string(struct kunit *test, struct string_stream *stream)
{
	char *str = string_stream_get_string(stream);

	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, str);

	return str;
}

/* string_stream object is initialized correctly. */
static void string_stream_init_test(struct kunit *test)
{
	struct string_stream *stream;

	stream = alloc_string_stream(test, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, stream);

	KUNIT_EXPECT_EQ(test, stream->length, 0);
	KUNIT_EXPECT_TRUE(test, list_empty(&stream->fragments));
	KUNIT_EXPECT_PTR_EQ(test, stream->test, test);
	KUNIT_EXPECT_TRUE(test, (stream->gfp == GFP_KERNEL));
	KUNIT_EXPECT_TRUE(test, string_stream_is_empty(stream));
}

/*
 * Add a series of lines to a string_stream. Check that all lines
 * appear in the correct order and no characters are dropped.
 */
static void string_stream_line_add_test(struct kunit *test)
{
	struct string_stream *stream;
	char line[60];
	char *concat_string, *pos, *string_end;
	size_t len, total_len;
	int num_lines, i;

	stream = alloc_string_stream(test, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, stream);

	/* Add series of sequence numbered lines */
	total_len = 0;
	for (i = 0; i < 100; ++i) {
		len = snprintf(line, sizeof(line),
			"The quick brown fox jumps over the lazy penguin %d\n", i);

		/* Sanity-check that our test string isn't truncated */
		KUNIT_ASSERT_LT(test, len, sizeof(line));

		string_stream_add(stream, line);
		total_len += len;
	}
	num_lines = i;

	concat_string = get_concatenated_string(test, stream);
	KUNIT_EXPECT_NOT_ERR_OR_NULL(test, concat_string);
	KUNIT_EXPECT_EQ(test, strlen(concat_string), total_len);

	/*
	 * Split the concatenated string at the newlines and check that
	 * all the original added strings are present.
	 */
	pos = concat_string;
	for (i = 0; i < num_lines; ++i) {
		string_end = strchr(pos, '\n');
		KUNIT_EXPECT_NOT_NULL(test, string_end);

		/* Convert to NULL-terminated string */
		*string_end = '\0';

		snprintf(line, sizeof(line),
			 "The quick brown fox jumps over the lazy penguin %d", i);
		KUNIT_EXPECT_STREQ(test, pos, line);

		pos = string_end + 1;
	}

	/* There shouldn't be any more data after this */
	KUNIT_EXPECT_EQ(test, strlen(pos), 0);
}

/* Add a series of lines of variable length to a string_stream. */
static void string_stream_variable_length_line_test(struct kunit *test)
{
	static const char line[] =
		"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"
		" 0123456789!$%^&*()_-+={}[]:;@'~#<>,.?/|";
	struct string_stream *stream;
	struct rnd_state rnd;
	char *concat_string, *pos, *string_end;
	size_t offset, total_len;
	int num_lines, i;

	stream = alloc_string_stream(test, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, stream);

	/*
	 * Log many lines of varying lengths until we have created
	 * many fragments.
	 * The "randomness" must be repeatable.
	 */
	prandom_seed_state(&rnd, 3141592653589793238ULL);
	total_len = 0;
	for (i = 0; i < 100; ++i) {
		offset = prandom_u32_state(&rnd) % (sizeof(line) - 1);
		string_stream_add(stream, "%s\n", &line[offset]);
		total_len += sizeof(line) - offset;
	}
	num_lines = i;

	concat_string = get_concatenated_string(test, stream);
	KUNIT_EXPECT_NOT_ERR_OR_NULL(test, concat_string);
	KUNIT_EXPECT_EQ(test, strlen(concat_string), total_len);

	/*
	 * Split the concatenated string at the newlines and check that
	 * all the original added strings are present.
	 */
	prandom_seed_state(&rnd, 3141592653589793238ULL);
	pos = concat_string;
	for (i = 0; i < num_lines; ++i) {
		string_end = strchr(pos, '\n');
		KUNIT_EXPECT_NOT_NULL(test, string_end);

		/* Convert to NULL-terminated string */
		*string_end = '\0';

		offset = prandom_u32_state(&rnd) % (sizeof(line) - 1);
		KUNIT_EXPECT_STREQ(test, pos, &line[offset]);

		pos = string_end + 1;
	}

	/* There shouldn't be any more data after this */
	KUNIT_EXPECT_EQ(test, strlen(pos), 0);
}

/* Appending the content of one string stream to another. */
static void string_stream_append_test(struct kunit *test)
{
	static const char * const strings_1[] = {
		"one", "two", "three", "four", "five", "six",
		"seven", "eight", "nine", "ten",
	};
	static const char * const strings_2[] = {
		"Apple", "Pear", "Orange", "Banana", "Grape", "Apricot",
	};
	struct string_stream *stream_1, *stream_2;
	const char *stream1_content_before_append, *stream_2_content;
	char *combined_content;
	size_t combined_length;
	int i;

	stream_1 = alloc_string_stream(test, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, stream_1);

	stream_2 = alloc_string_stream(test, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, stream_2);

	/* Append content of empty stream to empty stream */
	string_stream_append(stream_1, stream_2);
	KUNIT_EXPECT_EQ(test, strlen(get_concatenated_string(test, stream_1)), 0);

	/* Add some data to stream_1 */
	for (i = 0; i < ARRAY_SIZE(strings_1); ++i)
		string_stream_add(stream_1, "%s\n", strings_1[i]);

	stream1_content_before_append = get_concatenated_string(test, stream_1);

	/* Append content of empty stream to non-empty stream */
	string_stream_append(stream_1, stream_2);
	KUNIT_EXPECT_STREQ(test, get_concatenated_string(test, stream_1),
			   stream1_content_before_append);

	/* Add some data to stream_2 */
	for (i = 0; i < ARRAY_SIZE(strings_2); ++i)
		string_stream_add(stream_2, "%s\n", strings_2[i]);

	/* Append content of non-empty stream to non-empty stream */
	string_stream_append(stream_1, stream_2);

	/*
	 * End result should be the original content of stream_1 plus
	 * the content of stream_2.
	 */
	stream_2_content = get_concatenated_string(test, stream_2);
	combined_length = strlen(stream1_content_before_append) + strlen(stream_2_content);
	combined_length++; /* for terminating \0 */
	combined_content = kunit_kmalloc(test, combined_length, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, combined_content);
	snprintf(combined_content, combined_length, "%s%s",
		 stream1_content_before_append, stream_2_content);

	KUNIT_EXPECT_STREQ(test, get_concatenated_string(test, stream_1), combined_content);

	/* Append content of non-empty stream to empty stream */
	string_stream_destroy(stream_1);

	stream_1 = alloc_string_stream(test, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, stream_1);

	string_stream_append(stream_1, stream_2);
	KUNIT_EXPECT_STREQ(test, get_concatenated_string(test, stream_1), stream_2_content);
}

/* Adding an empty string should not create a fragment. */
static void string_stream_append_empty_string_test(struct kunit *test)
{
	struct string_stream *stream;
	int original_frag_count;

	stream = alloc_string_stream(test, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, stream);

	/* Formatted empty string */
	string_stream_add(stream, "%s", "");
	KUNIT_EXPECT_TRUE(test, string_stream_is_empty(stream));
	KUNIT_EXPECT_TRUE(test, list_empty(&stream->fragments));

	/* Adding an empty string to a non-empty stream */
	string_stream_add(stream, "Add this line");
	original_frag_count = list_count_nodes(&stream->fragments);

	string_stream_add(stream, "%s", "");
	KUNIT_EXPECT_EQ(test, list_count_nodes(&stream->fragments), original_frag_count);
	KUNIT_EXPECT_STREQ(test, get_concatenated_string(test, stream), "Add this line");
}

static struct kunit_case string_stream_test_cases[] = {
	KUNIT_CASE(string_stream_init_test),
	KUNIT_CASE(string_stream_line_add_test),
	KUNIT_CASE(string_stream_variable_length_line_test),
	KUNIT_CASE(string_stream_append_test),
	KUNIT_CASE(string_stream_append_empty_string_test),
	{}
};

static struct kunit_suite string_stream_test_suite = {
	.name = "string-stream-test",
	.test_cases = string_stream_test_cases
};
kunit_test_suites(&string_stream_test_suite);
