// SPDX-License-Identifier: GPL-2.0-only
/*
 * tools/testing/selftests/kvm/lib/assert.c
 *
 * Copyright (C) 2018, Google LLC.
 */
#include "test_util.h"

#include <execinfo.h>
#include <sys/syscall.h>

#include "kselftest.h"

/* Dumps the current stack trace to stderr. */
static void __attribute__((noinline)) test_dump_stack(void);
static void test_dump_stack(void)
{
	/*
	 * Build and run this command:
	 *
	 *	addr2line -s -e /proc/$PPID/exe -fpai {backtrace addresses} | \
	 *		cat -n 1>&2
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
	char *c = cmd;

	n = backtrace(stack, n);
	/*
	 * Skip the first 2 frames, which should be test_dump_stack() and
	 * test_assert(); both of which are declared noinline.  Bail if the
	 * resulting stack trace would be empty. Otherwise, addr2line will block
	 * waiting for addresses to be passed in via stdin.
	 */
	if (n <= 2) {
		fputs("  (stack trace empty)\n", stderr);
		return;
	}

	c += sprintf(c, "%s", addr2line);
	for (i = 2; i < n; i++)
		c += sprintf(c, " %lx", ((unsigned long) stack[i]) - 1);

	c += sprintf(c, "%s", pipeline);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-result"
	system(cmd);
#pragma GCC diagnostic pop
}

static pid_t _gettid(void)
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
			"  pid=%d tid=%d errno=%d - %s\n",
			file, line, exp_str, getpid(), _gettid(),
			errno, strerror(errno));
		test_dump_stack();
		if (fmt) {
			fputs("  ", stderr);
			vfprintf(stderr, fmt, ap);
			fputs("\n", stderr);
		}
		va_end(ap);

		if (errno == EACCES) {
			print_skip("Access denied - Exiting");
			exit(KSFT_SKIP);
		}
		exit(254);
	}

	return;
}
