// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#include <sched.h>

#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <signal.h>

#include "log.h"
#include "timens.h"

static int run_test(int clockid, struct timespec now)
{
	struct itimerspec new_value;
	long long elapsed;
	timer_t fd;
	int i;

	if (check_skip(clockid))
		return 0;

	for (i = 0; i < 2; i++) {
		struct sigevent sevp = {.sigev_notify = SIGEV_NONE};
		int flags = 0;

		new_value.it_value.tv_sec = 3600;
		new_value.it_value.tv_nsec = 0;
		new_value.it_interval.tv_sec = 1;
		new_value.it_interval.tv_nsec = 0;

		if (i == 1) {
			new_value.it_value.tv_sec += now.tv_sec;
			new_value.it_value.tv_nsec += now.tv_nsec;
		}

		if (timer_create(clockid, &sevp, &fd) == -1) {
			if (errno == ENOSYS) {
				ksft_test_result_skip("Posix Clocks & timers are supported\n");
				return 0;
			}
			return pr_perror("timerfd_create");
		}

		if (i == 1)
			flags |= TIMER_ABSTIME;
		if (timer_settime(fd, flags, &new_value, NULL) == -1)
			return pr_perror("timerfd_settime");

		if (timer_gettime(fd, &new_value) == -1)
			return pr_perror("timerfd_gettime");

		elapsed = new_value.it_value.tv_sec;
		if (llabs(elapsed - 3600) > 60) {
			ksft_test_result_fail("clockid: %d elapsed: %lld\n",
					      clockid, elapsed);
			return 1;
		}
	}

	ksft_test_result_pass("clockid=%d\n", clockid);

	return 0;
}

int main(int argc, char *argv[])
{
	int ret, status, len, fd;
	char buf[4096];
	pid_t pid;
	struct timespec btime_now, mtime_now;

	ksft_print_header();

	nscheck();

	check_supported_timers();

	ksft_set_plan(3);

	clock_gettime(CLOCK_MONOTONIC, &mtime_now);
	clock_gettime(CLOCK_BOOTTIME, &btime_now);

	if (unshare_timens())
		return 1;

	len = snprintf(buf, sizeof(buf), "%d %d 0\n%d %d 0",
			CLOCK_MONOTONIC, 70 * 24 * 3600,
			CLOCK_BOOTTIME, 9 * 24 * 3600);
	fd = open("/proc/self/timens_offsets", O_WRONLY);
	if (fd < 0)
		return pr_perror("/proc/self/timens_offsets");

	if (write(fd, buf, len) != len)
		return pr_perror("/proc/self/timens_offsets");

	close(fd);
	mtime_now.tv_sec += 70 * 24 * 3600;
	btime_now.tv_sec += 9 * 24 * 3600;

	pid = fork();
	if (pid < 0)
		return pr_perror("Unable to fork");
	if (pid == 0) {
		ret = 0;
		ret |= run_test(CLOCK_BOOTTIME, btime_now);
		ret |= run_test(CLOCK_MONOTONIC, mtime_now);
		ret |= run_test(CLOCK_BOOTTIME_ALARM, btime_now);

		if (ret)
			ksft_exit_fail();
		ksft_exit_pass();
		return ret;
	}

	if (waitpid(pid, &status, 0) != pid)
		return pr_perror("Unable to wait the child process");

	if (WIFEXITED(status))
		return WEXITSTATUS(status);

	return 1;
}
