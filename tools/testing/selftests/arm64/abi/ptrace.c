// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022 ARM Limited.
 */
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/auxv.h>
#include <sys/prctl.h>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <asm/sigcontext.h>
#include <asm/ptrace.h>

#include "../../kselftest.h"

#define EXPECTED_TESTS 3

#define MAX_TPIDRS 1

static bool have_sme(void)
{
	return getauxval(AT_HWCAP2) & HWCAP2_SME;
}

static void test_tpidr(pid_t child)
{
	uint64_t read_val[MAX_TPIDRS];
	uint64_t write_val[MAX_TPIDRS];
	struct iovec read_iov, write_iov;
	int ret;

	read_iov.iov_base = read_val;
	write_iov.iov_base = write_val;

	/* Should be able to read a single TPIDR... */
	read_iov.iov_len = sizeof(uint64_t);
	ret = ptrace(PTRACE_GETREGSET, child, NT_ARM_TLS, &read_iov);
	ksft_test_result(ret == 0, "read_tpidr_one\n");

	/* ...write a new value.. */
	write_iov.iov_len = sizeof(uint64_t);
	write_val[0] = read_val[0]++;
	ret = ptrace(PTRACE_SETREGSET, child, NT_ARM_TLS, &write_iov);
	ksft_test_result(ret == 0, "write_tpidr_one\n");

	/* ...then read it back */
	ret = ptrace(PTRACE_GETREGSET, child, NT_ARM_TLS, &read_iov);
	ksft_test_result(ret == 0 && write_val[0] == read_val[0],
			 "verify_tpidr_one\n");
}

static int do_child(void)
{
	if (ptrace(PTRACE_TRACEME, -1, NULL, NULL))
		ksft_exit_fail_msg("PTRACE_TRACEME", strerror(errno));

	if (raise(SIGSTOP))
		ksft_exit_fail_msg("raise(SIGSTOP)", strerror(errno));

	return EXIT_SUCCESS;
}

static int do_parent(pid_t child)
{
	int ret = EXIT_FAILURE;
	pid_t pid;
	int status;
	siginfo_t si;

	/* Attach to the child */
	while (1) {
		int sig;

		pid = wait(&status);
		if (pid == -1) {
			perror("wait");
			goto error;
		}

		/*
		 * This should never happen but it's hard to flag in
		 * the framework.
		 */
		if (pid != child)
			continue;

		if (WIFEXITED(status) || WIFSIGNALED(status))
			ksft_exit_fail_msg("Child died unexpectedly\n");

		if (!WIFSTOPPED(status))
			goto error;

		sig = WSTOPSIG(status);

		if (ptrace(PTRACE_GETSIGINFO, pid, NULL, &si)) {
			if (errno == ESRCH)
				goto disappeared;

			if (errno == EINVAL) {
				sig = 0; /* bust group-stop */
				goto cont;
			}

			ksft_test_result_fail("PTRACE_GETSIGINFO: %s\n",
					      strerror(errno));
			goto error;
		}

		if (sig == SIGSTOP && si.si_code == SI_TKILL &&
		    si.si_pid == pid)
			break;

	cont:
		if (ptrace(PTRACE_CONT, pid, NULL, sig)) {
			if (errno == ESRCH)
				goto disappeared;

			ksft_test_result_fail("PTRACE_CONT: %s\n",
					      strerror(errno));
			goto error;
		}
	}

	ksft_print_msg("Parent is %d, child is %d\n", getpid(), child);

	test_tpidr(child);

	ret = EXIT_SUCCESS;

error:
	kill(child, SIGKILL);

disappeared:
	return ret;
}

int main(void)
{
	int ret = EXIT_SUCCESS;
	pid_t child;

	srandom(getpid());

	ksft_print_header();

	ksft_set_plan(EXPECTED_TESTS);

	child = fork();
	if (!child)
		return do_child();

	if (do_parent(child))
		ret = EXIT_FAILURE;

	ksft_print_cnts();

	return ret;
}
