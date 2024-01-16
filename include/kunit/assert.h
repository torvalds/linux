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
 * struct kunit_loc - Identifies the source location of a line of code.
 * @line: the line number in the file.
 * @file: the file name.
 */
struct kunit_loc {
	int line;
	const char *file;
};

#define KUNIT_CURRENT_LOC { .file = __FILE__, .line = __LINE__ }

/**
 * struct kunit_assert - Data for printing a failed assertion or expectation.
 *
 * Represents a failed expectation/assertion. Contains all the data necessary to
 * format a string to a user reporting the failure.
 */
struct kunit_assert {};

typedef void (*assert_format_t)(const struct kunit_assert *assert,
				const struct va_format *message,
				struct string_stream *stream);

void kunit_assert_prologue(const struct kunit_loc *loc,
			   enum kunit_assert_type type,
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
			      const struct va_format *message,
			      struct string_stream *stream);

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
			       const struct va_format *message,
			       struct string_stream *stream);

/**
 * KUNIT_INIT_UNARY_ASSERT_STRUCT() - Initializes &struct kunit_unary_assert.
 * @cond: A string representation of the expression asserted true or false.
 * @expect_true: True if of type KUNIT_{EXPECT|ASSERT}_TRUE, false otherwise.
 *
 * Initializes a &struct kunit_unary_assert. Intended to be used in
 * KUNIT_EXPECT_* and KUNIT_ASSERT_* macros.
 */
#define KUNIT_INIT_UNARY_ASSERT_STRUCT(cond, expect_true) {		       \
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
				     const struct va_format *message,
				     struct string_stream *stream);

/**
 * KUNIT_INIT_PTR_NOT_ERR_ASSERT_STRUCT() - Initializes a
 *	&struct kunit_ptr_not_err_assert.
 * @txt: A string representation of the expression passed to the expectation.
 * @val: The actual evaluated pointer value of the expression.
 *
 * Initializes a &struct kunit_ptr_not_err_assert. Intended to be used in
 * KUNIT_EXPECT_* and KUNIT_ASSERT_* macros.
 */
#define KUNIT_INIT_PTR_NOT_ERR_STRUCT(txt, val) {			       \
	.text = txt,							       \
	.value = val							       \
}

/**
 * struct kunit_binary_assert_text - holds strings for &struct
 *	kunit_binary_assert and friends to try and make the structs smaller.
 * @operation: A string representation of the comparison operator (e.g. "==").
 * @left_text: A string representation of the left expression (e.g. "2+2").
 * @right_text: A string representation of the right expression (e.g. "2+2").
 */
struct kunit_binary_assert_text {
	const char *operation;
	const char *left_text;
	const char *right_text;
};

/**
 * struct kunit_binary_assert - An expectation/assertion that compares two
 *	non-pointer values (for example, KUNIT_EXPECT_EQ(test, 1 + 1, 2)).
 * @assert: The parent of this type.
 * @text: Holds the textual representations of the operands and op (e.g.  "==").
 * @left_value: The actual evaluated value of the expression in the left slot.
 * @right_value: The actual evaluated value of the expression in the right slot.
 *
 * Represents an expectation/assertion that compares two non-pointer values. For
 * example, to expect that 1 + 1 == 2, you can use the expectation
 * KUNIT_EXPECT_EQ(test, 1 + 1, 2);
 */
struct kunit_binary_assert {
	struct kunit_assert assert;
	const struct kunit_binary_assert_text *text;
	long long left_value;
	long long right_value;
};

void kunit_binary_assert_format(const struct kunit_assert *assert,
				const struct va_format *message,
				struct string_stream *stream);

/**
 * KUNIT_INIT_BINARY_ASSERT_STRUCT() - Initializes a binary assert like
 *	kunit_binary_assert, kunit_binary_ptr_assert, etc.
 *
 * @text_: Pointer to a kunit_binary_assert_text.
 * @left_val: The actual evaluated value of the expression in the left slot.
 * @right_val: The actual evaluated value of the expression in the right slot.
 *
 * Initializes a binary assert like kunit_binary_assert,
 * kunit_binary_ptr_assert, etc. This relies on these structs having the same
 * fields but with different types for left_val/right_val.
 * This is ultimately used by binary assertion macros like KUNIT_EXPECT_EQ, etc.
 */
#define KUNIT_INIT_BINARY_ASSERT_STRUCT(text_,				       \
					left_val,			       \
					right_val) {			       \
	.text = text_,							       \
	.left_value = left_val,						       \
	.right_value = right_val					       \
}

/**
 * struct kunit_binary_ptr_assert - An expectation/assertion that compares two
 *	pointer values (for example, KUNIT_EXPECT_PTR_EQ(test, foo, bar)).
 * @assert: The parent of this type.
 * @text: Holds the textual representations of the operands and op (e.g.  "==").
 * @left_value: The actual evaluated value of the expression in the left slot.
 * @right_value: The actual evaluated value of the expression in the right slot.
 *
 * Represents an expectation/assertion that compares two pointer values. For
 * example, to expect that foo and bar point to the same thing, you can use the
 * expectation KUNIT_EXPECT_PTR_EQ(test, foo, bar);
 */
struct kunit_binary_ptr_assert {
	struct kunit_assert assert;
	const struct kunit_binary_assert_text *text;
	const void *left_value;
	const void *right_value;
};

void kunit_binary_ptr_assert_format(const struct kunit_assert *assert,
				    const struct va_format *message,
				    struct string_stream *stream);

/**
 * struct kunit_binary_str_assert - An expectation/assertion that compares two
 *	string values (for example, KUNIT_EXPECT_STREQ(test, foo, "bar")).
 * @assert: The parent of this type.
 * @text: Holds the textual representations of the operands and comparator.
 * @left_value: The actual evaluated value of the expression in the left slot.
 * @right_value: The actual evaluated value of the expression in the right slot.
 *
 * Represents an expectation/assertion that compares two string values. For
 * example, to expect that the string in foo is equal to "bar", you can use the
 * expectation KUNIT_EXPECT_STREQ(test, foo, "bar");
 */
struct kunit_binary_str_assert {
	struct kunit_assert assert;
	const struct kunit_binary_assert_text *text;
	const char *left_value;
	const char *right_value;
};

void kunit_binary_str_assert_format(const struct kunit_assert *assert,
				    const struct va_format *message,
				    struct string_stream *stream);

#endif /*  _KUNIT_ASSERT_H */
