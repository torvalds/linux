// SPDX-License-Identifier: GPL-2.0
/*
 * Test sigreturn to an unaligned address, ie. low 2 bits set.
 * Nothing bad should happen.
 * This was able to trigger warnings with CONFIG_PPC_RFI_SRR_DEBUG=y.
 */

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ucontext.h>
#include <unistd.h>

#include "utils.h"


static void sigusr1_handler(int signo, siginfo_t *info, void *ptr)
{
	ucontext_t *uc = ptr;

	UCONTEXT_NIA(uc) |= 3;
}

static int test_sigreturn_unaligned(void)
{
	struct sigaction action;

	memset(&action, 0, sizeof(action));
	action.sa_sigaction = sigusr1_handler;
	action.sa_flags = SA_SIGINFO;

	FAIL_IF(sigaction(SIGUSR1, &action, NULL) == -1);

	raise(SIGUSR1);

	return 0;
}

int main(void)
{
	return test_harness(test_sigreturn_unaligned, "sigreturn_unaligned");
}
