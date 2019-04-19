/*
 * syscall_arg_fault.c - tests faults 32-bit fast syscall stack args
 * Copyright (c) 2015 Andrew Lutomirski
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
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

static void sigsegv_or_sigbus(int sig, siginfo_t *info, void *ctx_void)
{
	ucontext_t *ctx = (ucontext_t*)ctx_void;

	if (ctx->uc_mcontext.gregs[REG_EAX] != -EFAULT) {
		printf("[FAIL]\tAX had the wrong value: 0x%x\n",
		       ctx->uc_mcontext.gregs[REG_EAX]);
		n_errs++;
	} else {
		printf("[OK]\tSeems okay\n");
	}

	siglongjmp(jmpbuf, 1);
}

static void sigill(int sig, siginfo_t *info, void *ctx_void)
{
	printf("[SKIP]\tIllegal instruction\n");
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
			"pushl $0"	/* make sure we segfault cleanly */
			: : : "memory", "flags");
	}

	return 0;
}
