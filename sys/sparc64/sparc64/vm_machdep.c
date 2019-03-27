/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1982, 1986 The Regents of the University of California.
 * Copyright (c) 1989, 1990 William Jolitz
 * Copyright (c) 1994 John Dyson
 * Copyright (c) 2001 Jake Burkholder.
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
 *	from: @(#)vm_machdep.c	7.3 (Berkeley) 5/13/91
 *	Utah $Hdr: vm_machdep.c 1.16.1.1 89/06/23$
 *	from: FreeBSD: src/sys/i386/i386/vm_machdep.c,v 1.167 2001/07/12
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_pmap.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bio.h>
#include <sys/buf.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/sysent.h>
#include <sys/sched.h>
#include <sys/sf_buf.h>
#include <sys/sysctl.h>
#include <sys/unistd.h>
#include <sys/vmmeter.h>

#include <dev/ofw/openfirm.h>

#include <vm/vm.h>
#include <vm/vm_extern.h>
#include <vm/pmap.h>
#include <vm/vm_kern.h>
#include <vm/vm_map.h>
#include <vm/vm_page.h>
#include <vm/vm_pageout.h>
#include <vm/vm_param.h>
#include <vm/uma.h>
#include <vm/uma_int.h>

#include <machine/cache.h>
#include <machine/cpu.h>
#include <machine/fp.h>
#include <machine/frame.h>
#include <machine/fsr.h>
#include <machine/md_var.h>
#include <machine/ofw_machdep.h>
#include <machine/ofw_mem.h>
#include <machine/pcb.h>
#include <machine/tlb.h>
#include <machine/tstate.h>

PMAP_STATS_VAR(uma_nsmall_alloc);
PMAP_STATS_VAR(uma_nsmall_alloc_oc);
PMAP_STATS_VAR(uma_nsmall_free);

void
cpu_exit(struct thread *td)
{
	struct proc *p;

	p = td->td_proc;
	p->p_md.md_sigtramp = NULL;
	if (p->p_md.md_utrap != NULL) {
		utrap_free(p->p_md.md_utrap);
		p->p_md.md_utrap = NULL;
	}
}

void
cpu_thread_exit(struct thread *td)
{

}

void
cpu_thread_clean(struct thread *td)
{

}

void
cpu_thread_alloc(struct thread *td)
{
	struct pcb *pcb;

	pcb = (struct pcb *)((td->td_kstack + td->td_kstack_pages * PAGE_SIZE -
	    sizeof(struct pcb)) & ~0x3fUL);
	pcb->pcb_nsaved = 0;
	td->td_frame = (struct trapframe *)pcb - 1;
	td->td_pcb = pcb;
}

void
cpu_thread_free(struct thread *td)
{

}

void
cpu_thread_swapin(struct thread *td)
{

}

void
cpu_thread_swapout(struct thread *td)
{

}

void
cpu_set_syscall_retval(struct thread *td, int error)
{

	switch (error) {
	case 0:
		td->td_frame->tf_out[0] = td->td_retval[0];
		td->td_frame->tf_out[1] = td->td_retval[1];
		td->td_frame->tf_tstate &= ~TSTATE_XCC_C;
		break;

	case ERESTART:
		/*
		 * Undo the tpc advancement we have done on syscall
		 * enter, we want to reexecute the system call.
		 */
		td->td_frame->tf_tpc = td->td_pcb->pcb_tpc;
		td->td_frame->tf_tnpc -= 4;
		break;

	case EJUSTRETURN:
		break;

	default:
		td->td_frame->tf_out[0] = SV_ABI_ERRNO(td->td_proc, error);
		td->td_frame->tf_tstate |= TSTATE_XCC_C;
		break;
	}
}

void
cpu_copy_thread(struct thread *td, struct thread *td0)
{
	struct trapframe *tf;
	struct frame *fr;
	struct pcb *pcb;

	bcopy(td0->td_frame, td->td_frame, sizeof(struct trapframe));

	pcb = td->td_pcb;
	tf = td->td_frame;
	fr = (struct frame *)tf - 1;
	fr->fr_local[0] = (u_long)fork_return;
	fr->fr_local[1] = (u_long)td;
	fr->fr_local[2] = (u_long)tf;
	pcb->pcb_pc = (u_long)fork_trampoline - 8;
	pcb->pcb_sp = (u_long)fr - SPOFF;

	/* Setup to release the spin count in fork_exit(). */
	td->td_md.md_spinlock_count = 1;
	td->td_md.md_saved_pil = 0;
}

void
cpu_set_upcall(struct thread *td, void (*entry)(void *), void *arg,
    stack_t *stack)
{
	struct trapframe *tf;
	uint64_t sp;

	if (td == curthread)
		flushw();
	tf = td->td_frame;
	sp = (uint64_t)stack->ss_sp + stack->ss_size;
	tf->tf_out[0] = (uint64_t)arg;
	tf->tf_out[6] = sp - SPOFF - sizeof(struct frame);
	tf->tf_tpc = (uint64_t)entry;
	tf->tf_tnpc = tf->tf_tpc + 4;

	td->td_retval[0] = tf->tf_out[0];
	td->td_retval[1] = tf->tf_out[1];
}

int
cpu_set_user_tls(struct thread *td, void *tls_base)
{

	if (td == curthread)
		flushw();
	td->td_frame->tf_global[7] = (uint64_t)tls_base;
	return (0);
}

/*
 * Finish a fork operation, with process p2 nearly set up.
 * Copy and update the pcb, set up the stack so that the child
 * ready to run and return to user mode.
 */
void
cpu_fork(struct thread *td1, struct proc *p2, struct thread *td2, int flags)
{
	struct trapframe *tf;
	struct frame *fp;
	struct pcb *pcb1;
	struct pcb *pcb2;
	vm_offset_t sp;
	int error;
	int i;

	KASSERT(td1 == curthread || td1 == &thread0,
	    ("cpu_fork: p1 not curproc and not proc0"));

	if ((flags & RFPROC) == 0)
		return;

	p2->p_md.md_sigtramp = td1->td_proc->p_md.md_sigtramp;
	p2->p_md.md_utrap = utrap_hold(td1->td_proc->p_md.md_utrap);

	/* The pcb must be aligned on a 64-byte boundary. */
	pcb1 = td1->td_pcb;
	pcb2 = (struct pcb *)((td2->td_kstack + td2->td_kstack_pages *
	    PAGE_SIZE - sizeof(struct pcb)) & ~0x3fUL);
	td2->td_pcb = pcb2;

	/*
	 * Ensure that p1's pcb is up to date.
	 */
	critical_enter();
	if ((td1->td_frame->tf_fprs & FPRS_FEF) != 0)
		savefpctx(pcb1->pcb_ufp);
	critical_exit();
	/* Make sure the copied windows are spilled. */
	flushw();
	/* Copy the pcb (this will copy the windows saved in the pcb, too). */
	bcopy(pcb1, pcb2, sizeof(*pcb1));

	/*
	 * If we're creating a new user process and we're sharing the address
	 * space, the parent's top most frame must be saved in the pcb.  The
	 * child will pop the frame when it returns to user mode, and may
	 * overwrite it with its own data causing much suffering for the
	 * parent.  We check if its already in the pcb, and if not copy it
	 * in.  Its unlikely that the copyin will fail, but if so there's not
	 * much we can do.  The parent will likely crash soon anyway in that
	 * case.
	 */
	if ((flags & RFMEM) != 0 && td1 != &thread0) {
		sp = td1->td_frame->tf_sp;
		for (i = 0; i < pcb1->pcb_nsaved; i++) {
			if (pcb1->pcb_rwsp[i] == sp)
				break;
		}
		if (i == pcb1->pcb_nsaved) {
			error = copyin((caddr_t)sp + SPOFF, &pcb1->pcb_rw[i],
			    sizeof(struct rwindow));
			if (error == 0) {
				pcb1->pcb_rwsp[i] = sp;
				pcb1->pcb_nsaved++;
			}
		}
	}

	/*
	 * Create a new fresh stack for the new process.
	 * Copy the trap frame for the return to user mode as if from a
	 * syscall.  This copies most of the user mode register values.
	 */
	tf = (struct trapframe *)pcb2 - 1;
	bcopy(td1->td_frame, tf, sizeof(*tf));

	tf->tf_out[0] = 0;			/* Child returns zero */
	tf->tf_out[1] = 0;
	tf->tf_tstate &= ~TSTATE_XCC_C;		/* success */
	tf->tf_fprs = 0;

	td2->td_frame = tf;
	fp = (struct frame *)tf - 1;
	fp->fr_local[0] = (u_long)fork_return;
	fp->fr_local[1] = (u_long)td2;
	fp->fr_local[2] = (u_long)tf;
	/* Terminate stack traces at this frame. */
	fp->fr_pc = fp->fr_fp = 0;
	pcb2->pcb_sp = (u_long)fp - SPOFF;
	pcb2->pcb_pc = (u_long)fork_trampoline - 8;

	/* Setup to release the spin count in fork_exit(). */
	td2->td_md.md_spinlock_count = 1;
	td2->td_md.md_saved_pil = 0;

	/*
	 * Now, cpu_switch() can schedule the new process.
	 */
}

void
cpu_reset(void)
{
	static char bspec[64] = "";
	phandle_t chosen;
	static struct {
		cell_t	name;
		cell_t	nargs;
		cell_t	nreturns;
		cell_t	bootspec;
	} args = {
		(cell_t)"boot",
		1,
		0,
		(cell_t)bspec
	};

	if ((chosen = OF_finddevice("/chosen")) != -1) {
		if (OF_getprop(chosen, "bootpath", bspec, sizeof(bspec)) == -1)
			bspec[0] = '\0';
		bspec[sizeof(bspec) - 1] = '\0';
	}

	cpu_shutdown(&args);
}

/*
 * Intercept the return address from a freshly forked process that has NOT
 * been scheduled yet.
 *
 * This is needed to make kernel threads stay in kernel mode.
 */
void
cpu_fork_kthread_handler(struct thread *td, void (*func)(void *), void *arg)
{
	struct frame *fp;
	struct pcb *pcb;

	pcb = td->td_pcb;
	fp = (struct frame *)(pcb->pcb_sp + SPOFF);
	fp->fr_local[0] = (u_long)func;
	fp->fr_local[1] = (u_long)arg;
}

bool
cpu_exec_vmspace_reuse(struct proc *p __unused, vm_map_t map __unused)
{

	return (true);
}

int
cpu_procctl(struct thread *td __unused, int idtype __unused, id_t id __unused,
    int com __unused, void *data __unused)
{

	return (EINVAL);
}

int
is_physical_memory(vm_paddr_t addr)
{
	struct ofw_mem_region *mr;

	for (mr = sparc64_memreg; mr < sparc64_memreg + sparc64_nmemreg; mr++)
		if (addr >= mr->mr_start && addr < mr->mr_start + mr->mr_size)
			return (1);
	return (0);
}

void
swi_vm(void *v)
{

	/* Nothing to do here - busdma bounce buffers are not implemented. */
}

void *
uma_small_alloc(uma_zone_t zone, vm_size_t bytes, int domain, u_int8_t *flags,
    int wait)
{
	vm_paddr_t pa;
	vm_page_t m;
	void *va;

	PMAP_STATS_INC(uma_nsmall_alloc);

	*flags = UMA_SLAB_PRIV;

	m = vm_page_alloc_domain(NULL, 0, domain,
	    malloc2vm_flags(wait) | VM_ALLOC_WIRED | VM_ALLOC_NOOBJ);
	if (m == NULL)
		return (NULL);

	pa = VM_PAGE_TO_PHYS(m);
	if (dcache_color_ignore == 0 && m->md.color != DCACHE_COLOR(pa)) {
		KASSERT(m->md.colors[0] == 0 && m->md.colors[1] == 0,
		    ("uma_small_alloc: free page %p still has mappings!", m));
		PMAP_STATS_INC(uma_nsmall_alloc_oc);
		m->md.color = DCACHE_COLOR(pa);
		dcache_page_inval(pa);
	}
	va = (void *)TLB_PHYS_TO_DIRECT(pa);
	if ((wait & M_ZERO) && (m->flags & PG_ZERO) == 0)
		cpu_block_zero(va, PAGE_SIZE);
	return (va);
}

void
uma_small_free(void *mem, vm_size_t size, u_int8_t flags)
{
	vm_page_t m;

	PMAP_STATS_INC(uma_nsmall_free);
	m = PHYS_TO_VM_PAGE(TLB_DIRECT_TO_PHYS((vm_offset_t)mem));
	vm_page_unwire_noq(m);
	vm_page_free(m);
}

void
sf_buf_map(struct sf_buf *sf, int flags)
{

	pmap_qenter(sf->kva, &sf->m, 1);
}

int
sf_buf_unmap(struct sf_buf *sf)
{

	pmap_qremove(sf->kva, 1);
	return (1);
}
