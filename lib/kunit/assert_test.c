// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * KUnit test for the assertion formatting functions.
 * Author: Ivan Orlov <ivan.orlov0322@gmail.com>
 */
#include <kunit/test.h>
#include "string-stream.h"

#define TEST_PTR_EXPECTED_BUF_SIZE 32
#define HEXDUMP_TEST_BUF_LEN 5
#define ASSERT_TEST_EXPECT_CONTAIN(test, str, substr) KUNIT_EXPECT_TRUE(test, strstr(str, substr))
#define ASSERT_TEST_EXPECT_NCONTAIN(test, str, substr) KUNIT_EXPECT_FALSE(test, strstr(str, substr))

static void kunit_test_is_literal(struct kunit *test)
{
	KUNIT_EXPECT_TRUE(test, is_literal("5", 5));
	KUNIT_EXPECT_TRUE(test, is_literal("0", 0));
	KUNIT_EXPECT_TRUE(test, is_literal("1234567890", 1234567890));
	KUNIT_EXPECT_TRUE(test, is_literal("-1234567890", -1234567890));
	KUNIT_EXPECT_FALSE(test, is_literal("05", 5));
	KUNIT_EXPECT_FALSE(test, is_literal("", 0));
	KUNIT_EXPECT_FALSE(test, is_literal("-0", 0));
	KUNIT_EXPECT_FALSE(test, is_literal("12#45", 1245));
}

static void kunit_test_is_str_literal(struct kunit *test)
{
	KUNIT_EXPECT_TRUE(test, is_str_literal("\"Hello, World!\"", "Hello, World!"));
	KUNIT_EXPECT_TRUE(test, is_str_literal("\"\"", ""));
	KUNIT_EXPECT_TRUE(test, is_str_literal("\"\"\"", "\""));
	KUNIT_EXPECT_FALSE(test, is_str_literal("", ""));
	KUNIT_EXPECT_FALSE(test, is_str_literal("\"", "\""));
	KUNIT_EXPECT_FALSE(test, is_str_literal("\"Abacaba", "Abacaba"));
	KUNIT_EXPECT_FALSE(test, is_str_literal("Abacaba\"", "Abacaba"));
	KUNIT_EXPECT_FALSE(test, is_str_literal("\"Abacaba\"", "\"Abacaba\""));
}

KUNIT_DEFINE_ACTION_WRAPPER(kfree_wrapper, kfree, const void *);

/* this function is used to get a "char *" string from the string stream and defer its cleanup  */
static char *get_str_from_stream(struct kunit *test, struct string_stream *stream)
{
	char *str = string_stream_get_string(stream);

	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, str);
	kunit_add_action(test, kfree_wrapper, (void *)str);

	return str;
}

static void kunit_test_assert_prologue(struct kunit *test)
{
	struct string_stream *stream;
	char *str;
	const struct kunit_loc location = {
		.file = "testfile.c",
		.line = 1337,
	};

	stream = kunit_alloc_string_stream(test, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, stream);

	/* Test an expectation fail prologue */
	kunit_assert_prologue(&location, KUNIT_EXPECTATION, stream);
	str = get_str_from_stream(test, stream);
	ASSERT_TEST_EXPECT_CONTAIN(test, str, "EXPECTATION");
	ASSERT_TEST_EXPECT_CONTAIN(test, str, "testfile.c");
	ASSERT_TEST_EXPECT_CONTAIN(test, str, "1337");

	/* Test an assertion fail prologue */
	string_stream_clear(stream);
	kunit_assert_prologue(&location, KUNIT_ASSERTION, stream);
	str = get_str_from_stream(test, stream);
	ASSERT_TEST_EXPECT_CONTAIN(test, str, "ASSERTION");
	ASSERT_TEST_EXPECT_CONTAIN(test, str, "testfile.c");
	ASSERT_TEST_EXPECT_CONTAIN(test, str, "1337");
}

/*
 * This function accepts an arbitrary count of parameters and generates a va_format struct,
 * which can be used to validate kunit_assert_print_msg function
 */
static void verify_assert_print_msg(struct kunit *test,
				    struct string_stream *stream,
				    char *expected, const char *format, ...)
{
	va_list list;
	const struct va_format vformat = {
		.fmt = format,
		.va = &list,
	};

	va_start(list, format);
	string_stream_clear(stream);
	kunit_assert_print_msg(&vformat, stream);
	KUNIT_EXPECT_STREQ(test, get_str_from_stream(test, stream), expected);
}

static void kunit_test_assert_print_msg(struct kunit *test)
{
	struct string_stream *stream;

	stream = kunit_alloc_string_stream(test, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, stream);

	verify_assert_print_msg(test, stream, "\nTest", "Test");
	verify_assert_print_msg(test, stream, "\nAbacaba -123 234", "%s %d %u",
				"Abacaba", -123, 234U);
	verify_assert_print_msg(test, stream, "", NULL);
}

/*
 * Further code contains the tests for different assert format functions.
 * This helper function accepts the assert format function, executes it and
 * validates the result string from the stream by checking that all of the
 * substrings exist in the output.
 */
static void validate_assert(assert_format_t format_func, struct kunit *test,
			    const struct kunit_assert *assert,
			    struct string_stream *stream, int num_checks, ...)
{
	size_t i;
	va_list checks;
	char *cur_substr_exp;
	struct va_format message = { NULL, NULL };

	va_start(checks, num_checks);
	string_stream_clear(stream);
	format_func(assert, &message, stream);

	for (i = 0; i < num_checks; i++) {
		cur_substr_exp = va_arg(checks, char *);
		ASSERT_TEST_EXPECT_CONTAIN(test, get_str_from_stream(test, stream), cur_substr_exp);
	}
}

static void kunit_test_unary_assert_format(struct kunit *test)
{
	struct string_stream *stream;
	struct kunit_assert assert = {};
	struct kunit_unary_assert un_assert = {
		.assert = assert,
		.condition = "expr",
		.expected_true = true,
	};

	stream = kunit_alloc_string_stream(test, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, stream);

	validate_assert(kunit_unary_assert_format, test, &un_assert.assert,
			stream, 2, "true", "is false");

	un_assert.expected_true = false;
	validate_assert(kunit_unary_assert_format, test, &un_assert.assert,
			stream, 2, "false", "is true");
}

static void kunit_test_ptr_not_err_assert_format(struct kunit *test)
{
	struct string_stream *stream;
	struct kunit_assert assert = {};
	struct kunit_ptr_not_err_assert not_err_assert = {
		.assert = assert,
		.text = "expr",
		.value = NULL,
	};

	stream = kunit_alloc_string_stream(test, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, stream);

	/* Value is NULL. The corresponding message should be printed out */
	validate_assert(kunit_ptr_not_err_assert_format, test,
			&not_err_assert.assert,
			stream, 1, "null");

	/* Value is not NULL, but looks like an error pointer. Error should be printed out */
	not_err_assert.value = (void *)-12;
	validate_assert(kunit_ptr_not_err_assert_format, test,
			&not_err_assert.assert, stream, 2,
			"error", "-12");
}

static void kunit_test_binary_assert_format(struct kunit *test)
{
	struct string_stream *stream;
	struct kunit_assert assert = {};
	struct kunit_binary_assert_text text = {
		.left_text = "1 + 2",
		.operation = "==",
		.right_text = "2",
	};
	const struct kunit_binary_assert binary_assert = {
		.assert = assert,
		.text = &text,
		.left_value = 3,
		.right_value = 2,
	};

	stream = kunit_alloc_string_stream(test, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, stream);

	/*
	 * Printed values should depend on the input we provide: the left text, right text, left
	 * value and the right value.
	 */
	validate_assert(kunit_binary_assert_format, test, &binary_assert.assert,
			stream, 4, "1 + 2", "2", "3", "==");

	text.right_text = "4 - 2";
	validate_assert(kunit_binary_assert_format, test, &binary_assert.assert,
			stream, 3, "==", "1 + 2", "4 - 2");

	text.left_text = "3";
	validate_assert(kunit_binary_assert_format, test, &binary_assert.assert,
			stream, 4, "3", "4 - 2", "2", "==");

	text.right_text = "2";
	validate_assert(kunit_binary_assert_format, test, &binary_assert.assert,
			stream, 3, "3", "2", "==");
}

static void kunit_test_binary_ptr_assert_format(struct kunit *test)
{
	struct string_stream *stream;
	struct kunit_assert assert = {};
	char *addr_var_a, *addr_var_b;
	static const void *var_a = (void *)0xDEADBEEF;
	static const void *var_b = (void *)0xBADDCAFE;
	struct kunit_binary_assert_text text = {
		.left_text = "var_a",
		.operation = "==",
		.right_text = "var_b",
	};
	struct kunit_binary_ptr_assert binary_ptr_assert = {
		.assert = assert,
		.text = &text,
		.left_value = var_a,
		.right_value = var_b,
	};

	addr_var_a = kunit_kzalloc(test, TEST_PTR_EXPECTED_BUF_SIZE, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, addr_var_a);
	addr_var_b = kunit_kzalloc(test, TEST_PTR_EXPECTED_BUF_SIZE, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, addr_var_b);
	/*
	 * Print the addresses to the buffers first.
	 * This is necessary as we may have different count of leading zeros in the pointer
	 * on different architectures.
	 */
	snprintf(addr_var_a, TEST_PTR_EXPECTED_BUF_SIZE, "%px", var_a);
	snprintf(addr_var_b, TEST_PTR_EXPECTED_BUF_SIZE, "%px", var_b);

	stream = kunit_alloc_string_stream(test, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, stream);
	validate_assert(kunit_binary_ptr_assert_format, test, &binary_ptr_assert.assert,
			stream, 3, addr_var_a, addr_var_b, "==");
}

static void kunit_test_binary_str_assert_format(struct kunit *test)
{
	struct string_stream *stream;
	struct kunit_assert assert = {};
	static const char *var_a = "abacaba";
	static const char *var_b = "kernel";
	struct kunit_binary_assert_text text = {
		.left_text = "var_a",
		.operation = "==",
		.right_text = "var_b",
	};
	struct kunit_binary_str_assert binary_str_assert = {
		.assert = assert,
		.text = &text,
		.left_value = var_a,
		.right_value = var_b,
	};

	stream = kunit_alloc_string_stream(test, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, stream);

	validate_assert(kunit_binary_str_assert_format, test,
			&binary_str_assert.assert,
			stream, 5, "var_a", "var_b", "\"abacaba\"",
			"\"kernel\"", "==");

	text.left_text = "\"abacaba\"";
	validate_assert(kunit_binary_str_assert_format, test, &binary_str_assert.assert,
			stream, 4, "\"abacaba\"", "var_b", "\"kernel\"", "==");

	text.right_text = "\"kernel\"";
	validate_assert(kunit_binary_str_assert_format, test, &binary_str_assert.assert,
			stream, 3, "\"abacaba\"", "\"kernel\"", "==");
}

static const u8 hex_testbuf1[] = { 0x26, 0x74, 0x6b, 0x9c, 0x55,
				   0x45, 0x9d, 0x47, 0xd6, 0x47,
				   0x2,  0x89, 0x8c, 0x81, 0x94,
				   0x12, 0xfe, 0x01 };
static const u8 hex_testbuf2[] = { 0x26, 0x74, 0x6b, 0x9c, 0x55,
				   0x45, 0x9d, 0x47, 0x21, 0x47,
				   0xcd, 0x89, 0x24, 0x50, 0x94,
				   0x12, 0xba, 0x01 };
static void kunit_test_assert_hexdump(struct kunit *test)
{
	struct string_stream *stream;
	char *str;
	size_t i;
	char buf[HEXDUMP_TEST_BUF_LEN];

	stream = kunit_alloc_string_stream(test, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, stream);
	/* Check that we are getting output like <xx> for non-matching numbers. */
	kunit_assert_hexdump(stream, hex_testbuf1, hex_testbuf2, sizeof(hex_testbuf1));
	str = get_str_from_stream(test, stream);
	for (i = 0; i < sizeof(hex_testbuf1); i++) {
		snprintf(buf, HEXDUMP_TEST_BUF_LEN, "<%02x>", hex_testbuf1[i]);
		if (hex_testbuf1[i] != hex_testbuf2[i])
			ASSERT_TEST_EXPECT_CONTAIN(test, str, buf);
	}
	/* We shouldn't get any <xx> numbers when comparing the buffer with itself. */
	string_stream_clear(stream);
	kunit_assert_hexdump(stream, hex_testbuf1, hex_testbuf1, sizeof(hex_testbuf1));
	str = get_str_from_stream(test, stream);
	ASSERT_TEST_EXPECT_NCONTAIN(test, str, "<");
	ASSERT_TEST_EXPECT_NCONTAIN(test, str, ">");
}

static void kunit_test_mem_assert_format(struct kunit *test)
{
	struct string_stream *stream;
	struct string_stream *expected_stream;
	struct kunit_assert assert = {};
	static const struct kunit_binary_assert_text text = {
		.left_text = "hex_testbuf1",
		.operation = "==",
		.right_text = "hex_testbuf2",
	};
	struct kunit_mem_assert mem_assert = {
		.assert = assert,
		.text = &text,
		.left_value = NULL,
		.right_value = hex_testbuf2,
		.size = sizeof(hex_testbuf1),
	};

	expected_stream = kunit_alloc_string_stream(test, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, expected_stream);
	stream = kunit_alloc_string_stream(test, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, stream);

	/* The left value is NULL */
	validate_assert(kunit_mem_assert_format, test, &mem_assert.assert,
			stream, 2, "hex_testbuf1", "is not null");

	/* The right value is NULL, the left value is not NULL */
	mem_assert.left_value = hex_testbuf1;
	mem_assert.right_value = NULL;
	validate_assert(kunit_mem_assert_format, test, &mem_assert.assert,
			stream, 2, "hex_testbuf2", "is not null");

	/* Both arguments are not null */
	mem_assert.left_value = hex_testbuf1;
	mem_assert.right_value = hex_testbuf2;

	validate_assert(kunit_mem_assert_format, test, &mem_assert.assert,
			stream, 3, "hex_testbuf1", "hex_testbuf2", "==");
}

static struct kunit_case assert_test_cases[] = {
	KUNIT_CASE(kunit_test_is_literal),
	KUNIT_CASE(kunit_test_is_str_literal),
	KUNIT_CASE(kunit_test_assert_prologue),
	KUNIT_CASE(kunit_test_assert_print_msg),
	KUNIT_CASE(kunit_test_unary_assert_format),
	KUNIT_CASE(kunit_test_ptr_not_err_assert_format),
	KUNIT_CASE(kunit_test_binary_assert_format),
	KUNIT_CASE(kunit_test_binary_ptr_assert_format),
	KUNIT_CASE(kunit_test_binary_str_assert_format),
	KUNIT_CASE(kunit_test_assert_hexdump),
	KUNIT_CASE(kunit_test_mem_assert_format),
	{}
};

static struct kunit_suite assert_test_suite = {
	.name = "kunit-assert",
	.test_cases = assert_test_cases,
};

kunit_test_suites(&assert_test_suite);
