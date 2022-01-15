// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2016, Cyril Bur, IBM Corp.
 *
 * Syscalls can be performed provided the transactions are suspended.
 * The exec() class of syscall is unique as a new process is loaded.
 *
 * It makes little sense for after an exec() call for the previously
 * suspended transaction to still exist.
 */

#define _GNU_SOURCE
#include <errno.h>
#include <inttypes.h>
#include <libgen.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "utils.h"
#include "tm.h"

static char *path;

static int test_exec(void)
{
	SKIP_IF(!have_htm());
	SKIP_IF(htm_is_synthetic());

	asm __volatile__(
		"tbegin.;"
		"blt    1f; "
		"tsuspend.;"
		"1: ;"
		: : : "memory");

	execl(path, "tm-exec", "--child", NULL);

	/* Shouldn't get here */
	perror("execl() failed");
	return 1;
}

static int after_exec(void)
{
	asm __volatile__(
		"tbegin.;"
		"blt    1f;"
		"tsuspend.;"
		"1: ;"
		: : : "memory");

	FAIL_IF(failure_is_nesting());
	return 0;
}

int main(int argc, char *argv[])
{
	path = argv[0];

	if (argc > 1 && strcmp(argv[1], "--child") == 0)
		return after_exec();

	return test_harness(test_exec, "tm_exec");
}
