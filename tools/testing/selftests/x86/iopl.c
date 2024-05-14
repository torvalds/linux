// SPDX-License-Identifier: GPL-2.0
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

static void expect_ok_outb(unsigned short port)
{
	if (!try_outb(port)) {
		printf("[FAIL]\toutb to 0x%02hx failed\n", port);
		exit(1);
	}

	printf("[OK]\toutb to 0x%02hx worked\n", port);
}

static void expect_gp_outb(unsigned short port)
{
	if (try_outb(port)) {
		printf("[FAIL]\toutb to 0x%02hx worked\n", port);
		nerrs++;
	}

	printf("[OK]\toutb to 0x%02hx failed\n", port);
}

#define RET_FAULTED	0
#define RET_FAIL	1
#define RET_EMUL	2

static int try_cli(void)
{
	unsigned long flags;

	sethandler(SIGSEGV, sigsegv, SA_RESETHAND);
	if (sigsetjmp(jmpbuf, 1) != 0) {
		return RET_FAULTED;
	} else {
		asm volatile("cli; pushf; pop %[flags]"
				: [flags] "=rm" (flags));

		/* X86_FLAGS_IF */
		if (!(flags & (1 << 9)))
			return RET_FAIL;
		else
			return RET_EMUL;
	}
	clearhandler(SIGSEGV);
}

static int try_sti(bool irqs_off)
{
	unsigned long flags;

	sethandler(SIGSEGV, sigsegv, SA_RESETHAND);
	if (sigsetjmp(jmpbuf, 1) != 0) {
		return RET_FAULTED;
	} else {
		asm volatile("sti; pushf; pop %[flags]"
				: [flags] "=rm" (flags));

		/* X86_FLAGS_IF */
		if (irqs_off && (flags & (1 << 9)))
			return RET_FAIL;
		else
			return RET_EMUL;
	}
	clearhandler(SIGSEGV);
}

static void expect_gp_sti(bool irqs_off)
{
	int ret = try_sti(irqs_off);

	switch (ret) {
	case RET_FAULTED:
		printf("[OK]\tSTI faulted\n");
		break;
	case RET_EMUL:
		printf("[OK]\tSTI NOPped\n");
		break;
	default:
		printf("[FAIL]\tSTI worked\n");
		nerrs++;
	}
}

/*
 * Returns whether it managed to disable interrupts.
 */
static bool test_cli(void)
{
	int ret = try_cli();

	switch (ret) {
	case RET_FAULTED:
		printf("[OK]\tCLI faulted\n");
		break;
	case RET_EMUL:
		printf("[OK]\tCLI NOPped\n");
		break;
	default:
		printf("[FAIL]\tCLI worked\n");
		nerrs++;
		return true;
	}

	return false;
}

int main(void)
{
	cpu_set_t cpuset;

	CPU_ZERO(&cpuset);
	CPU_SET(0, &cpuset);
	if (sched_setaffinity(0, sizeof(cpuset), &cpuset) != 0)
		err(1, "sched_setaffinity to CPU 0");

	/* Probe for iopl support.  Note that iopl(0) works even as nonroot. */
	switch(iopl(3)) {
	case 0:
		break;
	case -ENOSYS:
		printf("[OK]\tiopl() nor supported\n");
		return 0;
	default:
		printf("[OK]\tiopl(3) failed (%d) -- try running as root\n",
		       errno);
		return 0;
	}

	/* Make sure that CLI/STI are blocked even with IOPL level 3 */
	expect_gp_sti(test_cli());
	expect_ok_outb(0x80);

	/* Establish an I/O bitmap to test the restore */
	if (ioperm(0x80, 1, 1) != 0)
		err(1, "ioperm(0x80, 1, 1) failed\n");

	/* Restore our original state prior to starting the fork test. */
	if (iopl(0) != 0)
		err(1, "iopl(0)");

	/*
	 * Verify that IOPL emulation is disabled and the I/O bitmap still
	 * works.
	 */
	expect_ok_outb(0x80);
	expect_gp_outb(0xed);
	/* Drop the I/O bitmap */
	if (ioperm(0x80, 1, 0) != 0)
		err(1, "ioperm(0x80, 1, 0) failed\n");

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

	expect_gp_outb(0x80);
	expect_gp_sti(test_cli());

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
