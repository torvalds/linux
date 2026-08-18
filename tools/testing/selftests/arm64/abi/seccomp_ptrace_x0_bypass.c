// SPDX-License-Identifier: GPL-2.0
/*
 * Test that seccomp, tracepoints and audit observe the correct syscall
 * arguments after a ptracer has modified them at syscall-enter-stop.
 *
 * On arm64, both the first argument and the return value of a syscall
 * are passed in register x0.  The original x0 is saved in
 * pt_regs::orig_x0 during syscall entry and returned as the first
 * argument by syscall_get_arguments().  Because ptrace modifications
 * to x0 are not automatically reflected in orig_x0, seccomp, tracepoints
 * and audit may see a stale value unless orig_x0 is explicitly
 * re-synchronised after a ptrace stop.
 *
 * This test sets up a seccomp filter that allows write(2, ...) but kills
 * the task for any other fd.  A ptracer changes the fd argument from 2
 * to 1 at the syscall-enter stop.  If the orig_x0 re-sync works, seccomp
 * sees the modified argument (fd=1) and kills the child with SIGSYS
 * (test passes).  If orig_x0 is not re-synced, seccomp sees the original
 * fd=2, the write succeeds and the child exits normally (test fails,
 * vulnerability present).
 */
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/prctl.h>
#include <sys/ptrace.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <asm/ptrace.h>
#include <linux/elf.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <asm/unistd.h>

#include "kselftest.h"

#define EXPECTED_TESTS 1

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define ARG0_OFFSET	(offsetof(struct seccomp_data, args))
#else
#define ARG0_OFFSET	(offsetof(struct seccomp_data, args) + 4)
#endif

static int do_child(void)
{
	if (ptrace(PTRACE_TRACEME, 0, NULL, NULL))
		ksft_exit_fail_perror("PTRACE_TRACEME");

	if (raise(SIGSTOP))
		ksft_exit_fail_perror("raise(SIGSTOP)");

	/*
	 * Seccomp filter:
	 *    If syscall is not write -> ALLOW
	 *    If syscall is write:
	 *	- If args[0] (fd) == 2 -> ALLOW
	 *	- Otherwise -> KILL
	 */
	struct sock_filter filter[] = {
		BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr)),	/* nr */
		BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_write, 0, 3),
		BPF_STMT(BPF_LD | BPF_W | BPF_ABS, ARG0_OFFSET),	/* args[0] */
		BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 2, 1, 0),
		BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL),
		BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
	};
	struct sock_fprog prog = {
		.len = ARRAY_SIZE(filter),
		.filter = filter,
	};

	if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0))
		ksft_exit_fail_perror("prctl NO_NEW_PRIVS");

	if (prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog))
		ksft_exit_fail_perror("prctl SECCOMP");

	/*
	 * Invoke write(2, ...) while the tracer will change the first
	 * argument (fd) from 2 to 1 at syscall entry.
	 */
	syscall(__NR_write, 2, NULL, 0);
	_exit(0);
}

static int do_parent(pid_t child)
{
	bool bypass = false;
	int status;

	/* Wait for the initial SIGSTOP */
	if (waitpid(child, &status, 0) != child)
		ksft_exit_fail_msg("waitpid failed");

	if (!WIFSTOPPED(status) || WSTOPSIG(status) != SIGSTOP)
		ksft_exit_fail_msg("unexpected stop status");

	if (ptrace(PTRACE_SETOPTIONS, child, 0, PTRACE_O_TRACESYSGOOD | PTRACE_O_EXITKILL))
		ksft_exit_fail_perror("PTRACE_SETOPTIONS");

	if (ptrace(PTRACE_SYSCALL, child, 0, 0))
		ksft_exit_fail_perror("PTRACE_SYSCALL");

	while (1) {
		int sig;

		if (waitpid(child, &status, 0) != child)
			ksft_exit_fail_msg("waitpid lost child");

		if (WIFEXITED(status)) {
			/* Child exited normally – bypass succeeded */
			bypass = true;
			break;
		}

		if (WIFSIGNALED(status)) {
			sig = WTERMSIG(status);
			if (sig == SIGSYS)
				break;
			ksft_exit_fail_msg("child died unexpectedly from signal %d (%s)",
					   sig, strsignal(sig));
		}

		if (!WIFSTOPPED(status))
			ksft_exit_fail_msg("unexpected wait status");

		sig = WSTOPSIG(status);

		if (sig == (SIGTRAP | 0x80)) {
			struct user_regs_struct regs;
			struct iovec iov = {
				.iov_base = &regs,
				.iov_len = sizeof(regs),
			};

			if (ptrace(PTRACE_GETREGSET, child, NT_PRSTATUS, &iov))
				ksft_exit_fail_perror("PTRACE_GETREGSET");

			unsigned long syscall_nr = regs.regs[8];
			unsigned long x0 = regs.regs[0];

			/* Modify fd from 2 to 1 at write entry */
			if (syscall_nr == __NR_write && x0 == 2) {
				regs.regs[0] = 1;
				if (ptrace(PTRACE_SETREGSET, child, NT_PRSTATUS, &iov))
					ksft_exit_fail_perror("PTRACE_SETREGSET");
			}

			if (ptrace(PTRACE_SYSCALL, child, 0, 0))
				ksft_exit_fail_perror("PTRACE_SYSCALL");
		} else {
			/* Forward other signals */
			if (ptrace(PTRACE_SYSCALL, child, 0, sig))
				ksft_exit_fail_perror("PTRACE_SYSCALL");
		}
	}

	/* bypass == true means vulnerability exists -> test fails */
	return bypass ? EXIT_FAILURE : EXIT_SUCCESS;
}

int main(void)
{
	pid_t child;

	ksft_print_header();
	ksft_set_plan(EXPECTED_TESTS);

	child = fork();
	if (child < 0)
		ksft_exit_fail_msg("fork failed: %s", strerror(errno));

	if (!child)
		return do_child();

	/*
	 * do_parent() returns EXIT_SUCCESS if the child was killed by
	 * SIGSYS (i.e. seccomp correctly saw the modified argument),
	 * and EXIT_FAILURE if the child exited normally (bypass).
	 */
	int result = do_parent(child);

	ksft_test_result(result == EXIT_SUCCESS, "seccomp_ptrace_x0_bypass\n");

	ksft_print_cnts();
	return result;
}
