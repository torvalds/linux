/*	$OpenBSD: vm_machdep.c,v 1.43 2025/05/21 09:06:58 mpi Exp $	*/
/*
 * Copyright (c) 1988 University of Utah.
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * the Systems Programming Group of the University of Utah Computer
 * Science Department and Ralph Campbell.
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
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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
 * from: Utah Hdr: vm_machdep.c 1.21 91/04/06
 *
 *	from: @(#)vm_machdep.c	8.3 (Berkeley) 1/4/94
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/malloc.h>
#include <sys/buf.h>
#include <sys/vnode.h>
#include <sys/user.h>
#include <sys/exec.h>
#include <sys/signalvar.h>

#include <uvm/uvm_extern.h>

#include <machine/cpu.h>
#include <mips64/mips_cpu.h>

extern void proc_trampoline(void);
/*
 * Finish a fork operation, with process p2 nearly set up.
 */
void
cpu_fork(struct proc *p1, struct proc *p2, void *stack, void *tcb,
    void (*func)(void *), void *arg)
{
	struct cpu_info *ci = curcpu();
	struct pcb *pcb;
#if UPAGES == 1
	paddr_t pa;

	/* replace p_addr with a direct translation address */
	p2->p_md.md_uarea = (vaddr_t)p2->p_addr;
	pmap_extract(pmap_kernel(), p2->p_md.md_uarea, &pa);
	p2->p_addr = (void *)PHYS_TO_XKPHYS(pa, CCA_CACHED);
#endif
	pcb = &p2->p_addr->u_pcb;

	/*
	 * If we own the FPU, save its state before copying the PCB.
	 */
	if (p1 == ci->ci_fpuproc)
		save_fpu();

	p2->p_md.md_flags = p1->p_md.md_flags & MDP_FORKSAVE;
#ifdef FPUEMUL
	if (!CPU_HAS_FPU(ci)) {
		p2->p_md.md_fppgva = p1->p_md.md_fppgva;
		KASSERT((p2->p_md.md_flags & MDP_FPUSED) == 0);
	}
#endif

	/* Copy pcb from p1 to p2 */
	if (p1 == curproc) {
		/* Sync the PCB before we copy it. */
		savectx(p1->p_addr, 0);
	}
#ifdef DIAGNOSTIC
	else if (p1 != &proc0)
		panic("cpu_fork: curproc");
#endif
	*pcb = p1->p_addr->u_pcb;
	p2->p_md.md_regs = &p2->p_addr->u_pcb.pcb_regs;

	/*
	 * If specified, give the child a different stack and/or TCB.
	 */
	if (stack != NULL)
		p2->p_md.md_regs->sp = (u_int64_t)stack;
	p2->p_md.md_tcb = tcb != NULL ? tcb : p1->p_md.md_tcb;

	/*
	 * Copy the process control block to the new proc and
	 * create a clean stack for exit through trampoline.
	 * pcb_context has s0-s7, sp, s8, ra, sr.
	 */
	if (p1 != curproc) {
		pcb->pcb_context.val[11] = (pcb->pcb_regs.sr & ~SR_INT_MASK) |
		    (idle_mask & SR_INT_MASK);
	}
	pcb->pcb_context.val[10] = (register_t)proc_trampoline;
	pcb->pcb_context.val[8] = (register_t)pcb +
	    ((USPACE - sizeof(struct trapframe)) & ~_STACKALIGNBYTES);
	pcb->pcb_context.val[1] = (register_t)arg;
	pcb->pcb_context.val[0] = (register_t)func;
}

void
cpu_exit(struct proc *p)
{
	struct cpu_info *ci = curcpu();

	if (ci->ci_fpuproc == p)
		ci->ci_fpuproc = NULL;

#if UPAGES == 1
	/* restore p_addr for proper deallocation */
	p->p_addr = (void *)p->p_md.md_uarea;
#endif
}

struct kmem_va_mode kv_physwait = {
	.kv_map = &phys_map,
	.kv_wait = 1,
};

/*
 * Map an IO request into kernel virtual address space.
 */
void
vmapbuf(struct buf *bp, vsize_t len)
{
	struct kmem_dyn_mode kd_prefer = { .kd_waitok = 1 };
	struct pmap *pm = vm_map_pmap(&bp->b_proc->p_vmspace->vm_map);
	vaddr_t kva, uva;
	vsize_t size, off;

#ifdef DIAGNOSTIC
	if ((bp->b_flags & B_PHYS) == 0)
		panic("vmapbuf");
#endif
	bp->b_saveaddr = bp->b_data;
	uva = trunc_page((vaddr_t)bp->b_data);
	off = (vaddr_t)bp->b_data - uva;
	size = round_page(off + len);

	kd_prefer.kd_prefer = uva;
	kva = (vaddr_t)km_alloc(size, &kv_physwait, &kp_none, &kd_prefer);
	bp->b_data = (caddr_t)(kva + off);
	while (size > 0) {
		paddr_t pa;

		if (pmap_extract(pm, uva, &pa) == FALSE)
			panic("vmapbuf: null page frame");
		else
			pmap_kenter_pa(kva, pa, PROT_READ | PROT_WRITE);
		uva += PAGE_SIZE;
		kva += PAGE_SIZE;
		size -= PAGE_SIZE;
	}
	pmap_update(pmap_kernel());
}

/*
 * Unmap IO request from the kernel virtual address space.
 */
void
vunmapbuf(struct buf *bp, vsize_t len)
{
	vaddr_t addr, off;

#ifdef DIAGNOSTIC
	if ((bp->b_flags & B_PHYS) == 0)
		panic("vunmapbuf");
#endif
	addr = trunc_page((vaddr_t)bp->b_data);
	off = (vaddr_t)bp->b_data - addr;
	len = round_page(off + len);
	pmap_kremove(addr, len);
	pmap_update(pmap_kernel());
	km_free((void *)addr, len, &kv_physwait, &kp_none);
	bp->b_data = bp->b_saveaddr;
	bp->b_saveaddr = NULL;
}
