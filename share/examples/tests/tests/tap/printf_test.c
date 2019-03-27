/* $FreeBSD$
 *
 * Copyright 2013 Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * * Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 * * Neither the name of Google Inc. nor the names of its contributors
 *   may be used to endorse or promote products derived from this software
 *   without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. */

/*
 * INTRODUCTION
 *
 * This plain test program mimics the structure and contents of its
 * ATF-based counterpart.  It attempts to represent various test cases
 * in different separate functions and just calls them all from main().
 *
 * In reality, plain test programs can be much simpler.  All they have
 * to do is return 0 on success and non-0 otherwise.
 */

#include <err.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static	int failed;
static	int test_num = 1;

#define	TEST_COUNT	7

static void
fail(const char *fmt, ...)
{
	char *msg;
	va_list ap;

	failed = 1;

	va_start(ap, fmt);
	if (vasprintf(&msg, fmt, ap) == -1)
		err(1, NULL);
	va_end(ap);
	printf("not ok %d - %s\n", test_num, msg);
	free(msg);

	test_num++;
}

static void
pass(void)
{

	printf("ok %d\n", test_num);
	test_num++;
}

static void
skip(int skip_num)
{
	int i;

	for (i = 0; i < skip_num; i++) {
		printf("not ok %d # SKIP\n", test_num);
		test_num++;
	}
}

static void
snprintf__two_formatters(void)
{
	char buffer[128];

	if (snprintf(buffer, sizeof(buffer), "%s, %s!", "Hello",
	    "tests") <= 0) {
		fail("snprintf with two formatters failed");
		skip(1);
	} else {
		pass();
		if (strcmp(buffer, "Hello, tests!") != 0)
			fail("Bad formatting: got %s", buffer);
		else
			pass();
	}
}

static void
snprintf__overflow(void)
{
	char buffer[10];

	if (snprintf(buffer, sizeof(buffer), "0123456789abcdef") != 16) {
		fail("snprintf did not return the expected "
		    "number of characters");
		skip(1);
		return;
	}
	pass();

	if (strcmp(buffer, "012345678") != 0)
		fail("Bad formatting: got %s", buffer);
	else
		pass();
}

static void
fprintf__simple_string(void)
{
	FILE *file;
	char buffer[128];
	size_t length;
	const char *contents = "This is a message\n";

	file = fopen("test.txt", "w+");
	if (fprintf(file, "%s", contents) <= 0) {
		fail("fprintf failed to write to file");
		skip(2);
		return;
	}
	pass();
	rewind(file);
	length = fread(buffer, 1, sizeof(buffer) - 1, file);
	if (length != strlen(contents)) {
		fail("fread failed");
		skip(1);
		return;
	}
	pass();
	buffer[length] = '\0';
	fclose(file);

	if (strcmp(buffer, contents) != 0)
		fail("Written and read data differ");
	else
		pass();

	/* Of special note here is that we are NOT deleting the temporary
	 * files we created in this test.  Kyua takes care of this cleanup
	 * automatically and tests can (and should) rely on this behavior. */
}

int
main(void)
{
	/* If you have read the printf_test.c counterpart in the atf/
	 * directory, you may think that the sequencing of tests below and
	 * the exposed behavior to the user is very similar.  But you'd be
	 * wrong.
	 *
	 * There are two major differences with this and the ATF version.
	 * The first is that the code below has no provisions to detect
	 * failures in one test and continue running the other tests: the
	 * first failure causes the whole test program to exit.  The second
	 * is that this particular main() has no arguments: without ATF,
	 * all test programs may expose a different command-line interface,
	 * and this is an issue for consistency purposes. */
	printf("1..%d\n", TEST_COUNT);

	snprintf__two_formatters();
	snprintf__overflow();
	fprintf__simple_string();

	return (failed);
}
