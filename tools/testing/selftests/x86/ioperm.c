/*
 * ioperm.c - Test case for ioperm(2)
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

static void clearhandler(int sig)
{
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = SIG_DFL;
	sigemptyset(&sa.sa_mask);
	if (sigaction(sig, &sa, 0))
		err(1, "sigaction");
}

static jmp_buf jmpbuf;

static void sigsegv(int sig, siginfo_t *si, void *ctx_void)
{
	siglongjmp(jmpbuf, 1);
}

static bool try_outb(unsigned short port)
{
	sethandler(SIGSEGV, sigsegv, SA_RESETHAND);
	if (sigsetjmp(jmpbuf, 1) != 0) {
		return false;
	} else {
		asm volatile ("outb %%al, %w[port]"
			      : : [port] "Nd" (port), "a" (0));
		return true;
	}
	clearhandler(SIGSEGV);
}

static void expect_ok(unsigned short port)
{
	if (!try_outb(port)) {
		printf("[FAIL]\toutb to 0x%02hx failed\n", port);
		exit(1);
	}

	printf("[OK]\toutb to 0x%02hx worked\n", port);
}

static void expect_gp(unsigned short port)
{
	if (try_outb(port)) {
		printf("[FAIL]\toutb to 0x%02hx worked\n", port);
		exit(1);
	}

	printf("[OK]\toutb to 0x%02hx failed\n", port);
}

int main(void)
{
	cpu_set_t cpuset;
	CPU_ZERO(&cpuset);
	CPU_SET(0, &cpuset);
	if (sched_setaffinity(0, sizeof(cpuset), &cpuset) != 0)
		err(1, "sched_setaffinity to CPU 0");

	expect_gp(0x80);
	expect_gp(0xed);

	/*
	 * Probe for ioperm support.  Note that clearing ioperm bits
	 * works even as nonroot.
	 */
	printf("[RUN]\tenable 0x80\n");
	if (ioperm(0x80, 1, 1) != 0) {
		printf("[OK]\tioperm(0x80, 1, 1) failed (%d) -- try running as root\n",
		       errno);
		return 0;
	}
	expect_ok(0x80);
	expect_gp(0xed);

	printf("[RUN]\tdisable 0x80\n");
	if (ioperm(0x80, 1, 0) != 0) {
		printf("[FAIL]\tioperm(0x80, 1, 0) failed (%d)", errno);
		return 1;
	}
	expect_gp(0x80);
	expect_gp(0xed);

	/* Make sure that fork() preserves ioperm. */
	if (ioperm(0x80, 1, 1) != 0) {
		printf("[FAIL]\tioperm(0x80, 1, 0) failed (%d)", errno);
		return 1;
	}

	pid_t child = fork();
	if (child == -1)
		err(1, "fork");

	if (child == 0) {
		printf("[RUN]\tchild: check that we inherited permissions\n");
		expect_ok(0x80);
		expect_gp(0xed);
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

	/* Test the capability checks. */

	printf("\tDrop privileges\n");
	if (setresuid(1, 1, 1) != 0) {
		printf("[WARN]\tDropping privileges failed\n");
		return 0;
	}

	printf("[RUN]\tdisable 0x80\n");
	if (ioperm(0x80, 1, 0) != 0) {
		printf("[FAIL]\tioperm(0x80, 1, 0) failed (%d)", errno);
		return 1;
	}
	printf("[OK]\tit worked\n");

	printf("[RUN]\tenable 0x80 again\n");
	if (ioperm(0x80, 1, 1) == 0) {
		printf("[FAIL]\tit succeeded but should have failed.\n");
		return 1;
	}
	printf("[OK]\tit failed\n");
	return 0;
}
