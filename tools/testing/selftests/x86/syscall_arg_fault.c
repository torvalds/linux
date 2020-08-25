// SPDX-License-Identifier: GPL-2.0-only
/*
 * syscall_arg_fault.c - tests faults 32-bit fast syscall stack args
 * Copyright (c) 2015 Andrew Lutomirski
 */

#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/signal.h>
#include <sys/ucontext.h>
#include <err.h>
#include <setjmp.h>
#include <errno.h>

#include "helpers.h"

/* Our sigaltstack scratch space. */
static unsigned char altstack_data[SIGSTKSZ];

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

static volatile sig_atomic_t sig_traps;
static sigjmp_buf jmpbuf;

static volatile sig_atomic_t n_errs;

#ifdef __x86_64__
#define REG_AX REG_RAX
#define REG_IP REG_RIP
#else
#define REG_AX REG_EAX
#define REG_IP REG_EIP
#endif

static void sigsegv_or_sigbus(int sig, siginfo_t *info, void *ctx_void)
{
	ucontext_t *ctx = (ucontext_t*)ctx_void;
	long ax = (long)ctx->uc_mcontext.gregs[REG_AX];

	if (ax != -EFAULT && ax != -ENOSYS) {
		printf("[FAIL]\tAX had the wrong value: 0x%lx\n",
		       (unsigned long)ax);
		printf("\tIP = 0x%lx\n", (unsigned long)ctx->uc_mcontext.gregs[REG_IP]);
		n_errs++;
	} else {
		printf("[OK]\tSeems okay\n");
	}

	siglongjmp(jmpbuf, 1);
}

static volatile sig_atomic_t sigtrap_consecutive_syscalls;

static void sigtrap(int sig, siginfo_t *info, void *ctx_void)
{
	/*
	 * KVM has some bugs that can cause us to stop making progress.
	 * detect them and complain, but don't infinite loop or fail the
	 * test.
	 */

	ucontext_t *ctx = (ucontext_t*)ctx_void;
	unsigned short *ip = (unsigned short *)ctx->uc_mcontext.gregs[REG_IP];

	if (*ip == 0x340f || *ip == 0x050f) {
		/* The trap was on SYSCALL or SYSENTER */
		sigtrap_consecutive_syscalls++;
		if (sigtrap_consecutive_syscalls > 3) {
			printf("[WARN]\tGot stuck single-stepping -- you probably have a KVM bug\n");
			siglongjmp(jmpbuf, 1);
		}
	} else {
		sigtrap_consecutive_syscalls = 0;
	}
}

static void sigill(int sig, siginfo_t *info, void *ctx_void)
{
	ucontext_t *ctx = (ucontext_t*)ctx_void;
	unsigned short *ip = (unsigned short *)ctx->uc_mcontext.gregs[REG_IP];

	if (*ip == 0x0b0f) {
		/* one of the ud2 instructions faulted */
		printf("[OK]\tSYSCALL returned normally\n");
	} else {
		printf("[SKIP]\tIllegal instruction\n");
	}
	siglongjmp(jmpbuf, 1);
}

int main()
{
	stack_t stack = {
		.ss_sp = altstack_data,
		.ss_size = SIGSTKSZ,
	};
	if (sigaltstack(&stack, NULL) != 0)
		err(1, "sigaltstack");

	sethandler(SIGSEGV, sigsegv_or_sigbus, SA_ONSTACK);
	/*
	 * The actual exception can vary.  On Atom CPUs, we get #SS
	 * instead of #PF when the vDSO fails to access the stack when
	 * ESP is too close to 2^32, and #SS causes SIGBUS.
	 */
	sethandler(SIGBUS, sigsegv_or_sigbus, SA_ONSTACK);
	sethandler(SIGILL, sigill, SA_ONSTACK);

	/*
	 * Exercise another nasty special case.  The 32-bit SYSCALL
	 * and SYSENTER instructions (even in compat mode) each
	 * clobber one register.  A Linux system call has a syscall
	 * number and six arguments, and the user stack pointer
	 * needs to live in some register on return.  That means
	 * that we need eight registers, but SYSCALL and SYSENTER
	 * only preserve seven registers.  As a result, one argument
	 * ends up on the stack.  The stack is user memory, which
	 * means that the kernel can fail to read it.
	 *
	 * The 32-bit fast system calls don't have a defined ABI:
	 * we're supposed to invoke them through the vDSO.  So we'll
	 * fudge it: we set all regs to invalid pointer values and
	 * invoke the entry instruction.  The return will fail no
	 * matter what, and we completely lose our program state,
	 * but we can fix it up with a signal handler.
	 */

	printf("[RUN]\tSYSENTER with invalid state\n");
	if (sigsetjmp(jmpbuf, 1) == 0) {
		asm volatile (
			"movl $-1, %%eax\n\t"
			"movl $-1, %%ebx\n\t"
			"movl $-1, %%ecx\n\t"
			"movl $-1, %%edx\n\t"
			"movl $-1, %%esi\n\t"
			"movl $-1, %%edi\n\t"
			"movl $-1, %%ebp\n\t"
			"movl $-1, %%esp\n\t"
			"sysenter"
			: : : "memory", "flags");
	}

	printf("[RUN]\tSYSCALL with invalid state\n");
	if (sigsetjmp(jmpbuf, 1) == 0) {
		asm volatile (
			"movl $-1, %%eax\n\t"
			"movl $-1, %%ebx\n\t"
			"movl $-1, %%ecx\n\t"
			"movl $-1, %%edx\n\t"
			"movl $-1, %%esi\n\t"
			"movl $-1, %%edi\n\t"
			"movl $-1, %%ebp\n\t"
			"movl $-1, %%esp\n\t"
			"syscall\n\t"
			"ud2"		/* make sure we recover cleanly */
			: : : "memory", "flags");
	}

	printf("[RUN]\tSYSENTER with TF and invalid state\n");
	sethandler(SIGTRAP, sigtrap, SA_ONSTACK);

	if (sigsetjmp(jmpbuf, 1) == 0) {
		sigtrap_consecutive_syscalls = 0;
		set_eflags(get_eflags() | X86_EFLAGS_TF);
		asm volatile (
			"movl $-1, %%eax\n\t"
			"movl $-1, %%ebx\n\t"
			"movl $-1, %%ecx\n\t"
			"movl $-1, %%edx\n\t"
			"movl $-1, %%esi\n\t"
			"movl $-1, %%edi\n\t"
			"movl $-1, %%ebp\n\t"
			"movl $-1, %%esp\n\t"
			"sysenter"
			: : : "memory", "flags");
	}
	set_eflags(get_eflags() & ~X86_EFLAGS_TF);

	printf("[RUN]\tSYSCALL with TF and invalid state\n");
	if (sigsetjmp(jmpbuf, 1) == 0) {
		sigtrap_consecutive_syscalls = 0;
		set_eflags(get_eflags() | X86_EFLAGS_TF);
		asm volatile (
			"movl $-1, %%eax\n\t"
			"movl $-1, %%ebx\n\t"
			"movl $-1, %%ecx\n\t"
			"movl $-1, %%edx\n\t"
			"movl $-1, %%esi\n\t"
			"movl $-1, %%edi\n\t"
			"movl $-1, %%ebp\n\t"
			"movl $-1, %%esp\n\t"
			"syscall\n\t"
			"ud2"		/* make sure we recover cleanly */
			: : : "memory", "flags");
	}
	set_eflags(get_eflags() & ~X86_EFLAGS_TF);

#ifdef __x86_64__
	printf("[RUN]\tSYSENTER with TF, invalid state, and GSBASE < 0\n");

	if (sigsetjmp(jmpbuf, 1) == 0) {
		sigtrap_consecutive_syscalls = 0;

		asm volatile ("wrgsbase %%rax\n\t"
			      :: "a" (0xffffffffffff0000UL));

		set_eflags(get_eflags() | X86_EFLAGS_TF);
		asm volatile (
			"movl $-1, %%eax\n\t"
			"movl $-1, %%ebx\n\t"
			"movl $-1, %%ecx\n\t"
			"movl $-1, %%edx\n\t"
			"movl $-1, %%esi\n\t"
			"movl $-1, %%edi\n\t"
			"movl $-1, %%ebp\n\t"
			"movl $-1, %%esp\n\t"
			"sysenter"
			: : : "memory", "flags");
	}
	set_eflags(get_eflags() & ~X86_EFLAGS_TF);
#endif

	return 0;
}
