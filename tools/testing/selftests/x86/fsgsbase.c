// SPDX-License-Identifier: GPL-2.0-only
/*
 * fsgsbase.c, an fsgsbase test
 * Copyright (c) 2014-2016 Andy Lutomirski
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
#include <stddef.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <setjmp.h>

#ifndef __x86_64__
# error This test is 64-bit only
#endif

static volatile sig_atomic_t want_segv;
static volatile unsigned long segv_addr;

static unsigned short *shared_scratch;

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

static jmp_buf jmpbuf;

static void sigill(int sig, siginfo_t *si, void *ctx_void)
{
	siglongjmp(jmpbuf, 1);
}

static bool have_fsgsbase;

static inline unsigned long rdgsbase(void)
{
	unsigned long gsbase;

	asm volatile("rdgsbase %0" : "=r" (gsbase) :: "memory");

	return gsbase;
}

static inline unsigned long rdfsbase(void)
{
	unsigned long fsbase;

	asm volatile("rdfsbase %0" : "=r" (fsbase) :: "memory");

	return fsbase;
}

static inline void wrgsbase(unsigned long gsbase)
{
	asm volatile("wrgsbase %0" :: "r" (gsbase) : "memory");
}

static inline void wrfsbase(unsigned long fsbase)
{
	asm volatile("wrfsbase %0" :: "r" (fsbase) : "memory");
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

static __thread int set_thread_area_entry_number = -1;

static unsigned short load_gs(void)
{
	/*
	 * Sets GS != 0 and GSBASE != 0 but arranges for the kernel to think
	 * that GSBASE == 0 (i.e. thread.gsbase == 0).
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
		printf("\tusing LDT slot 0\n");
		asm volatile ("mov %0, %%gs" : : "rm" ((unsigned short)0x7));
		return 0x7;
	} else {
		/* No modify_ldt for us (configured out, perhaps) */

		struct user_desc *low_desc = mmap(
			NULL, sizeof(desc),
			PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
		memcpy(low_desc, &desc, sizeof(desc));

		low_desc->entry_number = set_thread_area_entry_number;

		/* 32-bit set_thread_area */
		long ret;
		asm volatile ("int $0x80"
			      : "=a" (ret) : "a" (243), "b" (low_desc)
			      : "r8", "r9", "r10", "r11");
		memcpy(&desc, low_desc, sizeof(desc));
		munmap(low_desc, sizeof(desc));

		if (ret != 0) {
			printf("[NOTE]\tcould not create a segment -- test won't do anything\n");
			return 0;
		}
		printf("\tusing GDT slot %d\n", desc.entry_number);
		set_thread_area_entry_number = desc.entry_number;

		unsigned short gs = (unsigned short)((desc.entry_number << 3) | 0x3);
		asm volatile ("mov %0, %%gs" : : "rm" (gs));
		return gs;
	}
}

void test_wrbase(unsigned short index, unsigned long base)
{
	unsigned short newindex;
	unsigned long newbase;

	printf("[RUN]\tGS = 0x%hx, GSBASE = 0x%lx\n", index, base);

	asm volatile ("mov %0, %%gs" : : "rm" (index));
	wrgsbase(base);

	remote_base = 0;
	ftx = 1;
	syscall(SYS_futex, &ftx, FUTEX_WAKE, 0, NULL, NULL, 0);
	while (ftx != 0)
		syscall(SYS_futex, &ftx, FUTEX_WAIT, 1, NULL, NULL, 0);

	asm volatile ("mov %%gs, %0" : "=rm" (newindex));
	newbase = rdgsbase();

	if (newindex == index && newbase == base) {
		printf("[OK]\tIndex and base were preserved\n");
	} else {
		printf("[FAIL]\tAfter switch, GS = 0x%hx and GSBASE = 0x%lx\n",
		       newindex, newbase);
		nerrs++;
	}
}

static void *threadproc(void *ctx)
{
	while (1) {
		while (ftx == 0)
			syscall(SYS_futex, &ftx, FUTEX_WAIT, 0, NULL, NULL, 0);
		if (ftx == 3)
			return NULL;

		if (ftx == 1) {
			do_remote_base();
		} else if (ftx == 2) {
			/*
			 * On AMD chips, this causes GSBASE != 0, GS == 0, and
			 * thread.gsbase == 0.
			 */

			load_gs();
			asm volatile ("mov %0, %%gs" : : "rm" ((unsigned short)0));
		} else {
			errx(1, "helper thread got bad command");
		}

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

#define USER_REGS_OFFSET(r) offsetof(struct user_regs_struct, r)

static void test_ptrace_write_gsbase(void)
{
	int status;
	pid_t child = fork();

	if (child < 0)
		err(1, "fork");

	if (child == 0) {
		printf("[RUN]\tPTRACE_POKE(), write GSBASE from ptracer\n");

		*shared_scratch = load_gs();

		if (ptrace(PTRACE_TRACEME, 0, NULL, NULL) != 0)
			err(1, "PTRACE_TRACEME");

		raise(SIGTRAP);
		_exit(0);
	}

	wait(&status);

	if (WSTOPSIG(status) == SIGTRAP) {
		unsigned long gs, base;
		unsigned long gs_offset = USER_REGS_OFFSET(gs);
		unsigned long base_offset = USER_REGS_OFFSET(gs_base);

		gs = ptrace(PTRACE_PEEKUSER, child, gs_offset, NULL);

		if (gs != *shared_scratch) {
			nerrs++;
			printf("[FAIL]\tGS is not prepared with nonzero\n");
			goto END;
		}

		if (ptrace(PTRACE_POKEUSER, child, base_offset, 0xFF) != 0)
			err(1, "PTRACE_POKEUSER");

		gs = ptrace(PTRACE_PEEKUSER, child, gs_offset, NULL);
		base = ptrace(PTRACE_PEEKUSER, child, base_offset, NULL);

		/*
		 * In a non-FSGSBASE system, the nonzero selector will load
		 * GSBASE (again). But what is tested here is whether the
		 * selector value is changed or not by the GSBASE write in
		 * a ptracer.
		 */
		if (gs != *shared_scratch) {
			nerrs++;
			printf("[FAIL]\tGS changed to %lx\n", gs);

			/*
			 * On older kernels, poking a nonzero value into the
			 * base would zero the selector.  On newer kernels,
			 * this behavior has changed -- poking the base
			 * changes only the base and, if FSGSBASE is not
			 * available, this may have no effect.
			 */
			if (gs == 0)
				printf("\tNote: this is expected behavior on older kernels.\n");
		} else if (have_fsgsbase && (base != 0xFF)) {
			nerrs++;
			printf("[FAIL]\tGSBASE changed to %lx\n", base);
		} else {
			printf("[OK]\tGS remained 0x%hx%s", *shared_scratch, have_fsgsbase ? " and GSBASE changed to 0xFF" : "");
			printf("\n");
		}
	}

END:
	ptrace(PTRACE_CONT, child, NULL, NULL);
}

int main()
{
	pthread_t thread;

	shared_scratch = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
			      MAP_ANONYMOUS | MAP_SHARED, -1, 0);

	/* Probe FSGSBASE */
	sethandler(SIGILL, sigill, 0);
	if (sigsetjmp(jmpbuf, 1) == 0) {
		rdfsbase();
		have_fsgsbase = true;
		printf("\tFSGSBASE instructions are enabled\n");
	} else {
		printf("\tFSGSBASE instructions are disabled\n");
	}
	clearhandler(SIGILL);

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

	if (have_fsgsbase) {
		unsigned short ss;

		asm volatile ("mov %%ss, %0" : "=rm" (ss));

		test_wrbase(0, 0);
		test_wrbase(0, 1);
		test_wrbase(0, 0x200000000);
		test_wrbase(0, 0xffffffffffffffff);
		test_wrbase(ss, 0);
		test_wrbase(ss, 1);
		test_wrbase(ss, 0x200000000);
		test_wrbase(ss, 0xffffffffffffffff);
	}

	ftx = 3;  /* Kill the thread. */
	syscall(SYS_futex, &ftx, FUTEX_WAKE, 0, NULL, NULL, 0);

	if (pthread_join(thread, NULL) != 0)
		err(1, "pthread_join");

	test_ptrace_write_gsbase();

	return nerrs == 0 ? 0 : 1;
}
