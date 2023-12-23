// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022 ARM Limited.
 * Original author: Mark Brown <broonie@kernel.org>
 */

// SPDX-License-Identifier: GPL-2.0-only

#include <linux/sched.h>
#include <linux/wait.h>

#include "kselftest.h"

#define EXPECTED_TESTS 1

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
			ksft_print_msg("ZA state invalid in child\n");
			exit(0);
		} else {
			exit(1);
		}
	}
	if (newpid < 0) {
		ksft_print_msg("fork() failed: %d\n", newpid);

		return 0;
	}

	parent_result = verify_fork();
	if (!parent_result)
		ksft_print_msg("ZA state invalid in parent\n");

	for (;;) {
		waiting = waitpid(newpid, &child_status, 0);

		if (waiting < 0) {
			if (errno == EINTR)
				continue;
			ksft_print_msg("waitpid() failed: %d\n", errno);
			return 0;
		}
		if (waiting != newpid) {
			ksft_print_msg("waitpid() returned wrong PID\n");
			return 0;
		}

		if (!WIFEXITED(child_status)) {
			ksft_print_msg("child did not exit\n");
			return 0;
		}

		return WEXITSTATUS(child_status) && parent_result;
	}
}

int main(int argc, char **argv)
{
	int ret, i;

	ksft_print_header();
	ksft_set_plan(EXPECTED_TESTS);

	ksft_print_msg("PID: %d\n", getpid());

	/*
	 * This test is run with nolibc which doesn't support hwcap and
	 * it's probably disproportionate to implement so instead check
	 * for the default vector length configuration in /proc.
	 */
	ret = open("/proc/sys/abi/sme_default_vector_length", O_RDONLY, 0);
	if (ret >= 0) {
		ksft_test_result(fork_test(), "fork_test\n");

	} else {
		ksft_print_msg("SME not supported\n");
		for (i = 0; i < EXPECTED_TESTS; i++) {
			ksft_test_result_skip("fork_test\n");
		}
	}

	ksft_finished();

	return 0;
}
