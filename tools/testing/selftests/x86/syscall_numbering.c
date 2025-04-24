/* SPDX-License-Identifier: GPL-2.0 */
/*
 * syscall_numbering.c - test calling the x86-64 kernel with various
 * valid and invalid system call numbers.
 *
 * Copyright (c) 2018 Andrew Lutomirski
 */

#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <sysexits.h>

#include <sys/ptrace.h>
#include <sys/user.h>
#include <sys/wait.h>
#include <sys/mman.h>

#include <linux/ptrace.h>
#include "../kselftest.h"

/* Common system call numbers */
#define SYS_READ	  0
#define SYS_WRITE	  1
#define SYS_GETPID	 39
/* x64-only system call numbers */
#define X64_IOCTL	 16
#define X64_READV	 19
#define X64_WRITEV	 20
/* x32-only system call numbers (without X32_BIT) */
#define X32_IOCTL	514
#define X32_READV	515
#define X32_WRITEV	516

#define X32_BIT 0x40000000

static int nullfd = -1;		/* File descriptor for /dev/null */
static bool with_x32;		/* x32 supported on this kernel? */

enum ptrace_pass {
	PTP_NOTHING,
	PTP_GETREGS,
	PTP_WRITEBACK,
	PTP_FUZZRET,
	PTP_FUZZHIGH,
	PTP_INTNUM,
	PTP_DONE
};

static const char * const ptrace_pass_name[] =
{
	[PTP_NOTHING]	= "just stop, no data read",
	[PTP_GETREGS]	= "only getregs",
	[PTP_WRITEBACK]	= "getregs, unmodified setregs",
	[PTP_FUZZRET]	= "modifying the default return",
	[PTP_FUZZHIGH]	= "clobbering the top 32 bits",
	[PTP_INTNUM]	= "sign-extending the syscall number",
};

/*
 * Shared memory block between tracer and test
 */
struct shared {
	unsigned int nerr;	/* Total error count */
	unsigned int indent;	/* Message indentation level */
	enum ptrace_pass ptrace_pass;
	bool probing_syscall;	/* In probe_syscall() */
};
static volatile struct shared *sh;

static inline unsigned int offset(void)
{
	unsigned int level = sh ? sh->indent : 0;

	return 8 + level * 4;
}

#define msg(lvl, fmt, ...) printf("%-*s" fmt, offset(), "[" #lvl "]", \
				  ## __VA_ARGS__)

#define run(fmt, ...)  msg(RUN,  fmt, ## __VA_ARGS__)
#define info(fmt, ...) msg(INFO, fmt, ## __VA_ARGS__)
#define ok(fmt, ...)   msg(OK,   fmt, ## __VA_ARGS__)

#define fail(fmt, ...)					\
	do {						\
		msg(FAIL, fmt, ## __VA_ARGS__);		\
		sh->nerr++;				\
       } while (0)

#define crit(fmt, ...)					\
	do {						\
		sh->indent = 0;				\
		msg(FAIL, fmt, ## __VA_ARGS__);		\
		msg(SKIP, "Unable to run test\n");	\
		exit(EX_OSERR);				\
       } while (0)

/* Sentinel for ptrace-modified return value */
#define MODIFIED_BY_PTRACE	-9999

/*
 * Directly invokes the given syscall with nullfd as the first argument
 * and the rest zero. Avoids involving glibc wrappers in case they ever
 * end up intercepting some system calls for some reason, or modify
 * the system call number itself.
 */
static long long probe_syscall(int msb, int lsb)
{
	register long long arg1 asm("rdi") = nullfd;
	register long long arg2 asm("rsi") = 0;
	register long long arg3 asm("rdx") = 0;
	register long long arg4 asm("r10") = 0;
	register long long arg5 asm("r8")  = 0;
	register long long arg6 asm("r9")  = 0;
	long long nr = ((long long)msb << 32) | (unsigned int)lsb;
	long long ret;

	/*
	 * We pass in an extra copy of the extended system call number
	 * in %rbx, so we can examine it from the ptrace handler without
	 * worrying about it being possibly modified. This is to test
	 * the validity of struct user regs.orig_rax a.k.a.
	 * struct pt_regs.orig_ax.
	 */
	sh->probing_syscall = true;
	asm volatile("syscall"
		     : "=a" (ret)
		     : "a" (nr), "b" (nr),
		       "r" (arg1), "r" (arg2), "r" (arg3),
		       "r" (arg4), "r" (arg5), "r" (arg6)
		     : "rcx", "r11", "memory", "cc");
	sh->probing_syscall = false;

	return ret;
}

static const char *syscall_str(int msb, int start, int end)
{
	static char buf[64];
	const char * const type = (start & X32_BIT) ? "x32" : "x64";
	int lsb = start;

	/*
	 * Improve readability by stripping the x32 bit, but round
	 * toward zero so we don't display -1 as -1073741825.
	 */
	if (lsb < 0)
		lsb |= X32_BIT;
	else
		lsb &= ~X32_BIT;

	if (start == end)
		snprintf(buf, sizeof buf, "%s syscall %d:%d",
			 type, msb, lsb);
	else
		snprintf(buf, sizeof buf, "%s syscalls %d:%d..%d",
			 type, msb, lsb, lsb + (end-start));

	return buf;
}

static unsigned int _check_for(int msb, int start, int end, long long expect,
			       const char *expect_str)
{
	unsigned int err = 0;

	sh->indent++;
	if (start != end)
		sh->indent++;

	for (int nr = start; nr <= end; nr++) {
		long long ret = probe_syscall(msb, nr);

		if (ret != expect) {
			fail("%s returned %lld, but it should have returned %s\n",
			       syscall_str(msb, nr, nr),
			       ret, expect_str);
			err++;
		}
	}

	if (start != end)
		sh->indent--;

	if (err) {
		if (start != end)
			fail("%s had %u failure%s\n",
			     syscall_str(msb, start, end),
			     err, err == 1 ? "s" : "");
	} else {
		ok("%s returned %s as expected\n",
		   syscall_str(msb, start, end), expect_str);
	}

	sh->indent--;

	return err;
}

#define check_for(msb,start,end,expect) \
	_check_for(msb,start,end,expect,#expect)

static bool check_zero(int msb, int nr)
{
	return check_for(msb, nr, nr, 0);
}

static bool check_enosys(int msb, int nr)
{
	return check_for(msb, nr, nr, -ENOSYS);
}

/*
 * Anyone diagnosing a failure will want to know whether the kernel
 * supports x32. Tell them. This can also be used to conditionalize
 * tests based on existence or nonexistence of x32.
 */
static bool test_x32(void)
{
	long long ret;
	pid_t mypid = getpid();

	run("Checking for x32 by calling x32 getpid()\n");
	ret = probe_syscall(0, SYS_GETPID | X32_BIT);

	sh->indent++;
	if (ret == mypid) {
		info("x32 is supported\n");
		with_x32 = true;
	} else if (ret == -ENOSYS) {
		info("x32 is not supported\n");
		with_x32 = false;
	} else {
		fail("x32 getpid() returned %lld, but it should have returned either %lld or -ENOSYS\n", ret, (long long)mypid);
		with_x32 = false;
	}
	sh->indent--;
	return with_x32;
}

static void test_syscalls_common(int msb)
{
	enum ptrace_pass pass = sh->ptrace_pass;

	run("Checking some common syscalls as 64 bit\n");
	check_zero(msb, SYS_READ);
	check_zero(msb, SYS_WRITE);

	run("Checking some 64-bit only syscalls as 64 bit\n");
	check_zero(msb, X64_READV);
	check_zero(msb, X64_WRITEV);

	run("Checking out of range system calls\n");
	check_for(msb, -64, -2, -ENOSYS);
	if (pass >= PTP_FUZZRET)
		check_for(msb, -1, -1, MODIFIED_BY_PTRACE);
	else
		check_for(msb, -1, -1, -ENOSYS);
	check_for(msb, X32_BIT-64, X32_BIT-1, -ENOSYS);
	check_for(msb, -64-X32_BIT, -1-X32_BIT, -ENOSYS);
	check_for(msb, INT_MAX-64, INT_MAX-1, -ENOSYS);
}

static void test_syscalls_with_x32(int msb)
{
	/*
	 * Syscalls 512-547 are "x32" syscalls.  They are
	 * intended to be called with the x32 (0x40000000) bit
	 * set.  Calling them without the x32 bit set is
	 * nonsense and should not work.
	 */
	run("Checking x32 syscalls as 64 bit\n");
	check_for(msb, 512, 547, -ENOSYS);

	run("Checking some common syscalls as x32\n");
	check_zero(msb, SYS_READ   | X32_BIT);
	check_zero(msb, SYS_WRITE  | X32_BIT);

	run("Checking some x32 syscalls as x32\n");
	check_zero(msb, X32_READV  | X32_BIT);
	check_zero(msb, X32_WRITEV | X32_BIT);

	run("Checking some 64-bit syscalls as x32\n");
	check_enosys(msb, X64_IOCTL  | X32_BIT);
	check_enosys(msb, X64_READV  | X32_BIT);
	check_enosys(msb, X64_WRITEV | X32_BIT);
}

static void test_syscalls_without_x32(int msb)
{
	run("Checking for absence of x32 system calls\n");
	check_for(msb, 0 | X32_BIT, 999 | X32_BIT, -ENOSYS);
}

static void test_syscall_numbering(void)
{
	static const int msbs[] = {
		0, 1, -1, X32_BIT-1, X32_BIT, X32_BIT-1, -X32_BIT, INT_MAX,
		INT_MIN, INT_MIN+1
	};

	sh->indent++;

	/*
	 * The MSB is supposed to be ignored, so we loop over a few
	 * to test that out.
	 */
	for (size_t i = 0; i < ARRAY_SIZE(msbs); i++) {
		int msb = msbs[i];
		run("Checking system calls with msb = %d (0x%x)\n",
		    msb, msb);

		sh->indent++;

		test_syscalls_common(msb);
		if (with_x32)
			test_syscalls_with_x32(msb);
		else
			test_syscalls_without_x32(msb);

		sh->indent--;
	}

	sh->indent--;
}

static void syscall_numbering_tracee(void)
{
	enum ptrace_pass pass;

	if (ptrace(PTRACE_TRACEME, 0, 0, 0)) {
		crit("Failed to request tracing\n");
		return;
	}
	raise(SIGSTOP);

	for (sh->ptrace_pass = pass = PTP_NOTHING; pass < PTP_DONE;
	     sh->ptrace_pass = ++pass) {
		run("Running tests under ptrace: %s\n", ptrace_pass_name[pass]);
		test_syscall_numbering();
	}
}

static void mess_with_syscall(pid_t testpid, enum ptrace_pass pass)
{
	struct user_regs_struct regs;

	sh->probing_syscall = false; /* Do this on entry only */

	/* For these, don't even getregs */
	if (pass == PTP_NOTHING || pass == PTP_DONE)
		return;

	ptrace(PTRACE_GETREGS, testpid, NULL, &regs);

	if (regs.orig_rax != regs.rbx) {
		fail("orig_rax %#llx doesn't match syscall number %#llx\n",
		     (unsigned long long)regs.orig_rax,
		     (unsigned long long)regs.rbx);
	}

	switch (pass) {
	case PTP_GETREGS:
		/* Just read, no writeback */
		return;
	case PTP_WRITEBACK:
		/* Write back the same register state verbatim */
		break;
	case PTP_FUZZRET:
		regs.rax = MODIFIED_BY_PTRACE;
		break;
	case PTP_FUZZHIGH:
		regs.rax = MODIFIED_BY_PTRACE;
		regs.orig_rax = regs.orig_rax | 0xffffffff00000000ULL;
		break;
	case PTP_INTNUM:
		regs.rax = MODIFIED_BY_PTRACE;
		regs.orig_rax = (int)regs.orig_rax;
		break;
	default:
		crit("invalid ptrace_pass\n");
		break;
	}

	ptrace(PTRACE_SETREGS, testpid, NULL, &regs);
}

static void syscall_numbering_tracer(pid_t testpid)
{
	int wstatus;

	do {
		pid_t wpid = waitpid(testpid, &wstatus, 0);
		if (wpid < 0 && errno != EINTR)
			break;
		if (wpid != testpid)
			continue;
		if (!WIFSTOPPED(wstatus))
			break;	/* Thread exited? */

		if (sh->probing_syscall && WSTOPSIG(wstatus) == SIGTRAP)
			mess_with_syscall(testpid, sh->ptrace_pass);
	} while (sh->ptrace_pass != PTP_DONE &&
		 !ptrace(PTRACE_SYSCALL, testpid, NULL, NULL));

	ptrace(PTRACE_DETACH, testpid, NULL, NULL);

	/* Wait for the child process to terminate */
	while (waitpid(testpid, &wstatus, 0) != testpid || !WIFEXITED(wstatus))
		/* wait some more */;
}

static void test_traced_syscall_numbering(void)
{
	pid_t testpid;

	/* Launch the test thread; this thread continues as the tracer thread */
	testpid = fork();

	if (testpid < 0) {
		crit("Unable to launch tracer process\n");
	} else if (testpid == 0) {
		syscall_numbering_tracee();
		_exit(0);
	} else {
		syscall_numbering_tracer(testpid);
	}
}

int main(void)
{
	unsigned int nerr;

	/*
	 * It is quite likely to get a segfault on a failure, so make
	 * sure the message gets out by setting stdout to nonbuffered.
	 */
	setvbuf(stdout, NULL, _IONBF, 0);

	/*
	 * Harmless file descriptor to work on...
	 */
	nullfd = open("/dev/null", O_RDWR);
	if (nullfd < 0) {
		crit("Unable to open /dev/null: %s\n", strerror(errno));
	}

	/*
	 * Set up a block of shared memory...
	 */
	sh = mmap(NULL, sysconf(_SC_PAGE_SIZE), PROT_READ|PROT_WRITE,
		  MAP_ANONYMOUS|MAP_SHARED, 0, 0);
	if (sh == MAP_FAILED) {
		crit("Unable to allocated shared memory block: %s\n",
		     strerror(errno));
	}

	with_x32 = test_x32();

	run("Running tests without ptrace...\n");
	test_syscall_numbering();

	test_traced_syscall_numbering();

	nerr = sh->nerr;
	if (!nerr) {
		ok("All system calls succeeded or failed as expected\n");
		return 0;
	} else {
		fail("A total of %u system call%s had incorrect behavior\n",
		     nerr, nerr != 1 ? "s" : "");
		return 1;
	}
}
