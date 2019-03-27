/*-
 * Initial implementation:
 * Copyright (c) 2001 Robert Drehmel
 * All rights reserved.
 *
 * As long as the above copyright statement and this notice remain
 * unchanged, you can do what ever you want with this file.
 */

#include <machine/asm.h>
__FBSDID("$FreeBSD$");

#define	LOCORE

#include <machine/frame.h>
#include <machine/fsr.h>
#include <machine/intr_machdep.h>
#include <machine/pstate.h>

#define	PAGE_SIZE	8192
#define	PAGE_SHIFT	13

#define	STACK_SIZE	(2 * PAGE_SIZE)

ENTRY(_start)
	/* Limit interrupts. */
	wrpr	%g0, PIL_TICK - 1, %pil

	/*
	 * PSTATE: privileged, interrupts enabled, floating point
	 * unit enabled
	 */
	wrpr	%g0, PSTATE_PRIV | PSTATE_IE | PSTATE_PEF, %pstate
	wr	%g0, FPRS_FEF, %fprs

	setx	stack + STACK_SIZE - SPOFF - CCFSZ, %l7, %l6
	mov	%l6, %sp
	call	main
	 mov	%o4, %o0
	sir

	.comm	stack, STACK_SIZE, 32
