/*-
 * Copyright (c) 2002 Jake Burkholder.
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
#include <machine/ver.h>

#include "assym.inc"

/*
 * Handle a vectored interrupt.
 *
 * This is either a data bearing mondo vector interrupt, or a cross trap
 * request from another cpu.  In either case the hardware supplies an
 * interrupt packet, in the form of 3 data words which are read from internal
 * registers.  A data bearing mondo vector packet consists of an interrupt
 * number in the first data word, and zero in 2nd and 3rd.  We use the
 * interrupt number to find the function, argument and priority from the
 * intr_vector table, allocate and fill in an intr_request from the per-cpu
 * free list, link it onto the per-cpu active list and finally post a softint
 * at the desired priority.  Cross trap requests come in 2 forms, direct
 * and queued.  Direct requests are distinguished by the first data word
 * being zero.  The 2nd data word carries a function to call and the 3rd
 * an argument to pass.  The function is jumped to directly.  It executes
 * in nucleus context on interrupt globals and with all interrupts disabled,
 * therefore it must be fast, and the things that it can do are limited.
 * Queued cross trap requests are handled much like mondo vectors, except
 * that the function, argument and priority are contained in the interrupt
 * packet itself.  They are distinguished by the upper 4 bits of the data
 * word being non-zero, which specifies the priority of the softint to
 * deliver.
 *
 * Register usage:
 *	%g1 - pointer to intr_request
 *	%g2 - pointer to intr_vector, temp once required data is loaded
 *	%g3 - interrupt number for mondo vectors, unused otherwise
 *	%g4 - function, from the interrupt packet for cross traps, or
 *	      loaded from the interrupt registers for mondo vecors
 *	%g5 - argument, as above for %g4
 *	%g6 - softint priority
 */
ENTRY(intr_vector)
	/*
	 * Load the interrupt packet from the hardware.
	 */
	wr	%g0, ASI_SDB_INTR_R, %asi
	ldxa	[%g0 + AA_SDB_INTR_D0] %asi, %g3
	ldxa	[%g0 + AA_SDB_INTR_D1] %asi, %g4
	ldxa	[%g0 + AA_SDB_INTR_D2] %asi, %g5
	stxa	%g0, [%g0] ASI_INTR_RECEIVE
	membar	#Sync

	/*
	 * If the first data word is zero this is a direct cross trap request.
	 * The 2nd word points to code to execute and the 3rd is an argument
	 * to pass.  Jump to it.
	 */
	brnz,pt %g3, 1f
	/*
	 * NB: Zeus CPUs set some undocumented bits in the first data word.
	 */
	 and	%g3, IV_MAX - 1, %g3
	jmpl	%g4, %g0
	 nop
	/* NOTREACHED */

	/*
	 * If the high 4 bits of the 1st data word are non-zero, this is a
	 * queued cross trap request to be delivered as a softint.  The high
	 * 4 bits of the 1st data word specify a priority, and the 2nd and
	 * 3rd a function and argument.
	 */
1:	srlx	%g3, 60, %g6
	brnz,a,pn %g6, 2f
	 clr	%g3

	/*
	 * Find the function, argument and desired priority from the
	 * intr_vector table.
	 */
	SET(intr_vectors, %g4, %g2)
	sllx	%g3, IV_SHIFT, %g4
	add	%g2, %g4, %g2

	ldx	[%g2 + IV_FUNC], %g4
	ldx	[%g2 + IV_ARG], %g5
	lduw	[%g2 + IV_PRI], %g6

	/*
	 * Get an intr_request from the free list.  There should always be one
	 * unless we are getting an interrupt storm from stray interrupts, in
	 * which case the we will deference a NULL pointer and panic.
	 */
2:	ldx	[PCPU(IRFREE)], %g1
	ldx	[%g1 + IR_NEXT], %g2
	stx	%g2, [PCPU(IRFREE)]

	/*
	 * Store the vector number, function, argument and priority.
	 */
	stw	%g3, [%g1 + IR_VEC]
	stx	%g4, [%g1 + IR_FUNC]
	stx	%g5, [%g1 + IR_ARG]
	stw	%g6, [%g1 + IR_PRI]

	/*
	 * Link it onto the end of the active list.
	 */
	stx	%g0, [%g1 + IR_NEXT]
	ldx	[PCPU(IRTAIL)], %g4
	stx	%g1, [%g4]
	add	%g1, IR_NEXT, %g1
	stx	%g1, [PCPU(IRTAIL)]

	/*
	 * Trigger a softint at the level indicated by the priority.
	 */
	mov	1, %g1
	sllx	%g1, %g6, %g1
	wr	%g1, 0, %set_softint

	/*
	 * Done, retry the instruction.
	 */
	retry
END(intr_vector)

ENTRY(intr_vector_stray)
	/*
	 * SPARC64-VI trigger stray vector interrupts in order to indicate
	 * uncorrectable errors in interrupt packets, which still need to be
	 * acknowledged though.
	 * US-IV occasionally trigger stray vector interrupts for reasons
	 * unknown accompanied by a state in which they even fault on locked
	 * TLB entries so we can't even log these here.  Just retrying the
	 * instruction in that case gets the CPU back on track.
	 */
	rdpr	%ver, %g1
	srlx	%g1, VER_IMPL_SHIFT, %g1
	sll	%g1, VER_IMPL_SIZE, %g1
	srl	%g1, VER_IMPL_SIZE, %g1
	cmp	%g1, CPU_IMPL_SPARC64VI
	bne,a,pn %icc, 1f
	 nop
	stxa	%g0, [%g0] ASI_INTR_RECEIVE
	membar	#Sync

1:	retry
END(intr_vector_stray)

ENTRY(intr_fast)
	save	%sp, -CCFSZ, %sp

	/*
	 * Disable interrupts while we fiddle with the interrupt request lists
	 * as interrupts at levels higher than what got us here aren't blocked.
	 */
1:	wrpr	%g0, PSTATE_NORMAL, %pstate

	ldx	[PCPU(IRHEAD)], %l0
	brnz,a,pt %l0, 2f
	 nop

	wrpr	%g0, PSTATE_KERNEL, %pstate

	ret
	 restore

2:	ldx	[%l0 + IR_NEXT], %l1
	brnz,pt	%l1, 3f
	 stx	%l1, [PCPU(IRHEAD)]
	PCPU_ADDR(IRHEAD, %l1)
	stx	%l1, [PCPU(IRTAIL)]

3:	ldx	[%l0 + IR_FUNC], %o0
	ldx	[%l0 + IR_ARG], %o1
	lduw	[%l0 + IR_VEC], %l2

	ldx	[PCPU(IRFREE)], %l1
	stx	%l1, [%l0 + IR_NEXT]
	stx	%l0, [PCPU(IRFREE)]

	wrpr	%g0, PSTATE_KERNEL, %pstate

	KASSERT(%o0, "intr_fast: ir_func null")
	call	%o0
	 mov	%o1, %o0

	/* intrcnt[intr_countp[%l2]]++ */
	SET(intrcnt, %l7, %l3)		/* %l3 = intrcnt */
	prefetcha [%l3] ASI_N, 1
	SET(intr_countp, %l7, %l4)	/* %l4 = intr_countp */
	sllx	%l2, 1, %l2		/* %l2 = vec << 1 */
	lduh	[%l4 + %l2], %l4	/* %l4 = intr_countp[%l2] */
	sllx	%l4, 3, %l4		/* %l4 = intr_countp[%l2] << 3 */
	add	%l4, %l3, %l4		/* %l4 = intrcnt[intr_countp[%l2]] */
	ldx	[%l4], %l2
	inc	%l2
	stx	%l2, [%l4]

	ba,a	%xcc, 1b
	 nop
END(intr_fast)
