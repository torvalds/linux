/*
 * iopl.c - Test case for a Linux on Xen 64-bit bug
 * Copyright (c) 2015 Andrew Lutomirski
 */

#define _GNU_SOURCE
#include <err.h>
#include <stdio.h>
#include <stdint.h>
#include <signal.h>
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdbool.h>
#include <sched.h>
#include <sys/io.h>

static int nerrs = 0;

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

static jmp_buf jmpbuf;

static void sigsegv(int sig, siginfo_t *si, void *ctx_void)
{
	siglongjmp(jmpbuf, 1);
}

int main(void)
{
	cpu_set_t cpuset;
	CPU_ZERO(&cpuset);
	CPU_SET(0, &cpuset);
	if (sched_setaffinity(0, sizeof(cpuset), &cpuset) != 0)
		err(1, "sched_setaffinity to CPU 0");

	/* Probe for iopl support.  Note that iopl(0) works even as nonroot. */
	if (iopl(3) != 0) {
		printf("[OK]\tiopl(3) failed (%d) -- try running as root\n",
		       errno);
		return 0;
	}

	/* Restore our original state prior to starting the test. */
	if (iopl(0) != 0)
		err(1, "iopl(0)");

	pid_t child = fork();
	if (child == -1)
		err(1, "fork");

	if (child == 0) {
		printf("\tchild: set IOPL to 3\n");
		if (iopl(3) != 0)
			err(1, "iopl");

		printf("[RUN]\tchild: write to 0x80\n");
		asm volatile ("outb %%al, $0x80" : : "a" (0));

		return 0;
	} else {
		int status;
		if (waitpid(child, &status, 0) != child ||
		    !WIFEXITED(status)) {
			printf("[FAIL]\tChild died\n");
			nerrs++;
		} else if (WEXITSTATUS(status) != 0) {
			printf("[FAIL]\tChild failed\n");
			nerrs++;
		} else {
			printf("[OK]\tChild succeeded\n");
		}
	}

	printf("[RUN]\tparent: write to 0x80 (should fail)\n");

	sethandler(SIGSEGV, sigsegv, 0);
	if (sigsetjmp(jmpbuf, 1) != 0) {
		printf("[OK]\twrite was denied\n");
	} else {
		asm volatile ("outb %%al, $0x80" : : "a" (0));
		printf("[FAIL]\twrite was allowed\n");
		nerrs++;
	}

	/* Test the capability checks. */
	printf("\tiopl(3)\n");
	if (iopl(3) != 0)
		err(1, "iopl(3)");

	printf("\tDrop privileges\n");
	if (setresuid(1, 1, 1) != 0) {
		printf("[WARN]\tDropping privileges failed\n");
		goto done;
	}

	printf("[RUN]\tiopl(3) unprivileged but with IOPL==3\n");
	if (iopl(3) != 0) {
		printf("[FAIL]\tiopl(3) should work if iopl is already 3 even if unprivileged\n");
		nerrs++;
	}

	printf("[RUN]\tiopl(0) unprivileged\n");
	if (iopl(0) != 0) {
		printf("[FAIL]\tiopl(0) should work if iopl is already 3 even if unprivileged\n");
		nerrs++;
	}

	printf("[RUN]\tiopl(3) unprivileged\n");
	if (iopl(3) == 0) {
		printf("[FAIL]\tiopl(3) should fail if when unprivileged if iopl==0\n");
		nerrs++;
	} else {
		printf("[OK]\tFailed as expected\n");
	}

done:
	return nerrs ? 1 : 0;
}

