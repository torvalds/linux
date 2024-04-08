// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2022 Google LLC
 */
#define _GNU_SOURCE
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>
#include <asm-generic/unistd.h>
#include "vm_util.h"
#include "../kselftest.h"

#define MB(x) (x << 20)
#define MAX_SIZE_MB 1024

static int alloc_noexit(unsigned long nr_pages, int pipefd)
{
	int ppid = getppid();
	int timeout = 10; /* 10sec timeout to get killed */
	unsigned long i;
	char *buf;

	buf = (char *)mmap(NULL, nr_pages * psize(), PROT_READ | PROT_WRITE,
			   MAP_PRIVATE | MAP_ANON, 0, 0);
	if (buf == MAP_FAILED)
		ksft_exit_fail_msg("mmap failed, halting the test: %s\n", strerror(errno));

	for (i = 0; i < nr_pages; i++)
		*((unsigned long *)(buf + (i * psize()))) = i;

	/* Signal the parent that the child is ready */
	if (write(pipefd, "", 1) < 0)
		ksft_exit_fail_msg("write: %s\n", strerror(errno));

	/* Wait to be killed (when reparenting happens) */
	while (getppid() == ppid && timeout > 0) {
		sleep(1);
		timeout--;
	}

	munmap(buf, nr_pages * psize());

	return (timeout > 0) ? KSFT_PASS : KSFT_FAIL;
}

/* The process_mrelease calls in this test are expected to fail */
static void run_negative_tests(int pidfd)
{
	/* Test invalid flags. Expect to fail with EINVAL error code. */
	if (!syscall(__NR_process_mrelease, pidfd, (unsigned int)-1) ||
			errno != EINVAL) {
		ksft_exit_fail_msg("process_mrelease with wrong flags: %s\n", strerror(errno));
	}
	/*
	 * Test reaping while process is alive with no pending SIGKILL.
	 * Expect to fail with EINVAL error code.
	 */
	if (!syscall(__NR_process_mrelease, pidfd, 0) || errno != EINVAL)
		ksft_exit_fail_msg("process_mrelease on a live process: %s\n", strerror(errno));
}

static int child_main(int pipefd[], size_t size)
{
	int res;

	/* Allocate and fault-in memory and wait to be killed */
	close(pipefd[0]);
	res = alloc_noexit(MB(size) / psize(), pipefd[1]);
	close(pipefd[1]);
	return res;
}

int main(void)
{
	int pipefd[2], pidfd;
	bool success, retry;
	size_t size;
	pid_t pid;
	char byte;
	int res;

	ksft_print_header();
	ksft_set_plan(1);

	/* Test a wrong pidfd */
	if (!syscall(__NR_process_mrelease, -1, 0) || errno != EBADF) {
		if (errno == ENOSYS) {
			ksft_test_result_skip("process_mrelease not implemented\n");
			ksft_finished();
		} else {
			ksft_exit_fail_msg("process_mrelease with wrong pidfd: %s",
					   strerror(errno));
		}
	}

	/* Start the test with 1MB child memory allocation */
	size = 1;
retry:
	/*
	 * Pipe for the child to signal when it's done allocating
	 * memory
	 */
	if (pipe(pipefd))
		ksft_exit_fail_msg("pipe: %s\n", strerror(errno));

	pid = fork();
	if (pid < 0) {
		close(pipefd[0]);
		close(pipefd[1]);
		ksft_exit_fail_msg("fork: %s\n", strerror(errno));
	}

	if (pid == 0) {
		/* Child main routine */
		res = child_main(pipefd, size);
		exit(res);
	}

	/*
	 * Parent main routine:
	 * Wait for the child to finish allocations, then kill and reap
	 */
	close(pipefd[1]);
	/* Block until the child is ready */
	res = read(pipefd[0], &byte, 1);
	close(pipefd[0]);
	if (res < 0) {
		if (!kill(pid, SIGKILL))
			waitpid(pid, NULL, 0);
		ksft_exit_fail_msg("read: %s\n", strerror(errno));
	}

	pidfd = syscall(__NR_pidfd_open, pid, 0);
	if (pidfd < 0) {
		if (!kill(pid, SIGKILL))
			waitpid(pid, NULL, 0);
		ksft_exit_fail_msg("pidfd_open: %s\n", strerror(errno));
	}

	/* Run negative tests which require a live child */
	run_negative_tests(pidfd);

	if (kill(pid, SIGKILL))
		ksft_exit_fail_msg("kill: %s\n", strerror(errno));

	success = (syscall(__NR_process_mrelease, pidfd, 0) == 0);
	if (!success) {
		/*
		 * If we failed to reap because the child exited too soon,
		 * before we could call process_mrelease. Double child's memory
		 * which causes it to spend more time on cleanup and increases
		 * our chances of reaping its memory before it exits.
		 * Retry until we succeed or reach MAX_SIZE_MB.
		 */
		if (errno == ESRCH) {
			retry = (size <= MAX_SIZE_MB);
		} else {
			waitpid(pid, NULL, 0);
			ksft_exit_fail_msg("process_mrelease: %s\n", strerror(errno));
		}
	}

	/* Cleanup to prevent zombies */
	if (waitpid(pid, NULL, 0) < 0)
		ksft_exit_fail_msg("waitpid: %s\n", strerror(errno));

	close(pidfd);

	if (!success) {
		if (retry) {
			size *= 2;
			goto retry;
		}
		ksft_exit_fail_msg("All process_mrelease attempts failed!\n");
	}

	ksft_test_result_pass("Success reaping a child with %zuMB of memory allocations\n",
			      size);
	ksft_finished();
}
