/*
 * tools/testing/selftests/kvm/lib/assert.c
 *
 * Copyright (C) 2018, Google LLC.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 */

#define _GNU_SOURCE /* for getline(3) and strchrnul(3)*/

#include "test_util.h"

#include <execinfo.h>
#include <sys/syscall.h>

#include "../../kselftest.h"

/* Dumps the current stack trace to stderr. */
static void __attribute__((noinline)) test_dump_stack(void);
static void test_dump_stack(void)
{
	/*
	 * Build and run this command:
	 *
	 *	addr2line -s -e /proc/$PPID/exe -fpai {backtrace addresses} | \
	 *		grep -v test_dump_stack | cat -n 1>&2
	 *
	 * Note that the spacing is different and there's no newline.
	 */
	size_t i;
	size_t n = 20;
	void *stack[n];
	const char *addr2line = "addr2line -s -e /proc/$PPID/exe -fpai";
	const char *pipeline = "|cat -n 1>&2";
	char cmd[strlen(addr2line) + strlen(pipeline) +
		 /* N bytes per addr * 2 digits per byte + 1 space per addr: */
		 n * (((sizeof(void *)) * 2) + 1) +
		 /* Null terminator: */
		 1];
	char *c;

	n = backtrace(stack, n);
	c = &cmd[0];
	c += sprintf(c, "%s", addr2line);
	/*
	 * Skip the first 3 frames: backtrace, test_dump_stack, and
	 * test_assert. We hope that backtrace isn't inlined and the other two
	 * we've declared noinline.
	 */
	for (i = 2; i < n; i++)
		c += sprintf(c, " %lx", ((unsigned long) stack[i]) - 1);
	c += sprintf(c, "%s", pipeline);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-result"
	system(cmd);
#pragma GCC diagnostic pop
}

static pid_t gettid(void)
{
	return syscall(SYS_gettid);
}

void __attribute__((noinline))
test_assert(bool exp, const char *exp_str,
	const char *file, unsigned int line, const char *fmt, ...)
{
	va_list ap;

	if (!(exp)) {
		va_start(ap, fmt);

		fprintf(stderr, "==== Test Assertion Failure ====\n"
			"  %s:%u: %s\n"
			"  pid=%d tid=%d - %s\n",
			file, line, exp_str, getpid(), gettid(),
			strerror(errno));
		test_dump_stack();
		if (fmt) {
			fputs("  ", stderr);
			vfprintf(stderr, fmt, ap);
			fputs("\n", stderr);
		}
		va_end(ap);

		if (errno == EACCES)
			ksft_exit_skip("Access denied - Exiting.\n");
		exit(254);
	}

	return;
}
