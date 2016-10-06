/*
 * Copyright (C) 2016 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/ptrace.h>
#include <sys/stat.h>
#include <sys/timerfd.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "../kselftest.h"

void child(int cpu)
{
	cpu_set_t set;

	CPU_ZERO(&set);
	CPU_SET(cpu, &set);
	if (sched_setaffinity(0, sizeof(set), &set) != 0) {
		perror("sched_setaffinity() failed");
		_exit(1);
	}

	if (ptrace(PTRACE_TRACEME, 0, NULL, NULL) != 0) {
		perror("ptrace(PTRACE_TRACEME) failed");
		_exit(1);
	}

	if (raise(SIGSTOP) != 0) {
		perror("raise(SIGSTOP) failed");
		_exit(1);
	}

	_exit(0);
}

bool run_test(int cpu)
{
	int status;
	pid_t pid = fork();
	pid_t wpid;

	if (pid < 0) {
		perror("fork() failed");
		return false;
	}
	if (pid == 0)
		child(cpu);

	wpid = waitpid(pid, &status, __WALL);
	if (wpid != pid) {
		perror("waitpid() failed");
		return false;
	}
	if (!WIFSTOPPED(status)) {
		printf("child did not stop\n");
		return false;
	}
	if (WSTOPSIG(status) != SIGSTOP) {
		printf("child did not stop with SIGSTOP\n");
		return false;
	}

	if (ptrace(PTRACE_SINGLESTEP, pid, NULL, NULL) < 0) {
		if (errno == EIO) {
			printf("ptrace(PTRACE_SINGLESTEP) not supported on this architecture\n");
			ksft_exit_skip();
		}
		perror("ptrace(PTRACE_SINGLESTEP) failed");
		return false;
	}

	wpid = waitpid(pid, &status, __WALL);
	if (wpid != pid) {
		perror("waitpid() failed");
		return false;
	}
	if (WIFEXITED(status)) {
		printf("child did not single-step\n");
		return false;
	}
	if (!WIFSTOPPED(status)) {
		printf("child did not stop\n");
		return false;
	}
	if (WSTOPSIG(status) != SIGTRAP) {
		printf("child did not stop with SIGTRAP\n");
		return false;
	}

	if (ptrace(PTRACE_CONT, pid, NULL, NULL) < 0) {
		perror("ptrace(PTRACE_CONT) failed");
		return false;
	}

	wpid = waitpid(pid, &status, __WALL);
	if (wpid != pid) {
		perror("waitpid() failed");
		return false;
	}
	if (!WIFEXITED(status)) {
		printf("child did not exit after PTRACE_CONT\n");
		return false;
	}

	return true;
}

void suspend(void)
{
	int power_state_fd;
	struct sigevent event = {};
	int timerfd;
	int err;
	struct itimerspec spec = {};

	power_state_fd = open("/sys/power/state", O_RDWR);
	if (power_state_fd < 0) {
		perror("open(\"/sys/power/state\") failed (is this test running as root?)");
		ksft_exit_fail();
	}

	timerfd = timerfd_create(CLOCK_BOOTTIME_ALARM, 0);
	if (timerfd < 0) {
		perror("timerfd_create() failed");
		ksft_exit_fail();
	}

	spec.it_value.tv_sec = 5;
	err = timerfd_settime(timerfd, 0, &spec, NULL);
	if (err < 0) {
		perror("timerfd_settime() failed");
		ksft_exit_fail();
	}

	if (write(power_state_fd, "mem", strlen("mem")) != strlen("mem")) {
		perror("entering suspend failed");
		ksft_exit_fail();
	}

	close(timerfd);
	close(power_state_fd);
}

int main(int argc, char **argv)
{
	int opt;
	bool do_suspend = true;
	bool succeeded = true;
	cpu_set_t available_cpus;
	int err;
	int cpu;

	while ((opt = getopt(argc, argv, "n")) != -1) {
		switch (opt) {
		case 'n':
			do_suspend = false;
			break;
		default:
			printf("Usage: %s [-n]\n", argv[0]);
			printf("        -n: do not trigger a suspend/resume cycle before the test\n");
			return -1;
		}
	}

	if (do_suspend)
		suspend();

	err = sched_getaffinity(0, sizeof(available_cpus), &available_cpus);
	if (err < 0) {
		perror("sched_getaffinity() failed");
		ksft_exit_fail();
	}

	for (cpu = 0; cpu < CPU_SETSIZE; cpu++) {
		bool test_success;

		if (!CPU_ISSET(cpu, &available_cpus))
			continue;

		test_success = run_test(cpu);
		printf("CPU %d: ", cpu);
		if (test_success) {
			printf("[OK]\n");
			ksft_inc_pass_cnt();
		} else {
			printf("[FAILED]\n");
			ksft_inc_fail_cnt();
			succeeded = false;
		}
	}

	ksft_print_cnts();
	if (succeeded)
		ksft_exit_pass();
	else
		ksft_exit_fail();
}
