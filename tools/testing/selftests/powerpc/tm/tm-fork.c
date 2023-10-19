// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2015, Michael Neuling, IBM Corp.
 *
 * Edited: Rashmica Gupta, Nov 2015
 *
 * This test does a fork syscall inside a transaction. Basic sniff test
 * to see if we can enter the kernel during a transaction.
 */

#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "utils.h"
#include "tm.h"

int test_fork(void)
{
	SKIP_IF(!have_htm());
	SKIP_IF(htm_is_synthetic());

	asm __volatile__(
		"tbegin.;"
		"blt    1f; "
		"li     0, 2;"  /* fork syscall */
		"sc  ;"
		"tend.;"
		"1: ;"
		: : : "memory", "r0");
	/* If we reach here, we've passed.  Otherwise we've probably crashed
	 * the kernel */

	return 0;
}

int main(int argc, char *argv[])
{
	return test_harness(test_fork, "tm_fork");
}
