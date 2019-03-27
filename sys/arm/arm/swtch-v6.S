/*	$NetBSD: cpuswitch.S,v 1.41 2003/11/15 08:44:18 scw Exp $	*/

/*-
 * Copyright 2003 Wasabi Systems, Inc.
 * All rights reserved.
 *
 * Written by Steve C. Woodford for Wasabi Systems, Inc.
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
 *      This product includes software developed for the NetBSD Project by
 *      Wasabi Systems, Inc.
 * 4. The name of Wasabi Systems, Inc. may not be used to endorse
 *    or promote products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY WASABI SYSTEMS, INC. ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL WASABI SYSTEMS, INC
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
/*-
 * Copyright (c) 1994-1998 Mark Brinicombe.
 * Copyright (c) 1994 Brini.
 * All rights reserved.
 *
 * This code is derived from software written for Brini by Mark Brinicombe
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
 *	This product includes software developed by Brini.
 * 4. The name of the company nor the name of the author may be used to
 *    endorse or promote products derived from this software without specific
 *    prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY BRINI ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL BRINI OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * RiscBSD kernel project
 *
 * cpuswitch.S
 *
 * cpu switching functions
 *
 * Created      : 15/10/94
 *
 */

#include "assym.inc"
#include "opt_sched.h"

#include <machine/asm.h>
#include <machine/asmacros.h>
#include <machine/armreg.h>
#include <machine/sysreg.h>
#include <machine/vfp.h>

__FBSDID("$FreeBSD$");

#if defined(SMP)
#define GET_PCPU(tmp, tmp2) \
	mrc 	CP15_MPIDR(tmp);	\
	and	tmp, tmp, #0xf;		\
	ldr 	tmp2, .Lcurpcpu+4;	\
	mul 	tmp, tmp, tmp2;		\
	ldr	tmp2, .Lcurpcpu;	\
	add	tmp, tmp, tmp2;
#else

#define GET_PCPU(tmp, tmp2) \
	ldr	tmp, .Lcurpcpu
#endif

#ifdef VFP
	.fpu vfp	/* allow VFP instructions */
#endif

.Lcurpcpu:
	.word	_C_LABEL(__pcpu)
	.word	PCPU_SIZE
.Lblocked_lock:
	.word	_C_LABEL(blocked_lock)

ENTRY(cpu_context_switch)
	DSB
	/*
	* We can directly switch between translation tables only when the
	* size of the mapping for any given virtual address is the same
	* in the old and new translation tables.
	* Thus, we must switch to kernel pmap translation table as
	* intermediate mapping because all sizes of these mappings are same
	* (or unmapped). The same is true for switch from kernel pmap
	* translation table to new pmap one.
	*/
	mov	r2, #(CPU_ASID_KERNEL)
	ldr	r1, =(_C_LABEL(pmap_kern_ttb))
	ldr	r1, [r1]
	mcr	CP15_TTBR0(r1)		/* switch to kernel TTB */
	ISB
	mcr	CP15_TLBIASID(r2)	/* flush not global TLBs */
	DSB
	mcr	CP15_TTBR0(r0)		/* switch to new TTB */
	ISB
	/*
	* We must flush not global TLBs again because PT2MAP mapping
	* is different.
	*/
	mcr	CP15_TLBIASID(r2)	/* flush not global TLBs */
	/*
	* Flush entire Branch Target Cache because of the branch predictor
	* is not architecturally invisible. See ARM Architecture Reference
	* Manual ARMv7-A and ARMv7-R edition, page B2-1264(65), Branch
	* predictors and Requirements for branch predictor maintenance
	* operations sections.
	*/
	/*
	 * Additionally, to mitigate mistrained branch predictor attack
	 * we must invalidate it on affected CPUs. Unfortunately, BPIALL
	 * is effectively NOP on Cortex-A15 so it needs special treatment.
	 */
	ldr	r0, [r8, #PC_BP_HARDEN_KIND]
	cmp	r0, #PCPU_BP_HARDEN_KIND_ICIALLU
	mcrne	CP15_BPIALL		/* Flush entire Branch Target Cache   */
	mcreq	CP15_ICIALLU		/* This is the only way how to flush  */
					/* Branch Target Cache on Cortex-A15. */
	DSB
	mov	pc, lr
END(cpu_context_switch)

/*
 * cpu_throw(oldtd, newtd)
 *
 * Remove current thread state,	then select the	next thread to run
 * and load its	state.
 * r0 =	oldtd
 * r1 =	newtd
 */
ENTRY(cpu_throw)
	mov	r10, r0			/* r10 = oldtd */
	mov	r11, r1			/* r11 = newtd */

#ifdef VFP				/* This thread is dying, disable */
	bl	_C_LABEL(vfp_discard)	/* VFP without preserving state. */
#endif
	GET_PCPU(r8, r9)		/* r8 = current pcpu */
	ldr	r4, [r8, #PC_CPUID]	/* r4 = current cpu id */

	cmp	r10, #0			/* old thread? */
	beq	2f			/* no, skip */

	/* Remove this CPU from the active list. */
	ldr	r5, [r8, #PC_CURPMAP]
	mov	r0, #(PM_ACTIVE)
	add	r5, r0			/* r5 = old pm_active */

	/* Compute position and mask. */
#if _NCPUWORDS > 1
	lsr	r0, r4, #3
	bic	r0, #3
	add	r5, r0			/* r5 = position in old pm_active */
	mov	r2, #1
	and	r0, r4, #31
	lsl	r2, r0			/* r2 = mask */
#else
	mov	r2, #1
	lsl	r2, r4			/* r2 = mask */
#endif
	/* Clear cpu from old active list. */
#ifdef SMP
1:	ldrex	r0, [r5]
	bic	r0, r2
	strex	r1, r0, [r5]
	teq	r1, #0
	bne	1b
#else
	ldr	r0, [r5]
	bic	r0, r2
	str	r0, [r5]
#endif

2:
#ifdef INVARIANTS
	cmp	r11, #0			/* new thread? */
	beq	badsw1			/* no, panic */
#endif
	ldr	r7, [r11, #(TD_PCB)]	/* r7 = new PCB */

	/*
	 * Registers at this point
	 *   r4  = current cpu id
	 *   r7  = new PCB
	 *   r8  = current pcpu
	 *   r11 = newtd
	 */

	/* MMU switch to new thread. */
	ldr	r0, [r7, #(PCB_PAGEDIR)]
#ifdef INVARIANTS
	cmp	r0, #0			/* new thread? */
	beq	badsw4			/* no, panic */
#endif
	bl	_C_LABEL(cpu_context_switch)

	/*
	 * Set new PMAP as current one.
	 * Insert cpu to new active list.
	 */

	ldr	r6, [r11, #(TD_PROC)]	/* newtd->proc */
	ldr	r6, [r6, #(P_VMSPACE)]	/* newtd->proc->vmspace */
	add	r6, #VM_PMAP		/* newtd->proc->vmspace->pmap */
	str	r6, [r8, #PC_CURPMAP]	/* store to curpmap */

	mov	r0, #PM_ACTIVE
	add	r6, r0			/* r6 = new pm_active */

	/* compute position and mask */
#if _NCPUWORDS > 1
	lsr	r0, r4, #3
	bic	r0, #3
	add	r6, r0			/* r6 = position in new pm_active */
	mov	r2, #1
	and	r0, r4, #31
	lsl	r2, r0			/* r2 = mask */
#else
	mov	r2, #1
	lsl	r2, r4 			/* r2 = mask */
#endif
	/* Set cpu to new active list. */
#ifdef SMP
1:	ldrex	r0, [r6]
	orr	r0, r2
	strex	r1, r0, [r6]
	teq	r1, #0
	bne	1b
#else
	ldr	r0, [r6]
	orr	r0, r2
	str	r0, [r6]
#endif
	/*
	 * Registers at this point.
	 *   r7  = new PCB
	 *   r8  = current pcpu
	 *   r11 = newtd
	 * They must match the ones in sw1 position !!!
	 */
	DMB
	b	sw1	/* share new thread init with cpu_switch() */
END(cpu_throw)

/*
 * cpu_switch(oldtd, newtd, lock)
 *
 * Save the current thread state, then select the next thread to run
 * and load its state.
 * r0 = oldtd
 * r1 = newtd
 * r2 = lock (new lock for old thread)
 */
ENTRY(cpu_switch)
	/* Interrupts are disabled. */
#ifdef INVARIANTS
	cmp	r0, #0			/* old thread? */
	beq	badsw2			/* no, panic */
#endif
	/* Save all the registers in the old thread's pcb. */
	ldr	r3, [r0, #(TD_PCB)]
	add	r3, #(PCB_R4)
	stmia	r3, {r4-r12, sp, lr, pc}
	mrc	CP15_TPIDRURW(r4)
	str	r4, [r3, #(PCB_TPIDRURW - PCB_R4)]

#ifdef INVARIANTS
	cmp	r1, #0			/* new thread? */
	beq	badsw3			/* no, panic */
#endif
	/*
	 * Save arguments. Note that we can now use r0-r14 until
	 * it is time to restore them for the new thread. However,
	 * some registers are not safe over function call.
	 */
	mov	r9, r2			/* r9 = lock */
	mov	r10, r0			/* r10 = oldtd */
	mov	r11, r1			/* r11 = newtd */

	GET_PCPU(r8, r3)		/* r8 = current PCPU */
	ldr	r7, [r11, #(TD_PCB)]	/* r7 = newtd->td_pcb */



#ifdef VFP
	ldr	r3, [r10, #(TD_PCB)]
	fmrx	r0, fpexc		/* If the VFP is enabled */
	tst	r0, #(VFPEXC_EN)	/* the current thread has */
	movne	r1, #1			/* used it, so go save */
	addne	r0, r3, #(PCB_VFPSTATE)	/* the state into the PCB */
	blne	_C_LABEL(vfp_store)	/* and disable the VFP. */
#endif

	/*
	 * MMU switch. If we're switching to a thread with the same
	 * address space as the outgoing one, we can skip the MMU switch.
	 */
	mrc	CP15_TTBR0(r1)		/* r1 = old TTB */
	ldr	r0, [r7, #(PCB_PAGEDIR)] /* r0 = new TTB */
	cmp	r0, r1			/* Switching to the TTB? */
	beq	sw0			/* same TTB, skip */

#ifdef INVARIANTS
	cmp	r0, #0			/* new thread? */
	beq	badsw4			/* no, panic */
#endif

	bl	cpu_context_switch	/* new TTB as argument */

	/*
	 * Registers at this point
	 *   r7  = new PCB
	 *   r8  = current pcpu
	 *   r9  = lock
	 *   r10 = oldtd
	 *   r11 = newtd
	 */

	/*
	 * Set new PMAP as current one.
	 * Update active list on PMAPs.
	 */
	ldr	r6, [r11, #TD_PROC]	/* newtd->proc */
	ldr	r6, [r6, #P_VMSPACE]	/* newtd->proc->vmspace */
	add	r6, #VM_PMAP		/* newtd->proc->vmspace->pmap */

	ldr	r5, [r8, #PC_CURPMAP]	/* get old curpmap */
	str	r6, [r8, #PC_CURPMAP]	/* and save new one */

	mov	r0, #PM_ACTIVE
	add	r5, r0			/* r5 = old pm_active */
	add	r6, r0			/* r6 = new pm_active */

	/* Compute position and mask. */
	ldr	r4, [r8, #PC_CPUID]
#if _NCPUWORDS > 1
	lsr	r0, r4, #3
	bic	r0, #3
	add	r5, r0			/* r5 = position in old pm_active */
	add	r6, r0			/* r6 = position in new pm_active */
	mov	r2, #1
	and	r0, r4, #31
	lsl	r2, r0			/* r2 = mask */
#else
	mov	r2, #1
	lsl	r2, r4			/* r2 = mask */
#endif
	/* Clear cpu from old active list. */
#ifdef SMP
1:	ldrex	r0, [r5]
	bic	r0, r2
	strex	r1, r0, [r5]
	teq	r1, #0
	bne	1b
#else
	ldr	r0, [r5]
	bic	r0, r2
	str	r0, [r5]
#endif
	/* Set cpu to new active list. */
#ifdef SMP
1:	ldrex	r0, [r6]
	orr	r0, r2
	strex	r1, r0, [r6]
	teq	r1, #0
	bne	1b
#else
	ldr	r0, [r6]
	orr	r0, r2
	str	r0, [r6]
#endif

sw0:
	/*
	 * Registers at this point
	 *   r7  = new PCB
	 *   r8  = current pcpu
	 *   r9  = lock
	 *   r10 = oldtd
	 *   r11 = newtd
	 */

	/* Change the old thread lock. */
	add	r5, r10, #TD_LOCK
	DMB
1:	ldrex	r0, [r5]
	strex	r1, r9, [r5]
	teq	r1, #0
	bne	1b
	DMB

sw1:
	clrex
	/*
	 * Registers at this point
	 *   r7  = new PCB
	 *   r8  = current pcpu
	 *   r11 = newtd
	 */

#if defined(SMP) && defined(SCHED_ULE)
	/*
	 * 386 and amd64 do the blocked lock test only for SMP and SCHED_ULE
	 * QQQ: What does it mean in reality and why is it done?
	 */
	ldr	r6, =blocked_lock
1:
	ldr	r3, [r11, #TD_LOCK]	/* atomic write regular read */
	cmp	r3, r6
	beq	1b
#endif

	/* We have a new curthread now so make a note it */
	str	r11, [r8, #PC_CURTHREAD]
	mcr	CP15_TPIDRPRW(r11)

	/* store pcb in per cpu structure */
	str	r7, [r8, #PC_CURPCB]

	/*
	 * Restore all saved registers and return. Note that some saved
	 * registers can be changed when either cpu_fork(), cpu_copy_thread(),
	 * cpu_fork_kthread_handler(), or makectx() was called.
	 *
	 * The value of TPIDRURW is also written into TPIDRURO, as
	 * userspace still uses TPIDRURO, modifying it through
	 * sysarch(ARM_SET_TP, addr).
	 */
	ldr	r3, [r7, #PCB_TPIDRURW]
	mcr	CP15_TPIDRURW(r3)	/* write tls thread reg 2 */
	mcr	CP15_TPIDRURO(r3)	/* write tls thread reg 3 */
	add	r3, r7, #PCB_R4
	ldmia	r3, {r4-r12, sp, pc}

#ifdef INVARIANTS
badsw1:
	ldr	r0, =sw1_panic_str
	bl	_C_LABEL(panic)
1:	nop
	b	1b

badsw2:
	ldr	r0, =sw2_panic_str
	bl	_C_LABEL(panic)
1:	nop
	b	1b

badsw3:
	ldr	r0, =sw3_panic_str
	bl	_C_LABEL(panic)
1:	nop
	b	1b

badsw4:
	ldr	r0, =sw4_panic_str
	bl	_C_LABEL(panic)
1:	nop
	b	1b

sw1_panic_str:
	.asciz	"cpu_throw: no newthread supplied.\n"
sw2_panic_str:
	.asciz	"cpu_switch: no curthread supplied.\n"
sw3_panic_str:
	.asciz	"cpu_switch: no newthread supplied.\n"
sw4_panic_str:
	.asciz	"cpu_switch: new pagedir is NULL.\n"
#endif
END(cpu_switch)
