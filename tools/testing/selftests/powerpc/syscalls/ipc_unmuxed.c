// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2015, Michael Ellerman, IBM Corp.
 *
 * This test simply tests that certain syscalls are implemented. It doesn't
 * actually exercise their logic in any way.
 */

#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/syscall.h>

#include "utils.h"


#define DO_TEST(_name, _num)	\
static int test_##_name(void)			\
{						\
	int rc;					\
	printf("Testing " #_name);		\
	errno = 0;				\
	rc = syscall(_num, -1, 0, 0, 0, 0, 0);	\
	printf("\treturned %d, errno %d\n", rc, errno); \
	return errno == ENOSYS;			\
}

#include "ipc.h"
#undef DO_TEST

static int ipc_unmuxed(void)
{
	int tests_done = 0;

#define DO_TEST(_name, _num)		\
	FAIL_IF(test_##_name());	\
	tests_done++;

#include "ipc.h"
#undef DO_TEST

	/*
	 * If we ran no tests then it means none of the syscall numbers were
	 * defined, possibly because we were built against old headers. But it
	 * means we didn't really test anything, so instead of passing mark it
	 * as a skip to give the user a clue.
	 */
	SKIP_IF(tests_done == 0);

	return 0;
}

int main(void)
{
	return test_harness(ipc_unmuxed, "ipc_unmuxed");
}
