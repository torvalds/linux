/*
 * entry_from_vm86.c - tests kernel entries from vm86 mode
 * Copyright (c) 2014-2015 Andrew Lutomirski
 *
 * This exercises a few paths that need to special-case vm86 mode.
 *
 * GPL v2.
 */

#define _GNU_SOURCE

#include <assert.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <sys/signal.h>
#include <sys/ucontext.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <sys/mman.h>
#include <err.h>
#include <stddef.h>
#include <stdbool.h>
#include <errno.h>
#include <sys/vm86.h>

static unsigned long load_addr = 0x10000;
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

static sig_atomic_t got_signal;

static void sighandler(int sig, siginfo_t *info, void *ctx_void)
{
	ucontext_t *ctx = (ucontext_t*)ctx_void;

	if (ctx->uc_mcontext.gregs[REG_EFL] & X86_EFLAGS_VM ||
	    (ctx->uc_mcontext.gregs[REG_CS] & 3) != 3) {
		printf("[FAIL]\tSignal frame should not reflect vm86 mode\n");
		nerrs++;
	}

	const char *signame;
	if (sig == SIGSEGV)
		signame = "SIGSEGV";
	else if (sig == SIGILL)
		signame = "SIGILL";
	else
		signame = "unexpected signal";

	printf("[INFO]\t%s: FLAGS = 0x%lx, CS = 0x%hx\n", signame,
	       (unsigned long)ctx->uc_mcontext.gregs[REG_EFL],
	       (unsigned short)ctx->uc_mcontext.gregs[REG_CS]);

	got_signal = 1;
}

asm (
	".pushsection .rodata\n\t"
	".type vmcode_bound, @object\n\t"
	"vmcode:\n\t"
	"vmcode_bound:\n\t"
	".code16\n\t"
	"bound %ax, (2048)\n\t"
	"int3\n\t"
	"vmcode_sysenter:\n\t"
	"sysenter\n\t"
	"vmcode_syscall:\n\t"
	"syscall\n\t"
	"vmcode_sti:\n\t"
	"sti\n\t"
	"vmcode_int3:\n\t"
	"int3\n\t"
	"vmcode_int80:\n\t"
	"int $0x80\n\t"
	".size vmcode, . - vmcode\n\t"
	"end_vmcode:\n\t"
	".code32\n\t"
	".popsection"
	);

extern unsigned char vmcode[], end_vmcode[];
extern unsigned char vmcode_bound[], vmcode_sysenter[], vmcode_syscall[],
	vmcode_sti[], vmcode_int3[], vmcode_int80[];

/* Returns false if the test was skipped. */
static bool do_test(struct vm86plus_struct *v86, unsigned long eip,
		    unsigned int rettype, unsigned int retarg,
		    const char *text)
{
	long ret;

	printf("[RUN]\t%s from vm86 mode\n", text);
	v86->regs.eip = eip;
	ret = vm86(VM86_ENTER, v86);

	if (ret == -1 && (errno == ENOSYS || errno == EPERM)) {
		printf("[SKIP]\tvm86 %s\n",
		       errno == ENOSYS ? "not supported" : "not allowed");
		return false;
	}

	if (VM86_TYPE(ret) == VM86_INTx) {
		char trapname[32];
		int trapno = VM86_ARG(ret);
		if (trapno == 13)
			strcpy(trapname, "GP");
		else if (trapno == 5)
			strcpy(trapname, "BR");
		else if (trapno == 14)
			strcpy(trapname, "PF");
		else
			sprintf(trapname, "%d", trapno);

		printf("[INFO]\tExited vm86 mode due to #%s\n", trapname);
	} else if (VM86_TYPE(ret) == VM86_UNKNOWN) {
		printf("[INFO]\tExited vm86 mode due to unhandled GP fault\n");
	} else if (VM86_TYPE(ret) == VM86_TRAP) {
		printf("[INFO]\tExited vm86 mode due to a trap (arg=%ld)\n",
		       VM86_ARG(ret));
	} else if (VM86_TYPE(ret) == VM86_SIGNAL) {
		printf("[INFO]\tExited vm86 mode due to a signal\n");
	} else if (VM86_TYPE(ret) == VM86_STI) {
		printf("[INFO]\tExited vm86 mode due to STI\n");
	} else {
		printf("[INFO]\tExited vm86 mode due to type %ld, arg %ld\n",
		       VM86_TYPE(ret), VM86_ARG(ret));
	}

	if (rettype == -1 ||
	    (VM86_TYPE(ret) == rettype && VM86_ARG(ret) == retarg)) {
		printf("[OK]\tReturned correctly\n");
	} else {
		printf("[FAIL]\tIncorrect return reason\n");
		nerrs++;
	}

	return true;
}

int main(void)
{
	struct vm86plus_struct v86;
	unsigned char *addr = mmap((void *)load_addr, 4096,
				   PROT_READ | PROT_WRITE | PROT_EXEC,
				   MAP_ANONYMOUS | MAP_PRIVATE, -1,0);
	if (addr != (unsigned char *)load_addr)
		err(1, "mmap");

	memcpy(addr, vmcode, end_vmcode - vmcode);
	addr[2048] = 2;
	addr[2050] = 3;

	memset(&v86, 0, sizeof(v86));

	v86.regs.cs = load_addr / 16;
	v86.regs.ss = load_addr / 16;
	v86.regs.ds = load_addr / 16;
	v86.regs.es = load_addr / 16;

	assert((v86.regs.cs & 3) == 0);	/* Looks like RPL = 0 */

	/* #BR -- should deliver SIG??? */
	do_test(&v86, vmcode_bound - vmcode, VM86_INTx, 5, "#BR");

	/*
	 * SYSENTER -- should cause #GP or #UD depending on CPU.
	 * Expected return type -1 means that we shouldn't validate
	 * the vm86 return value.  This will avoid problems on non-SEP
	 * CPUs.
	 */
	sethandler(SIGILL, sighandler, 0);
	do_test(&v86, vmcode_sysenter - vmcode, -1, 0, "SYSENTER");
	clearhandler(SIGILL);

	/*
	 * SYSCALL would be a disaster in VM86 mode.  Fortunately,
	 * there is no kernel that both enables SYSCALL and sets
	 * EFER.SCE, so it's #UD on all systems.  But vm86 is
	 * buggy (or has a "feature"), so the SIGILL will actually
	 * be delivered.
	 */
	sethandler(SIGILL, sighandler, 0);
	do_test(&v86, vmcode_syscall - vmcode, VM86_SIGNAL, 0, "SYSCALL");
	clearhandler(SIGILL);

	/* STI with VIP set */
	v86.regs.eflags |= X86_EFLAGS_VIP;
	v86.regs.eflags &= ~X86_EFLAGS_IF;
	do_test(&v86, vmcode_sti - vmcode, VM86_STI, 0, "STI with VIP set");

	/* INT3 -- should cause #BP */
	do_test(&v86, vmcode_int3 - vmcode, VM86_TRAP, 3, "INT3");

	/* INT80 -- should exit with "INTx 0x80" */
	v86.regs.eax = (unsigned int)-1;
	do_test(&v86, vmcode_int80 - vmcode, VM86_INTx, 0x80, "int80");

	/* Execute a null pointer */
	v86.regs.cs = 0;
	v86.regs.ss = 0;
	sethandler(SIGSEGV, sighandler, 0);
	got_signal = 0;
	if (do_test(&v86, 0, VM86_SIGNAL, 0, "Execute null pointer") &&
	    !got_signal) {
		printf("[FAIL]\tDid not receive SIGSEGV\n");
		nerrs++;
	}
	clearhandler(SIGSEGV);

	/* Make sure nothing explodes if we fork. */
	if (fork() > 0)
		return 0;

	return (nerrs == 0 ? 0 : 1);
}
