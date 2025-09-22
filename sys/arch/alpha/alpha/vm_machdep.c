/* $OpenBSD: vm_machdep.c,v 1.55 2025/06/29 15:55:21 miod Exp $ */
/* $NetBSD: vm_machdep.c,v 1.55 2000/03/29 03:49:48 simonb Exp $ */

/*
 * Copyright (c) 1994, 1995, 1996 Carnegie-Mellon University.
 * All rights reserved.
 *
 * Author: Chris G. Demetriou
 * 
 * Permission to use, copy, modify and distribute this software and
 * its documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 * 
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS" 
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND 
 * FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 * 
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie the
 * rights to redistribute these changes.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/signalvar.h>
#include <sys/malloc.h>
#include <sys/buf.h>
#include <sys/vnode.h>
#include <sys/user.h>
#include <sys/exec.h>

#include <uvm/uvm_extern.h>

#include <machine/cpu.h>
#include <machine/pmap.h>
#include <machine/reg.h>


void
cpu_exit(struct proc *p)
{
	if (p->p_addr->u_pcb.pcb_fpcpu != NULL)
		fpusave_proc(p, 0);
}

/*
 * Finish a fork operation, with process p2 nearly set up.
 * Copy and update the pcb and trap frame, making the child ready to run.
 *
 * Rig the child's kernel stack so that it will start out in
 * proc_trampoline() and call 'func' with 'arg' as an argument.
 * For normal processes this is child_return(), which causes the
 * child to go directly to user level with an apparent return value
 * of 0 from fork(), while the parent process returns normally.
 * For kernel threads this will be a function that never returns.
 *
 * An alternate user-level stack or TCB can be requested by passing
 * a non-NULL value; these are poked into the PCB so they're in
 * effect at the initial return to userspace.
 */
void
cpu_fork(struct proc *p1, struct proc *p2, void *stack, void *tcb,
    void (*func)(void *), void *arg)
{
	struct user *up = p2->p_addr;

	p2->p_md.md_tf = p1->p_md.md_tf;

#ifndef NO_IEEE
	p2->p_md.md_flags = p1->p_md.md_flags & (MDP_FPUSED | MDP_FP_C);
#else
	p2->p_md.md_flags = p1->p_md.md_flags & MDP_FPUSED;
#endif

	/*
	 * Cache the physical address of the pcb, so we can
	 * swap to it easily.
	 */
	p2->p_md.md_pcbpaddr = (void *)vtophys((vaddr_t)&up->u_pcb);

	/*
	 * Copy floating point state from the FP chip to the PCB
	 * if this process has state stored there.
	 */
	if (p1->p_addr->u_pcb.pcb_fpcpu != NULL)
		fpusave_proc(p1, 1);

	/*
	 * Copy pcb and stack from proc p1 to p2.
	 * If specified, give the child a different stack and/or TCB.
	 * We do this as cheaply as possible, copying only the active
	 * part of the stack.  The stack and pcb need to agree;
	 */
	up->u_pcb = p1->p_addr->u_pcb;
	if (stack != NULL)
		up->u_pcb.pcb_hw.apcb_usp = (u_long)stack;
	else
		up->u_pcb.pcb_hw.apcb_usp = alpha_pal_rdusp();
	if (tcb != NULL)
		up->u_pcb.pcb_hw.apcb_unique = (unsigned long)tcb;

	/*
	 * Arrange for a non-local goto when the new process
	 * is started, to resume here, returning nonzero from setjmp.
	 */
#ifdef DIAGNOSTIC
	/*
	 * If p1 != curproc && p1 == &proc0, we are creating a kernel
	 * thread.
	 */
	if (p1 != curproc && p1 != &proc0)
		panic("cpu_fork: curproc");
#ifdef DEBUG
	if ((up->u_pcb.pcb_hw.apcb_flags & ALPHA_PCB_FLAGS_FEN) != 0)
		printf("DANGER WILL ROBINSON: FEN SET IN cpu_fork!\n");
#endif
#endif

	/*
	 * create the child's kernel stack, from scratch.
	 */
	/*
	 * Pick a stack pointer, leaving room for a trapframe;
	 * copy trapframe from parent so return to user mode
	 * will be to right address, with correct registers.
	 */
	p2->p_md.md_tf = (struct trapframe *)((char *)p2->p_addr + USPACE) - 1;
	bcopy(p1->p_md.md_tf, p2->p_md.md_tf, sizeof(struct trapframe));

	/*
	 * Arrange for continuation at child_return(), which
	 * will return to exception_return().  Note that the child
	 * process doesn't stay in the kernel for long!
	 */
	up->u_pcb.pcb_hw.apcb_ksp = (u_int64_t)p2->p_md.md_tf;
	up->u_pcb.pcb_context[0] = (u_int64_t)func;
	up->u_pcb.pcb_context[1] =
	    (u_int64_t)exception_return;	/* s1: ra */
	up->u_pcb.pcb_context[2] = (u_int64_t)arg;
	up->u_pcb.pcb_context[7] =
	    (u_int64_t)proc_trampoline;		/* ra: assembly magic */
	up->u_pcb.pcb_context[8] = IPL_SCHED;	/* ps: IPL */
}

struct kmem_va_mode kv_physwait = {
	.kv_map = &phys_map,
	.kv_wait = 1,
};

/*
 * Map a user I/O request into kernel virtual address space.
 * Note: the pages are already locked by uvm_vslock(), so we
 * do not need to pass an access_type to pmap_enter().
 */
void
vmapbuf(struct buf *bp, vsize_t len)
{
	vaddr_t faddr, taddr, off;
	paddr_t fpa;

	if ((bp->b_flags & B_PHYS) == 0)
		panic("vmapbuf");
	faddr = trunc_page((vaddr_t)(bp->b_saveaddr = bp->b_data));
	off = (vaddr_t)bp->b_data - faddr;
	len = round_page(off + len);
	taddr = (vaddr_t)km_alloc(len, &kv_physwait, &kp_none, &kd_waitok);
	bp->b_data = (caddr_t)(taddr + off);
	/*
	 * The region is locked, so we expect that pmap_pte() will return
	 * non-NULL.
	 * XXX: unwise to expect this in a multithreaded environment.
	 * anything can happen to a pmap between the time we lock a
	 * region, release the pmap lock, and then relock it for
	 * the pmap_extract().
	 *
	 * no need to flush TLB since we expect nothing to be mapped
	 * where we just allocated (TLB will be flushed when our
	 * mapping is removed).
	 */
	while (len) {
		(void) pmap_extract(vm_map_pmap(&bp->b_proc->p_vmspace->vm_map),
		    faddr, &fpa);
		pmap_kenter_pa(taddr, fpa, PROT_READ | PROT_WRITE);
		faddr += PAGE_SIZE;
		taddr += PAGE_SIZE;
		len -= PAGE_SIZE;
	}
	pmap_update(pmap_kernel());
}

/*
 * Unmap a previously-mapped user I/O request.
 */
void
vunmapbuf(struct buf *bp, vsize_t len)
{
	vaddr_t addr, off;

	if ((bp->b_flags & B_PHYS) == 0)
		panic("vunmapbuf");
	addr = trunc_page((vaddr_t)bp->b_data);
	off = (vaddr_t)bp->b_data - addr;
	len = round_page(off + len);
	pmap_kremove(addr, len);
	pmap_update(pmap_kernel());
	km_free((void *)addr, len, &kv_physwait, &kp_none);
	bp->b_data = bp->b_saveaddr;
	bp->b_saveaddr = NULL;
}

void *
tcb_get(struct proc *p)
{
	if (p == curproc)
		return (void *)alpha_pal_rdunique();
	else
		return (void *)p->p_addr->u_pcb.pcb_hw.apcb_unique;
}

void
tcb_set(struct proc *p, void *newtcb)
{
	KASSERT(p == curproc);

	p->p_addr->u_pcb.pcb_hw.apcb_unique = (unsigned long)newtcb;
	alpha_pal_wrunique((unsigned long)newtcb);
}
