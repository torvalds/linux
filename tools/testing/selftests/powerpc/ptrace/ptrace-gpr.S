/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * test helper assembly functions
 *
 * Copyright (C) 2016 Simon Guo, IBM Corporation.
 * Copyright 2022 Michael Ellerman, IBM Corporation.
 */
#include "basic_asm.h"

#define GPR_SIZE	__SIZEOF_LONG__
#define FIRST_GPR	14
#define NUM_GPRS	(32 - FIRST_GPR)
#define STACK_SIZE	(NUM_GPRS * GPR_SIZE)

// gpr_child_loop(int *read_flag, int *write_flag,
//		  unsigned long *gpr_buf, double *fpr_buf);
FUNC_START(gpr_child_loop)
	// r3 = read_flag
	// r4 = write_flag
	// r5 = gpr_buf
	// r6 = fpr_buf
	PUSH_BASIC_STACK(STACK_SIZE)

	// Save non-volatile GPRs
	OP_REGS PPC_STL, GPR_SIZE, FIRST_GPR, 31, %r1, STACK_FRAME_LOCAL(0, 0), FIRST_GPR

	// Load GPRs with expected values
	OP_REGS PPC_LL, GPR_SIZE, FIRST_GPR, 31, r5, 0, FIRST_GPR

	// Load FPRs with expected values
	OP_REGS lfd, 8, 0, 31, r6

	// Signal to parent that we're ready
	li	r0, 1
	stw	r0, 0(r4)

	// Wait for parent to finish
1:	lwz	r0, 0(r3)
	cmpwi	r0, 0
	beq	1b	// Loop while flag is zero

	// Save GPRs back to caller buffer
	OP_REGS PPC_STL, GPR_SIZE, FIRST_GPR, 31, r5, 0, FIRST_GPR

	// Save FPRs
	OP_REGS stfd, 8, 0, 31, r6

	// Reload non-volatile GPRs
	OP_REGS PPC_LL, GPR_SIZE, FIRST_GPR, 31, %r1, STACK_FRAME_LOCAL(0, 0), FIRST_GPR

	POP_BASIC_STACK(STACK_SIZE)
	blr
