// SPDX-License-Identifier: GPL-2.0
/*
 * Assertion and expectation serialization API.
 *
 * Copyright (C) 2019, Google LLC.
 * Author: Brendan Higgins <brendanhiggins@google.com>
 */
#include <kunit/assert.h>
#include <kunit/test.h>

#include "string-stream.h"

void kunit_assert_prologue(const struct kunit_loc *loc,
			   enum kunit_assert_type type,
			      struct string_stream *stream)
{
	const char *expect_or_assert = NULL;

	switch (type) {
	case KUNIT_EXPECTATION:
		expect_or_assert = "EXPECTATION";
		break;
	case KUNIT_ASSERTION:
		expect_or_assert = "ASSERTION";
		break;
	}

	string_stream_add(stream, "%s FAILED at %s:%d\n",
			  expect_or_assert, loc->file, loc->line);
}
EXPORT_SYMBOL_GPL(kunit_assert_prologue);

static void kunit_assert_print_msg(const struct va_format *message,
				   struct string_stream *stream)
{
	if (message->fmt)
		string_stream_add(stream, "\n%pV", message);
}

void kunit_fail_assert_format(const struct kunit_assert *assert,
			      const struct va_format *message,
			      struct string_stream *stream)
{
	string_stream_add(stream, "%pV", message);
}
EXPORT_SYMBOL_GPL(kunit_fail_assert_format);

void kunit_unary_assert_format(const struct kunit_assert *assert,
			       const struct va_format *message,
			       struct string_stream *stream)
{
	struct kunit_unary_assert *unary_assert;

	unary_assert = container_of(assert, struct kunit_unary_assert, assert);

	if (unary_assert->expected_true)
		string_stream_add(stream,
				  KUNIT_SUBTEST_INDENT "Expected %s to be true, but is false\n",
				  unary_assert->condition);
	else
		string_stream_add(stream,
				  KUNIT_SUBTEST_INDENT "Expected %s to be false, but is true\n",
				  unary_assert->condition);
	kunit_assert_print_msg(message, stream);
}
EXPORT_SYMBOL_GPL(kunit_unary_assert_format);

void kunit_ptr_not_err_assert_format(const struct kunit_assert *assert,
				     const struct va_format *message,
				     struct string_stream *stream)
{
	struct kunit_ptr_not_err_assert *ptr_assert;

	ptr_assert = container_of(assert, struct kunit_ptr_not_err_assert,
				  assert);

	if (!ptr_assert->value) {
		string_stream_add(stream,
				  KUNIT_SUBTEST_INDENT "Expected %s is not null, but is\n",
				  ptr_assert->text);
	} else if (IS_ERR(ptr_assert->value)) {
		string_stream_add(stream,
				  KUNIT_SUBTEST_INDENT "Expected %s is not error, but is: %ld\n",
				  ptr_assert->text,
				  PTR_ERR(ptr_assert->value));
	}
	kunit_assert_print_msg(message, stream);
}
EXPORT_SYMBOL_GPL(kunit_ptr_not_err_assert_format);

/* Checks if `text` is a literal representing `value`, e.g. "5" and 5 */
static bool is_literal(struct kunit *test, const char *text, long long value,
		       gfp_t gfp)
{
	char *buffer;
	int len;
	bool ret;

	len = snprintf(NULL, 0, "%lld", value);
	if (strlen(text) != len)
		return false;

	buffer = kunit_kmalloc(test, len+1, gfp);
	if (!buffer)
		return false;

	snprintf(buffer, len+1, "%lld", value);
	ret = strncmp(buffer, text, len) == 0;

	kunit_kfree(test, buffer);
	return ret;
}

void kunit_binary_assert_format(const struct kunit_assert *assert,
				const struct va_format *message,
				struct string_stream *stream)
{
	struct kunit_binary_assert *binary_assert;

	binary_assert = container_of(assert, struct kunit_binary_assert,
				     assert);

	string_stream_add(stream,
			  KUNIT_SUBTEST_INDENT "Expected %s %s %s, but\n",
			  binary_assert->text->left_text,
			  binary_assert->text->operation,
			  binary_assert->text->right_text);
	if (!is_literal(stream->test, binary_assert->text->left_text,
			binary_assert->left_value, stream->gfp))
		string_stream_add(stream, KUNIT_SUBSUBTEST_INDENT "%s == %lld\n",
				  binary_assert->text->left_text,
				  binary_assert->left_value);
	if (!is_literal(stream->test, binary_assert->text->right_text,
			binary_assert->right_value, stream->gfp))
		string_stream_add(stream, KUNIT_SUBSUBTEST_INDENT "%s == %lld",
				  binary_assert->text->right_text,
				  binary_assert->right_value);
	kunit_assert_print_msg(message, stream);
}
EXPORT_SYMBOL_GPL(kunit_binary_assert_format);

void kunit_binary_ptr_assert_format(const struct kunit_assert *assert,
				    const struct va_format *message,
				    struct string_stream *stream)
{
	struct kunit_binary_ptr_assert *binary_assert;

	binary_assert = container_of(assert, struct kunit_binary_ptr_assert,
				     assert);

	string_stream_add(stream,
			  KUNIT_SUBTEST_INDENT "Expected %s %s %s, but\n",
			  binary_assert->text->left_text,
			  binary_assert->text->operation,
			  binary_assert->text->right_text);
	string_stream_add(stream, KUNIT_SUBSUBTEST_INDENT "%s == %px\n",
			  binary_assert->text->left_text,
			  binary_assert->left_value);
	string_stream_add(stream, KUNIT_SUBSUBTEST_INDENT "%s == %px",
			  binary_assert->text->right_text,
			  binary_assert->right_value);
	kunit_assert_print_msg(message, stream);
}
EXPORT_SYMBOL_GPL(kunit_binary_ptr_assert_format);

/* Checks if KUNIT_EXPECT_STREQ() args were string literals.
 * Note: `text` will have ""s where as `value` will not.
 */
static bool is_str_literal(const char *text, const char *value)
{
	int len;

	len = strlen(text);
	if (len < 2)
		return false;
	if (text[0] != '\"' || text[len - 1] != '\"')
		return false;

	return strncmp(text + 1, value, len - 2) == 0;
}

void kunit_binary_str_assert_format(const struct kunit_assert *assert,
				    const struct va_format *message,
				    struct string_stream *stream)
{
	struct kunit_binary_str_assert *binary_assert;

	binary_assert = container_of(assert, struct kunit_binary_str_assert,
				     assert);

	string_stream_add(stream,
			  KUNIT_SUBTEST_INDENT "Expected %s %s %s, but\n",
			  binary_assert->text->left_text,
			  binary_assert->text->operation,
			  binary_assert->text->right_text);
	if (!is_str_literal(binary_assert->text->left_text, binary_assert->left_value))
		string_stream_add(stream, KUNIT_SUBSUBTEST_INDENT "%s == \"%s\"\n",
				  binary_assert->text->left_text,
				  binary_assert->left_value);
	if (!is_str_literal(binary_assert->text->right_text, binary_assert->right_value))
		string_stream_add(stream, KUNIT_SUBSUBTEST_INDENT "%s == \"%s\"",
				  binary_assert->text->right_text,
				  binary_assert->right_value);
	kunit_assert_print_msg(message, stream);
}
EXPORT_SYMBOL_GPL(kunit_binary_str_assert_format);
