/*	$OpenBSD: vm_machdep.c,v 1.56 2025/05/21 09:06:58 mpi Exp $	*/
/*	$NetBSD: vm_machdep.c,v 1.1 1996/09/30 16:34:57 ws Exp $	*/

/*
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
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/exec.h>
#include <sys/pool.h>
#include <sys/proc.h>
#include <sys/signalvar.h>
#include <sys/user.h>
#include <sys/vnode.h>
#include <sys/ptrace.h>

#include <uvm/uvm_extern.h>

#include <machine/pcb.h>
#include <machine/fpu.h>

/*
 * Finish a fork operation, with process p2 nearly set up.
 */
void
cpu_fork(struct proc *p1, struct proc *p2, void *stack, void *tcb,
    void (*func)(void *), void *arg)
{
	struct trapframe *tf;
	struct callframe *cf;
	struct switchframe *sf;
	caddr_t stktop1, stktop2;
	extern void proc_trampoline(void);
	struct pcb *pcb = &p2->p_addr->u_pcb;
	struct cpu_info *ci = curcpu();

	if (p1 == ci->ci_fpuproc)
		save_fpu();
	*pcb = p1->p_addr->u_pcb;
	
#ifdef ALTIVEC
	if (p1->p_addr->u_pcb.pcb_vr != NULL) {
		if (p1 == ci->ci_vecproc)
			save_vec(p1);
		pcb->pcb_vr = pool_get(&ppc_vecpl, PR_WAITOK);
		*pcb->pcb_vr = *p1->p_addr->u_pcb.pcb_vr;
	} else
		pcb->pcb_vr = NULL;

#endif /* ALTIVEC */

	pcb->pcb_pm = p2->p_vmspace->vm_map.pmap;

	pmap_extract(pmap_kernel(),
	    (vaddr_t)pcb->pcb_pm, (paddr_t *)&pcb->pcb_pmreal);
	
	/*
	 * Setup the trap frame for the new process
	 */
	stktop1 = (caddr_t)trapframe(p1);
	stktop2 = (caddr_t)trapframe(p2);
	bcopy(stktop1, stktop2, sizeof(struct trapframe));

	/*
	 * If specified, give the child a different stack.
	 */
	if (stack != NULL) {
		tf = trapframe(p2);
		tf->fixreg[1] = (register_t)stack;
	}
	if (tcb != NULL) {
		tf = trapframe(p2);
		tf->fixreg[2] = (register_t)tcb;
	}

	stktop2 = (caddr_t)((u_long)stktop2 & ~15);  /* Align stack pointer */
	
	/*
	 * There happens to be a callframe, too.
	 */
	cf = (struct callframe *)stktop2;
	cf->lr = (int)proc_trampoline;
	
	/*
	 * Below the trap frame, there is another call frame:
	 */
	stktop2 -= 16;
	cf = (struct callframe *)stktop2;
	cf->r31 = (register_t)func;
	cf->r30 = (register_t)arg;
	
	/*
	 * Below that, we allocate the switch frame:
	 */
	/* must match SFRAMELEN in genassym */
	stktop2 -= roundup(sizeof *sf, 16);

	sf = (struct switchframe *)stktop2;
	bzero((void *)sf, sizeof *sf);		/* just in case */
	sf->sp = (int)cf;
	sf->user_sr = pmap_kernel()->pm_sr[PPC_USER_SR]; /* just in case */
	pcb->pcb_sp = (int)stktop2;
}

/*
 * Since we don't have curproc anymore, we cannot sleep, and therefore
 * this is at least incorrect for the multiprocessor version.
 * Not sure whether we can get away with this in the single proc version.		XXX
 */
void
cpu_exit(struct proc *p)
{
	struct cpu_info *ci = curcpu();
#ifdef ALTIVEC
	struct pcb *pcb = &p->p_addr->u_pcb;
#endif /* ALTIVEC */
	
	/* XXX What if the state is on the other cpu? */
	if (p == ci->ci_fpuproc) 	/* release the fpu */
		ci->ci_fpuproc = NULL;

#ifdef ALTIVEC
	/* XXX What if the state is on the other cpu? */
	if (p == ci->ci_vecproc)
		ci->ci_vecproc = NULL; 	/* release the Altivec Unit */
	if (pcb->pcb_vr != NULL)
		pool_put(&ppc_vecpl, pcb->pcb_vr);
#endif /* ALTIVEC */
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
