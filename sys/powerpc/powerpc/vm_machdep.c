/*-
 * SPDX-License-Identifier: (BSD-4-Clause AND MIT-CMU)
 *
 * Copyright (c) 1982, 1986 The Regents of the University of California.
 * Copyright (c) 1989, 1990 William Jolitz
 * Copyright (c) 1994 John Dyson
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
 *	from: @(#)vm_machdep.c	7.3 (Berkeley) 5/13/91
 *	Utah $Hdr: vm_machdep.c 1.16.1.1 89/06/23$
 * $FreeBSD$
 */
/*-
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
#include <sys/malloc.h>
#include <sys/bio.h>
#include <sys/buf.h>
#include <sys/ktr.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/vnode.h>
#include <sys/vmmeter.h>
#include <sys/kernel.h>
#include <sys/mbuf.h>
#include <sys/sysctl.h>
#include <sys/sysent.h>
#include <sys/unistd.h>

#include <machine/cpu.h>
#include <machine/fpu.h>
#include <machine/frame.h>
#include <machine/md_var.h>
#include <machine/pcb.h>

#include <dev/ofw/openfirm.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/vm_kern.h>
#include <vm/vm_page.h>
#include <vm/vm_map.h>
#include <vm/vm_extern.h>

/*
 * Finish a fork operation, with process p2 nearly set up.
 * Copy and update the pcb, set up the stack so that the child
 * ready to run and return to user mode.
 */
void
cpu_fork(struct thread *td1, struct proc *p2, struct thread *td2, int flags)
{
	struct	trapframe *tf;
	struct	callframe *cf;
	struct	pcb *pcb;

	KASSERT(td1 == curthread || td1 == &thread0,
	    ("cpu_fork: p1 not curproc and not proc0"));
	CTR3(KTR_PROC, "cpu_fork: called td1=%p p2=%p flags=%x",
	    td1, p2, flags);

	if ((flags & RFPROC) == 0)
		return;

	pcb = (struct pcb *)((td2->td_kstack +
	    td2->td_kstack_pages * PAGE_SIZE - sizeof(struct pcb)) & ~0x2fUL);
	td2->td_pcb = pcb;

	/* Copy the pcb */
	bcopy(td1->td_pcb, pcb, sizeof(struct pcb));

	/*
	 * Create a fresh stack for the new process.
	 * Copy the trap frame for the return to user mode as if from a
	 * syscall.  This copies most of the user mode register values.
	 */
	tf = (struct trapframe *)pcb - 1;
	bcopy(td1->td_frame, tf, sizeof(*tf));

	/* Set up trap frame. */
	tf->fixreg[FIRSTARG] = 0;
	tf->fixreg[FIRSTARG + 1] = 0;
	tf->cr &= ~0x10000000;

	td2->td_frame = tf;

	cf = (struct callframe *)tf - 1;
	memset(cf, 0, sizeof(struct callframe));
	#if defined(__powerpc64__) && (!defined(_CALL_ELF) || _CALL_ELF == 1)
	cf->cf_toc = ((register_t *)fork_return)[1];
	#endif
	cf->cf_func = (register_t)fork_return;
	cf->cf_arg0 = (register_t)td2;
	cf->cf_arg1 = (register_t)tf;

	pcb->pcb_sp = (register_t)cf;
	KASSERT(pcb->pcb_sp % 16 == 0, ("stack misaligned"));
	#if defined(__powerpc64__) && (!defined(_CALL_ELF) || _CALL_ELF == 1)
	pcb->pcb_lr = ((register_t *)fork_trampoline)[0];
	pcb->pcb_toc = ((register_t *)fork_trampoline)[1];
	#else
	pcb->pcb_lr = (register_t)fork_trampoline;
	pcb->pcb_context[0] = pcb->pcb_lr;
	#endif
	#ifdef AIM
	pcb->pcb_cpu.aim.usr_vsid = 0;
	#endif

	/* Setup to release spin count in fork_exit(). */
	td2->td_md.md_spinlock_count = 1;
	td2->td_md.md_saved_msr = psl_kernset;

	/*
 	 * Now cpu_switch() can schedule the new process.
	 */
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
	struct callframe *cf;

	CTR4(KTR_PROC, "%s called with td=%p func=%p arg=%p",
	    __func__, td, func, arg);

	cf = (struct callframe *)td->td_pcb->pcb_sp;

	#if defined(__powerpc64__) && (!defined(_CALL_ELF) || _CALL_ELF == 1)
	cf->cf_toc = ((register_t *)func)[1];
	#endif
	cf->cf_func = (register_t)func;
	cf->cf_arg0 = (register_t)arg;
}

void
cpu_exit(struct thread *td)
{

}

/*
 * Software interrupt handler for queued VM system processing.
 */
void
swi_vm(void *dummy)
{

	if (busdma_swi_pending != 0)
		busdma_swi();
}

/*
 * Tell whether this address is in some physical memory region.
 * Currently used by the kernel coredump code in order to avoid
 * dumping the ``ISA memory hole'' which could cause indefinite hangs,
 * or other unpredictable behaviour.
 */
int
is_physical_memory(vm_offset_t addr)
{

	/*
	 * stuff other tests for known memory-mapped devices (PCI?)
	 * here
	 */
	return (1);
}

/*
 * CPU threading functions related to the VM layer. These could be used
 * to map the SLB bits required for the kernel stack instead of forcing a
 * fixed-size KVA.
 */

void
cpu_thread_swapin(struct thread *td)
{

}

void
cpu_thread_swapout(struct thread *td)
{

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
