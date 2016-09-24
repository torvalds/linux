/*
 * Copyright 2016, Chris Smart, IBM Corporation.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * Common code for copy, copy_first, paste and paste_last unaligned
 * tests.
 *
 */

#include <signal.h>
#include <string.h>
#include <unistd.h>
#include "utils.h"
#include "instructions.h"
#include "copy_paste_unaligned_common.h"

unsigned int expected_instruction;
unsigned int instruction_mask;

char cacheline_buf[128] __cacheline_aligned;

void signal_action_handler(int signal_num, siginfo_t *info, void *ptr)
{
	ucontext_t *ctx = ptr;
#if defined(__powerpc64__)
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
