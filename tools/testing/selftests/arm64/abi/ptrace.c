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

#define EXPECTED_TESTS 11

#define MAX_TPIDRS 2

static bool have_sme(void)
{
	return getauxval(AT_HWCAP2) & HWCAP2_SME;
}

static void test_tpidr(pid_t child)
{
	uint64_t read_val[MAX_TPIDRS];
	uint64_t write_val[MAX_TPIDRS];
	struct iovec read_iov, write_iov;
	bool test_tpidr2 = false;
	int ret, i;

	read_iov.iov_base = read_val;
	write_iov.iov_base = write_val;

	/* Should be able to read a single TPIDR... */
	read_iov.iov_len = sizeof(uint64_t);
	ret = ptrace(PTRACE_GETREGSET, child, NT_ARM_TLS, &read_iov);
	ksft_test_result(ret == 0, "read_tpidr_one\n");

	/* ...write a new value.. */
	write_iov.iov_len = sizeof(uint64_t);
	write_val[0] = read_val[0] + 1;
	ret = ptrace(PTRACE_SETREGSET, child, NT_ARM_TLS, &write_iov);
	ksft_test_result(ret == 0, "write_tpidr_one\n");

	/* ...then read it back */
	ret = ptrace(PTRACE_GETREGSET, child, NT_ARM_TLS, &read_iov);
	ksft_test_result(ret == 0 && write_val[0] == read_val[0],
			 "verify_tpidr_one\n");

	/* If we have TPIDR2 we should be able to read it */
	read_iov.iov_len = sizeof(read_val);
	ret = ptrace(PTRACE_GETREGSET, child, NT_ARM_TLS, &read_iov);
	if (ret == 0) {
		/* If we have SME there should be two TPIDRs */
		if (read_iov.iov_len >= sizeof(read_val))
			test_tpidr2 = true;

		if (have_sme() && test_tpidr2) {
			ksft_test_result(test_tpidr2, "count_tpidrs\n");
		} else {
			ksft_test_result(read_iov.iov_len % sizeof(uint64_t) == 0,
					 "count_tpidrs\n");
		}
	} else {
		ksft_test_result_fail("count_tpidrs\n");
	}

	if (test_tpidr2) {
		/* Try to write new values to all known TPIDRs... */
		write_iov.iov_len = sizeof(write_val);
		for (i = 0; i < MAX_TPIDRS; i++)
			write_val[i] = read_val[i] + 1;
		ret = ptrace(PTRACE_SETREGSET, child, NT_ARM_TLS, &write_iov);

		ksft_test_result(ret == 0 &&
				 write_iov.iov_len == sizeof(write_val),
				 "tpidr2_write\n");

		/* ...then read them back */
		read_iov.iov_len = sizeof(read_val);
		ret = ptrace(PTRACE_GETREGSET, child, NT_ARM_TLS, &read_iov);

		if (have_sme()) {
			/* Should read back the written value */
			ksft_test_result(ret == 0 &&
					 read_iov.iov_len >= sizeof(read_val) &&
					 memcmp(read_val, write_val,
						sizeof(read_val)) == 0,
					 "tpidr2_read\n");
		} else {
			/* TPIDR2 should read as zero */
			ksft_test_result(ret == 0 &&
					 read_iov.iov_len >= sizeof(read_val) &&
					 read_val[0] == write_val[0] &&
					 read_val[1] == 0,
					 "tpidr2_read\n");
		}

		/* Writing only TPIDR... */
		write_iov.iov_len = sizeof(uint64_t);
		memcpy(write_val, read_val, sizeof(read_val));
		write_val[0] += 1;
		ret = ptrace(PTRACE_SETREGSET, child, NT_ARM_TLS, &write_iov);

		if (ret == 0) {
			/* ...should leave TPIDR2 untouched */
			read_iov.iov_len = sizeof(read_val);
			ret = ptrace(PTRACE_GETREGSET, child, NT_ARM_TLS,
				     &read_iov);

			ksft_test_result(ret == 0 &&
					 read_iov.iov_len >= sizeof(read_val) &&
					 memcmp(read_val, write_val,
						sizeof(read_val)) == 0,
					 "write_tpidr_only\n");
		} else {
			ksft_test_result_fail("write_tpidr_only\n");
		}
	} else {
		ksft_test_result_skip("tpidr2_write\n");
		ksft_test_result_skip("tpidr2_read\n");
		ksft_test_result_skip("write_tpidr_only\n");
	}
}

static void test_hw_debug(pid_t child, int type, const char *type_name)
{
	struct user_hwdebug_state state;
	struct iovec iov;
	int slots, arch, ret;

	iov.iov_len = sizeof(state);
	iov.iov_base = &state;

	/* Should be able to read the values */
	ret = ptrace(PTRACE_GETREGSET, child, type, &iov);
	ksft_test_result(ret == 0, "read_%s\n", type_name);

	if (ret == 0) {
		/* Low 8 bits is the number of slots, next 4 bits the arch */
		slots = state.dbg_info & 0xff;
		arch = (state.dbg_info >> 8) & 0xf;

		ksft_print_msg("%s version %d with %d slots\n", type_name,
			       arch, slots);

		/* Zero is not currently architecturally valid */
		ksft_test_result(arch, "%s_arch_set\n", type_name);
	} else {
		ksft_test_result_skip("%s_arch_set\n", type_name);
	}
}

static int do_child(void)
{
	if (ptrace(PTRACE_TRACEME, -1, NULL, NULL))
		ksft_exit_fail_perror("PTRACE_TRACEME");

	if (raise(SIGSTOP))
		ksft_exit_fail_perror("raise(SIGSTOP)");

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
	test_hw_debug(child, NT_ARM_HW_WATCH, "NT_ARM_HW_WATCH");
	test_hw_debug(child, NT_ARM_HW_BREAK, "NT_ARM_HW_BREAK");

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
