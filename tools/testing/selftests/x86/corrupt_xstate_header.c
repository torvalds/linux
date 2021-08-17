// SPDX-License-Identifier: GPL-2.0-only
/*
 * Corrupt the XSTATE header in a signal frame
 *
 * Based on analysis and a test case from Thomas Gleixner.
 */

#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sched.h>
#include <signal.h>
#include <err.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/wait.h>

static inline void __cpuid(unsigned int *eax, unsigned int *ebx,
			   unsigned int *ecx, unsigned int *edx)
{
	asm volatile(
		"cpuid;"
		: "=a" (*eax),
		  "=b" (*ebx),
		  "=c" (*ecx),
		  "=d" (*edx)
		: "0" (*eax), "2" (*ecx));
}

static inline int xsave_enabled(void)
{
	unsigned int eax, ebx, ecx, edx;

	eax = 0x1;
	ecx = 0x0;
	__cpuid(&eax, &ebx, &ecx, &edx);

	/* Is CR4.OSXSAVE enabled ? */
	return ecx & (1U << 27);
}

static void sethandler(int sig, void (*handler)(int, siginfo_t *, void *),
		       int flags)
{
	struct sigaction sa;

	memset(&sa, 0, sizeof(sa));
	sa.sa_sigaction = handler;
	sa.sa_flags = SA_SIGINFO | flags;
	sigemptyset(&sa.sa_mask);
	if (sigaction(sig, &sa, 0))
		err(1, "sigaction");
}

static void sigusr1(int sig, siginfo_t *info, void *uc_void)
{
	ucontext_t *uc = uc_void;
	uint8_t *fpstate = (uint8_t *)uc->uc_mcontext.fpregs;
	uint64_t *xfeatures = (uint64_t *)(fpstate + 512);

	printf("\tWreck XSTATE header\n");
	/* Wreck the first reserved bytes in the header */
	*(xfeatures + 2) = 0xfffffff;
}

static void sigsegv(int sig, siginfo_t *info, void *uc_void)
{
	printf("\tGot SIGSEGV\n");
}

int main(void)
{
	cpu_set_t set;

	sethandler(SIGUSR1, sigusr1, 0);
	sethandler(SIGSEGV, sigsegv, 0);

	if (!xsave_enabled()) {
		printf("[SKIP] CR4.OSXSAVE disabled.\n");
		return 0;
	}

	CPU_ZERO(&set);
	CPU_SET(0, &set);

	/*
	 * Enforce that the child runs on the same CPU
	 * which in turn forces a schedule.
	 */
	sched_setaffinity(getpid(), sizeof(set), &set);

	printf("[RUN]\tSend ourselves a signal\n");
	raise(SIGUSR1);

	printf("[OK]\tBack from the signal.  Now schedule.\n");
	pid_t child = fork();
	if (child < 0)
		err(1, "fork");
	if (child == 0)
		return 0;
	if (child)
		waitpid(child, NULL, 0);
	printf("[OK]\tBack in the main thread.\n");

	/*
	 * We could try to confirm that extended state is still preserved
	 * when we schedule.  For now, the only indication of failure is
	 * a warning in the kernel logs.
	 */

	return 0;
}
