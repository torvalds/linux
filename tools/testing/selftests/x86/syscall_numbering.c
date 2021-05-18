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
#include <sysexits.h>

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

static unsigned int nerr = 0;	/* Cumulative error count */
static int nullfd = -1;		/* File descriptor for /dev/null */
static int indent = 0;

static inline unsigned int offset(void)
{
	return 8 + indent * 4;
}

#define msg(lvl, fmt, ...) printf("%-*s" fmt, offset(), "[" #lvl "]", \
				  ## __VA_ARGS__)

#define run(fmt, ...)  msg(RUN,  fmt, ## __VA_ARGS__)
#define info(fmt, ...) msg(INFO, fmt, ## __VA_ARGS__)
#define ok(fmt, ...)   msg(OK,   fmt, ## __VA_ARGS__)

#define fail(fmt, ...)					\
	do {						\
		msg(FAIL, fmt, ## __VA_ARGS__);		\
		nerr++;					\
	} while (0)

#define crit(fmt, ...)					\
	do {						\
		indent = 0;				\
		msg(FAIL, fmt, ## __VA_ARGS__);		\
		msg(SKIP, "Unable to run test\n");	\
		exit(EX_OSERR);
	} while (0)

/*
 * Directly invokes the given syscall with nullfd as the first argument
 * and the rest zero. Avoids involving glibc wrappers in case they ever
 * end up intercepting some system calls for some reason, or modify
 * the system call number itself.
 */
static inline long long probe_syscall(int msb, int lsb)
{
	register long long arg1 asm("rdi") = nullfd;
	register long long arg2 asm("rsi") = 0;
	register long long arg3 asm("rdx") = 0;
	register long long arg4 asm("r10") = 0;
	register long long arg5 asm("r8")  = 0;
	register long long arg6 asm("r9")  = 0;
	long long nr = ((long long)msb << 32) | (unsigned int)lsb;
	long long ret;

	asm volatile("syscall"
		     : "=a" (ret)
		     : "a" (nr), "r" (arg1), "r" (arg2), "r" (arg3),
		       "r" (arg4), "r" (arg5), "r" (arg6)
		     : "rcx", "r11", "memory", "cc");

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

	indent++;
	if (start != end)
		indent++;

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
		indent--;

	if (err) {
		nerr += err;
		if (start != end)
			fail("%s had %u failure%s\n",
			       syscall_str(msb, start, end),
			       err, err == 1 ? "s" : "");
	} else {
		ok("%s returned %s as expected\n",
		   syscall_str(msb, start, end), expect_str);
	}

	indent--;

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
	bool with_x32;

	run("Checking for x32 by calling x32 getpid()\n");
	ret = probe_syscall(0, SYS_GETPID | X32_BIT);

	indent++;
	if (ret == mypid) {
		info("x32 is supported\n");
		with_x32 = true;
	} else if (ret == -ENOSYS) {
		info("x32 is not supported\n");
		with_x32 = false;
	} else {
		fail("x32 getpid() returned %lld, but it should have returned either %lld or -ENOSYS\n", ret, mypid);
		with_x32 = false;
	}
	indent--;
	return with_x32;
}

static void test_syscalls_common(int msb)
{
	run("Checking some common syscalls as 64 bit\n");
	check_zero(msb, SYS_READ);
	check_zero(msb, SYS_WRITE);

	run("Checking some 64-bit only syscalls as 64 bit\n");
	check_zero(msb, X64_READV);
	check_zero(msb, X64_WRITEV);

	run("Checking out of range system calls\n");
	check_for(msb, -64, -1, -ENOSYS);
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
	bool with_x32 = test_x32();

	/*
	 * The MSB is supposed to be ignored, so we loop over a few
	 * to test that out.
	 */
	for (size_t i = 0; i < sizeof(msbs)/sizeof(msbs[0]); i++) {
		int msb = msbs[i];
		run("Checking system calls with msb = %d (0x%x)\n",
		    msb, msb);

		indent++;

		test_syscalls_common(msb);
		if (with_x32)
			test_syscalls_with_x32(msb);
		else
			test_syscalls_without_x32(msb);

		indent--;
	}
}

int main(void)
{
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

	test_syscall_numbering();
	if (!nerr) {
		ok("All system calls succeeded or failed as expected\n");
		return 0;
	} else {
		fail("A total of %u system call%s had incorrect behavior\n",
		     nerr, nerr != 1 ? "s" : "");
		return 1;
	}
}
