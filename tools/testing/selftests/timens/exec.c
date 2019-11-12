// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <stdio.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <time.h>
#include <string.h>

#include "log.h"
#include "timens.h"

#define OFFSET (36000)

int main(int argc, char *argv[])
{
	struct timespec now, tst;
	int status, i;
	pid_t pid;

	if (argc > 1) {
		if (sscanf(argv[1], "%ld", &now.tv_sec) != 1)
			return pr_perror("sscanf");

		for (i = 0; i < 2; i++) {
			_gettime(CLOCK_MONOTONIC, &tst, i);
			if (abs(tst.tv_sec - now.tv_sec) > 5)
				return pr_fail("%ld %ld\n", now.tv_sec, tst.tv_sec);
		}
		return 0;
	}

	nscheck();

	ksft_set_plan(1);

	clock_gettime(CLOCK_MONOTONIC, &now);

	if (unshare_timens())
		return 1;

	if (_settime(CLOCK_MONOTONIC, OFFSET))
		return 1;

	for (i = 0; i < 2; i++) {
		_gettime(CLOCK_MONOTONIC, &tst, i);
		if (abs(tst.tv_sec - now.tv_sec) > 5)
			return pr_fail("%ld %ld\n",
					now.tv_sec, tst.tv_sec);
	}

	if (argc > 1)
		return 0;

	pid = fork();
	if (pid < 0)
		return pr_perror("fork");

	if (pid == 0) {
		char now_str[64];
		char *cargv[] = {"exec", now_str, NULL};
		char *cenv[] = {NULL};

		/* Check that a child process is in the new timens. */
		for (i = 0; i < 2; i++) {
			_gettime(CLOCK_MONOTONIC, &tst, i);
			if (abs(tst.tv_sec - now.tv_sec - OFFSET) > 5)
				return pr_fail("%ld %ld\n",
						now.tv_sec + OFFSET, tst.tv_sec);
		}

		/* Check for proper vvar offsets after execve. */
		snprintf(now_str, sizeof(now_str), "%ld", now.tv_sec + OFFSET);
		execve("/proc/self/exe", cargv, cenv);
		return pr_perror("execve");
	}

	if (waitpid(pid, &status, 0) != pid)
		return pr_perror("waitpid");

	if (status)
		ksft_exit_fail();

	ksft_test_result_pass("exec\n");
	ksft_exit_pass();
	return 0;
}
