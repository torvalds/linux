/*
 * Copyright 2016, Chris Smart, IBM Corporation.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * Calls to copy_first which are not 128-byte aligned should be
 * caught and sent a SIGBUS.
 *
 */

#include <signal.h>
#include <string.h>
#include <unistd.h>
#include "utils.h"
#include "instructions.h"

unsigned int expected_instruction = PPC_INST_COPY_FIRST;
unsigned int instruction_mask = 0xfc2007fe;

void signal_action_handler(int signal_num, siginfo_t *info, void *ptr)
{
	ucontext_t *ctx = ptr;
#ifdef __powerpc64__
	unsigned int *pc = (unsigned int *)ctx->uc_mcontext.gp_regs[PT_NIP];
#else
	unsigned int *pc = (unsigned int *)ctx->uc_mcontext.uc_regs->gregs[PT_NIP];
#endif

	/*
	 * Check that the signal was on the correct instruction, using a
	 * mask because the compiler assigns the register at RB.
	 */
	if ((*pc & instruction_mask) == expected_instruction)
		_exit(0); /* We hit the right instruction */

	_exit(1);
}

void setup_signal_handler(void)
{
	struct sigaction signal_action;

	memset(&signal_action, 0, sizeof(signal_action));
	signal_action.sa_sigaction = signal_action_handler;
	signal_action.sa_flags = SA_SIGINFO;
	sigaction(SIGBUS, &signal_action, NULL);
}

char cacheline_buf[128] __cacheline_aligned;

int test_copy_first_unaligned(void)
{
	/* Only run this test on a P9 or later */
	SKIP_IF(!have_hwcap2(PPC_FEATURE2_ARCH_3_00));

	/* Register our signal handler with SIGBUS */
	setup_signal_handler();

	/* +1 makes buf unaligned */
	copy_first(cacheline_buf+1);

	/* We should not get here */
	return 1;
}

int main(int argc, char *argv[])
{
	return test_harness(test_copy_first_unaligned, "test_copy_first_unaligned");
}
