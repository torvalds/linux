/*-
 * Copyright (c) 2014 Andrew Turner
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
 *
 */

#include "opt_platform.h"

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/limits.h>
#include <sys/proc.h>
#include <sys/sf_buf.h>
#include <sys/signal.h>
#include <sys/sysent.h>
#include <sys/unistd.h>

#include <vm/vm.h>
#include <vm/vm_page.h>
#include <vm/vm_map.h>
#include <vm/uma.h>
#include <vm/uma_int.h>

#include <machine/armreg.h>
#include <machine/cpu.h>
#include <machine/md_var.h>
#include <machine/pcb.h>
#include <machine/frame.h>

#ifdef VFP
#include <machine/vfp.h>
#endif

#include <dev/psci/psci.h>

/*
 * Finish a fork operation, with process p2 nearly set up.
 * Copy and update the pcb, set up the stack so that the child
 * ready to run and return to user mode.
 */
void
cpu_fork(struct thread *td1, struct proc *p2, struct thread *td2, int flags)
{
	struct pcb *pcb2;
	struct trapframe *tf;

	if ((flags & RFPROC) == 0)
		return;

	if (td1 == curthread) {
		/*
		 * Save the tpidr_el0 and the vfp state, these normally happen
		 * in cpu_switch, but if userland changes these then forks
		 * this may not have happened.
		 */
		td1->td_pcb->pcb_tpidr_el0 = READ_SPECIALREG(tpidr_el0);
		td1->td_pcb->pcb_tpidrro_el0 = READ_SPECIALREG(tpidrro_el0);
#ifdef VFP
		if ((td1->td_pcb->pcb_fpflags & PCB_FP_STARTED) != 0)
			vfp_save_state(td1, td1->td_pcb);
#endif
	}

	pcb2 = (struct pcb *)(td2->td_kstack +
	    td2->td_kstack_pages * PAGE_SIZE) - 1;

	td2->td_pcb = pcb2;
	bcopy(td1->td_pcb, pcb2, sizeof(*pcb2));

	td2->td_proc->p_md.md_l0addr =
	    vtophys(vmspace_pmap(td2->td_proc->p_vmspace)->pm_l0);

	tf = (struct trapframe *)STACKALIGN((struct trapframe *)pcb2 - 1);
	bcopy(td1->td_frame, tf, sizeof(*tf));
	tf->tf_x[0] = 0;
	tf->tf_x[1] = 0;
	tf->tf_spsr = td1->td_frame->tf_spsr & PSR_M_32;

	td2->td_frame = tf;

	/* Set the return value registers for fork() */
	td2->td_pcb->pcb_x[8] = (uintptr_t)fork_return;
	td2->td_pcb->pcb_x[9] = (uintptr_t)td2;
	td2->td_pcb->pcb_x[PCB_LR] = (uintptr_t)fork_trampoline;
	td2->td_pcb->pcb_sp = (uintptr_t)td2->td_frame;
	td2->td_pcb->pcb_fpusaved = &td2->td_pcb->pcb_fpustate;
	td2->td_pcb->pcb_vfpcpu = UINT_MAX;

	/* Setup to release spin count in fork_exit(). */
	td2->td_md.md_spinlock_count = 1;
	td2->td_md.md_saved_daif = td1->td_md.md_saved_daif & ~DAIF_I_MASKED;
}

void
cpu_reset(void)
{

	psci_reset();

	printf("cpu_reset failed");
	while(1)
		__asm volatile("wfi" ::: "memory");
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
	struct trapframe *frame;

	frame = td->td_frame;

	switch (error) {
	case 0:
		frame->tf_x[0] = td->td_retval[0];
		frame->tf_x[1] = td->td_retval[1];
		frame->tf_spsr &= ~PSR_C;	/* carry bit */
		break;
	case ERESTART:
		frame->tf_elr -= 4;
		break;
	case EJUSTRETURN:
		break;
	default:
		frame->tf_spsr |= PSR_C;	/* carry bit */
		frame->tf_x[0] = SV_ABI_ERRNO(td->td_proc, error);
		break;
	}
}

/*
 * Initialize machine state, mostly pcb and trap frame for a new
 * thread, about to return to userspace.  Put enough state in the new
 * thread's PCB to get it to go back to the fork_return(), which
 * finalizes the thread state and handles peculiarities of the first
 * return to userspace for the new thread.
 */
void
cpu_copy_thread(struct thread *td, struct thread *td0)
{
	bcopy(td0->td_frame, td->td_frame, sizeof(struct trapframe));
	bcopy(td0->td_pcb, td->td_pcb, sizeof(struct pcb));

	td->td_pcb->pcb_x[8] = (uintptr_t)fork_return;
	td->td_pcb->pcb_x[9] = (uintptr_t)td;
	td->td_pcb->pcb_x[PCB_LR] = (uintptr_t)fork_trampoline;
	td->td_pcb->pcb_sp = (uintptr_t)td->td_frame;
	td->td_pcb->pcb_fpusaved = &td->td_pcb->pcb_fpustate;
	td->td_pcb->pcb_vfpcpu = UINT_MAX;

	/* Setup to release spin count in fork_exit(). */
	td->td_md.md_spinlock_count = 1;
	td->td_md.md_saved_daif = td0->td_md.md_saved_daif & ~DAIF_I_MASKED;
}

/*
 * Set that machine state for performing an upcall that starts
 * the entry function with the given argument.
 */
void
cpu_set_upcall(struct thread *td, void (*entry)(void *), void *arg,
	stack_t *stack)
{
	struct trapframe *tf = td->td_frame;

	/* 32bits processes use r13 for sp */
	if (td->td_frame->tf_spsr & PSR_M_32)
		tf->tf_x[13] = STACKALIGN((uintptr_t)stack->ss_sp + stack->ss_size);
	else
		tf->tf_sp = STACKALIGN((uintptr_t)stack->ss_sp + stack->ss_size);
	tf->tf_elr = (register_t)entry;
	tf->tf_x[0] = (register_t)arg;
}

int
cpu_set_user_tls(struct thread *td, void *tls_base)
{
	struct pcb *pcb;

	if ((uintptr_t)tls_base >= VM_MAXUSER_ADDRESS)
		return (EINVAL);

	pcb = td->td_pcb;
	if (td->td_frame->tf_spsr & PSR_M_32) {
		/* 32bits arm stores the user TLS into tpidrro */
		pcb->pcb_tpidrro_el0 = (register_t)tls_base;
		pcb->pcb_tpidr_el0 = (register_t)tls_base;
		if (td == curthread) {
			WRITE_SPECIALREG(tpidrro_el0, tls_base);
			WRITE_SPECIALREG(tpidr_el0, tls_base);
		}
	} else {
		pcb->pcb_tpidr_el0 = (register_t)tls_base;
		if (td == curthread)
			WRITE_SPECIALREG(tpidr_el0, tls_base);
	}

	return (0);
}

void
cpu_thread_exit(struct thread *td)
{
}

void
cpu_thread_alloc(struct thread *td)
{

	td->td_pcb = (struct pcb *)(td->td_kstack +
	    td->td_kstack_pages * PAGE_SIZE) - 1;
	td->td_frame = (struct trapframe *)STACKALIGN(
	    (struct trapframe *)td->td_pcb - 1);
}

void
cpu_thread_free(struct thread *td)
{
}

void
cpu_thread_clean(struct thread *td)
{
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

	td->td_pcb->pcb_x[8] = (uintptr_t)func;
	td->td_pcb->pcb_x[9] = (uintptr_t)arg;
	td->td_pcb->pcb_x[PCB_LR] = (uintptr_t)fork_trampoline;
	td->td_pcb->pcb_sp = (uintptr_t)td->td_frame;
	td->td_pcb->pcb_fpusaved = &td->td_pcb->pcb_fpustate;
	td->td_pcb->pcb_vfpcpu = UINT_MAX;
}

void
cpu_exit(struct thread *td)
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

void
swi_vm(void *v)
{

	if (busdma_swi_pending != 0)
		busdma_swi();
}
