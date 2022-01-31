/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Assertion and expectation serialization API.
 *
 * Copyright (C) 2019, Google LLC.
 * Author: Brendan Higgins <brendanhiggins@google.com>
 */

#ifndef _KUNIT_ASSERT_H
#define _KUNIT_ASSERT_H

#include <linux/err.h>
#include <linux/printk.h>

struct kunit;
struct string_stream;

/**
 * enum kunit_assert_type - Type of expectation/assertion.
 * @KUNIT_ASSERTION: Used to denote that a kunit_assert represents an assertion.
 * @KUNIT_EXPECTATION: Denotes that a kunit_assert represents an expectation.
 *
 * Used in conjunction with a &struct kunit_assert to denote whether it
 * represents an expectation or an assertion.
 */
enum kunit_assert_type {
	KUNIT_ASSERTION,
	KUNIT_EXPECTATION,
};

/**
 * struct kunit_assert - Data for printing a failed assertion or expectation.
 * @test: the test case this expectation/assertion is associated with.
 * @type: the type (either an expectation or an assertion) of this kunit_assert.
 * @line: the source code line number that the expectation/assertion is at.
 * @file: the file path of the source file that the expectation/assertion is in.
 * @message: an optional message to provide additional context.
 * @format: a function which formats the data in this kunit_assert to a string.
 *
 * Represents a failed expectation/assertion. Contains all the data necessary to
 * format a string to a user reporting the failure.
 */
struct kunit_assert {
	struct kunit *test;
	enum kunit_assert_type type;
	int line;
	const char *file;
	struct va_format message;
	void (*format)(const struct kunit_assert *assert,
		       struct string_stream *stream);
};

/**
 * KUNIT_INIT_VA_FMT_NULL - Default initializer for struct va_format.
 *
 * Used inside a struct initialization block to initialize struct va_format to
 * default values where fmt and va are null.
 */
#define KUNIT_INIT_VA_FMT_NULL { .fmt = NULL, .va = NULL }

/**
 * KUNIT_INIT_ASSERT_STRUCT() - Initializer for a &struct kunit_assert.
 * @kunit: The test case that this expectation/assertion is associated with.
 * @assert_type: The type (assertion or expectation) of this kunit_assert.
 * @fmt: The formatting function which builds a string out of this kunit_assert.
 *
 * The base initializer for a &struct kunit_assert.
 */
#define KUNIT_INIT_ASSERT_STRUCT(kunit, assert_type, fmt) {		       \
	.test = kunit,							       \
	.type = assert_type,						       \
	.file = __FILE__,						       \
	.line = __LINE__,						       \
	.message = KUNIT_INIT_VA_FMT_NULL,				       \
	.format = fmt							       \
}

void kunit_base_assert_format(const struct kunit_assert *assert,
			      struct string_stream *stream);

void kunit_assert_print_msg(const struct kunit_assert *assert,
			    struct string_stream *stream);

/**
 * struct kunit_fail_assert - Represents a plain fail expectation/assertion.
 * @assert: The parent of this type.
 *
 * Represents a simple KUNIT_FAIL/KUNIT_ASSERT_FAILURE that always fails.
 */
struct kunit_fail_assert {
	struct kunit_assert assert;
};

void kunit_fail_assert_format(const struct kunit_assert *assert,
			      struct string_stream *stream);

/**
 * KUNIT_INIT_FAIL_ASSERT_STRUCT() - Initializer for &struct kunit_fail_assert.
 * @test: The test case that this expectation/assertion is associated with.
 * @type: The type (assertion or expectation) of this kunit_assert.
 *
 * Initializes a &struct kunit_fail_assert. Intended to be used in
 * KUNIT_EXPECT_* and KUNIT_ASSERT_* macros.
 */
#define KUNIT_INIT_FAIL_ASSERT_STRUCT(test, type) {			       \
	.assert = KUNIT_INIT_ASSERT_STRUCT(test,			       \
					   type,			       \
					   kunit_fail_assert_format)	       \
}

/**
 * struct kunit_unary_assert - Represents a KUNIT_{EXPECT|ASSERT}_{TRUE|FALSE}
 * @assert: The parent of this type.
 * @condition: A string representation of a conditional expression.
 * @expected_true: True if of type KUNIT_{EXPECT|ASSERT}_TRUE, false otherwise.
 *
 * Represents a simple expectation or assertion that simply asserts something is
 * true or false. In other words, represents the expectations:
 * KUNIT_{EXPECT|ASSERT}_{TRUE|FALSE}
 */
struct kunit_unary_assert {
	struct kunit_assert assert;
	const char *condition;
	bool expected_true;
};

void kunit_unary_assert_format(const struct kunit_assert *assert,
			       struct string_stream *stream);

/**
 * KUNIT_INIT_UNARY_ASSERT_STRUCT() - Initializes &struct kunit_unary_assert.
 * @test: The test case that this expectation/assertion is associated with.
 * @type: The type (assertion or expectation) of this kunit_assert.
 * @cond: A string representation of the expression asserted true or false.
 * @expect_true: True if of type KUNIT_{EXPECT|ASSERT}_TRUE, false otherwise.
 *
 * Initializes a &struct kunit_unary_assert. Intended to be used in
 * KUNIT_EXPECT_* and KUNIT_ASSERT_* macros.
 */
#define KUNIT_INIT_UNARY_ASSERT_STRUCT(test, type, cond, expect_true) {	       \
	.assert = KUNIT_INIT_ASSERT_STRUCT(test,			       \
					   type,			       \
					   kunit_unary_assert_format),	       \
	.condition = cond,						       \
	.expected_true = expect_true					       \
}

/**
 * struct kunit_ptr_not_err_assert - An expectation/assertion that a pointer is
 *	not NULL and not a -errno.
 * @assert: The parent of this type.
 * @text: A string representation of the expression passed to the expectation.
 * @value: The actual evaluated pointer value of the expression.
 *
 * Represents an expectation/assertion that a pointer is not null and is does
 * not contain a -errno. (See IS_ERR_OR_NULL().)
 */
struct kunit_ptr_not_err_assert {
	struct kunit_assert assert;
	const char *text;
	const void *value;
};

void kunit_ptr_not_err_assert_format(const struct kunit_assert *assert,
				     struct string_stream *stream);

/**
 * KUNIT_INIT_PTR_NOT_ERR_ASSERT_STRUCT() - Initializes a
 *	&struct kunit_ptr_not_err_assert.
 * @test: The test case that this expectation/assertion is associated with.
 * @type: The type (assertion or expectation) of this kunit_assert.
 * @txt: A string representation of the expression passed to the expectation.
 * @val: The actual evaluated pointer value of the expression.
 *
 * Initializes a &struct kunit_ptr_not_err_assert. Intended to be used in
 * KUNIT_EXPECT_* and KUNIT_ASSERT_* macros.
 */
#define KUNIT_INIT_PTR_NOT_ERR_STRUCT(test, type, txt, val) {		       \
	.assert = KUNIT_INIT_ASSERT_STRUCT(test,			       \
					   type,			       \
					   kunit_ptr_not_err_assert_format),   \
	.text = txt,							       \
	.value = val							       \
}

/**
 * struct kunit_binary_assert - An expectation/assertion that compares two
 *	non-pointer values (for example, KUNIT_EXPECT_EQ(test, 1 + 1, 2)).
 * @assert: The parent of this type.
 * @operation: A string representation of the comparison operator (e.g. "==").
 * @left_text: A string representation of the expression in the left slot.
 * @left_value: The actual evaluated value of the expression in the left slot.
 * @right_text: A string representation of the expression in the right slot.
 * @right_value: The actual evaluated value of the expression in the right slot.
 *
 * Represents an expectation/assertion that compares two non-pointer values. For
 * example, to expect that 1 + 1 == 2, you can use the expectation
 * KUNIT_EXPECT_EQ(test, 1 + 1, 2);
 */
struct kunit_binary_assert {
	struct kunit_assert assert;
	const char *operation;
	const char *left_text;
	long long left_value;
	const char *right_text;
	long long right_value;
};

void kunit_binary_assert_format(const struct kunit_assert *assert,
				struct string_stream *stream);

/**
 * KUNIT_INIT_BINARY_ASSERT_STRUCT() - Initializes a
 *	&struct kunit_binary_assert.
 * @test: The test case that this expectation/assertion is associated with.
 * @type: The type (assertion or expectation) of this kunit_assert.
 * @op_str: A string representation of the comparison operator (e.g. "==").
 * @left_str: A string representation of the expression in the left slot.
 * @left_val: The actual evaluated value of the expression in the left slot.
 * @right_str: A string representation of the expression in the right slot.
 * @right_val: The actual evaluated value of the expression in the right slot.
 *
 * Initializes a &struct kunit_binary_assert. Intended to be used in
 * KUNIT_EXPECT_* and KUNIT_ASSERT_* macros.
 */
#define KUNIT_INIT_BINARY_ASSERT_STRUCT(test,				       \
					type,				       \
					op_str,				       \
					left_str,			       \
					left_val,			       \
					right_str,			       \
					right_val) {			       \
	.assert = KUNIT_INIT_ASSERT_STRUCT(test,			       \
					   type,			       \
					   kunit_binary_assert_format),	       \
	.operation = op_str,						       \
	.left_text = left_str,						       \
	.left_value = left_val,						       \
	.right_text = right_str,					       \
	.right_value = right_val					       \
}

/**
 * struct kunit_binary_ptr_assert - An expectation/assertion that compares two
 *	pointer values (for example, KUNIT_EXPECT_PTR_EQ(test, foo, bar)).
 * @assert: The parent of this type.
 * @operation: A string representation of the comparison operator (e.g. "==").
 * @left_text: A string representation of the expression in the left slot.
 * @left_value: The actual evaluated value of the expression in the left slot.
 * @right_text: A string representation of the expression in the right slot.
 * @right_value: The actual evaluated value of the expression in the right slot.
 *
 * Represents an expectation/assertion that compares two pointer values. For
 * example, to expect that foo and bar point to the same thing, you can use the
 * expectation KUNIT_EXPECT_PTR_EQ(test, foo, bar);
 */
struct kunit_binary_ptr_assert {
	struct kunit_assert assert;
	const char *operation;
	const char *left_text;
	const void *left_value;
	const char *right_text;
	const void *right_value;
};

void kunit_binary_ptr_assert_format(const struct kunit_assert *assert,
				    struct string_stream *stream);

/**
 * KUNIT_INIT_BINARY_PTR_ASSERT_STRUCT() - Initializes a
 *	&struct kunit_binary_ptr_assert.
 * @test: The test case that this expectation/assertion is associated with.
 * @type: The type (assertion or expectation) of this kunit_assert.
 * @op_str: A string representation of the comparison operator (e.g. "==").
 * @left_str: A string representation of the expression in the left slot.
 * @left_val: The actual evaluated value of the expression in the left slot.
 * @right_str: A string representation of the expression in the right slot.
 * @right_val: The actual evaluated value of the expression in the right slot.
 *
 * Initializes a &struct kunit_binary_ptr_assert. Intended to be used in
 * KUNIT_EXPECT_* and KUNIT_ASSERT_* macros.
 */
#define KUNIT_INIT_BINARY_PTR_ASSERT_STRUCT(test,			       \
					    type,			       \
					    op_str,			       \
					    left_str,			       \
					    left_val,			       \
					    right_str,			       \
					    right_val) {		       \
	.assert = KUNIT_INIT_ASSERT_STRUCT(test,			       \
					   type,			       \
					   kunit_binary_ptr_assert_format),    \
	.operation = op_str,						       \
	.left_text = left_str,						       \
	.left_value = left_val,						       \
	.right_text = right_str,					       \
	.right_value = right_val					       \
}

/**
 * struct kunit_binary_str_assert - An expectation/assertion that compares two
 *	string values (for example, KUNIT_EXPECT_STREQ(test, foo, "bar")).
 * @assert: The parent of this type.
 * @operation: A string representation of the comparison operator (e.g. "==").
 * @left_text: A string representation of the expression in the left slot.
 * @left_value: The actual evaluated value of the expression in the left slot.
 * @right_text: A string representation of the expression in the right slot.
 * @right_value: The actual evaluated value of the expression in the right slot.
 *
 * Represents an expectation/assertion that compares two string values. For
 * example, to expect that the string in foo is equal to "bar", you can use the
 * expectation KUNIT_EXPECT_STREQ(test, foo, "bar");
 */
struct kunit_binary_str_assert {
	struct kunit_assert assert;
	const char *operation;
	const char *left_text;
	const char *left_value;
	const char *right_text;
	const char *right_value;
};

void kunit_binary_str_assert_format(const struct kunit_assert *assert,
				    struct string_stream *stream);

/**
 * KUNIT_INIT_BINARY_STR_ASSERT_STRUCT() - Initializes a
 *	&struct kunit_binary_str_assert.
 * @test: The test case that this expectation/assertion is associated with.
 * @type: The type (assertion or expectation) of this kunit_assert.
 * @op_str: A string representation of the comparison operator (e.g. "==").
 * @left_str: A string representation of the expression in the left slot.
 * @left_val: The actual evaluated value of the expression in the left slot.
 * @right_str: A string representation of the expression in the right slot.
 * @right_val: The actual evaluated value of the expression in the right slot.
 *
 * Initializes a &struct kunit_binary_str_assert. Intended to be used in
 * KUNIT_EXPECT_* and KUNIT_ASSERT_* macros.
 */
#define KUNIT_INIT_BINARY_STR_ASSERT_STRUCT(test,			       \
					    type,			       \
					    op_str,			       \
					    left_str,			       \
					    left_val,			       \
					    right_str,			       \
					    right_val) {		       \
	.assert = KUNIT_INIT_ASSERT_STRUCT(test,			       \
					   type,			       \
					   kunit_binary_str_assert_format),    \
	.operation = op_str,						       \
	.left_text = left_str,						       \
	.left_value = left_val,						       \
	.right_text = right_str,					       \
	.right_value = right_val					       \
}

#endif /*  _KUNIT_ASSERT_H */
