/*-
 * Copyright (c) 2015-2016 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Andrew Turner under
 * sponsorship from the FreeBSD Foundation.
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#ifdef VFP
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/pcpu.h>
#include <sys/proc.h>

#include <machine/armreg.h>
#include <machine/pcb.h>
#include <machine/vfp.h>

/* Sanity check we can store all the VFP registers */
CTASSERT(sizeof(((struct pcb *)0)->pcb_fpustate.vfp_regs) == 16 * 32);

static MALLOC_DEFINE(M_FPUKERN_CTX, "fpukern_ctx",
    "Kernel contexts for VFP state");

struct fpu_kern_ctx {
	struct vfpstate	*prev;
#define	FPU_KERN_CTX_DUMMY	0x01	/* avoided save for the kern thread */
#define	FPU_KERN_CTX_INUSE	0x02
	uint32_t	 flags;
	struct vfpstate	 state;
};

static void
vfp_enable(void)
{
	uint32_t cpacr;

	cpacr = READ_SPECIALREG(cpacr_el1);
	cpacr = (cpacr & ~CPACR_FPEN_MASK) | CPACR_FPEN_TRAP_NONE;
	WRITE_SPECIALREG(cpacr_el1, cpacr);
	isb();
}

static void
vfp_disable(void)
{
	uint32_t cpacr;

	cpacr = READ_SPECIALREG(cpacr_el1);
	cpacr = (cpacr & ~CPACR_FPEN_MASK) | CPACR_FPEN_TRAP_ALL1;
	WRITE_SPECIALREG(cpacr_el1, cpacr);
	isb();
}

/*
 * Called when the thread is dying or when discarding the kernel VFP state.
 * If the thread was the last to use the VFP unit mark it as unused to tell
 * the kernel the fp state is unowned. Ensure the VFP unit is off so we get
 * an exception on the next access.
 */
void
vfp_discard(struct thread *td)
{

#ifdef INVARIANTS
	if (td != NULL)
		CRITICAL_ASSERT(td);
#endif
	if (PCPU_GET(fpcurthread) == td)
		PCPU_SET(fpcurthread, NULL);

	vfp_disable();
}

static void
vfp_store(struct vfpstate *state)
{
	__int128_t *vfp_state;
	uint64_t fpcr, fpsr;

	vfp_state = state->vfp_regs;
	__asm __volatile(
	    "mrs	%0, fpcr		\n"
	    "mrs	%1, fpsr		\n"
	    "stp	q0,  q1,  [%2, #16 *  0]\n"
	    "stp	q2,  q3,  [%2, #16 *  2]\n"
	    "stp	q4,  q5,  [%2, #16 *  4]\n"
	    "stp	q6,  q7,  [%2, #16 *  6]\n"
	    "stp	q8,  q9,  [%2, #16 *  8]\n"
	    "stp	q10, q11, [%2, #16 * 10]\n"
	    "stp	q12, q13, [%2, #16 * 12]\n"
	    "stp	q14, q15, [%2, #16 * 14]\n"
	    "stp	q16, q17, [%2, #16 * 16]\n"
	    "stp	q18, q19, [%2, #16 * 18]\n"
	    "stp	q20, q21, [%2, #16 * 20]\n"
	    "stp	q22, q23, [%2, #16 * 22]\n"
	    "stp	q24, q25, [%2, #16 * 24]\n"
	    "stp	q26, q27, [%2, #16 * 26]\n"
	    "stp	q28, q29, [%2, #16 * 28]\n"
	    "stp	q30, q31, [%2, #16 * 30]\n"
	    : "=&r"(fpcr), "=&r"(fpsr) : "r"(vfp_state));

	state->vfp_fpcr = fpcr;
	state->vfp_fpsr = fpsr;
}

static void
vfp_restore(struct vfpstate *state)
{
	__int128_t *vfp_state;
	uint64_t fpcr, fpsr;

	vfp_state = state->vfp_regs;
	fpcr = state->vfp_fpcr;
	fpsr = state->vfp_fpsr;

	__asm __volatile(
	    "ldp	q0,  q1,  [%2, #16 *  0]\n"
	    "ldp	q2,  q3,  [%2, #16 *  2]\n"
	    "ldp	q4,  q5,  [%2, #16 *  4]\n"
	    "ldp	q6,  q7,  [%2, #16 *  6]\n"
	    "ldp	q8,  q9,  [%2, #16 *  8]\n"
	    "ldp	q10, q11, [%2, #16 * 10]\n"
	    "ldp	q12, q13, [%2, #16 * 12]\n"
	    "ldp	q14, q15, [%2, #16 * 14]\n"
	    "ldp	q16, q17, [%2, #16 * 16]\n"
	    "ldp	q18, q19, [%2, #16 * 18]\n"
	    "ldp	q20, q21, [%2, #16 * 20]\n"
	    "ldp	q22, q23, [%2, #16 * 22]\n"
	    "ldp	q24, q25, [%2, #16 * 24]\n"
	    "ldp	q26, q27, [%2, #16 * 26]\n"
	    "ldp	q28, q29, [%2, #16 * 28]\n"
	    "ldp	q30, q31, [%2, #16 * 30]\n"
	    "msr	fpcr, %0		\n"
	    "msr	fpsr, %1		\n"
	    : : "r"(fpcr), "r"(fpsr), "r"(vfp_state));
}

void
vfp_save_state(struct thread *td, struct pcb *pcb)
{
	uint32_t cpacr;

	KASSERT(pcb != NULL, ("NULL vfp pcb"));
	KASSERT(td == NULL || td->td_pcb == pcb, ("Invalid vfp pcb"));

	/* 
	 * savectx() will be called on panic with dumppcb as an argument,
	 * dumppcb doesn't have pcb_fpusaved set, so set it to save
	 * the VFP registers.
	 */
	if (pcb->pcb_fpusaved == NULL)
		pcb->pcb_fpusaved = &pcb->pcb_fpustate;

	if (td == NULL)
		td = curthread;

	critical_enter();
	/*
	 * Only store the registers if the VFP is enabled,
	 * i.e. return if we are trapping on FP access.
	 */
	cpacr = READ_SPECIALREG(cpacr_el1);
	if ((cpacr & CPACR_FPEN_MASK) == CPACR_FPEN_TRAP_NONE) {
		KASSERT(PCPU_GET(fpcurthread) == td,
		    ("Storing an invalid VFP state"));

		vfp_store(pcb->pcb_fpusaved);
		dsb(ish);
		vfp_disable();
	}
	critical_exit();
}

void
vfp_restore_state(void)
{
	struct pcb *curpcb;
	u_int cpu;

	critical_enter();

	cpu = PCPU_GET(cpuid);
	curpcb = curthread->td_pcb;
	curpcb->pcb_fpflags |= PCB_FP_STARTED;

	vfp_enable();

	/*
	 * If the previous thread on this cpu to use the VFP was not the
	 * current thread, or the current thread last used it on a different
	 * cpu we need to restore the old state.
	 */
	if (PCPU_GET(fpcurthread) != curthread || cpu != curpcb->pcb_vfpcpu) {

		vfp_restore(curthread->td_pcb->pcb_fpusaved);
		PCPU_SET(fpcurthread, curthread);
		curpcb->pcb_vfpcpu = cpu;
	}

	critical_exit();
}

void
vfp_init(void)
{
	uint64_t pfr;

	/* Check if there is a vfp unit present */
	pfr = READ_SPECIALREG(id_aa64pfr0_el1);
	if ((pfr & ID_AA64PFR0_FP_MASK) == ID_AA64PFR0_FP_NONE)
		return;

	/* Disable to be enabled when it's used */
	vfp_disable();
}

SYSINIT(vfp, SI_SUB_CPU, SI_ORDER_ANY, vfp_init, NULL);

struct fpu_kern_ctx *
fpu_kern_alloc_ctx(u_int flags)
{
	struct fpu_kern_ctx *res;
	size_t sz;

	sz = sizeof(struct fpu_kern_ctx);
	res = malloc(sz, M_FPUKERN_CTX, ((flags & FPU_KERN_NOWAIT) ?
	    M_NOWAIT : M_WAITOK) | M_ZERO);
	return (res);
}

void
fpu_kern_free_ctx(struct fpu_kern_ctx *ctx)
{

	KASSERT((ctx->flags & FPU_KERN_CTX_INUSE) == 0, ("free'ing inuse ctx"));
	/* XXXAndrew clear the memory ? */
	free(ctx, M_FPUKERN_CTX);
}

void
fpu_kern_enter(struct thread *td, struct fpu_kern_ctx *ctx, u_int flags)
{
	struct pcb *pcb;

	pcb = td->td_pcb;
	KASSERT((flags & FPU_KERN_NOCTX) != 0 || ctx != NULL,
	    ("ctx is required when !FPU_KERN_NOCTX"));
	KASSERT(ctx == NULL || (ctx->flags & FPU_KERN_CTX_INUSE) == 0,
	    ("using inuse ctx"));
	KASSERT((pcb->pcb_fpflags & PCB_FP_NOSAVE) == 0,
	    ("recursive fpu_kern_enter while in PCB_FP_NOSAVE state"));

	if ((flags & FPU_KERN_NOCTX) != 0) {
		critical_enter();
		if (curthread == PCPU_GET(fpcurthread)) {
			vfp_save_state(curthread, pcb);
		}
		PCPU_SET(fpcurthread, NULL);

		vfp_enable();
		pcb->pcb_fpflags |= PCB_FP_KERN | PCB_FP_NOSAVE |
		    PCB_FP_STARTED;
		return;
	}

	if ((flags & FPU_KERN_KTHR) != 0 && is_fpu_kern_thread(0)) {
		ctx->flags = FPU_KERN_CTX_DUMMY | FPU_KERN_CTX_INUSE;
		return;
	}
	/*
	 * Check either we are already using the VFP in the kernel, or
	 * the the saved state points to the default user space.
	 */
	KASSERT((pcb->pcb_fpflags & PCB_FP_KERN) != 0 ||
	    pcb->pcb_fpusaved == &pcb->pcb_fpustate,
	    ("Mangled pcb_fpusaved %x %p %p", pcb->pcb_fpflags, pcb->pcb_fpusaved, &pcb->pcb_fpustate));
	ctx->flags = FPU_KERN_CTX_INUSE;
	vfp_save_state(curthread, pcb);
	ctx->prev = pcb->pcb_fpusaved;
	pcb->pcb_fpusaved = &ctx->state;
	pcb->pcb_fpflags |= PCB_FP_KERN;
	pcb->pcb_fpflags &= ~PCB_FP_STARTED;

	return;
}

int
fpu_kern_leave(struct thread *td, struct fpu_kern_ctx *ctx)
{
	struct pcb *pcb;

	pcb = td->td_pcb;

	if ((pcb->pcb_fpflags & PCB_FP_NOSAVE) != 0) {
		KASSERT(ctx == NULL, ("non-null ctx after FPU_KERN_NOCTX"));
		KASSERT(PCPU_GET(fpcurthread) == NULL,
		    ("non-NULL fpcurthread for PCB_FP_NOSAVE"));
		CRITICAL_ASSERT(td);

		vfp_disable();
		pcb->pcb_fpflags &= ~(PCB_FP_NOSAVE | PCB_FP_STARTED);
		critical_exit();
	} else {
		KASSERT((ctx->flags & FPU_KERN_CTX_INUSE) != 0,
		    ("FPU context not inuse"));
		ctx->flags &= ~FPU_KERN_CTX_INUSE;

		if (is_fpu_kern_thread(0) &&
		    (ctx->flags & FPU_KERN_CTX_DUMMY) != 0)
			return (0);
		KASSERT((ctx->flags & FPU_KERN_CTX_DUMMY) == 0, ("dummy ctx"));
		critical_enter();
		vfp_discard(td);
		critical_exit();
		pcb->pcb_fpflags &= ~PCB_FP_STARTED;
		pcb->pcb_fpusaved = ctx->prev;
	}

	if (pcb->pcb_fpusaved == &pcb->pcb_fpustate) {
		pcb->pcb_fpflags &= ~PCB_FP_KERN;
	} else {
		KASSERT((pcb->pcb_fpflags & PCB_FP_KERN) != 0,
		    ("unpaired fpu_kern_leave"));
	}

	return (0);
}

int
fpu_kern_thread(u_int flags)
{
	struct pcb *pcb = curthread->td_pcb;

	KASSERT((curthread->td_pflags & TDP_KTHREAD) != 0,
	    ("Only kthread may use fpu_kern_thread"));
	KASSERT(pcb->pcb_fpusaved == &pcb->pcb_fpustate,
	    ("Mangled pcb_fpusaved"));
	KASSERT((pcb->pcb_fpflags & PCB_FP_KERN) == 0,
	    ("Thread already setup for the VFP"));
	pcb->pcb_fpflags |= PCB_FP_KERN;
	return (0);
}

int
is_fpu_kern_thread(u_int flags)
{
	struct pcb *curpcb;

	if ((curthread->td_pflags & TDP_KTHREAD) == 0)
		return (0);
	curpcb = curthread->td_pcb;
	return ((curpcb->pcb_fpflags & PCB_FP_KERN) != 0);
}
#endif
