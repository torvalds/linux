/*	$OpenBSD: test.h,v 1.4 2025/05/31 11:37:18 tb Exp $ */
/*
 * Copyright (c) 2025 Joshua Sing <joshua@joshuasing.dev>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef HEADER_TEST_H
#define HEADER_TEST_H

#include <stddef.h>
#include <stdint.h>

struct test;

/*
 * test_init creates a new root test struct.
 *
 * Additional tests may be run under the root test struct by calling test_run.
 *
 * If the TEST_VERBOSE environment variable is set and not equal to "0", then
 * verbose mode will be enabled and all test logs will be written to stderr.
 */
struct test *test_init(void);

/*
 * test_result cleans up after all tests have completed and returns an
 * appropriate exit code indicating the result of the tests.
 */
int test_result(struct test *_t);

/*
 * test_run_func is an individual test function. It is passed the test struct
 * and an arbitrary argument which may be passed when test_run is called.
 */
typedef void (test_run_func)(struct test *_t, const void *_arg);

/*
 * test_fail marks the test and its parents as failed.
 */
void test_fail(struct test *_t);

/*
 * test_printf prints a test log message. When in verbose mode, the log message
 * will be written to stderr, otherwise it will be buffered and only written to
 * stderr if the test fails.
 *
 * This printf will write directly, without any additional formatting.
 */
void test_printf(struct test *_t, const char *_fmt, ...)
    __attribute__((__format__ (printf, 2, 3)))
    __attribute__((__nonnull__ (2)));

/*
 * test_logf_internal prints a test log message. When in verbose mode, the
 * log message will be written to stderr, otherwise it will be buffered and
 * only written to stderr if the test fails.
 *
 * label is an optional label indicating the severity of the log.
 * func, file and line are used to show where the log comes from and are
 * automatically set in the test log macros.
 *
 * This function should never be called directly.
 */
void test_logf_internal(struct test *_t, const char *_label, const char *_func,
    const char *_file, int _line, const char *_fmt, ...)
    __attribute__((__format__ (printf, 6, 7)))
    __attribute__((__nonnull__ (6)));

/*
 * test_logf prints an informational log message. When in verbose mode, the log
 * will be written to stderr, otherwise it will be buffered and only written to
 * stderr if the test fails.
 */
#define test_logf(t, fmt, ...) \
    do { \
	test_logf_internal(t, NULL, __func__, __FILE__, __LINE__, fmt, ##__VA_ARGS__); \
    } while (0)

/*
 * test_errorf prints an error message. It will also cause the test to fail.
 * If the test cannot proceed, it is recommended to return or goto a cleanup
 * label.
 *
 * Tests should not fail-fast if continuing will provide more detailed
 * information about what is broken.
 */
#define test_errorf(t, fmt, ...) \
    do { \
	test_logf_internal(t, "ERROR", __func__, __FILE__, __LINE__, fmt, ##__VA_ARGS__); \
	test_fail(t); \
    } while (0)

/*
 * test_skip marks the test as skipped. Once called, the test should return.
 */
void test_skip(struct test *_t, const char *_reason);

/*
 * test_skipf marks the test as skipped with a formatted reason. Once called,
 * the test should return.
 */
void test_skipf(struct test *_t, const char *_fmt, ...)
    __attribute__((__format__ (printf, 2, 3)))
    __attribute__((__nonnull__ (2)));

/*
 * test_run runs a test function. It will create a new test struct with the
 * given test as the parent. An argument may be provided to pass data to the
 * test function, otherwise NULL should be passed.
 *
 * Each test should have a unique and informational name.
 */
void test_run(struct test *_t, const char *_name, test_run_func *_fn, const void *_arg);

/*
 * test_hexdump prints the given data as hexadecimal.
 */
void test_hexdump(struct test *_t, const unsigned char *_buf, size_t _len);

/*
 * test_hexdiff prints the given data as hexadecimal. If a second comparison
 * buffer is not NULL, any differing bytes will be marked with an astrix.
 */
void test_hexdiff(struct test *_t, const uint8_t *_buf, size_t _len, const uint8_t *_compare);

#endif /* HEADER_TEST_H */
