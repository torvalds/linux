// SPDX-License-Identifier: GPL-2.0-only
/*
 * single_step_syscall.c - single-steps various x86 syscalls
 * Copyright (c) 2014-2015 Andrew Lutomirski
 *
 * This is a very simple series of tests that makes system calls with
 * the TF flag set.  This exercises some nasty kernel code in the
 * SYSENTER case: SYSENTER does not clear TF, so SYSENTER with TF set
 * immediately issues #DB from CPL 0.  This requires special handling in
 * the kernel.
 */

#define _GNU_SOURCE

#include <sys/time.h>
#include <time.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <sys/mman.h>
#include <sys/signal.h>
#include <sys/ucontext.h>
#include <asm/ldt.h>
#include <err.h>
#include <setjmp.h>
#include <stddef.h>
#include <stdbool.h>
#include <sys/ptrace.h>
#include <sys/user.h>

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

static volatile sig_atomic_t sig_traps, sig_eflags;
sigjmp_buf jmpbuf;
static unsigned char altstack_data[SIGSTKSZ];

#ifdef __x86_64__
# define REG_IP REG_RIP
# define WIDTH "q"
# define INT80_CLOBBERS "r8", "r9", "r10", "r11"
#else
# define REG_IP REG_EIP
# define WIDTH "l"
# define INT80_CLOBBERS
#endif

static unsigned long get_eflags(void)
{
	unsigned long eflags;
	asm volatile ("pushf" WIDTH "\n\tpop" WIDTH " %0" : "=rm" (eflags));
	return eflags;
}

static void set_eflags(unsigned long eflags)
{
	asm volatile ("push" WIDTH " %0\n\tpopf" WIDTH
		      : : "rm" (eflags) : "flags");
}

#define X86_EFLAGS_TF (1UL << 8)

static void sigtrap(int sig, siginfo_t *info, void *ctx_void)
{
	ucontext_t *ctx = (ucontext_t*)ctx_void;

	if (get_eflags() & X86_EFLAGS_TF) {
		set_eflags(get_eflags() & ~X86_EFLAGS_TF);
		printf("[WARN]\tSIGTRAP handler had TF set\n");
		_exit(1);
	}

	sig_traps++;

	if (sig_traps == 10000 || sig_traps == 10001) {
		printf("[WARN]\tHit %d SIGTRAPs with si_addr 0x%lx, ip 0x%lx\n",
		       (int)sig_traps,
		       (unsigned long)info->si_addr,
		       (unsigned long)ctx->uc_mcontext.gregs[REG_IP]);
	}
}

static char const * const signames[] = {
	[SIGSEGV] = "SIGSEGV",
	[SIGBUS] = "SIBGUS",
	[SIGTRAP] = "SIGTRAP",
	[SIGILL] = "SIGILL",
};

static void print_and_longjmp(int sig, siginfo_t *si, void *ctx_void)
{
	ucontext_t *ctx = ctx_void;

	printf("\tGot %s with RIP=%lx, TF=%ld\n", signames[sig],
	       (unsigned long)ctx->uc_mcontext.gregs[REG_IP],
	       (unsigned long)ctx->uc_mcontext.gregs[REG_EFL] & X86_EFLAGS_TF);

	sig_eflags = (unsigned long)ctx->uc_mcontext.gregs[REG_EFL];
	siglongjmp(jmpbuf, 1);
}

static void check_result(void)
{
	unsigned long new_eflags = get_eflags();
	set_eflags(new_eflags & ~X86_EFLAGS_TF);

	if (!sig_traps) {
		printf("[FAIL]\tNo SIGTRAP\n");
		exit(1);
	}

	if (!(new_eflags & X86_EFLAGS_TF)) {
		printf("[FAIL]\tTF was cleared\n");
		exit(1);
	}

	printf("[OK]\tSurvived with TF set and %d traps\n", (int)sig_traps);
	sig_traps = 0;
}

static void fast_syscall_no_tf(void)
{
	sig_traps = 0;
	printf("[RUN]\tFast syscall with TF cleared\n");
	fflush(stdout);  /* Force a syscall */
	if (get_eflags() & X86_EFLAGS_TF) {
		printf("[FAIL]\tTF is now set\n");
		exit(1);
	}
	if (sig_traps) {
		printf("[FAIL]\tGot SIGTRAP\n");
		exit(1);
	}
	printf("[OK]\tNothing unexpected happened\n");
}

int main()
{
#ifdef CAN_BUILD_32
	int tmp;
#endif

	sethandler(SIGTRAP, sigtrap, 0);

	printf("[RUN]\tSet TF and check nop\n");
	set_eflags(get_eflags() | X86_EFLAGS_TF);
	asm volatile ("nop");
	check_result();

#ifdef __x86_64__
	printf("[RUN]\tSet TF and check syscall-less opportunistic sysret\n");
	set_eflags(get_eflags() | X86_EFLAGS_TF);
	extern unsigned char post_nop[];
	asm volatile ("pushf" WIDTH "\n\t"
		      "pop" WIDTH " %%r11\n\t"
		      "nop\n\t"
		      "post_nop:"
		      : : "c" (post_nop) : "r11");
	check_result();
#endif
#ifdef CAN_BUILD_32
	printf("[RUN]\tSet TF and check int80\n");
	set_eflags(get_eflags() | X86_EFLAGS_TF);
	asm volatile ("int $0x80" : "=a" (tmp) : "a" (SYS_getpid)
			: INT80_CLOBBERS);
	check_result();
#endif

	/*
	 * This test is particularly interesting if fast syscalls use
	 * SYSENTER: it triggers a nasty design flaw in SYSENTER.
	 * Specifically, SYSENTER does not clear TF, so either SYSENTER
	 * or the next instruction traps at CPL0.  (Of course, Intel
	 * mostly forgot to document exactly what happens here.)  So we
	 * get a CPL0 fault with usergs (on 64-bit kernels) and possibly
	 * no stack.  The only sane way the kernel can possibly handle
	 * it is to clear TF on return from the #DB handler, but this
	 * happens way too early to set TF in the saved pt_regs, so the
	 * kernel has to do something clever to avoid losing track of
	 * the TF bit.
	 *
	 * Needless to say, we've had bugs in this area.
	 */
	syscall(SYS_getpid);  /* Force symbol binding without TF set. */
	printf("[RUN]\tSet TF and check a fast syscall\n");
	set_eflags(get_eflags() | X86_EFLAGS_TF);
	syscall(SYS_getpid);
	check_result();

	/* Now make sure that another fast syscall doesn't set TF again. */
	fast_syscall_no_tf();

	/*
	 * And do a forced SYSENTER to make sure that this works even if
	 * fast syscalls don't use SYSENTER.
	 *
	 * Invoking SYSENTER directly breaks all the rules.  Just handle
	 * the SIGSEGV.
	 */
	if (sigsetjmp(jmpbuf, 1) == 0) {
		unsigned long nr = SYS_getpid;
		printf("[RUN]\tSet TF and check SYSENTER\n");
		stack_t stack = {
			.ss_sp = altstack_data,
			.ss_size = SIGSTKSZ,
		};
		if (sigaltstack(&stack, NULL) != 0)
			err(1, "sigaltstack");
		sethandler(SIGSEGV, print_and_longjmp,
			   SA_RESETHAND | SA_ONSTACK);
		sethandler(SIGILL, print_and_longjmp, SA_RESETHAND);
		set_eflags(get_eflags() | X86_EFLAGS_TF);
		/* Clear EBP first to make sure we segfault cleanly. */
		asm volatile ("xorl %%ebp, %%ebp; SYSENTER" : "+a" (nr) :: "flags", "rcx"
#ifdef __x86_64__
				, "r11"
#endif
			);

		/* We're unreachable here.  SYSENTER forgets RIP. */
	}
	clearhandler(SIGSEGV);
	clearhandler(SIGILL);
	if (!(sig_eflags & X86_EFLAGS_TF)) {
		printf("[FAIL]\tTF was cleared\n");
		exit(1);
	}

	/* Now make sure that another fast syscall doesn't set TF again. */
	fast_syscall_no_tf();

	return 0;
}
