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
	"vmcode_umip:\n\t"
	/* addressing via displacements */
	"smsw (2052)\n\t"
	"sidt (2054)\n\t"
	"sgdt (2060)\n\t"
	/* addressing via registers */
	"mov $2066, %bx\n\t"
	"smsw (%bx)\n\t"
	"mov $2068, %bx\n\t"
	"sidt (%bx)\n\t"
	"mov $2074, %bx\n\t"
	"sgdt (%bx)\n\t"
	/* register operands, only for smsw */
	"smsw %ax\n\t"
	"mov %ax, (2080)\n\t"
	"int3\n\t"
	"vmcode_umip_str:\n\t"
	"str %eax\n\t"
	"vmcode_umip_sldt:\n\t"
	"sldt %eax\n\t"
	"int3\n\t"
	".size vmcode, . - vmcode\n\t"
	"end_vmcode:\n\t"
	".code32\n\t"
	".popsection"
	);

extern unsigned char vmcode[], end_vmcode[];
extern unsigned char vmcode_bound[], vmcode_sysenter[], vmcode_syscall[],
	vmcode_sti[], vmcode_int3[], vmcode_int80[], vmcode_umip[],
	vmcode_umip_str[], vmcode_umip_sldt[];

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

void do_umip_tests(struct vm86plus_struct *vm86, unsigned char *test_mem)
{
	struct table_desc {
		unsigned short limit;
		unsigned long base;
	} __attribute__((packed));

	/* Initialize variables with arbitrary values */
	struct table_desc gdt1 = { .base = 0x3c3c3c3c, .limit = 0x9999 };
	struct table_desc gdt2 = { .base = 0x1a1a1a1a, .limit = 0xaeae };
	struct table_desc idt1 = { .base = 0x7b7b7b7b, .limit = 0xf1f1 };
	struct table_desc idt2 = { .base = 0x89898989, .limit = 0x1313 };
	unsigned short msw1 = 0x1414, msw2 = 0x2525, msw3 = 3737;

	/* UMIP -- exit with INT3 unless kernel emulation did not trap #GP */
	do_test(vm86, vmcode_umip - vmcode, VM86_TRAP, 3, "UMIP tests");

	/* Results from displacement-only addressing */
	msw1 = *(unsigned short *)(test_mem + 2052);
	memcpy(&idt1, test_mem + 2054, sizeof(idt1));
	memcpy(&gdt1, test_mem + 2060, sizeof(gdt1));

	/* Results from register-indirect addressing */
	msw2 = *(unsigned short *)(test_mem + 2066);
	memcpy(&idt2, test_mem + 2068, sizeof(idt2));
	memcpy(&gdt2, test_mem + 2074, sizeof(gdt2));

	/* Results when using register operands */
	msw3 = *(unsigned short *)(test_mem + 2080);

	printf("[INFO]\tResult from SMSW:[0x%04x]\n", msw1);
	printf("[INFO]\tResult from SIDT: limit[0x%04x]base[0x%08lx]\n",
	       idt1.limit, idt1.base);
	printf("[INFO]\tResult from SGDT: limit[0x%04x]base[0x%08lx]\n",
	       gdt1.limit, gdt1.base);

	if (msw1 != msw2 || msw1 != msw3)
		printf("[FAIL]\tAll the results of SMSW should be the same.\n");
	else
		printf("[PASS]\tAll the results from SMSW are identical.\n");

	if (memcmp(&gdt1, &gdt2, sizeof(gdt1)))
		printf("[FAIL]\tAll the results of SGDT should be the same.\n");
	else
		printf("[PASS]\tAll the results from SGDT are identical.\n");

	if (memcmp(&idt1, &idt2, sizeof(idt1)))
		printf("[FAIL]\tAll the results of SIDT should be the same.\n");
	else
		printf("[PASS]\tAll the results from SIDT are identical.\n");

	sethandler(SIGILL, sighandler, 0);
	do_test(vm86, vmcode_umip_str - vmcode, VM86_SIGNAL, 0,
		"STR instruction");
	clearhandler(SIGILL);

	sethandler(SIGILL, sighandler, 0);
	do_test(vm86, vmcode_umip_sldt - vmcode, VM86_SIGNAL, 0,
		"SLDT instruction");
	clearhandler(SIGILL);
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

	/* UMIP -- should exit with INTx 0x80 unless UMIP was not disabled */
	do_umip_tests(&v86, addr);

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
