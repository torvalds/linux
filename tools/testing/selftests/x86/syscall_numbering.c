/* SPDX-License-Identifier: GPL-2.0 */
/*
 * syscall_arg_fault.c - tests faults 32-bit fast syscall stack args
 * Copyright (c) 2018 Andrew Lutomirski
 */

#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <errno.h>
#include <unistd.h>
#include <syscall.h>

static int nerrs;

#define X32_BIT 0x40000000UL

static void check_enosys(unsigned long nr, bool *ok)
{
	/* If this fails, a segfault is reasonably likely. */
	fflush(stdout);

	long ret = syscall(nr, 0, 0, 0, 0, 0, 0);
	if (ret == 0) {
		printf("[FAIL]\tsyscall %lu succeeded, but it should have failed\n", nr);
		*ok = false;
	} else if (errno != ENOSYS) {
		printf("[FAIL]\tsyscall %lu had error code %d, but it should have reported ENOSYS\n", nr, errno);
		*ok = false;
	}
}

static void test_x32_without_x32_bit(void)
{
	bool ok = true;

	/*
	 * Syscalls 512-547 are "x32" syscalls.  They are intended to be
	 * called with the x32 (0x40000000) bit set.  Calling them without
	 * the x32 bit set is nonsense and should not work.
	 */
	printf("[RUN]\tChecking syscalls 512-547\n");
	for (int i = 512; i <= 547; i++)
		check_enosys(i, &ok);

	/*
	 * Check that a handful of 64-bit-only syscalls are rejected if the x32
	 * bit is set.
	 */
	printf("[RUN]\tChecking some 64-bit syscalls in x32 range\n");
	check_enosys(16 | X32_BIT, &ok);	/* ioctl */
	check_enosys(19 | X32_BIT, &ok);	/* readv */
	check_enosys(20 | X32_BIT, &ok);	/* writev */

	/*
	 * Check some syscalls with high bits set.
	 */
	printf("[RUN]\tChecking numbers above 2^32-1\n");
	check_enosys((1UL << 32), &ok);
	check_enosys(X32_BIT | (1UL << 32), &ok);

	if (!ok)
		nerrs++;
	else
		printf("[OK]\tThey all returned -ENOSYS\n");
}

int main()
{
	/*
	 * Anyone diagnosing a failure will want to know whether the kernel
	 * supports x32.  Tell them.
	 */
	printf("\tChecking for x32...");
	fflush(stdout);
	if (syscall(39 | X32_BIT, 0, 0, 0, 0, 0, 0) >= 0) {
		printf(" supported\n");
	} else if (errno == ENOSYS) {
		printf(" not supported\n");
	} else {
		printf(" confused\n");
	}

	test_x32_without_x32_bit();

	return nerrs ? 1 : 0;
}
