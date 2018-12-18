/*
 * tools/testing/selftests/kvm/include/test_util.h
 *
 * Copyright (C) 2018, Google LLC.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 *
 */

#ifndef TEST_UTIL_H
#define TEST_UTIL_H 1

#include <stdlib.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include "kselftest.h"

ssize_t test_write(int fd, const void *buf, size_t count);
ssize_t test_read(int fd, void *buf, size_t count);
int test_seq_read(const char *path, char **bufp, size_t *sizep);

void test_assert(bool exp, const char *exp_str,
		 const char *file, unsigned int line, const char *fmt, ...);

#define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))

#define TEST_ASSERT(e, fmt, ...) \
	test_assert((e), #e, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define ASSERT_EQ(a, b) do { \
	typeof(a) __a = (a); \
	typeof(b) __b = (b); \
	TEST_ASSERT(__a == __b, \
		    "ASSERT_EQ(%s, %s) failed.\n" \
		    "\t%s is %#lx\n" \
		    "\t%s is %#lx", \
		    #a, #b, #a, (unsigned long) __a, #b, (unsigned long) __b); \
} while (0)

#endif /* TEST_UTIL_H */
