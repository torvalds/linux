// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024 ARM Ltd.
 *
 * Author: Dev Jain <dev.jain@arm.com>
 *
 * Test describing a clear distinction between signal states - delivered and
 * blocked, and their relation with ucontext.
 *
 * A process can request blocking of a signal by masking it into its set of
 * blocked signals; such a signal, when sent to the process by the kernel,
 * will get blocked by the process and it may later unblock it and take an
 * action. At that point, the signal will be delivered.
 *
 * We test the following functionalities of the kernel:
 *
 * ucontext_t describes the interrupted context of the thread; this implies
 * that, in case of registering a handler and catching the corresponding
 * signal, that state is before what was jumping into the handler.
 *
 * The thread's mask of blocked signals can be permanently changed, i.e, not
 * just during the execution of the handler, by mangling with uc_sigmask
 * from inside the handler.
 *
 * Assume that we block the set of signals, S1, by sigaction(), and say, the
 * signal for which the handler was installed, is S2. When S2 is sent to the
 * program, it will be considered "delivered", since we will act on the
 * signal and jump to the handler. Any instances of S1 or S2 raised, while the
 * program is executing inside the handler, will be blocked; they will be
 * delivered immediately upon termination of the handler.
 *
 * For standard signals (also see real-time signals in the man page), multiple
 * blocked instances of the same signal are not queued; such a signal will
 * be delivered just once.
 */

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <ucontext.h>

#include "../kselftest.h"

void handler_verify_ucontext(int signo, siginfo_t *info, void *uc)
{
	int ret;

	/* Kernel dumps ucontext with USR2 blocked */
	ret = sigismember(&(((ucontext_t *)uc)->uc_sigmask), SIGUSR2);
	ksft_test_result(ret == 1, "USR2 blocked in ucontext\n");

	/*
	 * USR2 is blocked; can be delivered neither here, nor after
	 * exit from handler
	 */
	if (raise(SIGUSR2))
		ksft_exit_fail_perror("raise");
}

void handler_segv(int signo, siginfo_t *info, void *uc)
{
	/*
	 * Three cases possible:
	 * 1. Program already terminated due to segmentation fault.
	 * 2. SEGV was blocked even after returning from handler_usr.
	 * 3. SEGV was delivered on returning from handler_usr.
	 * The last option must happen.
	 */
	ksft_test_result_pass("SEGV delivered\n");
}

static int cnt;

void handler_usr(int signo, siginfo_t *info, void *uc)
{
	int ret;

	/*
	 * Break out of infinite recursion caused by raise(SIGUSR1) invoked
	 * from inside the handler
	 */
	++cnt;
	if (cnt > 1)
		return;

	/* SEGV blocked during handler execution, delivered on return */
	if (raise(SIGSEGV))
		ksft_exit_fail_perror("raise");

	ksft_print_msg("SEGV bypassed successfully\n");

	/*
	 * Signal responsible for handler invocation is blocked by default;
	 * delivered on return, leading to recursion
	 */
	if (raise(SIGUSR1))
		ksft_exit_fail_perror("raise");

	ksft_test_result(cnt == 1,
			 "USR1 is blocked, cannot invoke handler right now\n");

	/* Raise USR1 again; only one instance must be delivered upon exit */
	if (raise(SIGUSR1))
		ksft_exit_fail_perror("raise");

	/* SEGV has been blocked in sa_mask, but ucontext is empty */
	ret = sigismember(&(((ucontext_t *)uc)->uc_sigmask), SIGSEGV);
	ksft_test_result(ret == 0, "SEGV not blocked in ucontext\n");

	/* USR1 has been blocked, but ucontext is empty */
	ret = sigismember(&(((ucontext_t *)uc)->uc_sigmask), SIGUSR1);
	ksft_test_result(ret == 0, "USR1 not blocked in ucontext\n");

	/*
	 * Mangle ucontext; this will be copied back into &current->blocked
	 * on return from the handler.
	 */
	if (sigaddset(&((ucontext_t *)uc)->uc_sigmask, SIGUSR2))
		ksft_exit_fail_perror("sigaddset");
}

int main(int argc, char *argv[])
{
	struct sigaction act, act2;
	sigset_t set, oldset;

	ksft_print_header();
	ksft_set_plan(7);

	act.sa_flags = SA_SIGINFO;
	act.sa_sigaction = &handler_usr;

	/* Add SEGV to blocked mask */
	if (sigemptyset(&act.sa_mask) || sigaddset(&act.sa_mask, SIGSEGV)
	    || (sigismember(&act.sa_mask, SIGSEGV) != 1))
		ksft_exit_fail_msg("Cannot add SEGV to blocked mask\n");

	if (sigaction(SIGUSR1, &act, NULL))
		ksft_exit_fail_perror("Cannot install handler");

	act2.sa_flags = SA_SIGINFO;
	act2.sa_sigaction = &handler_segv;

	if (sigaction(SIGSEGV, &act2, NULL))
		ksft_exit_fail_perror("Cannot install handler");

	/* Invoke handler */
	if (raise(SIGUSR1))
		ksft_exit_fail_perror("raise");

	/* USR1 must not be queued */
	ksft_test_result(cnt == 2, "handler invoked only twice\n");

	/* Mangled ucontext implies USR2 is blocked for current thread */
	if (raise(SIGUSR2))
		ksft_exit_fail_perror("raise");

	ksft_print_msg("USR2 bypassed successfully\n");

	act.sa_sigaction = &handler_verify_ucontext;
	if (sigaction(SIGUSR1, &act, NULL))
		ksft_exit_fail_perror("Cannot install handler");

	if (raise(SIGUSR1))
		ksft_exit_fail_perror("raise");

	/*
	 * Raising USR2 in handler_verify_ucontext is redundant since it
	 * is blocked
	 */
	ksft_print_msg("USR2 still blocked on return from handler\n");

	/* Confirm USR2 blockage by sigprocmask() too */
	if (sigemptyset(&set))
		ksft_exit_fail_perror("sigemptyset");

	if (sigprocmask(SIG_BLOCK, &set, &oldset))
		ksft_exit_fail_perror("sigprocmask");

	ksft_test_result(sigismember(&oldset, SIGUSR2) == 1,
			 "USR2 present in &current->blocked\n");

	ksft_finished();
}
