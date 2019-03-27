/* $FreeBSD$ */
/* $NetBSD: locore.S,v 1.24 2000/05/31 05:09:17 thorpej Exp $ */

/*-
 * Copyright (C) 2001 Benno Rice
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
 * THIS SOFTWARE IS PROVIDED BY Benno Rice ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL TOOLS GMBH BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
/*-
 * Copyright (C) 1995, 1996 Wolfgang Solfrank.
 * Copyright (C) 1995, 1996 TooLs GmbH.
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
 *	This product includes software developed by TooLs GmbH.
 * 4. The name of TooLs GmbH may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY TOOLS GMBH ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL TOOLS GMBH BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "assym.inc"
#include "opt_sched.h"

#include <sys/syscall.h>

#include <machine/trap.h>
#include <machine/spr.h>
#include <machine/param.h>
#include <machine/asm.h>

#ifdef _CALL_ELF
.abiversion _CALL_ELF
#endif

TOC_ENTRY(blocked_lock)

/*
 * void cpu_throw(struct thread *old, struct thread *new)
 */
ENTRY(cpu_throw)
	mr	%r13, %r4
	li	%r14,0	/* Tell cpu_switchin not to release a thread */

	b	cpu_switchin

/*
 * void cpu_switch(struct thread *old,
 *		   struct thread *new,
 *		   struct mutex *mtx); 
 *
 * Switch to a new thread saving the current state in the old thread.
 */
ENTRY(cpu_switch)
	ld	%r6,TD_PCB(%r3)		/* Get the old thread's PCB ptr */
	std	%r12,PCB_CONTEXT(%r6)	/* Save the non-volatile GP regs.
					   These can now be used for scratch */
	std	%r14,PCB_CONTEXT+2*8(%r6)	
	std	%r15,PCB_CONTEXT+3*8(%r6)	
	std	%r16,PCB_CONTEXT+4*8(%r6)	
	std	%r17,PCB_CONTEXT+5*8(%r6)	
	std	%r18,PCB_CONTEXT+6*8(%r6)	
	std	%r19,PCB_CONTEXT+7*8(%r6)	
	std	%r20,PCB_CONTEXT+8*8(%r6)	
	std	%r21,PCB_CONTEXT+9*8(%r6)	
	std	%r22,PCB_CONTEXT+10*8(%r6)	
	std	%r23,PCB_CONTEXT+11*8(%r6)	
	std	%r24,PCB_CONTEXT+12*8(%r6)	
	std	%r25,PCB_CONTEXT+13*8(%r6)	
	std	%r26,PCB_CONTEXT+14*8(%r6)	
	std	%r27,PCB_CONTEXT+15*8(%r6)	
	std	%r28,PCB_CONTEXT+16*8(%r6)	
	std	%r29,PCB_CONTEXT+17*8(%r6)	
	std	%r30,PCB_CONTEXT+18*8(%r6)	
	std	%r31,PCB_CONTEXT+19*8(%r6)	

	mfcr	%r16			/* Save the condition register */
	std	%r16,PCB_CR(%r6)
	mflr	%r16			/* Save the link register */
	std	%r16,PCB_LR(%r6)
	std	%r1,PCB_SP(%r6)		/* Save the stack pointer */
	std	%r2,PCB_TOC(%r6)	/* Save the TOC pointer */
	
	mr	%r14,%r3		/* Copy the old thread ptr... */
	mr	%r13,%r4		/* and the new thread ptr in curthread*/
	mr	%r16,%r5		/* and the new lock */
	mr	%r17,%r6		/* and the PCB */
	
	stdu	%r1,-48(%r1)

	lwz	%r7, PCB_FLAGS(%r17)
	andi.	%r7, %r7, PCB_CDSCR
	beq	.L0
	/* Custom DSCR was set. Reseting it to enter kernel */
	li	%r7, 0x0
	mtspr   SPR_DSCR, %r7

.L0:
	lwz	%r7,PCB_FLAGS(%r17)
	/* Save FPU context if needed */
	andi.	%r7, %r7, PCB_FPU
	beq	.L1
	bl	save_fpu
	nop

.L1:
	mr	%r3,%r14		/* restore old thread ptr */
	lwz	%r7,PCB_FLAGS(%r17)
	/* Save Altivec context if needed */
	andi.	%r7, %r7, PCB_VEC
	beq	.L2
	bl	save_vec
	nop
	
.L2:
	mr	%r3,%r14		/* restore old thread ptr */
	bl	pmap_deactivate	/* Deactivate the current pmap */
	nop

	sync				/* Make sure all of that finished */

cpu_switchin:
#if defined(SMP) && defined(SCHED_ULE)
	/* Wait for the new thread to become unblocked */
	ld	%r6,TOC_REF(blocked_lock)(%r2)
blocked_loop:
	ld	%r7,TD_LOCK(%r13)
	cmpd	%r6,%r7 
	beq-	blocked_loop
	isync
#endif
	
	ld	%r17,TD_PCB(%r13)	/* Get new PCB */
	ld	%r1,PCB_SP(%r17)	/* Load the stack pointer */
	addi	%r1,%r1,-48		/* Remember about cpu_switch stack frame */

	/* Release old thread now that we have a stack pointer set up */
	cmpdi	%r14,0
	beq-	1f
	std	%r16,TD_LOCK(%r14)	/* ULE:	update old thread's lock */

1:	mfsprg	%r7,0			/* Get the pcpu pointer */
	std	%r13,PC_CURTHREAD(%r7)	/* Store new current thread */
	ld	%r17,TD_PCB(%r13)	/* Store new current PCB */
	std	%r17,PC_CURPCB(%r7)

	mr	%r3,%r13		/* Get new thread ptr */
	bl	pmap_activate		/* Activate the new address space */
	nop

	lwz	%r6, PCB_FLAGS(%r17)
	/* Restore FPU context if needed */
	andi.	%r6, %r6, PCB_FPU
	beq	.L3
	mr	%r3,%r13		/* Pass curthread to enable_fpu */
	bl	enable_fpu
	nop

.L3:
	lwz	%r6, PCB_FLAGS(%r17)
	/* Restore Altivec context if needed */
	andi.	%r6, %r6, PCB_VEC
	beq	.L31
	mr	%r3,%r13		/* Pass curthread to enable_vec */
	bl	enable_vec
	nop

.L31:
	lwz	%r6, PCB_FLAGS(%r17)
	/* Restore Custom DSCR if needed */
	andi.	%r6, %r6, PCB_CDSCR
	beq	.L4
	ld	%r6, PCB_DSCR(%r17)	/* Load the DSCR register*/
	mtspr	SPR_DSCR, %r6

	/* thread to restore is in r3 */
.L4:
	addi	%r1,%r1,48
	mr	%r3,%r17		/* Recover PCB ptr */
	ld	%r12,PCB_CONTEXT(%r3)	/* Load the non-volatile GP regs. */
	ld	%r14,PCB_CONTEXT+2*8(%r3)	
	ld	%r15,PCB_CONTEXT+3*8(%r3)	
	ld	%r16,PCB_CONTEXT+4*8(%r3)	
	ld	%r17,PCB_CONTEXT+5*8(%r3)	
	ld	%r18,PCB_CONTEXT+6*8(%r3)	
	ld	%r19,PCB_CONTEXT+7*8(%r3)	
	ld	%r20,PCB_CONTEXT+8*8(%r3)	
	ld	%r21,PCB_CONTEXT+9*8(%r3)	
	ld	%r22,PCB_CONTEXT+10*8(%r3)	
	ld	%r23,PCB_CONTEXT+11*8(%r3)	
	ld	%r24,PCB_CONTEXT+12*8(%r3)	
	ld	%r25,PCB_CONTEXT+13*8(%r3)	
	ld	%r26,PCB_CONTEXT+14*8(%r3)	
	ld	%r27,PCB_CONTEXT+15*8(%r3)	
	ld	%r28,PCB_CONTEXT+16*8(%r3)
	ld	%r29,PCB_CONTEXT+17*8(%r3)	
	ld	%r30,PCB_CONTEXT+18*8(%r3)	
	ld	%r31,PCB_CONTEXT+19*8(%r3)	
	ld	%r5,PCB_CR(%r3)		/* Load the condition register */
	mtcr	%r5
	ld	%r5,PCB_LR(%r3)		/* Load the link register */
	mtlr	%r5
	ld	%r1,PCB_SP(%r3)		/* Load the stack pointer */
	ld	%r2,PCB_TOC(%r3)	/* Load the TOC pointer */

	/*
	 * Perform a dummy stdcx. to clear any reservations we may have
	 * inherited from the previous thread. It doesn't matter if the
	 * stdcx succeeds or not. pcb_context[0] can be clobbered.
	 */
	stdcx.	%r1, 0, %r3
	blr

/*
 * savectx(pcb)
 * Update pcb, saving current processor state
 */
ENTRY(savectx)
	std	%r12,PCB_CONTEXT(%r3)	/* Save the non-volatile GP regs. */
	std	%r13,PCB_CONTEXT+1*8(%r3)	
	std	%r14,PCB_CONTEXT+2*8(%r3)	
	std	%r15,PCB_CONTEXT+3*8(%r3)	
	std	%r16,PCB_CONTEXT+4*8(%r3)	
	std	%r17,PCB_CONTEXT+5*8(%r3)	
	std	%r18,PCB_CONTEXT+6*8(%r3)	
	std	%r19,PCB_CONTEXT+7*8(%r3)	
	std	%r20,PCB_CONTEXT+8*8(%r3)	
	std	%r21,PCB_CONTEXT+9*8(%r3)	
	std	%r22,PCB_CONTEXT+10*8(%r3)	
	std	%r23,PCB_CONTEXT+11*8(%r3)	
	std	%r24,PCB_CONTEXT+12*8(%r3)	
	std	%r25,PCB_CONTEXT+13*8(%r3)	
	std	%r26,PCB_CONTEXT+14*8(%r3)	
	std	%r27,PCB_CONTEXT+15*8(%r3)	
	std	%r28,PCB_CONTEXT+16*8(%r3)
	std	%r29,PCB_CONTEXT+17*8(%r3)	
	std	%r30,PCB_CONTEXT+18*8(%r3)	
	std	%r31,PCB_CONTEXT+19*8(%r3)	

	mfcr	%r4			/* Save the condition register */
	std	%r4,PCB_CR(%r3)
	std	%r1,PCB_SP(%r3)		/* Save the stack pointer */
	std	%r2,PCB_TOC(%r3)	/* Save the TOC pointer */
	mflr	%r4			/* Save the link register */
	std	%r4,PCB_LR(%r3)
	blr

/*
 * fork_trampoline()
 * Set up the return from cpu_fork()
 */

ENTRY_NOPROF(fork_trampoline)
	ld	%r3,CF_FUNC(%r1)
	ld	%r4,CF_ARG0(%r1)
	ld	%r5,CF_ARG1(%r1)

	stdu	%r1,-48(%r1)
	bl	fork_exit
	nop
	addi	%r1,%r1,48+CF_SIZE-FSP	/* Allow 8 bytes in front of
					   trapframe to simulate FRAME_SETUP
					   does when allocating space for
					   a frame pointer/saved LR */
	bl	trapexit
	nop
