// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit test for struct string_stream.
 *
 * Copyright (C) 2019, Google LLC.
 * Author: Brendan Higgins <brendanhiggins@google.com>
 */

#include <kunit/static_stub.h>
#include <kunit/test.h>
#include <linux/ktime.h>
#include <linux/slab.h>
#include <linux/timekeeping.h>

#include "string-stream.h"

struct string_stream_test_priv {
	/* For testing resource-managed free. */
	struct string_stream *expected_free_stream;
	bool stream_was_freed;
	bool stream_free_again;
};

/* Avoids a cast warning if kfree() is passed direct to kunit_add_action(). */
KUNIT_DEFINE_ACTION_WRAPPER(kfree_wrapper, kfree, const void *);

/* Avoids a cast warning if string_stream_destroy() is passed direct to kunit_add_action(). */
KUNIT_DEFINE_ACTION_WRAPPER(cleanup_raw_stream, string_stream_destroy, struct string_stream *);

static char *get_concatenated_string(struct kunit *test, struct string_stream *stream)
{
	char *str = string_stream_get_string(stream);

	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, str);
	kunit_add_action(test, kfree_wrapper, (void *)str);

	return str;
}

/* Managed string_stream object is initialized correctly. */
static void string_stream_managed_init_test(struct kunit *test)
{
	struct string_stream *stream;

	/* Resource-managed initialization. */
	stream = kunit_alloc_string_stream(test, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, stream);

	KUNIT_EXPECT_EQ(test, stream->length, 0);
	KUNIT_EXPECT_TRUE(test, list_empty(&stream->fragments));
	KUNIT_EXPECT_TRUE(test, (stream->gfp == GFP_KERNEL));
	KUNIT_EXPECT_FALSE(test, stream->append_newlines);
	KUNIT_EXPECT_TRUE(test, string_stream_is_empty(stream));
}

/* Unmanaged string_stream object is initialized correctly. */
static void string_stream_unmanaged_init_test(struct kunit *test)
{
	struct string_stream *stream;

	stream = alloc_string_stream(GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, stream);
	kunit_add_action(test, cleanup_raw_stream, stream);

	KUNIT_EXPECT_EQ(test, stream->length, 0);
	KUNIT_EXPECT_TRUE(test, list_empty(&stream->fragments));
	KUNIT_EXPECT_TRUE(test, (stream->gfp == GFP_KERNEL));
	KUNIT_EXPECT_FALSE(test, stream->append_newlines);

	KUNIT_EXPECT_TRUE(test, string_stream_is_empty(stream));
}

static void string_stream_destroy_stub(struct string_stream *stream)
{
	struct kunit *fake_test = kunit_get_current_test();
	struct string_stream_test_priv *priv = fake_test->priv;

	/* The kunit could own string_streams other than the one we are testing. */
	if (stream == priv->expected_free_stream) {
		if (priv->stream_was_freed)
			priv->stream_free_again = true;
		else
			priv->stream_was_freed = true;
	}

	/*
	 * Calling string_stream_destroy() will only call this function again
	 * because the redirection stub is still active.
	 * Avoid calling deactivate_static_stub() or changing current->kunit_test
	 * during cleanup.
	 */
	string_stream_clear(stream);
	kfree(stream);
}

/* kunit_free_string_stream() calls string_stream_desrtoy() */
static void string_stream_managed_free_test(struct kunit *test)
{
	struct string_stream_test_priv *priv = test->priv;

	priv->expected_free_stream = NULL;
	priv->stream_was_freed = false;
	priv->stream_free_again = false;

	kunit_activate_static_stub(test,
				   string_stream_destroy,
				   string_stream_destroy_stub);

	priv->expected_free_stream = kunit_alloc_string_stream(test, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, priv->expected_free_stream);

	/* This should call the stub function. */
	kunit_free_string_stream(test, priv->expected_free_stream);

	KUNIT_EXPECT_TRUE(test, priv->stream_was_freed);
	KUNIT_EXPECT_FALSE(test, priv->stream_free_again);
}

/* string_stream object is freed when test is cleaned up. */
static void string_stream_resource_free_test(struct kunit *test)
{
	struct string_stream_test_priv *priv = test->priv;
	struct kunit *fake_test;

	fake_test = kunit_kzalloc(test, sizeof(*fake_test), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, fake_test);

	kunit_init_test(fake_test, "string_stream_fake_test", NULL);
	fake_test->priv = priv;

	/*
	 * Activate stub before creating string_stream so the
	 * string_stream will be cleaned up first.
	 */
	priv->expected_free_stream = NULL;
	priv->stream_was_freed = false;
	priv->stream_free_again = false;

	kunit_activate_static_stub(fake_test,
				   string_stream_destroy,
				   string_stream_destroy_stub);

	priv->expected_free_stream = kunit_alloc_string_stream(fake_test, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, priv->expected_free_stream);

	/* Set current->kunit_test to fake_test so the static stub will be called. */
	current->kunit_test = fake_test;

	/* Cleanup test - the stub function should be called */
	kunit_cleanup(fake_test);

	/* Set current->kunit_test back to current test. */
	current->kunit_test = test;

	KUNIT_EXPECT_TRUE(test, priv->stream_was_freed);
	KUNIT_EXPECT_FALSE(test, priv->stream_free_again);
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

	stream = kunit_alloc_string_stream(test, GFP_KERNEL);
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

	stream = kunit_alloc_string_stream(test, GFP_KERNEL);
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

	stream_1 = kunit_alloc_string_stream(test, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, stream_1);

	stream_2 = kunit_alloc_string_stream(test, GFP_KERNEL);
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
	kunit_free_string_stream(test, stream_1);

	stream_1 = kunit_alloc_string_stream(test, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, stream_1);

	string_stream_append(stream_1, stream_2);
	KUNIT_EXPECT_STREQ(test, get_concatenated_string(test, stream_1), stream_2_content);
}

/* Appending the content of one string stream to one with auto-newlining. */
static void string_stream_append_auto_newline_test(struct kunit *test)
{
	struct string_stream *stream_1, *stream_2;

	/* Stream 1 has newline appending enabled */
	stream_1 = kunit_alloc_string_stream(test, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, stream_1);
	string_stream_set_append_newlines(stream_1, true);
	KUNIT_EXPECT_TRUE(test, stream_1->append_newlines);

	/* Stream 2 does not append newlines */
	stream_2 = kunit_alloc_string_stream(test, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, stream_2);

	/* Appending a stream with a newline should not add another newline */
	string_stream_add(stream_1, "Original string\n");
	string_stream_add(stream_2, "Appended content\n");
	string_stream_add(stream_2, "More stuff\n");
	string_stream_append(stream_1, stream_2);
	KUNIT_EXPECT_STREQ(test, get_concatenated_string(test, stream_1),
			   "Original string\nAppended content\nMore stuff\n");

	kunit_free_string_stream(test, stream_2);
	stream_2 = kunit_alloc_string_stream(test, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, stream_2);

	/*
	 * Appending a stream without newline should add a final newline.
	 * The appended string_stream is treated as a single string so newlines
	 * should not be inserted between fragments.
	 */
	string_stream_add(stream_2, "Another");
	string_stream_add(stream_2, "And again");
	string_stream_append(stream_1, stream_2);
	KUNIT_EXPECT_STREQ(test, get_concatenated_string(test, stream_1),
			   "Original string\nAppended content\nMore stuff\nAnotherAnd again\n");
}

/* Adding an empty string should not create a fragment. */
static void string_stream_append_empty_string_test(struct kunit *test)
{
	struct string_stream *stream;
	int original_frag_count;

	stream = kunit_alloc_string_stream(test, GFP_KERNEL);
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

/* Adding strings without automatic newline appending */
static void string_stream_no_auto_newline_test(struct kunit *test)
{
	struct string_stream *stream;

	stream = kunit_alloc_string_stream(test, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, stream);

	/*
	 * Add some strings with and without newlines. All formatted newlines
	 * should be preserved. It should not add any extra newlines.
	 */
	string_stream_add(stream, "One");
	string_stream_add(stream, "Two\n");
	string_stream_add(stream, "%s\n", "Three");
	string_stream_add(stream, "%s", "Four\n");
	string_stream_add(stream, "Five\n%s", "Six");
	string_stream_add(stream, "Seven\n\n");
	string_stream_add(stream, "Eight");
	KUNIT_EXPECT_STREQ(test, get_concatenated_string(test, stream),
			   "OneTwo\nThree\nFour\nFive\nSixSeven\n\nEight");
}

/* Adding strings with automatic newline appending */
static void string_stream_auto_newline_test(struct kunit *test)
{
	struct string_stream *stream;

	stream = kunit_alloc_string_stream(test, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, stream);

	string_stream_set_append_newlines(stream, true);
	KUNIT_EXPECT_TRUE(test, stream->append_newlines);

	/*
	 * Add some strings with and without newlines. Newlines should
	 * be appended to lines that do not end with \n, but newlines
	 * resulting from the formatting should not be changed.
	 */
	string_stream_add(stream, "One");
	string_stream_add(stream, "Two\n");
	string_stream_add(stream, "%s\n", "Three");
	string_stream_add(stream, "%s", "Four\n");
	string_stream_add(stream, "Five\n%s", "Six");
	string_stream_add(stream, "Seven\n\n");
	string_stream_add(stream, "Eight");
	KUNIT_EXPECT_STREQ(test, get_concatenated_string(test, stream),
			   "One\nTwo\nThree\nFour\nFive\nSix\nSeven\n\nEight\n");
}

/*
 * This doesn't actually "test" anything. It reports time taken
 * and memory used for logging a large number of lines.
 */
static void string_stream_performance_test(struct kunit *test)
{
	struct string_stream_fragment *frag_container;
	struct string_stream *stream;
	char test_line[101];
	ktime_t start_time, end_time;
	size_t len, bytes_requested, actual_bytes_used, total_string_length;
	int offset, i;

	stream = kunit_alloc_string_stream(test, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, stream);

	memset(test_line, 'x', sizeof(test_line) - 1);
	test_line[sizeof(test_line) - 1] = '\0';

	start_time = ktime_get();
	for (i = 0; i < 10000; i++) {
		offset = i % (sizeof(test_line) - 1);
		string_stream_add(stream, "%s: %d\n", &test_line[offset], i);
	}
	end_time = ktime_get();

	/*
	 * Calculate memory used. This doesn't include invisible
	 * overhead due to kernel allocator fragment size rounding.
	 */
	bytes_requested = sizeof(*stream);
	actual_bytes_used = ksize(stream);
	total_string_length = 0;

	list_for_each_entry(frag_container, &stream->fragments, node) {
		bytes_requested += sizeof(*frag_container);
		actual_bytes_used += ksize(frag_container);

		len = strlen(frag_container->fragment);
		total_string_length += len;
		bytes_requested += len + 1; /* +1 for '\0' */
		actual_bytes_used += ksize(frag_container->fragment);
	}

	kunit_info(test, "Time elapsed:           %lld us\n",
		   ktime_us_delta(end_time, start_time));
	kunit_info(test, "Total string length:    %zu\n", total_string_length);
	kunit_info(test, "Bytes requested:        %zu\n", bytes_requested);
	kunit_info(test, "Actual bytes allocated: %zu\n", actual_bytes_used);
}

static int string_stream_test_init(struct kunit *test)
{
	struct string_stream_test_priv *priv;

	priv = kunit_kzalloc(test, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	test->priv = priv;

	return 0;
}

static struct kunit_case string_stream_test_cases[] = {
	KUNIT_CASE(string_stream_managed_init_test),
	KUNIT_CASE(string_stream_unmanaged_init_test),
	KUNIT_CASE(string_stream_managed_free_test),
	KUNIT_CASE(string_stream_resource_free_test),
	KUNIT_CASE(string_stream_line_add_test),
	KUNIT_CASE(string_stream_variable_length_line_test),
	KUNIT_CASE(string_stream_append_test),
	KUNIT_CASE(string_stream_append_auto_newline_test),
	KUNIT_CASE(string_stream_append_empty_string_test),
	KUNIT_CASE(string_stream_no_auto_newline_test),
	KUNIT_CASE(string_stream_auto_newline_test),
	KUNIT_CASE(string_stream_performance_test),
	{}
};

static struct kunit_suite string_stream_test_suite = {
	.name = "string-stream-test",
	.test_cases = string_stream_test_cases,
	.init = string_stream_test_init,
};
kunit_test_suites(&string_stream_test_suite);
