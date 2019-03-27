/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2008 Yahoo!, Inc.
 * All rights reserved.
 * Written by: John Baldwin <jhb@FreeBSD.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <stdarg.h>
#include <stdio.h>

#include "test.h"

static int test_index;
static struct regression_test *test;
static int test_acknowleged;

SET_DECLARE(regression_tests_set, struct regression_test);

/*
 * Outputs a test summary of the following:
 *
 * <status> <test #> [name] [# <fmt> [fmt args]]
 */
static void
vprint_status(const char *status, const char *fmt, va_list ap)
{

	printf("%s %d", status, test_index);
	if (test->rt_name)
		printf(" - %s", test->rt_name);
	if (fmt) {
		printf(" # ");
		vprintf(fmt, ap);
	}
	printf("\n");
	test_acknowleged = 1;
}

static void
print_status(const char *status, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vprint_status(status, fmt, ap);
	va_end(ap);
}

void
pass(void)
{

	print_status("ok", NULL);
}

void
fail(void)
{

	print_status("not ok", NULL);
}

void
fail_err(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vprint_status("not ok", fmt, ap);
	va_end(ap);	
}

void
skip(const char *reason)
{

	print_status("ok", "skip %s", reason);
}

void
todo(const char *reason)
{

	print_status("not ok", "TODO %s", reason);
}

void
run_tests(void)
{
	struct regression_test **testp;

	printf("1..%td\n", SET_COUNT(regression_tests_set));
	test_index = 1;
	SET_FOREACH(testp, regression_tests_set) {
		test_acknowleged = 0;
		test = *testp;
		test->rt_function();
		if (!test_acknowleged)
			print_status("not ok", "unknown status");
		test_index++;
	}
}
