/*	$OpenBSD: vm_machdep.c,v 1.51 2025/07/07 18:33:36 kettenis Exp $	*/
/*	$NetBSD: vm_machdep.c,v 1.1 2003/04/26 18:39:33 fvdl Exp $	*/

/*-
 * Copyright (c) 1995 Charles M. Hannum.  All rights reserved.
 * Copyright (c) 1982, 1986 The Regents of the University of California.
 * Copyright (c) 1989, 1990 William Jolitz
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * the Systems Programming Group of the University of Utah Computer
 * Science Department, and William Jolitz.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)vm_machdep.c	7.3 (Berkeley) 5/13/91
 */

/*
 *	Utah $Hdr: vm_machdep.c 1.16.1.1 89/06/23$
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/buf.h>
#include <sys/user.h>

#include <uvm/uvm_extern.h>

#include <machine/cpu.h>
#include <machine/fpu.h>

/*
 * Finish a fork operation, with process p2 nearly set up.
 * Copy and update the kernel stack and pcb, making the child
 * ready to run, and marking it so that it can return differently
 * than the parent.
 */
void
cpu_fork(struct proc *p1, struct proc *p2, void *stack, void *tcb,
    void (*func)(void *), void *arg)
{
	struct cpu_info *ci = curcpu();
	struct pcb *pcb = &p2->p_addr->u_pcb;
	struct pcb *pcb1 = &p1->p_addr->u_pcb;
	struct trapframe *tf;
	struct switchframe *sf;

	/* Save the fpu h/w state to p1's pcb so that we can copy it. */
	if (p1 != &proc0 && (ci->ci_pflags & CPUPF_USERXSTATE))
		fpusave(&pcb1->pcb_savefpu);

	p2->p_md.md_flags = p1->p_md.md_flags;

#ifdef DIAGNOSTIC
	if (p1 != curproc && p1 != &proc0)
		panic("cpu_fork: curproc");
#endif
	*pcb = *pcb1;

	/*
	 * Activate the address space.
	 */
	pmap_activate(p2);

	/* Record where this process's kernel stack is */
	pcb->pcb_kstack = (u_int64_t)p2->p_addr + USPACE - 16 -
	    (arc4random() & PAGE_MASK & ~_STACKALIGNBYTES);

	/*
	 * Copy the trapframe.
	 */
	p2->p_md.md_regs = tf = (struct trapframe *)pcb->pcb_kstack - 1;
	*tf = *p1->p_md.md_regs;

	/*
	 * If specified, give the child a different stack and/or TCB
	 */
	if (stack != NULL)
		tf->tf_rsp = (u_int64_t)stack;
	if (tcb != NULL)
		pcb->pcb_fsbase = (u_int64_t)tcb;

	sf = (struct switchframe *)tf - 1;
	sf->sf_r12 = (u_int64_t)func;
	sf->sf_r13 = (u_int64_t)arg;
	sf->sf_rip = (u_int64_t)proc_trampoline;
	pcb->pcb_rsp = (u_int64_t)sf;
	pcb->pcb_rbp = 0;
}

void
cpu_exit(struct proc *p)
{
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
	return ((void *)p->p_addr->u_pcb.pcb_fsbase);
}

void
tcb_set(struct proc *p, void *tcb)
{
	KASSERT(p == curproc);
	reset_segs();
	p->p_addr->u_pcb.pcb_fsbase = (u_int64_t)tcb;
}
