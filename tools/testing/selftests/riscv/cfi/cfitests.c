// SPDX-License-Identifier: GPL-2.0-only

#include "../../kselftest.h"
#include <sys/signal.h>
#include <asm/ucontext.h>
#include <linux/prctl.h>
#include <errno.h>
#include <linux/ptrace.h>
#include <sys/wait.h>
#include <linux/elf.h>
#include <sys/uio.h>
#include <asm-generic/unistd.h>

#include "cfi_rv_test.h"

/* do not optimize cfi related test functions */
#pragma GCC push_options
#pragma GCC optimize("O0")

void sigsegv_handler(int signum, siginfo_t *si, void *uc)
{
	struct ucontext *ctx = (struct ucontext *)uc;

	if (si->si_code == SEGV_CPERR) {
		ksft_print_msg("Control flow violation happened somewhere\n");
		ksft_print_msg("PC where violation happened %lx\n", ctx->uc_mcontext.gregs[0]);
		exit(-1);
	}

	/* all other cases are expected to be of shadow stack write case */
	exit(CHILD_EXIT_CODE_SSWRITE);
}

bool register_signal_handler(void)
{
	struct sigaction sa = {};

	sa.sa_sigaction = sigsegv_handler;
	sa.sa_flags = SA_SIGINFO;
	if (sigaction(SIGSEGV, &sa, NULL)) {
		ksft_print_msg("Registering signal handler for landing pad violation failed\n");
		return false;
	}

	return true;
}

long ptrace(int request, pid_t pid, void *addr, void *data);

bool cfi_ptrace_test(void)
{
	pid_t pid;
	int status, ret = 0;
	unsigned long ptrace_test_num = 0, total_ptrace_tests = 2;

	struct user_cfi_state cfi_reg;
	struct iovec iov;

	pid = fork();

	if (pid == -1) {
		ksft_exit_fail_msg("%s: fork failed\n", __func__);
		exit(1);
	}

	if (pid == 0) {
		/* allow to be traced */
		ptrace(PTRACE_TRACEME, 0, NULL, NULL);
		raise(SIGSTOP);
		asm volatile ("la a5, 1f\n"
			      "jalr a5\n"
			      "nop\n"
			      "nop\n"
			      "1: nop\n"
			      : : : "a5");
		exit(11);
		/* child shouldn't go beyond here */
	}

	/* parent's code goes here */
	iov.iov_base = &cfi_reg;
	iov.iov_len = sizeof(cfi_reg);

	while (ptrace_test_num < total_ptrace_tests) {
		memset(&cfi_reg, 0, sizeof(cfi_reg));
		waitpid(pid, &status, 0);
		if (WIFSTOPPED(status)) {
			errno = 0;
			ret = ptrace(PTRACE_GETREGSET, pid, (void *)NT_RISCV_USER_CFI, &iov);
			if (ret == -1 && errno)
				ksft_exit_fail_msg("%s: PTRACE_GETREGSET failed\n", __func__);
		} else {
			ksft_exit_fail_msg("%s: child didn't stop, failed\n", __func__);
		}

		switch (ptrace_test_num) {
#define CFI_ENABLE_MASK (PTRACE_CFI_LP_EN_STATE |	\
			 PTRACE_CFI_SS_EN_STATE |	\
			 PTRACE_CFI_SS_PTR_STATE)
		case 0:
			if ((cfi_reg.cfi_status.cfi_state & CFI_ENABLE_MASK) != CFI_ENABLE_MASK)
				ksft_exit_fail_msg("%s: ptrace_getregset failed, %llu\n", __func__,
						   cfi_reg.cfi_status.cfi_state);
			if (!cfi_reg.shstk_ptr)
				ksft_exit_fail_msg("%s: NULL shadow stack pointer, test failed\n",
						   __func__);
			break;
		case 1:
			if (!(cfi_reg.cfi_status.cfi_state & PTRACE_CFI_ELP_STATE))
				ksft_exit_fail_msg("%s: elp must have been set\n", __func__);
			/* clear elp state. not interested in anything else */
			cfi_reg.cfi_status.cfi_state = 0;

			ret = ptrace(PTRACE_SETREGSET, pid, (void *)NT_RISCV_USER_CFI, &iov);
			if (ret == -1 && errno)
				ksft_exit_fail_msg("%s: PTRACE_GETREGSET failed\n", __func__);
			break;
		default:
			ksft_exit_fail_msg("%s: unreachable switch case\n", __func__);
			break;
		}
		ptrace(PTRACE_CONT, pid, NULL, NULL);
		ptrace_test_num++;
	}

	waitpid(pid, &status, 0);
	if (WEXITSTATUS(status) != 11)
		ksft_print_msg("%s, bad return code from child\n", __func__);

	ksft_print_msg("%s, ptrace test succeeded\n", __func__);
	return true;
}

int main(int argc, char *argv[])
{
	int ret = 0;
	unsigned long lpad_status = 0, ss_status = 0;

	ksft_print_header();

	ksft_print_msg("Starting risc-v tests\n");

	/*
	 * Landing pad test. Not a lot of kernel changes to support landing
	 * pads for user mode except lighting up a bit in senvcfg via a prctl.
	 * Enable landing pad support throughout the execution of the test binary.
	 */
	ret = my_syscall5(__NR_prctl, PR_GET_INDIR_BR_LP_STATUS, &lpad_status, 0, 0, 0);
	if (ret)
		ksft_exit_fail_msg("Get landing pad status failed with %d\n", ret);

	if (!(lpad_status & PR_INDIR_BR_LP_ENABLE))
		ksft_exit_fail_msg("Landing pad is not enabled, should be enabled via glibc\n");

	ret = my_syscall5(__NR_prctl, PR_GET_SHADOW_STACK_STATUS, &ss_status, 0, 0, 0);
	if (ret)
		ksft_exit_fail_msg("Get shadow stack failed with %d\n", ret);

	if (!(ss_status & PR_SHADOW_STACK_ENABLE))
		ksft_exit_fail_msg("Shadow stack is not enabled, should be enabled via glibc\n");

	if (!register_signal_handler())
		ksft_exit_fail_msg("Registering signal handler for SIGSEGV failed\n");

	ksft_print_msg("Landing pad and shadow stack are enabled for binary\n");
	cfi_ptrace_test();

	execute_shadow_stack_tests();

	return 0;
}

#pragma GCC pop_options
