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
#include <machine/param.h>
#include <machine/asm.h>
#include <machine/spr.h>

/*
 * void cpu_throw(struct thread *old, struct thread *new)
 */
ENTRY(cpu_throw)
	mr	%r2, %r4
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
	lwz	%r6,TD_PCB(%r3)		/* Get the old thread's PCB ptr */
	stmw	%r12,PCB_CONTEXT(%r6)	/* Save the non-volatile GP regs.
					   These can now be used for scratch */

	mfcr	%r16			/* Save the condition register */
	stw	%r16,PCB_CR(%r6)
	mflr	%r16			/* Save the link register */
	stw	%r16,PCB_LR(%r6)
	stw	%r1,PCB_SP(%r6)		/* Save the stack pointer */

	mr	%r14,%r3		/* Copy the old thread ptr... */
	mr	%r2,%r4			/* and the new thread ptr in curthread */
	mr	%r16,%r5		/* and the new lock */
	mr	%r17,%r6		/* and the PCB */
	
	lwz	%r7,PCB_FLAGS(%r17)
	/* Save FPU context if needed */
	andi.	%r7, %r7, PCB_FPU
	beq	.L1
	bl	save_fpu

.L1:
	mr	%r3,%r14		/* restore old thread ptr */
	lwz	%r7,PCB_FLAGS(%r17)
	/* Save Altivec context if needed */
	andi.	%r7, %r7, PCB_VEC
	beq	.L2
	bl	save_vec
	
.L2:
#if defined(__SPE__)
	mfspr	%r3,SPR_SPEFSCR
	stw	%r3,PCB_VSCR(%r17)
#endif
	mr	%r3,%r14		/* restore old thread ptr */
	bl	pmap_deactivate		/* Deactivate the current pmap */

	sync				/* Make sure all of that finished */

cpu_switchin:
#if defined(SMP) && defined(SCHED_ULE)
	/* Wait for the new thread to become unblocked */
	bl	_GLOBAL_OFFSET_TABLE_@local-4
	mflr	%r6
	lwz	%r6,blocked_lock@got(%r6)
blocked_loop:
	lwz	%r7,TD_LOCK(%r2)
	cmpw	%r6,%r7 
	beq-	blocked_loop
	isync
#endif

	lwz	%r17,TD_PCB(%r2)	/* Get new current PCB */
	lwz	%r1,PCB_SP(%r17)	/* Load new stack pointer */

	/* Release old thread now that we have a stack pointer set up */
	cmpwi	%r14,0
	beq-	1f
	stw	%r16,TD_LOCK(%r14)	/* ULE:	update old thread's lock */

1:	mfsprg	%r7,0			/* Get the pcpu pointer */
	stw	%r2,PC_CURTHREAD(%r7)	/* Store new current thread */
	lwz	%r17,TD_PCB(%r2)	/* Store new current PCB */
	stw	%r17,PC_CURPCB(%r7)

	mr	%r3,%r2			/* Get new thread ptr */
	bl	pmap_activate		/* Activate the new address space */

	lwz	%r6, PCB_FLAGS(%r17)
	/* Restore FPU context if needed */
	andi.	%r6, %r6, PCB_FPU
	beq	.L3
	mr	%r3,%r2			/* Pass curthread to enable_fpu */
	bl	enable_fpu

.L3:
	lwz	%r6, PCB_FLAGS(%r17)
	/* Restore Altivec context if needed */
	andi.	%r6, %r6, PCB_VEC
	beq	.L4
	mr	%r3,%r2			/* Pass curthread to enable_vec */
	bl	enable_vec

.L4:
#if defined(__SPE__)
	lwz	%r3,PCB_VSCR(%r17)
	mtspr	SPR_SPEFSCR,%r3
#endif
	/* thread to restore is in r3 */
	mr	%r3,%r17		/* Recover PCB ptr */
	lmw	%r12,PCB_CONTEXT(%r3)	/* Load the non-volatile GP regs */
	lwz	%r5,PCB_CR(%r3)		/* Load the condition register */
	mtcr	%r5
	lwz	%r5,PCB_LR(%r3)		/* Load the link register */
	mtlr	%r5
	lwz	%r1,PCB_SP(%r3)		/* Load the stack pointer */
	/*
	 * Perform a dummy stwcx. to clear any reservations we may have
	 * inherited from the previous thread. It doesn't matter if the
	 * stwcx succeeds or not. pcb_context[0] can be clobbered.
	 */
	stwcx.	%r1, 0, %r3
	blr

/*
 * savectx(pcb)
 * Update pcb, saving current processor state
 */
ENTRY(savectx)
	stmw	%r12,PCB_CONTEXT(%r3)	/* Save the non-volatile GP regs */
	mfcr	%r4			/* Save the condition register */
	stw	%r4,PCB_CR(%r3)
	stw	%r1,PCB_SP(%r3)		/* Save the stack pointer */
	mflr	%r4			/* Save the link register */
	stw	%r4,PCB_LR(%r3)
	blr

/*
 * fork_trampoline()
 * Set up the return from cpu_fork()
 */
ENTRY(fork_trampoline)
	lwz	%r3,CF_FUNC(%r1)
	lwz	%r4,CF_ARG0(%r1)
	lwz	%r5,CF_ARG1(%r1)
	bl	fork_exit
	addi	%r1,%r1,CF_SIZE-FSP	/* Allow 8 bytes in front of
					   trapframe to simulate FRAME_SETUP
					   does when allocating space for
					   a frame pointer/saved LR */
#ifdef __SPE__
	li	%r3,SPEFSCR_FINVE|SPEFSCR_FDBZE|SPEFSCR_FUNFE|SPEFSCR_FOVFE
	mtspr	SPR_SPEFSCR, %r3
#endif
	b	trapexit
