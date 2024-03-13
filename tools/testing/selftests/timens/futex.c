// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#include <sched.h>

#include <linux/unistd.h>
#include <linux/futex.h>
#include <stdio.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "log.h"
#include "timens.h"

#define NSEC_PER_SEC 1000000000ULL

static int run_test(int clockid)
{
	int futex_op = FUTEX_WAIT_BITSET;
	struct timespec timeout, end;
	int val = 0;

	if (clockid == CLOCK_REALTIME)
		futex_op |= FUTEX_CLOCK_REALTIME;

	clock_gettime(clockid, &timeout);
	timeout.tv_nsec += NSEC_PER_SEC / 10; // 100ms
	if (timeout.tv_nsec > NSEC_PER_SEC) {
		timeout.tv_sec++;
		timeout.tv_nsec -= NSEC_PER_SEC;
	}

	if (syscall(__NR_futex, &val, futex_op, 0,
		    &timeout, 0, FUTEX_BITSET_MATCH_ANY) >= 0) {
		ksft_test_result_fail("futex didn't return ETIMEDOUT\n");
		return 1;
	}

	if (errno != ETIMEDOUT) {
		ksft_test_result_fail("futex didn't return ETIMEDOUT: %s\n",
							strerror(errno));
		return 1;
	}

	clock_gettime(clockid, &end);

	if (end.tv_sec < timeout.tv_sec ||
	    (end.tv_sec == timeout.tv_sec && end.tv_nsec < timeout.tv_nsec)) {
		ksft_test_result_fail("futex slept less than 100ms\n");
		return 1;
	}


	ksft_test_result_pass("futex with the %d clockid\n", clockid);

	return 0;
}

int main(int argc, char *argv[])
{
	int status, len, fd;
	char buf[4096];
	pid_t pid;
	struct timespec mtime_now;

	nscheck();

	ksft_set_plan(2);

	clock_gettime(CLOCK_MONOTONIC, &mtime_now);

	if (unshare_timens())
		return 1;

	len = snprintf(buf, sizeof(buf), "%d %d 0",
			CLOCK_MONOTONIC, 70 * 24 * 3600);
	fd = open("/proc/self/timens_offsets", O_WRONLY);
	if (fd < 0)
		return pr_perror("/proc/self/timens_offsets");

	if (write(fd, buf, len) != len)
		return pr_perror("/proc/self/timens_offsets");

	close(fd);

	pid = fork();
	if (pid < 0)
		return pr_perror("Unable to fork");
	if (pid == 0) {
		int ret = 0;

		ret |= run_test(CLOCK_REALTIME);
		ret |= run_test(CLOCK_MONOTONIC);
		if (ret)
			ksft_exit_fail();
		ksft_exit_pass();
		return 0;
	}

	if (waitpid(pid, &status, 0) != pid)
		return pr_perror("Unable to wait the child process");

	if (WIFEXITED(status))
		return WEXITSTATUS(status);

	return 1;
}
