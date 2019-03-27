/*-
 * Copyright (c) 2015-2018 Ruslan Bukin <br@bsdpad.com>
 * All rights reserved.
 *
 * Portions of this software were developed by SRI International and the
 * University of Cambridge Computer Laboratory under DARPA/AFRL contract
 * FA8750-10-C-0237 ("CTSRD"), as part of the DARPA CRASH research programme.
 *
 * Portions of this software were developed by the University of Cambridge
 * Computer Laboratory as part of the CTSRD Project, with support from the
 * UK Higher Education Innovation Fund (HEIF).
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/pioctl.h>
#include <sys/bus.h>
#include <sys/proc.h>
#include <sys/ptrace.h>
#include <sys/syscall.h>
#include <sys/sysent.h>
#ifdef KDB
#include <sys/kdb.h>
#endif

#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/vm_kern.h>
#include <vm/vm_map.h>
#include <vm/vm_param.h>
#include <vm/vm_extern.h>

#ifdef FPE
#include <machine/fpe.h>
#endif
#include <machine/frame.h>
#include <machine/pcb.h>
#include <machine/pcpu.h>

#include <machine/resource.h>
#include <machine/intr.h>

#ifdef KDTRACE_HOOKS
#include <sys/dtrace_bsd.h>
#endif

int (*dtrace_invop_jump_addr)(struct trapframe *);

extern register_t fsu_intr_fault;

/* Called from exception.S */
void do_trap_supervisor(struct trapframe *);
void do_trap_user(struct trapframe *);

static __inline void
call_trapsignal(struct thread *td, int sig, int code, void *addr)
{
	ksiginfo_t ksi;

	ksiginfo_init_trap(&ksi);
	ksi.ksi_signo = sig;
	ksi.ksi_code = code;
	ksi.ksi_addr = addr;
	trapsignal(td, &ksi);
}

int
cpu_fetch_syscall_args(struct thread *td)
{
	struct proc *p;
	register_t *ap;
	struct syscall_args *sa;
	int nap;

	nap = NARGREG;
	p = td->td_proc;
	sa = &td->td_sa;
	ap = &td->td_frame->tf_a[0];

	sa->code = td->td_frame->tf_t[0];

	if (sa->code == SYS_syscall || sa->code == SYS___syscall) {
		sa->code = *ap++;
		nap--;
	}

	if (sa->code >= p->p_sysent->sv_size)
		sa->callp = &p->p_sysent->sv_table[0];
	else
		sa->callp = &p->p_sysent->sv_table[sa->code];

	sa->narg = sa->callp->sy_narg;
	memcpy(sa->args, ap, nap * sizeof(register_t));
	if (sa->narg > nap)
		panic("TODO: Could we have more then %d args?", NARGREG);

	td->td_retval[0] = 0;
	td->td_retval[1] = 0;

	return (0);
}

#include "../../kern/subr_syscall.c"

static void
dump_regs(struct trapframe *frame)
{
	int n;
	int i;

	n = (sizeof(frame->tf_t) / sizeof(frame->tf_t[0]));
	for (i = 0; i < n; i++)
		printf("t[%d] == 0x%016lx\n", i, frame->tf_t[i]);

	n = (sizeof(frame->tf_s) / sizeof(frame->tf_s[0]));
	for (i = 0; i < n; i++)
		printf("s[%d] == 0x%016lx\n", i, frame->tf_s[i]);

	n = (sizeof(frame->tf_a) / sizeof(frame->tf_a[0]));
	for (i = 0; i < n; i++)
		printf("a[%d] == 0x%016lx\n", i, frame->tf_a[i]);

	printf("sepc == 0x%016lx\n", frame->tf_sepc);
	printf("sstatus == 0x%016lx\n", frame->tf_sstatus);
}

static void
svc_handler(struct trapframe *frame)
{
	struct thread *td;
	int error;

	td = curthread;
	td->td_frame = frame;

	error = syscallenter(td);
	syscallret(td, error);
}

static void
data_abort(struct trapframe *frame, int usermode)
{
	struct vm_map *map;
	uint64_t stval;
	struct thread *td;
	struct pcb *pcb;
	vm_prot_t ftype;
	vm_offset_t va;
	struct proc *p;
	int error, sig, ucode;

#ifdef KDB
	if (kdb_active) {
		kdb_reenter();
		return;
	}
#endif

	td = curthread;
	p = td->td_proc;
	pcb = td->td_pcb;
	stval = frame->tf_stval;

	if (td->td_critnest != 0 || td->td_intr_nesting_level != 0 ||
	    WITNESS_CHECK(WARN_SLEEPOK | WARN_GIANTOK, NULL,
	    "Kernel page fault") != 0)
		goto fatal;

	if (usermode)
		map = &td->td_proc->p_vmspace->vm_map;
	else if (stval >= VM_MAX_USER_ADDRESS)
		map = kernel_map;
	else {
		if (pcb->pcb_onfault == 0)
			goto fatal;
		map = &td->td_proc->p_vmspace->vm_map;
	}

	va = trunc_page(stval);

	if ((frame->tf_scause == EXCP_FAULT_STORE) ||
	    (frame->tf_scause == EXCP_STORE_PAGE_FAULT)) {
		ftype = VM_PROT_WRITE;
	} else if (frame->tf_scause == EXCP_INST_PAGE_FAULT) {
		ftype = VM_PROT_EXECUTE;
	} else {
		ftype = VM_PROT_READ;
	}

	if (pmap_fault_fixup(map->pmap, va, ftype))
		goto done;

	if (map != kernel_map) {
		/*
		 * Keep swapout from messing with us during this
		 *	critical time.
		 */
		PROC_LOCK(p);
		++p->p_lock;
		PROC_UNLOCK(p);

		/* Fault in the user page: */
		error = vm_fault(map, va, ftype, VM_FAULT_NORMAL);

		PROC_LOCK(p);
		--p->p_lock;
		PROC_UNLOCK(p);
	} else {
		/*
		 * Don't have to worry about process locking or stacks in the
		 * kernel.
		 */
		error = vm_fault(map, va, ftype, VM_FAULT_NORMAL);
	}

	if (error != KERN_SUCCESS) {
		if (usermode) {
			sig = SIGSEGV;
			if (error == KERN_PROTECTION_FAILURE)
				ucode = SEGV_ACCERR;
			else
				ucode = SEGV_MAPERR;
			call_trapsignal(td, sig, ucode, (void *)stval);
		} else {
			if (pcb->pcb_onfault != 0) {
				frame->tf_a[0] = error;
				frame->tf_sepc = pcb->pcb_onfault;
				return;
			}
			goto fatal;
		}
	}

done:
	if (usermode)
		userret(td, frame);
	return;

fatal:
	dump_regs(frame);
	panic("Fatal page fault at %#lx: %#016lx", frame->tf_sepc, stval);
}

void
do_trap_supervisor(struct trapframe *frame)
{
	uint64_t exception;
	uint64_t sstatus;

	/* Ensure we came from supervisor mode, interrupts disabled */
	__asm __volatile("csrr %0, sstatus" : "=&r" (sstatus));
	KASSERT((sstatus & (SSTATUS_SPP | SSTATUS_SIE)) == SSTATUS_SPP,
			("We must came from S mode with interrupts disabled"));

	exception = (frame->tf_scause & EXCP_MASK);
	if (frame->tf_scause & EXCP_INTR) {
		/* Interrupt */
		riscv_cpu_intr(frame);
		return;
	}

#ifdef KDTRACE_HOOKS
	if (dtrace_trap_func != NULL && (*dtrace_trap_func)(frame, exception))
		return;
#endif

	CTR3(KTR_TRAP, "do_trap_supervisor: curthread: %p, sepc: %lx, frame: %p",
	    curthread, frame->tf_sepc, frame);

	switch(exception) {
	case EXCP_FAULT_LOAD:
	case EXCP_FAULT_STORE:
	case EXCP_FAULT_FETCH:
	case EXCP_STORE_PAGE_FAULT:
	case EXCP_LOAD_PAGE_FAULT:
		data_abort(frame, 0);
		break;
	case EXCP_BREAKPOINT:
#ifdef KDTRACE_HOOKS
		if (dtrace_invop_jump_addr != 0) {
			dtrace_invop_jump_addr(frame);
			break;
		}
#endif
#ifdef KDB
		kdb_trap(exception, 0, frame);
#else
		dump_regs(frame);
		panic("No debugger in kernel.\n");
#endif
		break;
	case EXCP_ILLEGAL_INSTRUCTION:
		dump_regs(frame);
		panic("Illegal instruction at 0x%016lx\n", frame->tf_sepc);
		break;
	default:
		dump_regs(frame);
		panic("Unknown kernel exception %x trap value %lx\n",
		    exception, frame->tf_stval);
	}
}

void
do_trap_user(struct trapframe *frame)
{
	uint64_t exception;
	struct thread *td;
	uint64_t sstatus;
	struct pcb *pcb;

	td = curthread;
	td->td_frame = frame;
	pcb = td->td_pcb;

	/* Ensure we came from usermode, interrupts disabled */
	__asm __volatile("csrr %0, sstatus" : "=&r" (sstatus));
	KASSERT((sstatus & (SSTATUS_SPP | SSTATUS_SIE)) == 0,
			("We must came from U mode with interrupts disabled"));

	exception = (frame->tf_scause & EXCP_MASK);
	if (frame->tf_scause & EXCP_INTR) {
		/* Interrupt */
		riscv_cpu_intr(frame);
		return;
	}

	CTR3(KTR_TRAP, "do_trap_user: curthread: %p, sepc: %lx, frame: %p",
	    curthread, frame->tf_sepc, frame);

	switch(exception) {
	case EXCP_FAULT_LOAD:
	case EXCP_FAULT_STORE:
	case EXCP_FAULT_FETCH:
	case EXCP_STORE_PAGE_FAULT:
	case EXCP_LOAD_PAGE_FAULT:
	case EXCP_INST_PAGE_FAULT:
		data_abort(frame, 1);
		break;
	case EXCP_USER_ECALL:
		frame->tf_sepc += 4;	/* Next instruction */
		svc_handler(frame);
		break;
	case EXCP_ILLEGAL_INSTRUCTION:
#ifdef FPE
		if ((pcb->pcb_fpflags & PCB_FP_STARTED) == 0) {
			/*
			 * May be a FPE trap. Enable FPE usage
			 * for this thread and try again.
			 */
			fpe_state_clear();
			frame->tf_sstatus &= ~SSTATUS_FS_MASK;
			frame->tf_sstatus |= SSTATUS_FS_CLEAN;
			pcb->pcb_fpflags |= PCB_FP_STARTED;
			break;
		}
#endif
		call_trapsignal(td, SIGILL, ILL_ILLTRP, (void *)frame->tf_sepc);
		userret(td, frame);
		break;
	case EXCP_BREAKPOINT:
		call_trapsignal(td, SIGTRAP, TRAP_BRKPT, (void *)frame->tf_sepc);
		userret(td, frame);
		break;
	default:
		dump_regs(frame);
		panic("Unknown userland exception %x, trap value %lx\n",
		    exception, frame->tf_stval);
	}
}
