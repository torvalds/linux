// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2016 Google, Inc.
 *
 * Original Code by Pavel Labath <labath@google.com>
 *
 * Code modified by Pratyush Anand <panand@redhat.com>
 * for testing different byte select for each access size.
 */

#define _GNU_SOURCE

#include <asm/ptrace.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ptrace.h>
#include <sys/param.h>
#include <sys/uio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <elf.h>
#include <errno.h>
#include <signal.h>

#include "../kselftest.h"

static volatile uint8_t var[96] __attribute__((__aligned__(32)));

static void child(int size, int wr)
{
	volatile uint8_t *addr = &var[32 + wr];

	if (ptrace(PTRACE_TRACEME, 0, NULL, NULL) != 0) {
		ksft_print_msg(
			"ptrace(PTRACE_TRACEME) failed: %s\n",
			strerror(errno));
		_exit(1);
	}

	if (raise(SIGSTOP) != 0) {
		ksft_print_msg(
			"raise(SIGSTOP) failed: %s\n", strerror(errno));
		_exit(1);
	}

	if ((uintptr_t) addr % size) {
		ksft_print_msg(
			 "Wrong address write for the given size: %s\n",
			 strerror(errno));
		_exit(1);
	}

	switch (size) {
	case 1:
		*addr = 47;
		break;
	case 2:
		*(uint16_t *)addr = 47;
		break;
	case 4:
		*(uint32_t *)addr = 47;
		break;
	case 8:
		*(uint64_t *)addr = 47;
		break;
	case 16:
		__asm__ volatile ("stp x29, x30, %0" : "=m" (addr[0]));
		break;
	case 32:
		__asm__ volatile ("stp q29, q30, %0" : "=m" (addr[0]));
		break;
	}

	_exit(0);
}

static bool set_watchpoint(pid_t pid, int size, int wp)
{
	const volatile uint8_t *addr = &var[32 + wp];
	const int offset = (uintptr_t)addr % 8;
	const unsigned int byte_mask = ((1 << size) - 1) << offset;
	const unsigned int type = 2; /* Write */
	const unsigned int enable = 1;
	const unsigned int control = byte_mask << 5 | type << 3 | enable;
	struct user_hwdebug_state dreg_state;
	struct iovec iov;

	memset(&dreg_state, 0, sizeof(dreg_state));
	dreg_state.dbg_regs[0].addr = (uintptr_t)(addr - offset);
	dreg_state.dbg_regs[0].ctrl = control;
	iov.iov_base = &dreg_state;
	iov.iov_len = offsetof(struct user_hwdebug_state, dbg_regs) +
				sizeof(dreg_state.dbg_regs[0]);
	if (ptrace(PTRACE_SETREGSET, pid, NT_ARM_HW_WATCH, &iov) == 0)
		return true;

	if (errno == EIO)
		ksft_print_msg(
			"ptrace(PTRACE_SETREGSET, NT_ARM_HW_WATCH) not supported on this hardware: %s\n",
			strerror(errno));

	ksft_print_msg(
		"ptrace(PTRACE_SETREGSET, NT_ARM_HW_WATCH) failed: %s\n",
		strerror(errno));
	return false;
}

static bool arun_test(int wr_size, int wp_size, int wr, int wp)
{
	int status;
	siginfo_t siginfo;
	pid_t pid = fork();
	pid_t wpid;

	if (pid < 0) {
		ksft_test_result_fail(
			"fork() failed: %s\n", strerror(errno));
		return false;
	}
	if (pid == 0)
		child(wr_size, wr);

	wpid = waitpid(pid, &status, __WALL);
	if (wpid != pid) {
		ksft_print_msg(
			"waitpid() failed: %s\n", strerror(errno));
		return false;
	}
	if (!WIFSTOPPED(status)) {
		ksft_print_msg(
			"child did not stop: %s\n", strerror(errno));
		return false;
	}
	if (WSTOPSIG(status) != SIGSTOP) {
		ksft_print_msg("child did not stop with SIGSTOP\n");
		return false;
	}

	if (!set_watchpoint(pid, wp_size, wp))
		return false;

	if (ptrace(PTRACE_CONT, pid, NULL, NULL) < 0) {
		ksft_print_msg(
			"ptrace(PTRACE_SINGLESTEP) failed: %s\n",
			strerror(errno));
		return false;
	}

	alarm(3);
	wpid = waitpid(pid, &status, __WALL);
	if (wpid != pid) {
		ksft_print_msg(
			"waitpid() failed: %s\n", strerror(errno));
		return false;
	}
	alarm(0);
	if (WIFEXITED(status)) {
		ksft_print_msg("child did not single-step\n");
		return false;
	}
	if (!WIFSTOPPED(status)) {
		ksft_print_msg("child did not stop\n");
		return false;
	}
	if (WSTOPSIG(status) != SIGTRAP) {
		ksft_print_msg("child did not stop with SIGTRAP\n");
		return false;
	}
	if (ptrace(PTRACE_GETSIGINFO, pid, NULL, &siginfo) != 0) {
		ksft_print_msg(
			"ptrace(PTRACE_GETSIGINFO): %s\n",
			strerror(errno));
		return false;
	}
	if (siginfo.si_code != TRAP_HWBKPT) {
		ksft_print_msg(
			"Unexpected si_code %d\n", siginfo.si_code);
		return false;
	}

	kill(pid, SIGKILL);
	wpid = waitpid(pid, &status, 0);
	if (wpid != pid) {
		ksft_print_msg(
			"waitpid() failed: %s\n", strerror(errno));
		return false;
	}
	return true;
}

static void sigalrm(int sig)
{
}

int main(int argc, char **argv)
{
	int opt;
	bool succeeded = true;
	struct sigaction act;
	int wr, wp, size;
	bool result;

	ksft_print_header();
	ksft_set_plan(213);

	act.sa_handler = sigalrm;
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;
	sigaction(SIGALRM, &act, NULL);
	for (size = 1; size <= 32; size = size*2) {
		for (wr = 0; wr <= 32; wr = wr + size) {
			for (wp = wr - size; wp <= wr + size; wp = wp + size) {
				result = run_test(size, MIN(size, 8), wr, wp);
				if ((result && wr == wp) ||
				    (!result && wr != wp))
					ksft_test_result_pass(
						"Test size = %d write offset = %d watchpoint offset = %d\n",
						size, wr, wp);
				else {
					ksft_test_result_fail(
						"Test size = %d write offset = %d watchpoint offset = %d\n",
						size, wr, wp);
					succeeded = false;
				}
			}
		}
	}

	for (size = 1; size <= 32; size = size*2) {
		if (run_test(size, 8, -size, -8))
			ksft_test_result_pass(
				"Test size = %d write offset = %d watchpoint offset = -8\n",
				size, -size);
		else {
			ksft_test_result_fail(
				"Test size = %d write offset = %d watchpoint offset = -8\n",
				size, -size);
			succeeded = false;
		}
	}

	if (succeeded)
		ksft_exit_pass();
	else
		ksft_exit_fail();
}
