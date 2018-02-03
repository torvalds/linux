// SPDX-License-Identifier: GPL-2.0
/*
 * ldt_gdt.c - Test cases for LDT and GDT access
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
#include <sys/syscall.h>
#include <asm/ldt.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdbool.h>
#include <pthread.h>
#include <sched.h>
#include <linux/futex.h>
#include <sys/mman.h>
#include <asm/prctl.h>
#include <sys/prctl.h>

#define AR_ACCESSED		(1<<8)

#define AR_TYPE_RODATA		(0 * (1<<9))
#define AR_TYPE_RWDATA		(1 * (1<<9))
#define AR_TYPE_RODATA_EXPDOWN	(2 * (1<<9))
#define AR_TYPE_RWDATA_EXPDOWN	(3 * (1<<9))
#define AR_TYPE_XOCODE		(4 * (1<<9))
#define AR_TYPE_XRCODE		(5 * (1<<9))
#define AR_TYPE_XOCODE_CONF	(6 * (1<<9))
#define AR_TYPE_XRCODE_CONF	(7 * (1<<9))

#define AR_DPL3			(3 * (1<<13))

#define AR_S			(1 << 12)
#define AR_P			(1 << 15)
#define AR_AVL			(1 << 20)
#define AR_L			(1 << 21)
#define AR_DB			(1 << 22)
#define AR_G			(1 << 23)

#ifdef __x86_64__
# define INT80_CLOBBERS "r8", "r9", "r10", "r11"
#else
# define INT80_CLOBBERS
#endif

static int nerrs;

/* Points to an array of 1024 ints, each holding its own index. */
static const unsigned int *counter_page;
static struct user_desc *low_user_desc;
static struct user_desc *low_user_desc_clear;  /* Use to delete GDT entry */
static int gdt_entry_num;

static void check_invalid_segment(uint16_t index, int ldt)
{
	uint32_t has_limit = 0, has_ar = 0, limit, ar;
	uint32_t selector = (index << 3) | (ldt << 2) | 3;

	asm ("lsl %[selector], %[limit]\n\t"
	     "jnz 1f\n\t"
	     "movl $1, %[has_limit]\n\t"
	     "1:"
	     : [limit] "=r" (limit), [has_limit] "+rm" (has_limit)
	     : [selector] "r" (selector));
	asm ("larl %[selector], %[ar]\n\t"
	     "jnz 1f\n\t"
	     "movl $1, %[has_ar]\n\t"
	     "1:"
	     : [ar] "=r" (ar), [has_ar] "+rm" (has_ar)
	     : [selector] "r" (selector));

	if (has_limit || has_ar) {
		printf("[FAIL]\t%s entry %hu is valid but should be invalid\n",
		       (ldt ? "LDT" : "GDT"), index);
		nerrs++;
	} else {
		printf("[OK]\t%s entry %hu is invalid\n",
		       (ldt ? "LDT" : "GDT"), index);
	}
}

static void check_valid_segment(uint16_t index, int ldt,
				uint32_t expected_ar, uint32_t expected_limit,
				bool verbose)
{
	uint32_t has_limit = 0, has_ar = 0, limit, ar;
	uint32_t selector = (index << 3) | (ldt << 2) | 3;

	asm ("lsl %[selector], %[limit]\n\t"
	     "jnz 1f\n\t"
	     "movl $1, %[has_limit]\n\t"
	     "1:"
	     : [limit] "=r" (limit), [has_limit] "+rm" (has_limit)
	     : [selector] "r" (selector));
	asm ("larl %[selector], %[ar]\n\t"
	     "jnz 1f\n\t"
	     "movl $1, %[has_ar]\n\t"
	     "1:"
	     : [ar] "=r" (ar), [has_ar] "+rm" (has_ar)
	     : [selector] "r" (selector));

	if (!has_limit || !has_ar) {
		printf("[FAIL]\t%s entry %hu is invalid but should be valid\n",
		       (ldt ? "LDT" : "GDT"), index);
		nerrs++;
		return;
	}

	/* The SDM says "bits 19:16 are undefined".  Thanks. */
	ar &= ~0xF0000;

	/*
	 * NB: Different Linux versions do different things with the
	 * accessed bit in set_thread_area().
	 */
	if (ar != expected_ar &&
	    (ldt || ar != (expected_ar | AR_ACCESSED))) {
		printf("[FAIL]\t%s entry %hu has AR 0x%08X but expected 0x%08X\n",
		       (ldt ? "LDT" : "GDT"), index, ar, expected_ar);
		nerrs++;
	} else if (limit != expected_limit) {
		printf("[FAIL]\t%s entry %hu has limit 0x%08X but expected 0x%08X\n",
		       (ldt ? "LDT" : "GDT"), index, limit, expected_limit);
		nerrs++;
	} else if (verbose) {
		printf("[OK]\t%s entry %hu has AR 0x%08X and limit 0x%08X\n",
		       (ldt ? "LDT" : "GDT"), index, ar, limit);
	}
}

static bool install_valid_mode(const struct user_desc *desc, uint32_t ar,
			       bool oldmode)
{
	int ret = syscall(SYS_modify_ldt, oldmode ? 1 : 0x11,
			  desc, sizeof(*desc));
	if (ret < -1)
		errno = -ret;
	if (ret == 0) {
		uint32_t limit = desc->limit;
		if (desc->limit_in_pages)
			limit = (limit << 12) + 4095;
		check_valid_segment(desc->entry_number, 1, ar, limit, true);
		return true;
	} else if (errno == ENOSYS) {
		printf("[OK]\tmodify_ldt returned -ENOSYS\n");
		return false;
	} else {
		if (desc->seg_32bit) {
			printf("[FAIL]\tUnexpected modify_ldt failure %d\n",
			       errno);
			nerrs++;
			return false;
		} else {
			printf("[OK]\tmodify_ldt rejected 16 bit segment\n");
			return false;
		}
	}
}

static bool install_valid(const struct user_desc *desc, uint32_t ar)
{
	return install_valid_mode(desc, ar, false);
}

static void install_invalid(const struct user_desc *desc, bool oldmode)
{
	int ret = syscall(SYS_modify_ldt, oldmode ? 1 : 0x11,
			  desc, sizeof(*desc));
	if (ret < -1)
		errno = -ret;
	if (ret == 0) {
		check_invalid_segment(desc->entry_number, 1);
	} else if (errno == ENOSYS) {
		printf("[OK]\tmodify_ldt returned -ENOSYS\n");
	} else {
		if (desc->seg_32bit) {
			printf("[FAIL]\tUnexpected modify_ldt failure %d\n",
			       errno);
			nerrs++;
		} else {
			printf("[OK]\tmodify_ldt rejected 16 bit segment\n");
		}
	}
}

static int safe_modify_ldt(int func, struct user_desc *ptr,
			   unsigned long bytecount)
{
	int ret = syscall(SYS_modify_ldt, 0x11, ptr, bytecount);
	if (ret < -1)
		errno = -ret;
	return ret;
}

static void fail_install(struct user_desc *desc)
{
	if (safe_modify_ldt(0x11, desc, sizeof(*desc)) == 0) {
		printf("[FAIL]\tmodify_ldt accepted a bad descriptor\n");
		nerrs++;
	} else if (errno == ENOSYS) {
		printf("[OK]\tmodify_ldt returned -ENOSYS\n");
	} else {
		printf("[OK]\tmodify_ldt failure %d\n", errno);
	}
}

static void do_simple_tests(void)
{
	struct user_desc desc = {
		.entry_number    = 0,
		.base_addr       = 0,
		.limit           = 10,
		.seg_32bit       = 1,
		.contents        = 2, /* Code, not conforming */
		.read_exec_only  = 0,
		.limit_in_pages  = 0,
		.seg_not_present = 0,
		.useable         = 0
	};
	install_valid(&desc, AR_DPL3 | AR_TYPE_XRCODE | AR_S | AR_P | AR_DB);

	desc.limit_in_pages = 1;
	install_valid(&desc, AR_DPL3 | AR_TYPE_XRCODE |
		      AR_S | AR_P | AR_DB | AR_G);

	check_invalid_segment(1, 1);

	desc.entry_number = 2;
	install_valid(&desc, AR_DPL3 | AR_TYPE_XRCODE |
		      AR_S | AR_P | AR_DB | AR_G);

	check_invalid_segment(1, 1);

	desc.base_addr = 0xf0000000;
	install_valid(&desc, AR_DPL3 | AR_TYPE_XRCODE |
		      AR_S | AR_P | AR_DB | AR_G);

	desc.useable = 1;
	install_valid(&desc, AR_DPL3 | AR_TYPE_XRCODE |
		      AR_S | AR_P | AR_DB | AR_G | AR_AVL);

	desc.seg_not_present = 1;
	install_valid(&desc, AR_DPL3 | AR_TYPE_XRCODE |
		      AR_S | AR_DB | AR_G | AR_AVL);

	desc.seg_32bit = 0;
	install_valid(&desc, AR_DPL3 | AR_TYPE_XRCODE |
		      AR_S | AR_G | AR_AVL);

	desc.seg_32bit = 1;
	desc.contents = 0;
	install_valid(&desc, AR_DPL3 | AR_TYPE_RWDATA |
		      AR_S | AR_DB | AR_G | AR_AVL);

	desc.read_exec_only = 1;
	install_valid(&desc, AR_DPL3 | AR_TYPE_RODATA |
		      AR_S | AR_DB | AR_G | AR_AVL);

	desc.contents = 1;
	install_valid(&desc, AR_DPL3 | AR_TYPE_RODATA_EXPDOWN |
		      AR_S | AR_DB | AR_G | AR_AVL);

	desc.read_exec_only = 0;
	desc.limit_in_pages = 0;
	install_valid(&desc, AR_DPL3 | AR_TYPE_RWDATA_EXPDOWN |
		      AR_S | AR_DB | AR_AVL);

	desc.contents = 3;
	install_valid(&desc, AR_DPL3 | AR_TYPE_XRCODE_CONF |
		      AR_S | AR_DB | AR_AVL);

	desc.read_exec_only = 1;
	install_valid(&desc, AR_DPL3 | AR_TYPE_XOCODE_CONF |
		      AR_S | AR_DB | AR_AVL);

	desc.read_exec_only = 0;
	desc.contents = 2;
	install_valid(&desc, AR_DPL3 | AR_TYPE_XRCODE |
		      AR_S | AR_DB | AR_AVL);

	desc.read_exec_only = 1;

#ifdef __x86_64__
	desc.lm = 1;
	install_valid(&desc, AR_DPL3 | AR_TYPE_XOCODE |
		      AR_S | AR_DB | AR_AVL);
	desc.lm = 0;
#endif

	bool entry1_okay = install_valid(&desc, AR_DPL3 | AR_TYPE_XOCODE |
					 AR_S | AR_DB | AR_AVL);

	if (entry1_okay) {
		printf("[RUN]\tTest fork\n");
		pid_t child = fork();
		if (child == 0) {
			nerrs = 0;
			check_valid_segment(desc.entry_number, 1,
					    AR_DPL3 | AR_TYPE_XOCODE |
					    AR_S | AR_DB | AR_AVL, desc.limit,
					    true);
			check_invalid_segment(1, 1);
			exit(nerrs ? 1 : 0);
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

		printf("[RUN]\tTest size\n");
		int i;
		for (i = 0; i < 8192; i++) {
			desc.entry_number = i;
			desc.limit = i;
			if (safe_modify_ldt(0x11, &desc, sizeof(desc)) != 0) {
				printf("[FAIL]\tFailed to install entry %d\n", i);
				nerrs++;
				break;
			}
		}
		for (int j = 0; j < i; j++) {
			check_valid_segment(j, 1, AR_DPL3 | AR_TYPE_XOCODE |
					    AR_S | AR_DB | AR_AVL, j, false);
		}
		printf("[DONE]\tSize test\n");
	} else {
		printf("[SKIP]\tSkipping fork and size tests because we have no LDT\n");
	}

	/* Test entry_number too high. */
	desc.entry_number = 8192;
	fail_install(&desc);

	/* Test deletion and actions mistakeable for deletion. */
	memset(&desc, 0, sizeof(desc));
	install_valid(&desc, AR_DPL3 | AR_TYPE_RWDATA | AR_S | AR_P);

	desc.seg_not_present = 1;
	install_valid(&desc, AR_DPL3 | AR_TYPE_RWDATA | AR_S);

	desc.seg_not_present = 0;
	desc.read_exec_only = 1;
	install_valid(&desc, AR_DPL3 | AR_TYPE_RODATA | AR_S | AR_P);

	desc.read_exec_only = 0;
	desc.seg_not_present = 1;
	install_valid(&desc, AR_DPL3 | AR_TYPE_RWDATA | AR_S);

	desc.read_exec_only = 1;
	desc.limit = 1;
	install_valid(&desc, AR_DPL3 | AR_TYPE_RODATA | AR_S);

	desc.limit = 0;
	desc.base_addr = 1;
	install_valid(&desc, AR_DPL3 | AR_TYPE_RODATA | AR_S);

	desc.base_addr = 0;
	install_invalid(&desc, false);

	desc.seg_not_present = 0;
	desc.seg_32bit = 1;
	desc.read_exec_only = 0;
	desc.limit = 0xfffff;

	install_valid(&desc, AR_DPL3 | AR_TYPE_RWDATA | AR_S | AR_P | AR_DB);

	desc.limit_in_pages = 1;

	install_valid(&desc, AR_DPL3 | AR_TYPE_RWDATA | AR_S | AR_P | AR_DB | AR_G);
	desc.read_exec_only = 1;
	install_valid(&desc, AR_DPL3 | AR_TYPE_RODATA | AR_S | AR_P | AR_DB | AR_G);
	desc.contents = 1;
	desc.read_exec_only = 0;
	install_valid(&desc, AR_DPL3 | AR_TYPE_RWDATA_EXPDOWN | AR_S | AR_P | AR_DB | AR_G);
	desc.read_exec_only = 1;
	install_valid(&desc, AR_DPL3 | AR_TYPE_RODATA_EXPDOWN | AR_S | AR_P | AR_DB | AR_G);

	desc.limit = 0;
	install_invalid(&desc, true);
}

/*
 * 0: thread is idle
 * 1: thread armed
 * 2: thread should clear LDT entry 0
 * 3: thread should exit
 */
static volatile unsigned int ftx;

static void *threadproc(void *ctx)
{
	cpu_set_t cpuset;
	CPU_ZERO(&cpuset);
	CPU_SET(1, &cpuset);
	if (sched_setaffinity(0, sizeof(cpuset), &cpuset) != 0)
		err(1, "sched_setaffinity to CPU 1");	/* should never fail */

	while (1) {
		syscall(SYS_futex, &ftx, FUTEX_WAIT, 0, NULL, NULL, 0);
		while (ftx != 2) {
			if (ftx >= 3)
				return NULL;
		}

		/* clear LDT entry 0 */
		const struct user_desc desc = {};
		if (syscall(SYS_modify_ldt, 1, &desc, sizeof(desc)) != 0)
			err(1, "modify_ldt");

		/* If ftx == 2, set it to zero.  If ftx == 100, quit. */
		unsigned int x = -2;
		asm volatile ("lock xaddl %[x], %[ftx]" :
			      [x] "+r" (x), [ftx] "+m" (ftx));
		if (x != 2)
			return NULL;
	}
}

#ifdef __i386__

#ifndef SA_RESTORE
#define SA_RESTORER 0x04000000
#endif

/*
 * The UAPI header calls this 'struct sigaction', which conflicts with
 * glibc.  Sigh.
 */
struct fake_ksigaction {
	void *handler;  /* the real type is nasty */
	unsigned long sa_flags;
	void (*sa_restorer)(void);
	unsigned char sigset[8];
};

static void fix_sa_restorer(int sig)
{
	struct fake_ksigaction ksa;

	if (syscall(SYS_rt_sigaction, sig, NULL, &ksa, 8) == 0) {
		/*
		 * glibc has a nasty bug: it sometimes writes garbage to
		 * sa_restorer.  This interacts quite badly with anything
		 * that fiddles with SS because it can trigger legacy
		 * stack switching.  Patch it up.  See:
		 *
		 * https://sourceware.org/bugzilla/show_bug.cgi?id=21269
		 */
		if (!(ksa.sa_flags & SA_RESTORER) && ksa.sa_restorer) {
			ksa.sa_restorer = NULL;
			if (syscall(SYS_rt_sigaction, sig, &ksa, NULL,
				    sizeof(ksa.sigset)) != 0)
				err(1, "rt_sigaction");
		}
	}
}
#else
static void fix_sa_restorer(int sig)
{
	/* 64-bit glibc works fine. */
}
#endif

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

	fix_sa_restorer(sig);
}

static jmp_buf jmpbuf;

static void sigsegv(int sig, siginfo_t *info, void *ctx_void)
{
	siglongjmp(jmpbuf, 1);
}

static void do_multicpu_tests(void)
{
	cpu_set_t cpuset;
	pthread_t thread;
	int failures = 0, iters = 5, i;
	unsigned short orig_ss;

	CPU_ZERO(&cpuset);
	CPU_SET(1, &cpuset);
	if (sched_setaffinity(0, sizeof(cpuset), &cpuset) != 0) {
		printf("[SKIP]\tCannot set affinity to CPU 1\n");
		return;
	}

	CPU_ZERO(&cpuset);
	CPU_SET(0, &cpuset);
	if (sched_setaffinity(0, sizeof(cpuset), &cpuset) != 0) {
		printf("[SKIP]\tCannot set affinity to CPU 0\n");
		return;
	}

	sethandler(SIGSEGV, sigsegv, 0);
#ifdef __i386__
	/* True 32-bit kernels send SIGILL instead of SIGSEGV on IRET faults. */
	sethandler(SIGILL, sigsegv, 0);
#endif

	printf("[RUN]\tCross-CPU LDT invalidation\n");

	if (pthread_create(&thread, 0, threadproc, 0) != 0)
		err(1, "pthread_create");

	asm volatile ("mov %%ss, %0" : "=rm" (orig_ss));

	for (i = 0; i < 5; i++) {
		if (sigsetjmp(jmpbuf, 1) != 0)
			continue;

		/* Make sure the thread is ready after the last test. */
		while (ftx != 0)
			;

		struct user_desc desc = {
			.entry_number    = 0,
			.base_addr       = 0,
			.limit           = 0xfffff,
			.seg_32bit       = 1,
			.contents        = 0, /* Data */
			.read_exec_only  = 0,
			.limit_in_pages  = 1,
			.seg_not_present = 0,
			.useable         = 0
		};

		if (safe_modify_ldt(0x11, &desc, sizeof(desc)) != 0) {
			if (errno != ENOSYS)
				err(1, "modify_ldt");
			printf("[SKIP]\tmodify_ldt unavailable\n");
			break;
		}

		/* Arm the thread. */
		ftx = 1;
		syscall(SYS_futex, &ftx, FUTEX_WAKE, 0, NULL, NULL, 0);

		asm volatile ("mov %0, %%ss" : : "r" (0x7));

		/* Go! */
		ftx = 2;

		while (ftx != 0)
			;

		/*
		 * On success, modify_ldt will segfault us synchronously,
		 * and we'll escape via siglongjmp.
		 */

		failures++;
		asm volatile ("mov %0, %%ss" : : "rm" (orig_ss));
	};

	ftx = 100;  /* Kill the thread. */
	syscall(SYS_futex, &ftx, FUTEX_WAKE, 0, NULL, NULL, 0);

	if (pthread_join(thread, NULL) != 0)
		err(1, "pthread_join");

	if (failures) {
		printf("[FAIL]\t%d of %d iterations failed\n", failures, iters);
		nerrs++;
	} else {
		printf("[OK]\tAll %d iterations succeeded\n", iters);
	}
}

static int finish_exec_test(void)
{
	/*
	 * In a sensible world, this would be check_invalid_segment(0, 1);
	 * For better or for worse, though, the LDT is inherited across exec.
	 * We can probably change this safely, but for now we test it.
	 */
	check_valid_segment(0, 1,
			    AR_DPL3 | AR_TYPE_XRCODE | AR_S | AR_P | AR_DB,
			    42, true);

	return nerrs ? 1 : 0;
}

static void do_exec_test(void)
{
	printf("[RUN]\tTest exec\n");

	struct user_desc desc = {
		.entry_number    = 0,
		.base_addr       = 0,
		.limit           = 42,
		.seg_32bit       = 1,
		.contents        = 2, /* Code, not conforming */
		.read_exec_only  = 0,
		.limit_in_pages  = 0,
		.seg_not_present = 0,
		.useable         = 0
	};
	install_valid(&desc, AR_DPL3 | AR_TYPE_XRCODE | AR_S | AR_P | AR_DB);

	pid_t child = fork();
	if (child == 0) {
		execl("/proc/self/exe", "ldt_gdt_test_exec", NULL);
		printf("[FAIL]\tCould not exec self\n");
		exit(1);	/* exec failed */
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
}

static void setup_counter_page(void)
{
	unsigned int *page = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
			 MAP_ANONYMOUS | MAP_PRIVATE | MAP_32BIT, -1, 0);
	if (page == MAP_FAILED)
		err(1, "mmap");

	for (int i = 0; i < 1024; i++)
		page[i] = i;
	counter_page = page;
}

static int invoke_set_thread_area(void)
{
	int ret;
	asm volatile ("int $0x80"
		      : "=a" (ret), "+m" (low_user_desc) :
			"a" (243), "b" (low_user_desc)
		      : INT80_CLOBBERS);
	return ret;
}

static void setup_low_user_desc(void)
{
	low_user_desc = mmap(NULL, 2 * sizeof(struct user_desc),
			     PROT_READ | PROT_WRITE,
			     MAP_ANONYMOUS | MAP_PRIVATE | MAP_32BIT, -1, 0);
	if (low_user_desc == MAP_FAILED)
		err(1, "mmap");

	low_user_desc->entry_number	= -1;
	low_user_desc->base_addr	= (unsigned long)&counter_page[1];
	low_user_desc->limit		= 0xfffff;
	low_user_desc->seg_32bit	= 1;
	low_user_desc->contents		= 0; /* Data, grow-up*/
	low_user_desc->read_exec_only	= 0;
	low_user_desc->limit_in_pages	= 1;
	low_user_desc->seg_not_present	= 0;
	low_user_desc->useable		= 0;

	if (invoke_set_thread_area() == 0) {
		gdt_entry_num = low_user_desc->entry_number;
		printf("[NOTE]\tset_thread_area is available; will use GDT index %d\n", gdt_entry_num);
	} else {
		printf("[NOTE]\tset_thread_area is unavailable\n");
	}

	low_user_desc_clear = low_user_desc + 1;
	low_user_desc_clear->entry_number = gdt_entry_num;
	low_user_desc_clear->read_exec_only = 1;
	low_user_desc_clear->seg_not_present = 1;
}

static void test_gdt_invalidation(void)
{
	if (!gdt_entry_num)
		return;	/* 64-bit only system -- we can't use set_thread_area */

	unsigned short prev_sel;
	unsigned short sel;
	unsigned int eax;
	const char *result;
#ifdef __x86_64__
	unsigned long saved_base;
	unsigned long new_base;
#endif

	/* Test DS */
	invoke_set_thread_area();
	eax = 243;
	sel = (gdt_entry_num << 3) | 3;
	asm volatile ("movw %%ds, %[prev_sel]\n\t"
		      "movw %[sel], %%ds\n\t"
#ifdef __i386__
		      "pushl %%ebx\n\t"
#endif
		      "movl %[arg1], %%ebx\n\t"
		      "int $0x80\n\t"	/* Should invalidate ds */
#ifdef __i386__
		      "popl %%ebx\n\t"
#endif
		      "movw %%ds, %[sel]\n\t"
		      "movw %[prev_sel], %%ds"
		      : [prev_sel] "=&r" (prev_sel), [sel] "+r" (sel),
			"+a" (eax)
		      : "m" (low_user_desc_clear),
			[arg1] "r" ((unsigned int)(unsigned long)low_user_desc_clear)
		      : INT80_CLOBBERS);

	if (sel != 0) {
		result = "FAIL";
		nerrs++;
	} else {
		result = "OK";
	}
	printf("[%s]\tInvalidate DS with set_thread_area: new DS = 0x%hx\n",
	       result, sel);

	/* Test ES */
	invoke_set_thread_area();
	eax = 243;
	sel = (gdt_entry_num << 3) | 3;
	asm volatile ("movw %%es, %[prev_sel]\n\t"
		      "movw %[sel], %%es\n\t"
#ifdef __i386__
		      "pushl %%ebx\n\t"
#endif
		      "movl %[arg1], %%ebx\n\t"
		      "int $0x80\n\t"	/* Should invalidate es */
#ifdef __i386__
		      "popl %%ebx\n\t"
#endif
		      "movw %%es, %[sel]\n\t"
		      "movw %[prev_sel], %%es"
		      : [prev_sel] "=&r" (prev_sel), [sel] "+r" (sel),
			"+a" (eax)
		      : "m" (low_user_desc_clear),
			[arg1] "r" ((unsigned int)(unsigned long)low_user_desc_clear)
		      : INT80_CLOBBERS);

	if (sel != 0) {
		result = "FAIL";
		nerrs++;
	} else {
		result = "OK";
	}
	printf("[%s]\tInvalidate ES with set_thread_area: new ES = 0x%hx\n",
	       result, sel);

	/* Test FS */
	invoke_set_thread_area();
	eax = 243;
	sel = (gdt_entry_num << 3) | 3;
#ifdef __x86_64__
	syscall(SYS_arch_prctl, ARCH_GET_FS, &saved_base);
#endif
	asm volatile ("movw %%fs, %[prev_sel]\n\t"
		      "movw %[sel], %%fs\n\t"
#ifdef __i386__
		      "pushl %%ebx\n\t"
#endif
		      "movl %[arg1], %%ebx\n\t"
		      "int $0x80\n\t"	/* Should invalidate fs */
#ifdef __i386__
		      "popl %%ebx\n\t"
#endif
		      "movw %%fs, %[sel]\n\t"
		      : [prev_sel] "=&r" (prev_sel), [sel] "+r" (sel),
			"+a" (eax)
		      : "m" (low_user_desc_clear),
			[arg1] "r" ((unsigned int)(unsigned long)low_user_desc_clear)
		      : INT80_CLOBBERS);

#ifdef __x86_64__
	syscall(SYS_arch_prctl, ARCH_GET_FS, &new_base);
#endif

	/* Restore FS/BASE for glibc */
	asm volatile ("movw %[prev_sel], %%fs" : : [prev_sel] "rm" (prev_sel));
#ifdef __x86_64__
	if (saved_base)
		syscall(SYS_arch_prctl, ARCH_SET_FS, saved_base);
#endif

	if (sel != 0) {
		result = "FAIL";
		nerrs++;
	} else {
		result = "OK";
	}
	printf("[%s]\tInvalidate FS with set_thread_area: new FS = 0x%hx\n",
	       result, sel);

#ifdef __x86_64__
	if (sel == 0 && new_base != 0) {
		nerrs++;
		printf("[FAIL]\tNew FSBASE was 0x%lx\n", new_base);
	} else {
		printf("[OK]\tNew FSBASE was zero\n");
	}
#endif

	/* Test GS */
	invoke_set_thread_area();
	eax = 243;
	sel = (gdt_entry_num << 3) | 3;
#ifdef __x86_64__
	syscall(SYS_arch_prctl, ARCH_GET_GS, &saved_base);
#endif
	asm volatile ("movw %%gs, %[prev_sel]\n\t"
		      "movw %[sel], %%gs\n\t"
#ifdef __i386__
		      "pushl %%ebx\n\t"
#endif
		      "movl %[arg1], %%ebx\n\t"
		      "int $0x80\n\t"	/* Should invalidate gs */
#ifdef __i386__
		      "popl %%ebx\n\t"
#endif
		      "movw %%gs, %[sel]\n\t"
		      : [prev_sel] "=&r" (prev_sel), [sel] "+r" (sel),
			"+a" (eax)
		      : "m" (low_user_desc_clear),
			[arg1] "r" ((unsigned int)(unsigned long)low_user_desc_clear)
		      : INT80_CLOBBERS);

#ifdef __x86_64__
	syscall(SYS_arch_prctl, ARCH_GET_GS, &new_base);
#endif

	/* Restore GS/BASE for glibc */
	asm volatile ("movw %[prev_sel], %%gs" : : [prev_sel] "rm" (prev_sel));
#ifdef __x86_64__
	if (saved_base)
		syscall(SYS_arch_prctl, ARCH_SET_GS, saved_base);
#endif

	if (sel != 0) {
		result = "FAIL";
		nerrs++;
	} else {
		result = "OK";
	}
	printf("[%s]\tInvalidate GS with set_thread_area: new GS = 0x%hx\n",
	       result, sel);

#ifdef __x86_64__
	if (sel == 0 && new_base != 0) {
		nerrs++;
		printf("[FAIL]\tNew GSBASE was 0x%lx\n", new_base);
	} else {
		printf("[OK]\tNew GSBASE was zero\n");
	}
#endif
}

int main(int argc, char **argv)
{
	if (argc == 1 && !strcmp(argv[0], "ldt_gdt_test_exec"))
		return finish_exec_test();

	setup_counter_page();
	setup_low_user_desc();

	do_simple_tests();

	do_multicpu_tests();

	do_exec_test();

	test_gdt_invalidation();

	return nerrs ? 1 : 0;
}
