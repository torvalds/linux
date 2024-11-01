// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2015, Laurent Dufour, IBM Corp.
 *
 * Test the kernel's signal returning code to check reclaim is done if the
 * sigreturn() is called while in a transaction (suspended since active is
 * already dropped trough the system call path).
 *
 * The kernel must discard the transaction when entering sigreturn, since
 * restoring the potential TM SPRS from the signal frame is requiring to not be
 * in a transaction.
 */

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "tm.h"
#include "utils.h"


void handler(int sig)
{
	uint64_t ret;

	asm __volatile__(
		"li             3,1             ;"
		"tbegin.                        ;"
		"beq            1f              ;"
		"li             3,0             ;"
		"tsuspend.                      ;"
		"1:                             ;"
		"std%X[ret]     3, %[ret]       ;"
		: [ret] "=m"(ret)
		:
		: "memory", "3", "cr0");

	if (ret)
		exit(1);

	/*
	 * We return from the signal handle while in a suspended transaction
	 */
}


int tm_sigreturn(void)
{
	struct sigaction sa;
	uint64_t ret = 0;

	SKIP_IF(!have_htm());
	SKIP_IF(htm_is_synthetic());
	SKIP_IF(!is_ppc64le());

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = handler;
	sigemptyset(&sa.sa_mask);

	if (sigaction(SIGSEGV, &sa, NULL))
		exit(1);

	asm __volatile__(
		"tbegin.                        ;"
		"beq            1f              ;"
		"li             3,0             ;"
		"std            3,0(3)          ;" /* trigger SEGV */
		"li             3,1             ;"
		"std%X[ret]     3,%[ret]        ;"
		"tend.                          ;"
		"b              2f              ;"
		"1:                             ;"
		"li             3,2             ;"
		"std%X[ret]     3,%[ret]        ;"
		"2:                             ;"
		: [ret] "=m"(ret)
		:
		: "memory", "3", "cr0");

	if (ret != 2)
		exit(1);

	exit(0);
}

int main(void)
{
	return test_harness(tm_sigreturn, "tm_sigreturn");
}
