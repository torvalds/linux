/*
 * fsgsbase.c, an fsgsbase test
 * Copyright (c) 2014-2016 Andy Lutomirski
 * GPL v2
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <err.h>
#include <sys/user.h>
#include <asm/prctl.h>
#include <sys/prctl.h>
#include <signal.h>
#include <limits.h>
#include <sys/ucontext.h>
#include <sched.h>
#include <linux/futex.h>
#include <pthread.h>
#include <asm/ldt.h>
#include <sys/mman.h>

#ifndef __x86_64__
# error This test is 64-bit only
#endif

static volatile sig_atomic_t want_segv;
static volatile unsigned long segv_addr;

static int nerrs;

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

static void sigsegv(int sig, siginfo_t *si, void *ctx_void)
{
	ucontext_t *ctx = (ucontext_t*)ctx_void;

	if (!want_segv) {
		clearhandler(SIGSEGV);
		return;  /* Crash cleanly. */
	}

	want_segv = false;
	segv_addr = (unsigned long)si->si_addr;

	ctx->uc_mcontext.gregs[REG_RIP] += 4;	/* Skip the faulting mov */

}

enum which_base { FS, GS };

static unsigned long read_base(enum which_base which)
{
	unsigned long offset;
	/*
	 * Unless we have FSGSBASE, there's no direct way to do this from
	 * user mode.  We can get at it indirectly using signals, though.
	 */

	want_segv = true;

	offset = 0;
	if (which == FS) {
		/* Use a constant-length instruction here. */
		asm volatile ("mov %%fs:(%%rcx), %%rax" : : "c" (offset) : "rax");
	} else {
		asm volatile ("mov %%gs:(%%rcx), %%rax" : : "c" (offset) : "rax");
	}
	if (!want_segv)
		return segv_addr + offset;

	/*
	 * If that didn't segfault, try the other end of the address space.
	 * Unless we get really unlucky and run into the vsyscall page, this
	 * is guaranteed to segfault.
	 */

	offset = (ULONG_MAX >> 1) + 1;
	if (which == FS) {
		asm volatile ("mov %%fs:(%%rcx), %%rax"
			      : : "c" (offset) : "rax");
	} else {
		asm volatile ("mov %%gs:(%%rcx), %%rax"
			      : : "c" (offset) : "rax");
	}
	if (!want_segv)
		return segv_addr + offset;

	abort();
}

static void check_gs_value(unsigned long value)
{
	unsigned long base;
	unsigned short sel;

	printf("[RUN]\tARCH_SET_GS to 0x%lx\n", value);
	if (syscall(SYS_arch_prctl, ARCH_SET_GS, value) != 0)
		err(1, "ARCH_SET_GS");

	asm volatile ("mov %%gs, %0" : "=rm" (sel));
	base = read_base(GS);
	if (base == value) {
		printf("[OK]\tGSBASE was set as expected (selector 0x%hx)\n",
		       sel);
	} else {
		nerrs++;
		printf("[FAIL]\tGSBASE was not as expected: got 0x%lx (selector 0x%hx)\n",
		       base, sel);
	}

	if (syscall(SYS_arch_prctl, ARCH_GET_GS, &base) != 0)
		err(1, "ARCH_GET_GS");
	if (base == value) {
		printf("[OK]\tARCH_GET_GS worked as expected (selector 0x%hx)\n",
		       sel);
	} else {
		nerrs++;
		printf("[FAIL]\tARCH_GET_GS was not as expected: got 0x%lx (selector 0x%hx)\n",
		       base, sel);
	}
}

static void mov_0_gs(unsigned long initial_base, bool schedule)
{
	unsigned long base, arch_base;

	printf("[RUN]\tARCH_SET_GS to 0x%lx then mov 0 to %%gs%s\n", initial_base, schedule ? " and schedule " : "");
	if (syscall(SYS_arch_prctl, ARCH_SET_GS, initial_base) != 0)
		err(1, "ARCH_SET_GS");

	if (schedule)
		usleep(10);

	asm volatile ("mov %0, %%gs" : : "rm" (0));
	base = read_base(GS);
	if (syscall(SYS_arch_prctl, ARCH_GET_GS, &arch_base) != 0)
		err(1, "ARCH_GET_GS");
	if (base == arch_base) {
		printf("[OK]\tGSBASE is 0x%lx\n", base);
	} else {
		nerrs++;
		printf("[FAIL]\tGSBASE changed to 0x%lx but kernel reports 0x%lx\n", base, arch_base);
	}
}

static volatile unsigned long remote_base;
static volatile bool remote_hard_zero;
static volatile unsigned int ftx;

/*
 * ARCH_SET_FS/GS(0) may or may not program a selector of zero.  HARD_ZERO
 * means to force the selector to zero to improve test coverage.
 */
#define HARD_ZERO 0xa1fa5f343cb85fa4

static void do_remote_base()
{
	unsigned long to_set = remote_base;
	bool hard_zero = false;
	if (to_set == HARD_ZERO) {
		to_set = 0;
		hard_zero = true;
	}

	if (syscall(SYS_arch_prctl, ARCH_SET_GS, to_set) != 0)
		err(1, "ARCH_SET_GS");

	if (hard_zero)
		asm volatile ("mov %0, %%gs" : : "rm" ((unsigned short)0));

	unsigned short sel;
	asm volatile ("mov %%gs, %0" : "=rm" (sel));
	printf("\tother thread: ARCH_SET_GS(0x%lx)%s -- sel is 0x%hx\n",
	       to_set, hard_zero ? " and clear gs" : "", sel);
}

void do_unexpected_base(void)
{
	/*
	 * The goal here is to try to arrange for GS == 0, GSBASE !=
	 * 0, and for the the kernel the think that GSBASE == 0.
	 *
	 * To make the test as reliable as possible, this uses
	 * explicit descriptorss.  (This is not the only way.  This
	 * could use ARCH_SET_GS with a low, nonzero base, but the
	 * relevant side effect of ARCH_SET_GS could change.)
	 */

	/* Step 1: tell the kernel that we have GSBASE == 0. */
	if (syscall(SYS_arch_prctl, ARCH_SET_GS, 0) != 0)
		err(1, "ARCH_SET_GS");

	/* Step 2: change GSBASE without telling the kernel. */
	struct user_desc desc = {
		.entry_number    = 0,
		.base_addr       = 0xBAADF00D,
		.limit           = 0xfffff,
		.seg_32bit       = 1,
		.contents        = 0, /* Data, grow-up */
		.read_exec_only  = 0,
		.limit_in_pages  = 1,
		.seg_not_present = 0,
		.useable         = 0
	};
	if (syscall(SYS_modify_ldt, 1, &desc, sizeof(desc)) == 0) {
		printf("\tother thread: using LDT slot 0\n");
		asm volatile ("mov %0, %%gs" : : "rm" ((unsigned short)0x7));
	} else {
		/* No modify_ldt for us (configured out, perhaps) */

		struct user_desc *low_desc = mmap(
			NULL, sizeof(desc),
			PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
		memcpy(low_desc, &desc, sizeof(desc));

		low_desc->entry_number = -1;

		/* 32-bit set_thread_area */
		long ret;
		asm volatile ("int $0x80"
			      : "=a" (ret) : "a" (243), "b" (low_desc)
			      : "r8", "r9", "r10", "r11");
		memcpy(&desc, low_desc, sizeof(desc));
		munmap(low_desc, sizeof(desc));

		if (ret != 0) {
			printf("[NOTE]\tcould not create a segment -- test won't do anything\n");
			return;
		}
		printf("\tother thread: using GDT slot %d\n", desc.entry_number);
		asm volatile ("mov %0, %%gs" : : "rm" ((unsigned short)((desc.entry_number << 3) | 0x3)));
	}

	/*
	 * Step 3: set the selector back to zero.  On AMD chips, this will
	 * preserve GSBASE.
	 */

	asm volatile ("mov %0, %%gs" : : "rm" ((unsigned short)0));
}

static void *threadproc(void *ctx)
{
	while (1) {
		while (ftx == 0)
			syscall(SYS_futex, &ftx, FUTEX_WAIT, 0, NULL, NULL, 0);
		if (ftx == 3)
			return NULL;

		if (ftx == 1)
			do_remote_base();
		else if (ftx == 2)
			do_unexpected_base();
		else
			errx(1, "helper thread got bad command");

		ftx = 0;
		syscall(SYS_futex, &ftx, FUTEX_WAKE, 0, NULL, NULL, 0);
	}
}

static void set_gs_and_switch_to(unsigned long local,
				 unsigned short force_sel,
				 unsigned long remote)
{
	unsigned long base;
	unsigned short sel_pre_sched, sel_post_sched;

	bool hard_zero = false;
	if (local == HARD_ZERO) {
		hard_zero = true;
		local = 0;
	}

	printf("[RUN]\tARCH_SET_GS(0x%lx)%s, then schedule to 0x%lx\n",
	       local, hard_zero ? " and clear gs" : "", remote);
	if (force_sel)
		printf("\tBefore schedule, set selector to 0x%hx\n", force_sel);
	if (syscall(SYS_arch_prctl, ARCH_SET_GS, local) != 0)
		err(1, "ARCH_SET_GS");
	if (hard_zero)
		asm volatile ("mov %0, %%gs" : : "rm" ((unsigned short)0));

	if (read_base(GS) != local) {
		nerrs++;
		printf("[FAIL]\tGSBASE wasn't set as expected\n");
	}

	if (force_sel) {
		asm volatile ("mov %0, %%gs" : : "rm" (force_sel));
		sel_pre_sched = force_sel;
		local = read_base(GS);

		/*
		 * Signal delivery seems to mess up weird selectors.  Put it
		 * back.
		 */
		asm volatile ("mov %0, %%gs" : : "rm" (force_sel));
	} else {
		asm volatile ("mov %%gs, %0" : "=rm" (sel_pre_sched));
	}

	remote_base = remote;
	ftx = 1;
	syscall(SYS_futex, &ftx, FUTEX_WAKE, 0, NULL, NULL, 0);
	while (ftx != 0)
		syscall(SYS_futex, &ftx, FUTEX_WAIT, 1, NULL, NULL, 0);

	asm volatile ("mov %%gs, %0" : "=rm" (sel_post_sched));
	base = read_base(GS);
	if (base == local && sel_pre_sched == sel_post_sched) {
		printf("[OK]\tGS/BASE remained 0x%hx/0x%lx\n",
		       sel_pre_sched, local);
	} else {
		nerrs++;
		printf("[FAIL]\tGS/BASE changed from 0x%hx/0x%lx to 0x%hx/0x%lx\n",
		       sel_pre_sched, local, sel_post_sched, base);
	}
}

static void test_unexpected_base(void)
{
	unsigned long base;

	printf("[RUN]\tARCH_SET_GS(0), clear gs, then manipulate GSBASE in a different thread\n");
	if (syscall(SYS_arch_prctl, ARCH_SET_GS, 0) != 0)
		err(1, "ARCH_SET_GS");
	asm volatile ("mov %0, %%gs" : : "rm" ((unsigned short)0));

	ftx = 2;
	syscall(SYS_futex, &ftx, FUTEX_WAKE, 0, NULL, NULL, 0);
	while (ftx != 0)
		syscall(SYS_futex, &ftx, FUTEX_WAIT, 1, NULL, NULL, 0);

	base = read_base(GS);
	if (base == 0) {
		printf("[OK]\tGSBASE remained 0\n");
	} else {
		nerrs++;
		printf("[FAIL]\tGSBASE changed to 0x%lx\n", base);
	}
}

int main()
{
	pthread_t thread;

	sethandler(SIGSEGV, sigsegv, 0);

	check_gs_value(0);
	check_gs_value(1);
	check_gs_value(0x200000000);
	check_gs_value(0);
	check_gs_value(0x200000000);
	check_gs_value(1);

	for (int sched = 0; sched < 2; sched++) {
		mov_0_gs(0, !!sched);
		mov_0_gs(1, !!sched);
		mov_0_gs(0x200000000, !!sched);
	}

	/* Set up for multithreading. */

	cpu_set_t cpuset;
	CPU_ZERO(&cpuset);
	CPU_SET(0, &cpuset);
	if (sched_setaffinity(0, sizeof(cpuset), &cpuset) != 0)
		err(1, "sched_setaffinity to CPU 0");	/* should never fail */

	if (pthread_create(&thread, 0, threadproc, 0) != 0)
		err(1, "pthread_create");

	static unsigned long bases_with_hard_zero[] = {
		0, HARD_ZERO, 1, 0x200000000,
	};

	for (int local = 0; local < 4; local++) {
		for (int remote = 0; remote < 4; remote++) {
			for (unsigned short s = 0; s < 5; s++) {
				unsigned short sel = s;
				if (s == 4)
					asm ("mov %%ss, %0" : "=rm" (sel));
				set_gs_and_switch_to(
					bases_with_hard_zero[local],
					sel,
					bases_with_hard_zero[remote]);
			}
		}
	}

	test_unexpected_base();

	ftx = 3;  /* Kill the thread. */
	syscall(SYS_futex, &ftx, FUTEX_WAKE, 0, NULL, NULL, 0);

	if (pthread_join(thread, NULL) != 0)
		err(1, "pthread_join");

	return nerrs == 0 ? 0 : 1;
}
