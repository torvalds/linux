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
 * Original Code by Pavel Labath <labath@google.com>
 *
 * Code modified by Pratyush Anand <panand@redhat.com>
 * for testing different byte select for each access size.
 *
 */

#define _GNU_SOURCE

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
		perror("ptrace(PTRACE_TRACEME) failed");
		_exit(1);
	}

	if (raise(SIGSTOP) != 0) {
		perror("raise(SIGSTOP) failed");
		_exit(1);
	}

	if ((uintptr_t) addr % size) {
		perror("Wrong address write for the given size\n");
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

	if (errno == EIO) {
		printf("ptrace(PTRACE_SETREGSET, NT_ARM_HW_WATCH) "
			"not supported on this hardware\n");
		ksft_exit_skip();
	}
	perror("ptrace(PTRACE_SETREGSET, NT_ARM_HW_WATCH) failed");
	return false;
}

static bool run_test(int wr_size, int wp_size, int wr, int wp)
{
	int status;
	siginfo_t siginfo;
	pid_t pid = fork();
	pid_t wpid;

	if (pid < 0) {
		perror("fork() failed");
		return false;
	}
	if (pid == 0)
		child(wr_size, wr);

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

	if (!set_watchpoint(pid, wp_size, wp))
		return false;

	if (ptrace(PTRACE_CONT, pid, NULL, NULL) < 0) {
		perror("ptrace(PTRACE_SINGLESTEP) failed");
		return false;
	}

	alarm(3);
	wpid = waitpid(pid, &status, __WALL);
	if (wpid != pid) {
		perror("waitpid() failed");
		return false;
	}
	alarm(0);
	if (WIFEXITED(status)) {
		printf("child did not single-step\t");
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
	if (ptrace(PTRACE_GETSIGINFO, pid, NULL, &siginfo) != 0) {
		perror("ptrace(PTRACE_GETSIGINFO)");
		return false;
	}
	if (siginfo.si_code != TRAP_HWBKPT) {
		printf("Unexpected si_code %d\n", siginfo.si_code);
		return false;
	}

	kill(pid, SIGKILL);
	wpid = waitpid(pid, &status, 0);
	if (wpid != pid) {
		perror("waitpid() failed");
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

	act.sa_handler = sigalrm;
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;
	sigaction(SIGALRM, &act, NULL);
	for (size = 1; size <= 32; size = size*2) {
		for (wr = 0; wr <= 32; wr = wr + size) {
			for (wp = wr - size; wp <= wr + size; wp = wp + size) {
				printf("Test size = %d write offset = %d watchpoint offset = %d\t", size, wr, wp);
				result = run_test(size, MIN(size, 8), wr, wp);
				if ((result && wr == wp) || (!result && wr != wp)) {
					printf("[OK]\n");
					ksft_inc_pass_cnt();
				} else {
					printf("[FAILED]\n");
					ksft_inc_fail_cnt();
					succeeded = false;
				}
			}
		}
	}

	for (size = 1; size <= 32; size = size*2) {
		printf("Test size = %d write offset = %d watchpoint offset = -8\t", size, -size);

		if (run_test(size, 8, -size, -8)) {
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
