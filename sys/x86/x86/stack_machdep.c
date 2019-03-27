/*-
 * Copyright (c) 2015 EMC Corporation
 * Copyright (c) 2005 Antoine Brodin
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_stack.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/stack.h>

#include <machine/pcb.h>
#include <machine/smp.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/pmap.h>

#include <x86/stack.h>

#ifdef __i386__
#define	PCB_FP(pcb)	((pcb)->pcb_ebp)
#define	TF_FLAGS(tf)	((tf)->tf_eflags)
#define	TF_FP(tf)	((tf)->tf_ebp)
#define	TF_PC(tf)	((tf)->tf_eip)

typedef struct i386_frame *x86_frame_t;
#else
#define	PCB_FP(pcb)	((pcb)->pcb_rbp)
#define	TF_FLAGS(tf)	((tf)->tf_rflags)
#define	TF_FP(tf)	((tf)->tf_rbp)
#define	TF_PC(tf)	((tf)->tf_rip)

typedef struct amd64_frame *x86_frame_t;
#endif

#ifdef STACK
static struct stack *nmi_stack;
static volatile struct thread *nmi_pending;

#ifdef SMP
static struct mtx nmi_lock;
MTX_SYSINIT(nmi_lock, &nmi_lock, "stack_nmi", MTX_SPIN);
#endif
#endif

static void
stack_capture(struct thread *td, struct stack *st, register_t fp)
{
	x86_frame_t frame;
	vm_offset_t callpc;

	stack_zero(st);
	frame = (x86_frame_t)fp;
	while (1) {
		if ((vm_offset_t)frame < td->td_kstack ||
		    (vm_offset_t)frame >= td->td_kstack +
		    td->td_kstack_pages * PAGE_SIZE)
			break;
		callpc = frame->f_retaddr;
		if (!INKERNEL(callpc))
			break;
		if (stack_put(st, callpc) == -1)
			break;
		if (frame->f_frame <= frame)
			break;
		frame = frame->f_frame;
	}
}

int
stack_nmi_handler(struct trapframe *tf)
{

#ifdef STACK
	/* Don't consume an NMI that wasn't meant for us. */
	if (nmi_stack == NULL || curthread != nmi_pending)
		return (0);

	if (!TRAPF_USERMODE(tf) && (TF_FLAGS(tf) & PSL_I) != 0)
		stack_capture(curthread, nmi_stack, TF_FP(tf));
	else
		/* We were running in usermode or had interrupts disabled. */
		nmi_stack->depth = 0;

	atomic_store_rel_ptr((long *)&nmi_pending, (long)NULL);
	return (1);
#else
	return (0);
#endif
}

void
stack_save_td(struct stack *st, struct thread *td)
{

	if (TD_IS_SWAPPED(td))
		panic("stack_save_td: swapped");
	if (TD_IS_RUNNING(td))
		panic("stack_save_td: running");

	stack_capture(td, st, PCB_FP(td->td_pcb));
}

int
stack_save_td_running(struct stack *st, struct thread *td)
{

#ifdef STACK
	THREAD_LOCK_ASSERT(td, MA_OWNED);
	MPASS(TD_IS_RUNNING(td));

	if (td == curthread) {
		stack_save(st);
		return (0);
	}

#ifdef SMP
	mtx_lock_spin(&nmi_lock);

	nmi_stack = st;
	nmi_pending = td;
	ipi_cpu(td->td_oncpu, IPI_TRACE);
	while ((void *)atomic_load_acq_ptr((long *)&nmi_pending) != NULL)
		cpu_spinwait();
	nmi_stack = NULL;

	mtx_unlock_spin(&nmi_lock);

	if (st->depth == 0)
		return (EAGAIN);
#else /* !SMP */
	KASSERT(0, ("curthread isn't running"));
#endif /* SMP */
	return (0);
#else /* !STACK */
	return (EOPNOTSUPP);
#endif /* STACK */
}

void
stack_save(struct stack *st)
{
	register_t fp;

#ifdef __i386__
	__asm __volatile("movl %%ebp,%0" : "=g" (fp));
#else
	__asm __volatile("movq %%rbp,%0" : "=g" (fp));
#endif
	stack_capture(curthread, st, fp);
}
