/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright 2015, Cyril Bur, IBM Corp.
 */

#include "basic_asm.h"

/* long signal_self(pid_t pid, int sig); */
FUNC_START(signal_self)
	li	r0,37 /* sys_kill */
	/* r3 already has our pid in it */
	/* r4 already has signal type in it */
	sc
	bc	4,3,1f
	subfze	r3,r3
1:	blr
FUNC_END(signal_self)

/* long tm_signal_self(pid_t pid, int sig, int *ret); */
FUNC_START(tm_signal_self)
	PUSH_BASIC_STACK(8)
	std	r5,STACK_FRAME_PARAM(0)(sp) /* ret */
	tbegin.
	beq	1f
	tsuspend.
	li	r0,37 /* sys_kill */
	/* r3 already has our pid in it */
	/* r4 already has signal type in it */
	sc
	ld	r5,STACK_FRAME_PARAM(0)(sp) /* ret */
	bc	4,3,2f
	subfze	r3,r3
2:	std	r3,0(r5)
	tabort. 0
	tresume. /* Be nice to some cleanup, jumps back to tbegin then to 1: */
	/*
	 * Transaction should be proper doomed and we should never get
	 * here
	 */
	li	r3,1
	POP_BASIC_STACK(8)
	blr
1:	li	r3,0
	POP_BASIC_STACK(8)
	blr
FUNC_END(tm_signal_self)
