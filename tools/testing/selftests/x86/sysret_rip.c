// SPDX-License-Identifier: GPL-2.0-only
/*
 * sigreturn.c - tests that x86 avoids Intel SYSRET pitfalls
 * Copyright (c) 2014-2016 Andrew Lutomirski
 */

#define _GNU_SOURCE

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <sys/signal.h>
#include <sys/ucontext.h>
#include <sys/syscall.h>
#include <err.h>
#include <stddef.h>
#include <stdbool.h>
#include <setjmp.h>
#include <sys/user.h>
#include <sys/mman.h>
#include <assert.h>


asm (
	".pushsection \".text\", \"ax\"\n\t"
	".balign 4096\n\t"
	"test_page: .globl test_page\n\t"
	".fill 4094,1,0xcc\n\t"
	"test_syscall_insn:\n\t"
	"syscall\n\t"
	".ifne . - test_page - 4096\n\t"
	".error \"test page is not one page long\"\n\t"
	".endif\n\t"
	".popsection"
    );

extern const char test_page[];
static void const *current_test_page_addr = test_page;

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

/* State used by our signal handlers. */
static gregset_t initial_regs;

static volatile unsigned long rip;

static void sigsegv_for_sigreturn_test(int sig, siginfo_t *info, void *ctx_void)
{
	ucontext_t *ctx = (ucontext_t*)ctx_void;

	if (rip != ctx->uc_mcontext.gregs[REG_RIP]) {
		printf("[FAIL]\tRequested RIP=0x%lx but got RIP=0x%lx\n",
		       rip, (unsigned long)ctx->uc_mcontext.gregs[REG_RIP]);
		fflush(stdout);
		_exit(1);
	}

	memcpy(&ctx->uc_mcontext.gregs, &initial_regs, sizeof(gregset_t));

	printf("[OK]\tGot SIGSEGV at RIP=0x%lx\n", rip);
}

static void sigusr1(int sig, siginfo_t *info, void *ctx_void)
{
	ucontext_t *ctx = (ucontext_t*)ctx_void;

	memcpy(&initial_regs, &ctx->uc_mcontext.gregs, sizeof(gregset_t));

	/* Set IP and CX to match so that SYSRET can happen. */
	ctx->uc_mcontext.gregs[REG_RIP] = rip;
	ctx->uc_mcontext.gregs[REG_RCX] = rip;

	/* R11 and EFLAGS should already match. */
	assert(ctx->uc_mcontext.gregs[REG_EFL] ==
	       ctx->uc_mcontext.gregs[REG_R11]);

	sethandler(SIGSEGV, sigsegv_for_sigreturn_test, SA_RESETHAND);

	return;
}

static void test_sigreturn_to(unsigned long ip)
{
	rip = ip;
	printf("[RUN]\tsigreturn to 0x%lx\n", ip);
	raise(SIGUSR1);
}

static jmp_buf jmpbuf;

static void sigsegv_for_fallthrough(int sig, siginfo_t *info, void *ctx_void)
{
	ucontext_t *ctx = (ucontext_t*)ctx_void;

	if (rip != ctx->uc_mcontext.gregs[REG_RIP]) {
		printf("[FAIL]\tExpected SIGSEGV at 0x%lx but got RIP=0x%lx\n",
		       rip, (unsigned long)ctx->uc_mcontext.gregs[REG_RIP]);
		fflush(stdout);
		_exit(1);
	}

	siglongjmp(jmpbuf, 1);
}

static void test_syscall_fallthrough_to(unsigned long ip)
{
	void *new_address = (void *)(ip - 4096);
	void *ret;

	printf("[RUN]\tTrying a SYSCALL that falls through to 0x%lx\n", ip);

	ret = mremap((void *)current_test_page_addr, 4096, 4096,
		     MREMAP_MAYMOVE | MREMAP_FIXED, new_address);
	if (ret == MAP_FAILED) {
		if (ip <= (1UL << 47) - PAGE_SIZE) {
			err(1, "mremap to %p", new_address);
		} else {
			printf("[OK]\tmremap to %p failed\n", new_address);
			return;
		}
	}

	if (ret != new_address)
		errx(1, "mremap malfunctioned: asked for %p but got %p\n",
		     new_address, ret);

	current_test_page_addr = new_address;
	rip = ip;

	if (sigsetjmp(jmpbuf, 1) == 0) {
		asm volatile ("call *%[syscall_insn]" :: "a" (SYS_getpid),
			      [syscall_insn] "rm" (ip - 2));
		errx(1, "[FAIL]\tSyscall trampoline returned");
	}

	printf("[OK]\tWe survived\n");
}

int main()
{
	/*
	 * When the kernel returns from a slow-path syscall, it will
	 * detect whether SYSRET is appropriate.  If it incorrectly
	 * thinks that SYSRET is appropriate when RIP is noncanonical,
	 * it'll crash on Intel CPUs.
	 */
	sethandler(SIGUSR1, sigusr1, 0);
	for (int i = 47; i < 64; i++)
		test_sigreturn_to(1UL<<i);

	clearhandler(SIGUSR1);

	sethandler(SIGSEGV, sigsegv_for_fallthrough, 0);

	/* One extra test to check that we didn't screw up the mremap logic. */
	test_syscall_fallthrough_to((1UL << 47) - 2*PAGE_SIZE);

	/* These are the interesting cases. */
	for (int i = 47; i < 64; i++) {
		test_syscall_fallthrough_to((1UL<<i) - PAGE_SIZE);
		test_syscall_fallthrough_to(1UL<<i);
	}

	return 0;
}
