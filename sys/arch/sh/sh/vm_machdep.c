/*	$OpenBSD: vm_machdep.c,v 1.18 2023/10/24 13:20:10 claudio Exp $	*/
/*	$NetBSD: vm_machdep.c,v 1.53 2006/08/31 16:49:21 matt Exp $	*/

/*
 * Copyright (c) 2007 Miodrag Vallat.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice, this permission notice, and the disclaimer below
 * appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
/*-
 * Copyright (c) 2002 The NetBSD Foundation, Inc. All rights reserved.
 * Copyright (c) 1982, 1986 The Regents of the University of California.
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
/*-
 * Copyright (c) 1995 Charles M. Hannum.  All rights reserved.
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
 *	@(#)vm_machdep.c	7.3 (Berkeley) 5/13/91
 */

/*
 *	Utah $Hdr: vm_machdep.c 1.16.1.1 89/06/23$
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/malloc.h>
#include <sys/vnode.h>
#include <sys/buf.h>
#include <sys/user.h>
#include <sys/exec.h>
#include <sys/ptrace.h>
#include <sys/signalvar.h>

#include <uvm/uvm_extern.h>

#include <sh/locore.h>
#include <sh/cpu.h>
#include <sh/reg.h>
#include <sh/mmu.h>
#include <sh/cache.h>

extern void proc_trampoline(void);

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
	struct pcb *pcb;
	struct trapframe *tf;
	struct switchframe *sf;
	vaddr_t spbase, fptop;
#define	P1ADDR(x)	(SH3_PHYS_TO_P1SEG(*__pmap_kpte_lookup(x) & PG_PPN))

	KDASSERT(p1 == curproc || p1 == &proc0);

	bzero(&p2->p_md, sizeof(p2->p_md));

	/* Copy flags */
	p2->p_md.md_flags = p1->p_md.md_flags;

	pcb = NULL;		/* XXXGCC: -Wuninitialized */
#ifdef SH3
	/*
	 * Convert frame pointer top to P1. because SH3 can't make
	 * wired TLB entry, context store space accessing must not cause
	 * exception. For SH3, we are 4K page, P3/P1 conversion don't
	 * cause virtual-aliasing.
	 */
	if (CPU_IS_SH3)
		pcb = (struct pcb *)P1ADDR((vaddr_t)&p2->p_addr->u_pcb);
#endif /* SH3 */
#ifdef SH4
	/* SH4 can make wired entry, no need to convert to P1. */
	if (CPU_IS_SH4)
		pcb = &p2->p_addr->u_pcb;
#endif /* SH4 */

	p2->p_md.md_pcb = pcb;
	fptop = (vaddr_t)pcb + PAGE_SIZE;

	/* set up the kernel stack pointer */
	spbase = (vaddr_t)p2->p_addr + PAGE_SIZE;
#ifdef P1_STACK
	/* Convert to P1 from P3 */
	/*
	 * wbinv u-area to avoid cache-aliasing, since kernel stack
	 * is accessed from P1 instead of P3.
	 */
	if (SH_HAS_VIRTUAL_ALIAS)
		sh_dcache_wbinv_range((vaddr_t)p2->p_addr, USPACE);
	spbase = P1ADDR(spbase);
#else /* !P1_STACK */
	/* Prepare u-area PTEs */
#ifdef SH3
	if (CPU_IS_SH3)
		sh3_switch_setup(p2);
#endif
#ifdef SH4
	if (CPU_IS_SH4)
		sh4_switch_setup(p2);
#endif
#endif /* !P1_STACK */

#ifdef KSTACK_DEBUG
	/* Fill magic number for tracking */
	memset((char *)fptop - PAGE_SIZE + sizeof(struct user), 0x5a,
	    PAGE_SIZE - sizeof(struct user));
	memset((char *)spbase, 0xa5, (USPACE - PAGE_SIZE));
	memset(&pcb->pcb_sf, 0xb4, sizeof(struct switchframe));
#endif /* KSTACK_DEBUG */

	/*
	 * Copy the user context.
	 */
	p2->p_md.md_regs = tf = (struct trapframe *)fptop - 1;
	memcpy(tf, p1->p_md.md_regs, sizeof(struct trapframe));

	/*
	 * If specified, give the child a different stack.
	 */
	if (stack != NULL)
		tf->tf_r15 = (int)stack;
	if (tcb != NULL)
		tf->tf_gbr = (int)tcb;

	/* Setup switch frame */
	sf = &pcb->pcb_sf;
	sf->sf_r11 = (int)arg;		/* proc_trampoline hook func's arg */
	sf->sf_r12 = (int)func;		/* proc_trampoline hook func */
	sf->sf_r15 = spbase + USPACE - PAGE_SIZE;/* current stack pointer */
	sf->sf_r7_bank = sf->sf_r15;	/* stack top */
	sf->sf_r6_bank = (vaddr_t)tf;	/* current frame pointer */
	/* when switch to me, jump to proc_trampoline */
	sf->sf_pr = (int)proc_trampoline;
	/*
	 * Enable interrupt when switch frame is restored, since
	 * kernel thread begin to run without restoring trapframe.
	 */
	sf->sf_sr = PSL_MD |		/* kernel mode, interrupt enable */
	    (IPL_SCHED << 4);

#ifdef SH4
	if (CPU_IS_SH4) {
		/*
		 * Propagate floating point registers to the new process
		 * (they are not in the trapframe).
		 */
		if (p1 == curproc)
			fpu_save(&p1->p_md.md_pcb->pcb_fp);
		bcopy(&p1->p_md.md_pcb->pcb_fp, &pcb->pcb_fp,
		    sizeof(struct fpreg));
	}
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
