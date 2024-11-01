// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020 Collabora Ltd.
 *
 * Benchmark and test syscall user dispatch
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <sys/sysinfo.h>
#include <sys/prctl.h>
#include <sys/syscall.h>

#ifndef PR_SET_SYSCALL_USER_DISPATCH
# define PR_SET_SYSCALL_USER_DISPATCH	59
# define PR_SYS_DISPATCH_OFF	0
# define PR_SYS_DISPATCH_ON	1
# define SYSCALL_DISPATCH_FILTER_ALLOW	0
# define SYSCALL_DISPATCH_FILTER_BLOCK	1
#endif

#ifdef __NR_syscalls
# define MAGIC_SYSCALL_1 (__NR_syscalls + 1) /* Bad Linux syscall number */
#else
# define MAGIC_SYSCALL_1 (0xff00)  /* Bad Linux syscall number */
#endif

/*
 * To test returning from a sigsys with selector blocked, the test
 * requires some per-architecture support (i.e. knowledge about the
 * signal trampoline address).  On i386, we know it is on the vdso, and
 * a small trampoline is open-coded for x86_64.  Other architectures
 * that have a trampoline in the vdso will support TEST_BLOCKED_RETURN
 * out of the box, but don't enable them until they support syscall user
 * dispatch.
 */
#if defined(__x86_64__) || defined(__i386__)
#define TEST_BLOCKED_RETURN
#endif

#ifdef __x86_64__
void* (syscall_dispatcher_start)(void);
void* (syscall_dispatcher_end)(void);
#else
unsigned long syscall_dispatcher_start = 0;
unsigned long syscall_dispatcher_end = 0;
#endif

unsigned long trapped_call_count = 0;
unsigned long native_call_count = 0;

char selector;
#define SYSCALL_BLOCK   (selector = SYSCALL_DISPATCH_FILTER_BLOCK)
#define SYSCALL_UNBLOCK (selector = SYSCALL_DISPATCH_FILTER_ALLOW)

#define CALIBRATION_STEP 100000
#define CALIBRATE_TO_SECS 5
int factor;

static double one_sysinfo_step(void)
{
	struct timespec t1, t2;
	int i;
	struct sysinfo info;

	clock_gettime(CLOCK_MONOTONIC, &t1);
	for (i = 0; i < CALIBRATION_STEP; i++)
		sysinfo(&info);
	clock_gettime(CLOCK_MONOTONIC, &t2);
	return (t2.tv_sec - t1.tv_sec) + 1.0e-9 * (t2.tv_nsec - t1.tv_nsec);
}

static void calibrate_set(void)
{
	double elapsed = 0;

	printf("Calibrating test set to last ~%d seconds...\n", CALIBRATE_TO_SECS);

	while (elapsed < 1) {
		elapsed += one_sysinfo_step();
		factor += CALIBRATE_TO_SECS;
	}

	printf("test iterations = %d\n", CALIBRATION_STEP * factor);
}

static double perf_syscall(void)
{
	unsigned int i;
	double partial = 0;

	for (i = 0; i < factor; ++i)
		partial += one_sysinfo_step()/(CALIBRATION_STEP*factor);
	return partial;
}

static void handle_sigsys(int sig, siginfo_t *info, void *ucontext)
{
	char buf[1024];
	int len;

	SYSCALL_UNBLOCK;

	/* printf and friends are not signal-safe. */
	len = snprintf(buf, 1024, "Caught sys_%x\n", info->si_syscall);
	write(1, buf, len);

	if (info->si_syscall == MAGIC_SYSCALL_1)
		trapped_call_count++;
	else
		native_call_count++;

#ifdef TEST_BLOCKED_RETURN
	SYSCALL_BLOCK;
#endif

#ifdef __x86_64__
	__asm__ volatile("movq $0xf, %rax");
	__asm__ volatile("leaveq");
	__asm__ volatile("add $0x8, %rsp");
	__asm__ volatile("syscall_dispatcher_start:");
	__asm__ volatile("syscall");
	__asm__ volatile("nop"); /* Landing pad within dispatcher area */
	__asm__ volatile("syscall_dispatcher_end:");
#endif

}

int main(void)
{
	struct sigaction act;
	double time1, time2;
	int ret;
	sigset_t mask;

	memset(&act, 0, sizeof(act));
	sigemptyset(&mask);

	act.sa_sigaction = handle_sigsys;
	act.sa_flags = SA_SIGINFO;
	act.sa_mask = mask;

	calibrate_set();

	time1 = perf_syscall();
	printf("Avg syscall time %.0lfns.\n", time1 * 1.0e9);

	ret = sigaction(SIGSYS, &act, NULL);
	if (ret) {
		perror("Error sigaction:");
		exit(-1);
	}

	fprintf(stderr, "Enabling syscall trapping.\n");

	if (prctl(PR_SET_SYSCALL_USER_DISPATCH, PR_SYS_DISPATCH_ON,
		  syscall_dispatcher_start,
		  (syscall_dispatcher_end - syscall_dispatcher_start + 1),
		  &selector)) {
		perror("prctl failed\n");
		exit(-1);
	}

	SYSCALL_BLOCK;
	syscall(MAGIC_SYSCALL_1);

#ifdef TEST_BLOCKED_RETURN
	if (selector == SYSCALL_DISPATCH_FILTER_ALLOW) {
		fprintf(stderr, "Failed to return with selector blocked.\n");
		exit(-1);
	}
#endif

	SYSCALL_UNBLOCK;

	if (!trapped_call_count) {
		fprintf(stderr, "syscall trapping does not work.\n");
		exit(-1);
	}

	time2 = perf_syscall();

	if (native_call_count) {
		perror("syscall trapping intercepted more syscalls than expected\n");
		exit(-1);
	}

	printf("trapped_call_count %lu, native_call_count %lu.\n",
	       trapped_call_count, native_call_count);
	printf("Avg syscall time %.0lfns.\n", time2 * 1.0e9);
	printf("Interception overhead: %.1lf%% (+%.0lfns).\n",
	       100.0 * (time2 / time1 - 1.0), 1.0e9 * (time2 - time1));
	return 0;

}
