// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022 ARM Limited.
 * Original author: Mark Brown <broonie@kernel.org>
 */

// SPDX-License-Identifier: GPL-2.0-only

#include <linux/sched.h>
#include <linux/wait.h>

#define EXPECTED_TESTS 1

static void putstr(const char *str)
{
	write(1, str, strlen(str));
}

static void putnum(unsigned int num)
{
	char c;

	if (num / 10)
		putnum(num / 10);

	c = '0' + (num % 10);
	write(1, &c, 1);
}

static int tests_run;
static int tests_passed;
static int tests_failed;
static int tests_skipped;

static void print_summary(void)
{
	if (tests_passed + tests_failed + tests_skipped != EXPECTED_TESTS)
		putstr("# UNEXPECTED TEST COUNT: ");

	putstr("# Totals: pass:");
	putnum(tests_passed);
	putstr(" fail:");
	putnum(tests_failed);
	putstr(" xfail:0 xpass:0 skip:");
	putnum(tests_skipped);
	putstr(" error:0\n");
}

int fork_test(void);
int verify_fork(void);

/*
 * If we fork the value in the parent should be unchanged and the
 * child should start with the same value.  This is called from the
 * fork_test() asm function.
 */
int fork_test_c(void)
{
	pid_t newpid, waiting;
	int child_status, parent_result;

	newpid = fork();
	if (newpid == 0) {
		/* In child */
		if (!verify_fork()) {
			putstr("# ZA state invalid in child\n");
			exit(0);
		} else {
			exit(1);
		}
	}
	if (newpid < 0) {
		putstr("# fork() failed: -");
		putnum(-newpid);
		putstr("\n");
		return 0;
	}

	parent_result = verify_fork();
	if (!parent_result)
		putstr("# ZA state invalid in parent\n");

	for (;;) {
		waiting = waitpid(newpid, &child_status, 0);

		if (waiting < 0) {
			if (errno == EINTR)
				continue;
			putstr("# waitpid() failed: ");
			putnum(errno);
			putstr("\n");
			return 0;
		}
		if (waiting != newpid) {
			putstr("# waitpid() returned wrong PID\n");
			return 0;
		}

		if (!WIFEXITED(child_status)) {
			putstr("# child did not exit\n");
			return 0;
		}

		return WEXITSTATUS(child_status) && parent_result;
	}
}

#define run_test(name)			     \
	if (name()) {			     \
		tests_passed++;		     \
	} else {			     \
		tests_failed++;		     \
		putstr("not ");		     \
	}				     \
	putstr("ok ");			     \
	putnum(++tests_run);		     \
	putstr(" " #name "\n");

int main(int argc, char **argv)
{
	int ret, i;

	putstr("TAP version 13\n");
	putstr("1..");
	putnum(EXPECTED_TESTS);
	putstr("\n");

	putstr("# PID: ");
	putnum(getpid());
	putstr("\n");

	/*
	 * This test is run with nolibc which doesn't support hwcap and
	 * it's probably disproportionate to implement so instead check
	 * for the default vector length configuration in /proc.
	 */
	ret = open("/proc/sys/abi/sme_default_vector_length", O_RDONLY, 0);
	if (ret >= 0) {
		run_test(fork_test);

	} else {
		putstr("# SME support not present\n");

		for (i = 0; i < EXPECTED_TESTS; i++) {
			putstr("ok ");
			putnum(i);
			putstr(" skipped\n");
		}

		tests_skipped += EXPECTED_TESTS;
	}

	print_summary();

	return 0;
}
