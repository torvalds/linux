/*	$OpenBSD: cpuswitch7.S,v 1.18 2023/10/24 13:20:09 claudio Exp $	*/
/*	$NetBSD: cpuswitch.S,v 1.41 2003/11/15 08:44:18 scw Exp $	*/

/*
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
/*
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
 */

#include "assym.h"

#include <machine/frame.h>
#include <machine/intr.h>
#include <machine/asm.h>
#include <arm/armreg.h>
#include <arm/sysreg.h>

/* LINTSTUB: include <sys/param.h> */
	
#define IRQdisableALL \
	cpsid	if

#define IRQenableALL \
	cpsie	if

	.text

.Lcpufuncs:
	.word	cpufuncs

.Lcpu_do_powersave:
	.word	cpu_do_powersave

/*
 * Idle loop, exercised while waiting for a process to wake up.
 */
ENTRY(cpu_idle_enter)
	mov	pc, lr

ENTRY(cpu_idle_cycle)
	stmfd	sp!, {r6, lr}

	ldr	r6, .Lcpu_do_powersave
	ldr	r6, [r6]		/* r6 = cpu_do_powersave */

	teq	r6, #0			/* cpu_do_powersave non zero? */
	ldrne	r6, .Lcpufuncs
	ldrne	r6, [r6, #(CF_SLEEP)]

	teq	r6, #0			/* Powersave idle? */
	beq	.Lidle_return		/* Nope. Just continue. */

	/*
	 * Before going into powersave idle mode, disable interrupts.
	 */
	IRQdisableALL
	mov	lr, pc
	mov	pc, r6			/* If so, do powersave idle */
	IRQenableALL

.Lidle_return:
	ldmfd	sp!, {r6, pc}

ENTRY(cpu_idle_leave)
	mov	pc, lr


/*
 * cpu_switchto(struct proc *oldproc, struct proc *newproc)
 *
 * Performs a process context switch from oldproc (which may be NULL)
 * to newproc.
 *
 * Arguments:
 *	r0	'struct proc *' of the context to switch from
 *	r1	'struct proc *' of the context to switch to
 */

ENTRY(cpu_switchto)
	/* check if old context needs to be saved */
	teq	r0, #0
	beq	1f

	/* create switchframe */
	stmfd	sp!, {r4-r7, lr}
	sub	sp, sp, #4

	mrc	CP15_TPIDRPRW(r2)		/* load curcpu */
	ldr	r5, [r2, #(CI_CURPCB)]

	/* save old stack pointer */
	mrs	r3, cpsr
	cpsid	i, #(PSR_UND32_MODE)
	str	sp, [r5, #(PCB_UND_SP)]
	msr	cpsr_c, r3

	add	r6, r5, #(PCB_R8)
	stmia	r6, {r8-r13}

1:
	mrc	CP15_TPIDRPRW(r2)		/* load curcpu */
	mov	r5, #SONPROC
	strb	r5, [r1, #(P_STAT)]		/* mark new on cpu */
	str	r2, [r1, #(P_CPU)]
	ldr	r5, [r1, #(P_ADDR)]		/* load new pcb */
	str	r5, [r2, #(CI_CURPCB)]
	str	r1, [r2, #(CI_CURPROC)]

	ldr	r4, [r5, #(PCB_TCB)]
	mcr	CP15_TPIDRURO(r4)		/* load user tls */

	add	r6, r5, #PCB_R8
	ldmia	r6, {r8-r13}

	/* load new stack pointer */
	mrs	r3, cpsr
	cps	#(PSR_UND32_MODE)
	ldr	sp, [r5, #(PCB_UND_SP)]
	msr	cpsr_c, r3

	IRQdisableALL

	ldr	r0, [r5, #(PCB_PAGEDIR)]
	ldr	r3, .Lcpufuncs
	mov	lr, pc
	ldr	pc, [r3, #CF_CONTEXT_SWITCH]

	IRQenableALL
	
	add	sp, sp, #4
	ldmfd	sp!, {r4-r7, pc}


ENTRY(savectx)
	/*
	 * r0 = pcb
	 */

	/* Push registers.*/
	stmfd	sp!, {r4-r7, lr}
	sub	sp, sp, #4

	/* Store all the registers in the process's pcb */
	add	r2, r0, #(PCB_R8)
	stmia	r2, {r8-r13}

	/* Pull the regs of the stack */
	add	sp, sp, #4
	ldmfd	sp!, {r4-r7, pc}


ENTRY(proc_trampoline)
	bl	proc_trampoline_mi

	mov	r0, r5
	mov	r1, sp
	mov	lr, pc
	mov	pc, r4

	/* Kill irq's */
	cpsid	i

	PULLFRAME

	movs	pc, lr			/* Exit */
	dsb	nsh
	isb
