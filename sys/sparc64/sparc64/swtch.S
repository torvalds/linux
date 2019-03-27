/*-
 * Copyright (c) 2001 Jake Burkholder.
 * Copyright (c) 2011 Marius Strobl <marius@FreeBSD.org>
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

#include <machine/asmacros.h>
#include <machine/asi.h>
#include <machine/fsr.h>
#include <machine/ktr.h>
#include <machine/pcb.h>
#include <machine/tstate.h>

#include "assym.inc"
#include "opt_sched.h"

	.register	%g2, #ignore
	.register	%g3, #ignore

/*
 * void cpu_throw(struct thread *old, struct thread *new)
 */
ENTRY(cpu_throw)
	save	%sp, -CCFSZ, %sp
	flushw
	ba	%xcc, .Lsw1
	 mov	%g0, %i2
END(cpu_throw)

/*
 * void cpu_switch(struct thread *old, struct thread *new, struct mtx *mtx)
 */
ENTRY(cpu_switch)
	save	%sp, -CCFSZ, %sp

	/*
	 * If the current thread was using floating point in the kernel, save
	 * its context.  The userland floating point context has already been
	 * saved in that case.
	 */
	rd	%fprs, %l2
	andcc	%l2, FPRS_FEF, %g0
	bz,a,pt	%xcc, 1f
	 nop
	call	savefpctx
	 add	PCB_REG, PCB_KFP, %o0
	ba,a,pt	%xcc, 2f
	 nop

	/*
	 * If the current thread was using floating point in userland, save
	 * its context.
	 */
1:	sub	PCB_REG, TF_SIZEOF, %l2
	ldx	[%l2 + TF_FPRS], %l3
	andcc	%l3, FPRS_FEF, %g0
	bz,a,pt	%xcc, 2f
	 nop
	call	savefpctx
	 add	PCB_REG, PCB_UFP, %o0
	andn	%l3, FPRS_FEF, %l3
	stx	%l3, [%l2 + TF_FPRS]

	ldx	[PCB_REG + PCB_FLAGS], %l3
	or	%l3, PCB_FEF, %l3
	stx	%l3, [PCB_REG + PCB_FLAGS]

	/*
	 * Flush the windows out to the stack and save the current frame
	 * pointer and program counter.
	 */
2:	flushw
	wrpr	%g0, 0, %cleanwin
	stx	%fp, [PCB_REG + PCB_SP]
	stx	%i7, [PCB_REG + PCB_PC]

	/*
	 * Load the new thread's frame pointer and program counter, and set
	 * the current thread and pcb.
	 */
.Lsw1:
#if KTR_COMPILE & KTR_PROC
	CATR(KTR_PROC, "cpu_switch: new td=%p pc=%#lx fp=%#lx"
	    , %g1, %g2, %g3, 8, 9, 10)
	stx	%i1, [%g1 + KTR_PARM1]
	ldx	[%i1 + TD_PCB], %g2
	ldx	[%g2 + PCB_PC], %g3
	stx	%g3, [%g1 + KTR_PARM2]
	ldx	[%g2 + PCB_SP], %g3
	stx	%g3, [%g1 + KTR_PARM3]
10:
#endif
	ldx	[%i1 + TD_PCB], %l0

	stx	%i1, [PCPU(CURTHREAD)]
	stx	%l0, [PCPU(CURPCB)]

	wrpr	%g0, PSTATE_NORMAL, %pstate
	mov	%l0, PCB_REG
	wrpr	%g0, PSTATE_ALT, %pstate
	mov	%l0, PCB_REG
	wrpr	%g0, PSTATE_KERNEL, %pstate

	ldx	[PCB_REG + PCB_SP], %fp
	ldx	[PCB_REG + PCB_PC], %i7
	sub	%fp, CCFSZ, %sp

	/*
	 * Point to the pmaps of the new process, and of the last non-kernel
	 * process to run.
	 */
	ldx	[%i1 + TD_PROC], %l1
	ldx	[PCPU(PMAP)], %l2
	ldx	[%l1 + P_VMSPACE], %i5
	add	%i5, VM_PMAP, %l1

#if KTR_COMPILE & KTR_PROC
	CATR(KTR_PROC, "cpu_switch: new pmap=%p old pmap=%p"
	    , %g1, %g2, %g3, 8, 9, 10)
	stx	%l1, [%g1 + KTR_PARM1]
	stx	%l2, [%g1 + KTR_PARM2]
10:
#endif

	/*
	 * If they are the same we are done.
	 */
	cmp	%l2, %l1
	be,a,pn	%xcc, 8f
	 nop

	/*
	 * If the new process is a kernel thread we can just leave the old
	 * context active and avoid recycling its context number.
	 */
	SET(vmspace0, %i4, %i3)
	cmp	%i5, %i3
	be,a,pn	%xcc, 8f
	 nop

	/*
	 * If there was no non-kernel pmap, don't try to deactivate it.
	 */
	brz,pn	%l2, 3f
	 lduw	[PCPU(CPUID)], %l3

	/*
	 * Mark the pmap of the last non-kernel vmspace to run as no longer
	 * active on this CPU.
	 */
	mov	_NCPUBITS, %l5
	udivx	%l3, %l5, %l6
	srl	%l6, 0, %l4
	sllx	%l4, PTR_SHIFT, %l4
	add	%l4, PM_ACTIVE, %l4
	smul	%l6, %l5, %l5
	sub	%l3, %l5, %l5
	mov	1, %l6
	sllx	%l6, %l5, %l5
#ifdef SMP
	add	%l2, %l4, %l4
	membar	#LoadStore | #StoreStore
	ATOMIC_CLEAR_LONG(%l4, %l6, %l7, %l5)
#else
	ldx	[%l2 + %l4], %l6
	andn	%l6, %l5, %l6
	stx	%l6, [%l2 + %l4]
#endif

	/*
	 * Take away its context number.
	 */
	sllx	%l3, INT_SHIFT, %l3
	add	%l2, PM_CONTEXT, %l4
	mov	-1, %l5
	stw	%l5, [%l3 + %l4]

3:	cmp	%i2, %g0
	be,pn	%xcc, 4f
	 add	%i0, TD_LOCK, %l4
#if defined(SCHED_ULE) && defined(SMP)
	membar	#LoadStore | #StoreStore
	ATOMIC_STORE_LONG(%l4, %l6, %l7, %i2)
#else
	stx	%i2, [%l4]
#endif

	/*
	 * Find a new TLB context.  If we've run out we have to flush all
	 * user mappings from the TLB and reset the context numbers.
	 */
4:	lduw	[PCPU(TLB_CTX)], %i3
	lduw	[PCPU(TLB_CTX_MAX)], %i4
	cmp	%i3, %i4
	bne,a,pt %xcc, 5f
	 nop
	SET(tlb_flush_user, %i5, %i4)
	ldx	[%i4], %i5
	call	%i5
	 lduw	[PCPU(TLB_CTX_MIN)], %i3

	/*
	 * Advance next free context.
	 */
5:	add	%i3, 1, %i4
	stw	%i4, [PCPU(TLB_CTX)]

	/*
	 * Set the new context number in the pmap.
	 */
	lduw	[PCPU(CPUID)], %l3
	sllx	%l3, INT_SHIFT, %i4
	add	%l1, PM_CONTEXT, %i5
	stw	%i3, [%i4 + %i5]

	/*
	 * Mark the pmap as active on this CPU.
	 */
	mov	_NCPUBITS, %l5
	udivx	%l3, %l5, %l6
	srl	%l6, 0, %l4
	sllx	%l4, PTR_SHIFT, %l4
	add	%l4, PM_ACTIVE, %l4
	smul	%l6, %l5, %l5
	sub	%l3, %l5, %l5
	mov	1, %l6
	sllx	%l6, %l5, %l5
#ifdef SMP
	add	%l1, %l4, %l4
	ATOMIC_SET_LONG(%l4, %l6, %l7, %l5)
#else
	ldx	[%l1 + %l4], %l6
	or	%l6, %l5, %l6
	stx	%l6, [%l1 + %l4]
#endif

	/*
	 * Make note of the change in pmap.
	 */
#ifdef SMP
	PCPU_ADDR(PMAP, %l4)
	ATOMIC_STORE_LONG(%l4, %l5, %l6, %l1)
#else
	stx	%l1, [PCPU(PMAP)]
#endif

	/*
	 * Fiddle the hardware bits.  Set the TSB registers and install the
	 * new context number in the CPU.
	 */
	ldx	[%l1 + PM_TSB], %i4
	mov	AA_DMMU_TSB, %i5
	stxa	%i4, [%i5] ASI_DMMU
	mov	AA_IMMU_TSB, %i5
	stxa	%i4, [%i5] ASI_IMMU
	setx	TLB_CXR_PGSZ_MASK, %i5, %i4
	mov	AA_DMMU_PCXR, %i5
	ldxa	[%i5] ASI_DMMU, %l1
	and	%l1, %i4, %l1
	or	%i3, %l1, %i3
	sethi	%hi(KERNBASE), %i4
	stxa	%i3, [%i5] ASI_DMMU
	flush	%i4

6:
#if defined(SCHED_ULE) && defined(SMP)
	SET(blocked_lock, %l2, %l1)
	add	%i1, TD_LOCK, %l2
7:
	ATOMIC_LOAD_LONG(%l2, %l3)
	cmp	%l1, %l3
	be,a,pn	%xcc, 7b
	 nop
#endif

	/*
	 * Done, return and load the new process's window from the stack.
	 */
	ret
	 restore

8:	cmp	%i2, %g0
	be,pn	%xcc, 6b
	 add	%i0, TD_LOCK, %l4
#if defined(SCHED_ULE) && defined(SMP)
	membar	#LoadStore | #StoreStore
	ATOMIC_STORE_LONG(%l4, %l6, %l7, %i2)
	ba,pt	%xcc, 6b
	 nop
#else
	ba,pt	%xcc, 6b
	 stx	%i2, [%l4]
#endif
END(cpu_switch)

ENTRY(savectx)
	save	%sp, -CCFSZ, %sp
	flushw
	call	savefpctx
	 add	%i0, PCB_UFP, %o0
	stx	%fp, [%i0 + PCB_SP]
	stx	%i7, [%i0 + PCB_PC]
	ret
	 restore %g0, 0, %o0
END(savectx)

/*
 * void savefpctx(uint32_t *);
 */
ENTRY(savefpctx)
	wr	%g0, FPRS_FEF, %fprs
	wr	%g0, ASI_BLK_S, %asi
	stda	%f0, [%o0 + (0 * 64)] %asi
	stda	%f16, [%o0 + (1 * 64)] %asi
	stda	%f32, [%o0 + (2 * 64)] %asi
	stda	%f48, [%o0 + (3 * 64)] %asi
	membar	#Sync
	retl
	 wr	%g0, 0, %fprs
END(savefpctx)
