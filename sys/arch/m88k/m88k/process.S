/*	$OpenBSD: process.S,v 1.27 2022/12/06 18:50:59 guenther Exp $	*/
/*
 * Copyright (c) 1996 Nivas Madhur
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Nivas Madhur.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include "assym.h"
#include <machine/asm.h>
#include <machine/psl.h>

/*
 * void cpu_switchto(struct proc *oldproc, struct proc *newproc)
 *
 * Switch context from oldproc to newproc. oldproc may be NULL if there is
 * no need to save the current context.
 *
 * Note that this code assumes that oldproc == curproc if not NULL.
 */
ENTRY(cpu_switchto)
	/*
	 * Save state of previous process in its pcb if required.
	 */
	bcnd	eq0,  %r2, 1f

	ldcr	%r2,  CPU
	ld	%r2,  %r2,  CI_CURPCB
	st	%r1,  %r2,  PCB_PC		/* save return address */
	bsr	__savectx
	/* note that we don't need to recover r1 at this point */

	/* inline pmap_deactivate(oldproc) */
	ldcr	%r11, CPU
	st	%r0,  %r11, CI_CURPMAP

1:
	ldcr	%r2, PSR
	set	%r2, %r2, 1<PSR_INTERRUPT_DISABLE_BIT>
	stcr	%r2, PSR
	FLUSH_PIPELINE

	ldcr	%r11, CPU
	st	%r3,  %r11, CI_CURPROC			/* curproc = newproc */

	or	%r2,  %r0,  SONPROC
	st.b	%r2,  %r3,  P_STAT
#ifdef MULTIPROCESSOR
	st	%r11, %r3,  P_CPU			/* p->p_cpu = curcpu */
#endif
	ld	%r2,  %r3,  P_ADDR
	st	%r2,  %r11, CI_CURPCB			/* curpcb = p->p_addr */

	bsr.n	pmap_activate			/* pmap_activate(newproc) */
	 or	%r2, %r0, %r3

	ldcr	%r2, PSR
	clr	%r2, %r2, 1<PSR_INTERRUPT_DISABLE_BIT>
	stcr	%r2, PSR
	FLUSH_PIPELINE

	ldcr	%r10,  CPU
	ld	%r10,  %r10, CI_CURPCB

	/* restore from the current context */
	ld.d	%r2,  %r10, PCB_FCR62
	fstcr	%r2,  %fcr62
	fstcr	%r3,  %fcr63
	ld	%r1,  %r10, PCB_PC
	ld.d	%r14, %r10, PCB_R14
	ld.d	%r16, %r10, PCB_R16
	ld.d	%r18, %r10, PCB_R18
	ld.d	%r20, %r10, PCB_R20
	ld.d	%r22, %r10, PCB_R22
	ld.d	%r24, %r10, PCB_R24
	ld.d	%r26, %r10, PCB_R26
	ld.d	%r28, %r10, PCB_R28
	jmp.n	%r1
	 ld.d	%r30, %r10, PCB_R30	/* restore frame pointer & stack */

/*
 * savectx(pcb)
 * Update pcb, saving current processor state.
 */
ENTRY(savectx)
	/*
	 * Save preserved general register set.
	 */
	st	%r1,  %r2,  PCB_PC		/* save return address */
ASLOCAL(__savectx)
	st.d	%r14, %r2,  PCB_R14
	st.d	%r16, %r2,  PCB_R16
	st.d	%r18, %r2,  PCB_R18
	st.d	%r20, %r2,  PCB_R20
	st.d	%r22, %r2,  PCB_R22
	st.d	%r24, %r2,  PCB_R24
	st.d	%r26, %r2,  PCB_R26
	st.d	%r28, %r2,  PCB_R28
	st.d	%r30, %r2,  PCB_R30	/* save frame pointer & stack pointer */

	/*
	 * Save FP state.
	 */
	fldcr	%r4,  %fcr62
	fldcr	%r5,  %fcr63
	jmp.n	%r1
	 st.d	%r4,  %r2, PCB_FCR62
