/*	$OpenBSD: vm_machdep.c,v 1.46 2025/05/21 09:06:58 mpi Exp $	*/
/*	$NetBSD: vm_machdep.c,v 1.38 2001/06/30 00:02:20 eeh Exp $ */

/*
 * Copyright (c) 1996
 *	The President and Fellows of Harvard College. All rights reserved.
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This software was developed by the Computer Systems Engineering group
 * at Lawrence Berkeley Laboratory under DARPA contract BG 91-66 and
 * contributed to Berkeley.
 *
 * All advertising materials mentioning features or use of this software
 * must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Lawrence Berkeley Laboratory.
 *	This product includes software developed by Harvard University.
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
 *	This product includes software developed by Harvard University.
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
 *	@(#)vm_machdep.c	8.2 (Berkeley) 9/23/93
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/user.h>
#include <sys/malloc.h>
#include <sys/buf.h>
#include <sys/exec.h>
#include <sys/vnode.h>
#include <sys/signalvar.h>

#include <uvm/uvm_extern.h>

#include <machine/cpu.h>
#include <machine/frame.h>
#include <machine/trap.h>
#include <machine/bus.h>

#include <sparc64/sparc64/cache.h>

/*
 * The offset of the topmost frame in the kernel stack.
 */
#define	TOPFRAMEOFF (USPACE-sizeof(struct trapframe)-CC64FSZ)

#ifdef DEBUG
char cpu_forkname[] = "cpu_fork()";
#endif

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
	struct pcb *opcb = &p1->p_addr->u_pcb;
	struct pcb *npcb = &p2->p_addr->u_pcb;
	struct trapframe *tf2;
	struct rwindow *rp;
	size_t pcbsz;
	extern struct proc proc0;

	/*
	 * Cache the physical address of the pcb, to speed up window
	 * spills in locore.
	 */
	(void)pmap_extract(pmap_kernel(), (vaddr_t)npcb,
	    &p2->p_md.md_pcbpaddr);

	/*
	 * Save all user registers to p1's stack or, in the case of
	 * user registers and invalid stack pointers, to opcb.
	 * We then copy the whole pcb to p2; when switch() selects p2
	 * to run, it will run at the `proc_trampoline' stub, rather
	 * than returning at the copying code below.
	 *
	 * If process p1 has an FPU state, we must copy it.  If it is
	 * the FPU user, we must save the FPU state first.
	 */

#ifdef NOTDEF_DEBUG
	printf("cpu_fork()\n");
#endif
	if (p1 == curproc) {
		write_user_windows();

		/*
		 * We're in the kernel, so we don't really care about
		 * %ccr or %asi.  We do want to duplicate %pstate and %cwp.
		 */
		opcb->pcb_pstate = getpstate();
		opcb->pcb_cwp = getcwp();
	}
#ifdef DIAGNOSTIC
	else if (p1 != &proc0)
		panic("cpu_fork: curproc");
#endif
#ifdef DEBUG
	/* prevent us from having NULL lastcall */
	opcb->lastcall = cpu_forkname;
#else
	opcb->lastcall = NULL;
#endif
	/*
	 * If a new stack is provided, do not bother copying saved windows
	 * in the new pcb. Also, we'll reset pcb_nsaved accordingly below.
	 */
	if (stack != NULL)
		pcbsz = offsetof(struct pcb, pcb_rw);
	else
		pcbsz = sizeof(struct pcb);
	bcopy((caddr_t)opcb, (caddr_t)npcb, pcbsz);
	if (p1->p_md.md_fpstate) {
		fpusave_proc(p1, 1);
		p2->p_md.md_fpstate = malloc(sizeof(struct fpstate),
		    M_SUBPROC, M_WAITOK);
		bcopy(p1->p_md.md_fpstate, p2->p_md.md_fpstate,
		    sizeof(struct fpstate));
	} else
		p2->p_md.md_fpstate = NULL;

	/*
	 * Setup (kernel) stack frame that will by-pass the child
	 * out of the kernel. (The trap frame invariably resides at
	 * the tippity-top of the u. area.)
	 */
	tf2 = p2->p_md.md_tf = (struct trapframe *)
			((long)npcb + USPACE - sizeof(*tf2));

	/* Copy parent's trapframe */
	*tf2 = *(struct trapframe *)((long)opcb + USPACE - sizeof(*tf2));

	/*
	 * If specified, give the child a different stack, offset and
	 * with space reserved for the frame, and zero the frame pointer.
	 */
	if (stack != NULL) {
		npcb->pcb_nsaved = 0;
		tf2->tf_out[6] = (u_int64_t)(u_long)stack - (BIAS + CC64FSZ);
		tf2->tf_in[6] = 0;
	}
	if (tcb != NULL)
		tf2->tf_global[7] = (u_int64_t)tcb;

	/* Construct kernel frame to return to in cpu_switch() */
	rp = (struct rwindow *)((u_long)npcb + TOPFRAMEOFF);
	*rp = *(struct rwindow *)((u_long)opcb + TOPFRAMEOFF);
	rp->rw_local[0] = (long)func;		/* Function to call */
	rp->rw_local[1] = (long)arg;		/* and its argument */

	npcb->pcb_pc = (long)proc_trampoline - 8;
	npcb->pcb_sp = (long)rp - BIAS;

	/* Need to create a %tstate if we're forking from proc0. */
	if (p1 == &proc0)
		tf2->tf_tstate =
		    ((u_int64_t)ASI_PRIMARY_NO_FAULT << TSTATE_ASI_SHIFT) |
		    ((PSTATE_USER) << TSTATE_PSTATE_SHIFT);
	else
		/* Clear condition codes and disable FPU. */
		tf2->tf_tstate &=
		    ~((PSTATE_PEF << TSTATE_PSTATE_SHIFT) | TSTATE_CCR);

#ifdef NOTDEF_DEBUG
	printf("cpu_fork: Copying over trapframe: otf=%p ntf=%p sp=%p opcb=%p npcb=%p\n", 
	       (struct trapframe *)((char *)opcb + USPACE - sizeof(*tf2)), tf2, rp, opcb, npcb);
	printf("cpu_fork: tstate=%lx pc=%lx npc=%lx rsp=%lx\n",
	       (long)tf2->tf_tstate, (long)tf2->tf_pc, (long)tf2->tf_npc,
	       (long)(tf2->tf_out[6]));
	db_enter();
#endif
}

/*
 * These are the "function" entry points in locore.s to handle IPI's.
 */
void	ipi_save_fpstate(void);
void	ipi_drop_fpstate(void);

void
fpusave_cpu(struct cpu_info *ci, int save)
{
	struct proc *p;

	KDASSERT(ci == curcpu());

	p = ci->ci_fpproc;
	if (p == NULL)
		return;

	if (save)
		savefpstate(p->p_md.md_fpstate);
	else
		clearfpstate();

	ci->ci_fpproc = NULL;
}

void
fpusave_proc(struct proc *p, int save)
{
	struct cpu_info *ci = curcpu();

#ifdef MULTIPROCESSOR
	if (p == ci->ci_fpproc) {
		u_int64_t s = intr_disable();
		fpusave_cpu(ci, save);
		intr_restore(s);
		return;
	}

	for (ci = cpus; ci != NULL; ci = ci->ci_next) {
		if (ci == curcpu())
			continue;
		if (ci->ci_fpproc != p)
			continue;
		sparc64_send_ipi(ci->ci_itid,
		    save ? ipi_save_fpstate : ipi_drop_fpstate, (vaddr_t)p, 0);
		while (ci->ci_fpproc == p)
			membar_sync();
		break;
	}
#else
	if (p == ci->ci_fpproc)
		fpusave_cpu(ci, save);
#endif
}

void
cpu_exit(struct proc *p)
{
	if (p->p_md.md_fpstate != NULL) {
		fpusave_proc(p, 0);
		free(p->p_md.md_fpstate, M_SUBPROC, sizeof(struct fpstate));
		p->p_md.md_fpstate = NULL;
	}
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
