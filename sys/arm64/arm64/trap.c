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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/pioctl.h>
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

#include <machine/frame.h>
#include <machine/pcb.h>
#include <machine/pcpu.h>
#include <machine/undefined.h>

#ifdef KDTRACE_HOOKS
#include <sys/dtrace_bsd.h>
#endif

#ifdef VFP
#include <machine/vfp.h>
#endif

#ifdef KDB
#include <machine/db_machdep.h>
#endif

#ifdef DDB
#include <ddb/db_output.h>
#endif

extern register_t fsu_intr_fault;

/* Called from exception.S */
void do_el1h_sync(struct thread *, struct trapframe *);
void do_el0_sync(struct thread *, struct trapframe *);
void do_el0_error(struct trapframe *);
void do_serror(struct trapframe *);
void unhandled_exception(struct trapframe *);

static void print_registers(struct trapframe *frame);

int (*dtrace_invop_jump_addr)(struct trapframe *);

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

	nap = 8;
	p = td->td_proc;
	ap = td->td_frame->tf_x;
	sa = &td->td_sa;

	sa->code = td->td_frame->tf_x[8];

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
		panic("ARM64TODO: Could we have more than 8 args?");

	td->td_retval[0] = 0;
	td->td_retval[1] = 0;

	return (0);
}

#include "../../kern/subr_syscall.c"

static void
svc_handler(struct thread *td, struct trapframe *frame)
{
	int error;

	if ((frame->tf_esr & ESR_ELx_ISS_MASK) == 0) {
		error = syscallenter(td);
		syscallret(td, error);
	} else {
		call_trapsignal(td, SIGILL, ILL_ILLOPN, (void *)frame->tf_elr);
		userret(td, frame);
	}
}

static void
data_abort(struct thread *td, struct trapframe *frame, uint64_t esr,
    uint64_t far, int lower, int exec)
{
	struct vm_map *map;
	struct proc *p;
	struct pcb *pcb;
	vm_prot_t ftype;
	vm_offset_t va;
	int error, sig, ucode;
#ifdef KDB
	bool handled;
#endif

	/*
	 * According to the ARMv8-A rev. A.g, B2.10.5 "Load-Exclusive
	 * and Store-Exclusive instruction usage restrictions", state
	 * of the exclusive monitors after data abort exception is unknown.
	 */
	clrex();

#ifdef KDB
	if (kdb_active) {
		kdb_reenter();
		return;
	}
#endif

	pcb = td->td_pcb;
	p = td->td_proc;
	if (lower)
		map = &p->p_vmspace->vm_map;
	else {
		/* The top bit tells us which range to use */
		if (far >= VM_MAXUSER_ADDRESS) {
			map = kernel_map;
		} else {
			map = &p->p_vmspace->vm_map;
			if (map == NULL)
				map = kernel_map;
		}
	}

	/*
	 * The call to pmap_fault can be dangerous when coming from the
	 * kernel as it may be not be able to lock the pmap to check if
	 * the address is now valid. Because of this we filter the cases
	 * when we are not going to see superpage activity.
	 */
	if (!lower) {
		/*
		 * We may fault in a DMAP region due to a superpage being
		 * unmapped when the access took place.
		 */
		if (map == kernel_map && !VIRT_IN_DMAP(far))
			goto no_pmap_fault;
		/*
		 * We can also fault in the userspace handling functions,
		 * e.g. copyin. In these cases we will have set a fault
		 * handler so we can check if this is set before calling
		 * pmap_fault.
		 */
		if (map != kernel_map && pcb->pcb_onfault == 0)
			goto no_pmap_fault;
	}

	if (pmap_fault(map->pmap, esr, far) == KERN_SUCCESS)
		return;

no_pmap_fault:
	KASSERT(td->td_md.md_spinlock_count == 0,
	    ("data abort with spinlock held"));
	if (td->td_critnest != 0 || WITNESS_CHECK(WARN_SLEEPOK |
	    WARN_GIANTOK, NULL, "Kernel page fault") != 0) {
		print_registers(frame);
		printf(" far: %16lx\n", far);
		printf(" esr:         %.8lx\n", esr);
		panic("data abort in critical section or under mutex");
	}

	va = trunc_page(far);
	ftype = ((esr >> 6) & 1) ? VM_PROT_READ | VM_PROT_WRITE : VM_PROT_READ;
	if (exec)
		ftype |= VM_PROT_EXECUTE;

	/* Fault in the page. */
	error = vm_fault(map, va, ftype, VM_FAULT_NORMAL);
	if (error != KERN_SUCCESS) {
		if (lower) {
			sig = SIGSEGV;
			if (error == KERN_PROTECTION_FAILURE)
				ucode = SEGV_ACCERR;
			else
				ucode = SEGV_MAPERR;
			call_trapsignal(td, sig, ucode, (void *)far);
		} else {
			if (td->td_intr_nesting_level == 0 &&
			    pcb->pcb_onfault != 0) {
				frame->tf_x[0] = error;
				frame->tf_elr = pcb->pcb_onfault;
				return;
			}

			printf("Fatal data abort:\n");
			print_registers(frame);
			printf(" far: %16lx\n", far);
			printf(" esr:         %.8lx\n", esr);

#ifdef KDB
			if (debugger_on_trap) {
				kdb_why = KDB_WHY_TRAP;
				handled = kdb_trap(ESR_ELx_EXCEPTION(esr), 0,
				    frame);
				kdb_why = KDB_WHY_UNSET;
				if (handled)
					return;
			}
#endif
			panic("vm_fault failed: %lx", frame->tf_elr);
		}
	}

	if (lower)
		userret(td, frame);
}

static void
print_registers(struct trapframe *frame)
{
	u_int reg;

	for (reg = 0; reg < nitems(frame->tf_x); reg++) {
		printf(" %sx%d: %16lx\n", (reg < 10) ? " " : "", reg,
		    frame->tf_x[reg]);
	}
	printf("  sp: %16lx\n", frame->tf_sp);
	printf("  lr: %16lx\n", frame->tf_lr);
	printf(" elr: %16lx\n", frame->tf_elr);
	printf("spsr:         %8x\n", frame->tf_spsr);
}

void
do_el1h_sync(struct thread *td, struct trapframe *frame)
{
	struct trapframe *oframe;
	uint32_t exception;
	uint64_t esr, far;

	/* Read the esr register to get the exception details */
	esr = frame->tf_esr;
	exception = ESR_ELx_EXCEPTION(esr);

#ifdef KDTRACE_HOOKS
	if (dtrace_trap_func != NULL && (*dtrace_trap_func)(frame, exception))
		return;
#endif

	CTR4(KTR_TRAP,
	    "do_el1_sync: curthread: %p, esr %lx, elr: %lx, frame: %p", td,
	    esr, frame->tf_elr, frame);

	oframe = td->td_frame;

	switch (exception) {
	case EXCP_BRK:
	case EXCP_WATCHPT_EL1:
	case EXCP_SOFTSTP_EL1:
		break;
	default:
		td->td_frame = frame;
		break;
	}

	switch(exception) {
	case EXCP_FP_SIMD:
	case EXCP_TRAP_FP:
#ifdef VFP
		if ((td->td_pcb->pcb_fpflags & PCB_FP_KERN) != 0) {
			vfp_restore_state();
		} else
#endif
		{
			print_registers(frame);
			printf(" esr:         %.8lx\n", esr);
			panic("VFP exception in the kernel");
		}
		break;
	case EXCP_INSN_ABORT:
	case EXCP_DATA_ABORT:
		far = READ_SPECIALREG(far_el1);
		intr_enable();
		data_abort(td, frame, esr, far, 0,
		    exception == EXCP_INSN_ABORT);
		break;
	case EXCP_BRK:
#ifdef KDTRACE_HOOKS
		if ((esr & ESR_ELx_ISS_MASK) == 0x40d && \
		    dtrace_invop_jump_addr != 0) {
			dtrace_invop_jump_addr(frame);
			break;
		}
#endif
#ifdef KDB
		kdb_trap(exception, 0,
		    (td->td_frame != NULL) ? td->td_frame : frame);
#else
		panic("No debugger in kernel.\n");
#endif
		frame->tf_elr += 4;
		break;
	case EXCP_WATCHPT_EL1:
	case EXCP_SOFTSTP_EL1:
#ifdef KDB
		kdb_trap(exception, 0,
		    (td->td_frame != NULL) ? td->td_frame : frame);
#else
		panic("No debugger in kernel.\n");
#endif
		break;
	case EXCP_UNKNOWN:
		if (undef_insn(1, frame))
			break;
		/* FALLTHROUGH */
	default:
		print_registers(frame);
		panic("Unknown kernel exception %x esr_el1 %lx\n", exception,
		    esr);
	}

	td->td_frame = oframe;
}

void
do_el0_sync(struct thread *td, struct trapframe *frame)
{
	pcpu_bp_harden bp_harden;
	uint32_t exception;
	uint64_t esr, far;

	/* Check we have a sane environment when entering from userland */
	KASSERT((uintptr_t)get_pcpu() >= VM_MIN_KERNEL_ADDRESS,
	    ("Invalid pcpu address from userland: %p (tpidr %lx)",
	     get_pcpu(), READ_SPECIALREG(tpidr_el1)));

	esr = frame->tf_esr;
	exception = ESR_ELx_EXCEPTION(esr);
	switch (exception) {
	case EXCP_INSN_ABORT_L:
		far = READ_SPECIALREG(far_el1);

		/*
		 * Userspace may be trying to train the branch predictor to
		 * attack the kernel. If we are on a CPU affected by this
		 * call the handler to clear the branch predictor state.
		 */
		if (far > VM_MAXUSER_ADDRESS) {
			bp_harden = PCPU_GET(bp_harden);
			if (bp_harden != NULL)
				bp_harden();
		}
		break;
	case EXCP_UNKNOWN:
	case EXCP_DATA_ABORT_L:
	case EXCP_DATA_ABORT:
		far = READ_SPECIALREG(far_el1);
		break;
	}
	intr_enable();

	CTR4(KTR_TRAP,
	    "do_el0_sync: curthread: %p, esr %lx, elr: %lx, frame: %p", td, esr,
	    frame->tf_elr, frame);

	switch(exception) {
	case EXCP_FP_SIMD:
	case EXCP_TRAP_FP:
#ifdef VFP
		vfp_restore_state();
#else
		panic("VFP exception in userland");
#endif
		break;
	case EXCP_SVC32:
	case EXCP_SVC64:
		svc_handler(td, frame);
		break;
	case EXCP_INSN_ABORT_L:
	case EXCP_DATA_ABORT_L:
	case EXCP_DATA_ABORT:
		data_abort(td, frame, esr, far, 1,
		    exception == EXCP_INSN_ABORT_L);
		break;
	case EXCP_UNKNOWN:
		if (!undef_insn(0, frame))
			call_trapsignal(td, SIGILL, ILL_ILLTRP, (void *)far);
		userret(td, frame);
		break;
	case EXCP_SP_ALIGN:
		call_trapsignal(td, SIGBUS, BUS_ADRALN, (void *)frame->tf_sp);
		userret(td, frame);
		break;
	case EXCP_PC_ALIGN:
		call_trapsignal(td, SIGBUS, BUS_ADRALN, (void *)frame->tf_elr);
		userret(td, frame);
		break;
	case EXCP_BRK:
		call_trapsignal(td, SIGTRAP, TRAP_BRKPT, (void *)frame->tf_elr);
		userret(td, frame);
		break;
	case EXCP_MSR:
		call_trapsignal(td, SIGILL, ILL_PRVOPC, (void *)frame->tf_elr); 
		userret(td, frame);
		break;
	case EXCP_SOFTSTP_EL0:
		td->td_frame->tf_spsr &= ~PSR_SS;
		td->td_pcb->pcb_flags &= ~PCB_SINGLE_STEP;
		WRITE_SPECIALREG(MDSCR_EL1,
		    READ_SPECIALREG(MDSCR_EL1) & ~DBG_MDSCR_SS);
		call_trapsignal(td, SIGTRAP, TRAP_TRACE,
		    (void *)frame->tf_elr);
		userret(td, frame);
		break;
	default:
		call_trapsignal(td, SIGBUS, BUS_OBJERR, (void *)frame->tf_elr);
		userret(td, frame);
		break;
	}

	KASSERT((td->td_pcb->pcb_fpflags & ~PCB_FP_USERMASK) == 0,
	    ("Kernel VFP flags set while entering userspace"));
	KASSERT(
	    td->td_pcb->pcb_fpusaved == &td->td_pcb->pcb_fpustate,
	    ("Kernel VFP state in use when entering userspace"));
}

/*
 * TODO: We will need to handle these later when we support ARMv8.2 RAS.
 */
void
do_serror(struct trapframe *frame)
{
	uint64_t esr, far;

	far = READ_SPECIALREG(far_el1);
	esr = frame->tf_esr;

	print_registers(frame);
	printf(" far: %16lx\n", far);
	printf(" esr:         %.8lx\n", esr);
	panic("Unhandled System Error");
}

void
unhandled_exception(struct trapframe *frame)
{
	uint64_t esr, far;

	far = READ_SPECIALREG(far_el1);
	esr = frame->tf_esr;

	print_registers(frame);
	printf(" far: %16lx\n", far);
	printf(" esr:         %.8lx\n", esr);
	panic("Unhandled exception");
}
