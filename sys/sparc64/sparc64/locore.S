/*-
 * Copyright (c) 2001 Jake Burkholder.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <machine/asm.h>
__FBSDID("$FreeBSD$");

#include <machine/asi.h>
#include <machine/asmacros.h>
#include <machine/intr_machdep.h>
#include <machine/pstate.h>
#include <machine/wstate.h>

#include "assym.inc"

	.register %g2,#ignore

	.globl	kernbase
	.set	kernbase, KERNBASE

/*
 * void _start(caddr_t metadata, u_long o1, u_long o2, u_long o3,
 *     u_long ofw_vec)
 */
ENTRY(btext)
ENTRY(_start)
	/*
	 * Initialize misc. state to known values: interrupts disabled, normal
	 * globals, windows flushed (cr = 0, cs = nwindows - 1), PIL_TICK and
	 * floating point disabled.
	 * Note that some firmware versions don't implement a clean window
	 * trap handler so we unfortunately can't clear the windows by setting
	 * %cleanwin to zero here.
	 */
	wrpr	%g0, PSTATE_NORMAL, %pstate
	flushw
	wrpr	%g0, PIL_TICK, %pil
	wr	%g0, 0, %fprs

	/*
	 * Get onto our per-CPU panic stack, which precedes the struct pcpu in
	 * the per-CPU page.
	 */
	SET(pcpu0 + (PCPU_PAGES * PAGE_SIZE) - PC_SIZEOF, %l1, %l0)
	sub	%l0, SPOFF + CCFSZ, %sp

	/*
	 * Do initial bootstrap to setup pmap and thread0.
	 */
	call	sparc64_init
	 nop

	/*
	 * Get onto thread0's kstack.
	 */
	sub	PCB_REG, SPOFF + CCFSZ, %sp

	/*
	 * And away we go.  This doesn't return.
	 */
	call	mi_startup
	 nop
	sir
	! NOTREACHED
END(_start)

/*
 * void cpu_setregs(struct pcpu *pc)
 */
ENTRY(cpu_setregs)
	ldx	[%o0 + PC_CURPCB], %o1

	/*
	 * Ensure we are on normal globals.
	 */
	wrpr	%g0, PSTATE_NORMAL, %pstate

	/*
	 * Normal %g6 points to the current thread's PCB, and %g7 points to
	 * the per-CPU data structure.
	 */
	mov	%o1, PCB_REG
	mov	%o0, PCPU_REG

	/*
	 * Switch to alternate globals.
	 */
	wrpr	%g0, PSTATE_ALT, %pstate

	/*
	 * Alternate %g5 points to a per-CPU panic stack, %g6 points to the
	 * current thread's PCB, and %g7 points to the per-CPU data structure.
	 */
	mov	%o0, ASP_REG
	mov	%o1, PCB_REG
	mov	%o0, PCPU_REG

	/*
	 * Switch to interrupt globals.
	 */
	wrpr	%g0, PSTATE_INTR, %pstate

	/*
	 * Interrupt %g7 points to the per-CPU data structure.
	 */
	mov	%o0, PCPU_REG

	/*
	 * Switch to normal globals again.
	 */
	wrpr	%g0, PSTATE_NORMAL, %pstate

	/*
	 * Force trap level 1 and take over the trap table.
	 */
	SET(tl0_base, %o2, %o1)
	SET(tba_taken_over, %o3, %o2)
	mov	1, %o3
	wrpr	%g0, WSTATE_KERNEL, %wstate
	wrpr	%g0, 1, %tl
	wrpr	%o1, 0, %tba
	stw	%o3, [%o2]

	retl
	 nop
END(cpu_setregs)
