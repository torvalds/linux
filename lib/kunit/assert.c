// SPDX-License-Identifier: GPL-2.0
/*
 * Assertion and expectation serialization API.
 *
 * Copyright (C) 2019, Google LLC.
 * Author: Brendan Higgins <brendanhiggins@google.com>
 */
#include <kunit/assert.h>

#include "string-stream.h"

void kunit_base_assert_format(const struct kunit_assert *assert,
			      struct string_stream *stream)
{
	const char *expect_or_assert = NULL;

	switch (assert->type) {
	case KUNIT_EXPECTATION:
		expect_or_assert = "EXPECTATION";
		break;
	case KUNIT_ASSERTION:
		expect_or_assert = "ASSERTION";
		break;
	}

	string_stream_add(stream, "%s FAILED at %s:%d\n",
			 expect_or_assert, assert->file, assert->line);
}
EXPORT_SYMBOL_GPL(kunit_base_assert_format);

void kunit_assert_print_msg(const struct kunit_assert *assert,
			    struct string_stream *stream)
{
	if (assert->message.fmt)
		string_stream_add(stream, "\n%pV", &assert->message);
}
EXPORT_SYMBOL_GPL(kunit_assert_print_msg);

void kunit_fail_assert_format(const struct kunit_assert *assert,
			      struct string_stream *stream)
{
	kunit_base_assert_format(assert, stream);
	string_stream_add(stream, "%pV", &assert->message);
}
EXPORT_SYMBOL_GPL(kunit_fail_assert_format);

void kunit_unary_assert_format(const struct kunit_assert *assert,
			       struct string_stream *stream)
{
	struct kunit_unary_assert *unary_assert = container_of(
			assert, struct kunit_unary_assert, assert);

	kunit_base_assert_format(assert, stream);
	if (unary_assert->expected_true)
		string_stream_add(stream,
				 "\tExpected %s to be true, but is false\n",
				 unary_assert->condition);
	else
		string_stream_add(stream,
				 "\tExpected %s to be false, but is true\n",
				 unary_assert->condition);
	kunit_assert_print_msg(assert, stream);
}
EXPORT_SYMBOL_GPL(kunit_unary_assert_format);

void kunit_ptr_not_err_assert_format(const struct kunit_assert *assert,
				     struct string_stream *stream)
{
	struct kunit_ptr_not_err_assert *ptr_assert = container_of(
			assert, struct kunit_ptr_not_err_assert, assert);

	kunit_base_assert_format(assert, stream);
	if (!ptr_assert->value) {
		string_stream_add(stream,
				 "\tExpected %s is not null, but is\n",
				 ptr_assert->text);
	} else if (IS_ERR(ptr_assert->value)) {
		string_stream_add(stream,
				 "\tExpected %s is not error, but is: %ld\n",
				 ptr_assert->text,
				 PTR_ERR(ptr_assert->value));
	}
	kunit_assert_print_msg(assert, stream);
}
EXPORT_SYMBOL_GPL(kunit_ptr_not_err_assert_format);

void kunit_binary_assert_format(const struct kunit_assert *assert,
				struct string_stream *stream)
{
	struct kunit_binary_assert *binary_assert = container_of(
			assert, struct kunit_binary_assert, assert);

	kunit_base_assert_format(assert, stream);
	string_stream_add(stream,
			 "\tExpected %s %s %s, but\n",
			 binary_assert->left_text,
			 binary_assert->operation,
			 binary_assert->right_text);
	string_stream_add(stream, "\t\t%s == %lld\n",
			 binary_assert->left_text,
			 binary_assert->left_value);
	string_stream_add(stream, "\t\t%s == %lld",
			 binary_assert->right_text,
			 binary_assert->right_value);
	kunit_assert_print_msg(assert, stream);
}
EXPORT_SYMBOL_GPL(kunit_binary_assert_format);

void kunit_binary_ptr_assert_format(const struct kunit_assert *assert,
				    struct string_stream *stream)
{
	struct kunit_binary_ptr_assert *binary_assert = container_of(
			assert, struct kunit_binary_ptr_assert, assert);

	kunit_base_assert_format(assert, stream);
	string_stream_add(stream,
			 "\tExpected %s %s %s, but\n",
			 binary_assert->left_text,
			 binary_assert->operation,
			 binary_assert->right_text);
	string_stream_add(stream, "\t\t%s == %pK\n",
			 binary_assert->left_text,
			 binary_assert->left_value);
	string_stream_add(stream, "\t\t%s == %pK",
			 binary_assert->right_text,
			 binary_assert->right_value);
	kunit_assert_print_msg(assert, stream);
}
EXPORT_SYMBOL_GPL(kunit_binary_ptr_assert_format);

void kunit_binary_str_assert_format(const struct kunit_assert *assert,
				    struct string_stream *stream)
{
	struct kunit_binary_str_assert *binary_assert = container_of(
			assert, struct kunit_binary_str_assert, assert);

	kunit_base_assert_format(assert, stream);
	string_stream_add(stream,
			 "\tExpected %s %s %s, but\n",
			 binary_assert->left_text,
			 binary_assert->operation,
			 binary_assert->right_text);
	string_stream_add(stream, "\t\t%s == %s\n",
			 binary_assert->left_text,
			 binary_assert->left_value);
	string_stream_add(stream, "\t\t%s == %s",
			 binary_assert->right_text,
			 binary_assert->right_value);
	kunit_assert_print_msg(assert, stream);
}
EXPORT_SYMBOL_GPL(kunit_binary_str_assert_format);
