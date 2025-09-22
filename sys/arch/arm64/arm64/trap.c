/* $OpenBSD: trap.c,v 1.54 2025/08/01 10:14:59 kettenis Exp $ */
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
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/ptrace.h>
#include <sys/syscall.h>
#include <sys/signalvar.h>
#include <sys/user.h>

#include <uvm/uvm_extern.h>

#include <machine/cpu.h>
#include <machine/fpu.h>
#include <machine/frame.h>
#include <machine/pcb.h>
#include <machine/vmparam.h>

#ifdef DDB
#include <ddb/db_output.h>
#include <machine/db_machdep.h>
#endif

/* Called from exception.S */
void do_el1h_sync(struct trapframe *);
void do_el0_sync(struct trapframe *);
void do_el0_error(struct trapframe *);

void dumpregs(struct trapframe*);

/* Check whether we're executing an unprivileged load/store instruction. */
static inline int
is_unpriv_ldst(uint64_t elr)
{
	if ((elr >> 63) == 1) {
		uint32_t insn = *(uint32_t *)elr;
		return ((insn & 0x3f200c00) == 0x38000800);
	}

	return 0;
}

static inline int
accesstype(uint64_t esr, int exe)
{
	if (exe)
		return PROT_EXEC;
	return (!(esr & ISS_DATA_CM) && (esr & ISS_DATA_WnR)) ?
	    PROT_WRITE : PROT_READ;
}

static inline void
fault(const char *fmt, ...)
{
	struct cpu_info *ci = curcpu();
	va_list ap;

	atomic_cas_ptr(&panicstr, NULL, ci->ci_panicbuf);

	va_start(ap, fmt);
	vsnprintf(ci->ci_panicbuf, sizeof(ci->ci_panicbuf), fmt, ap);
	va_end(ap);
#ifdef DDB
	db_printf("%s\n", ci->ci_panicbuf);
#else
	printf("%s", ci->ci_panicbuf);
#endif
}

static void
udata_abort(struct trapframe *frame, uint64_t esr, uint64_t far, int exe)
{
	struct vm_map *map;
	struct proc *p;
	struct pcb *pcb;
	vm_prot_t access_type = accesstype(esr, exe);
	vaddr_t va;
	union sigval sv;
	int error = 0, sig, code;

	pcb = curcpu()->ci_curpcb;
	p = curcpu()->ci_curproc;

	va = trunc_page(far);
	if (va >= VM_MAXUSER_ADDRESS)
		curcpu()->ci_flush_bp();

	switch (esr & ISS_DATA_DFSC_MASK) {
	case ISS_DATA_DFSC_ALIGN:
		sv.sival_ptr = (void *)far;
		trapsignal(p, SIGBUS, esr, BUS_ADRALN, sv);
		return;
	default:
		break;
	}

	map = &p->p_vmspace->vm_map;

	if (!uvm_map_inentry(p, &p->p_spinentry, PROC_STACK(p),
	    "[%s]%d/%d sp=%lx inside %lx-%lx: not MAP_STACK\n",
	    uvm_map_inentry_sp, p->p_vmspace->vm_map.sserial))
		return;

	/* Handle referenced/modified emulation */
	if (pmap_fault_fixup(map->pmap, va, access_type))
		return;

	error = uvm_fault(map, va, 0, access_type);

	if (error == 0) {
		uvm_grow(p, va);
		return;
	}

	if (error == ENOMEM) {
		sig = SIGKILL;
		code = 0;
	} else if (error == EIO) {
		sig = SIGBUS;
		code = BUS_OBJERR;
	} else if (error == EACCES) {
		sig = SIGSEGV;
		code = SEGV_ACCERR;
	} else {
		sig = SIGSEGV;
		code = SEGV_MAPERR;
	}
	sv.sival_ptr = (void *)far;
	trapsignal(p, sig, esr, code, sv);
}

static void
kdata_abort(struct trapframe *frame, uint64_t esr, uint64_t far, int exe)
{
	struct vm_map *map;
	struct proc *p;
	struct pcb *pcb;
	vm_prot_t access_type = accesstype(esr, exe);
	vaddr_t va;
	int error = 0;

	pcb = curcpu()->ci_curpcb;
	p = curcpu()->ci_curproc;

	va = trunc_page(far);

	/* The top bit tells us which range to use */
	if ((far >> 63) == 1)
		map = kernel_map;
	else {
		/*
		 * Only allow user-space access using
		 * unprivileged load/store instructions.
		 */
		if (is_unpriv_ldst(frame->tf_elr))
			map = &p->p_vmspace->vm_map;
		else if (pcb->pcb_onfault != NULL)
			map = kernel_map;
		else {
#ifdef DDB
			fault("attempt to %s user address 0x%llx from EL1",
			    exe ? "execute" : "access", far);
			db_ktrap(ESR_ELx_EXCEPTION(esr), frame);
			map = kernel_map;
#else
			panic("attempt to %s user address 0x%llx from EL1",
			    exe ? "execute" : "access", far);
#endif			
		}
	}

	/* Handle referenced/modified emulation */
	if (!pmap_fault_fixup(map->pmap, va, access_type)) {
		error = uvm_fault(map, va, 0, access_type);

		if (error == 0 && map != kernel_map)
			uvm_grow(p, va);
	}

	if (error != 0) {
		if (curcpu()->ci_idepth == 0 &&
		    pcb->pcb_onfault != NULL) {
			frame->tf_elr = (register_t)pcb->pcb_onfault;
			return;
		}
		panic("uvm_fault failed: %lx esr %llx far %llx",
		    frame->tf_elr, esr, far);
	}
}

static int
emulate_msr(struct trapframe *frame, uint64_t esr)
{
	u_int rt = ISS_MSR_Rt(esr);
	uint64_t val;

	/* Only emulate reads. */
	if ((esr & ISS_MSR_DIR) == 0)
		return 0;

	/* Only emulate non-debug System register access. */
	if (ISS_MSR_OP0(esr) != 3 || ISS_MSR_OP1(esr) != 0 ||
	    ISS_MSR_CRn(esr) != 0)
		return 0;

	switch (ISS_MSR_CRm(esr)) {
	case 0:
		switch (ISS_MSR_OP2(esr)) {
		case 0:		/* MIDR_EL1 */
			val = READ_SPECIALREG(midr_el1);
			break;
		case 5:		/* MPIDR_EL1 */
			/*
			 * Don't reveal the topology to userland.  But
			 * return a valid value; Bit 31 is RES1.
			 */
			val = 0x80000000;
			break;
		case 6:		/* REVIDR_EL1 */
			val = 0;
			break;
		default:
			return 0;
		}
		break;
	case 4:
		switch (ISS_MSR_OP2(esr)) {
		case 0:		/* ID_AA64PFR0_EL1 */
			val = cpu_id_aa64pfr0;
			break;
		case 1:		/* ID_AA64PFR1_EL1 */
			val = cpu_id_aa64pfr1;
			break;
		case 2:		/* ID_AA64PFR2_EL1 */
		case 4:		/* ID_AA64ZFR0_EL1 */
		case 5:		/* ID_AA64SMFR0_EL1 */
			val = 0;
			break;
		default:
			return 0;
		}
		break;
	case 6:
		switch (ISS_MSR_OP2(esr)) {
		case 0:	/* ID_AA64ISAR0_EL1 */
			val = cpu_id_aa64isar0;
			break;
		case 1: /* ID_AA64ISAR1_EL1 */
			val = cpu_id_aa64isar1;
			break;
		case 2: /* ID_AA64ISAR2_EL2 */
			val = cpu_id_aa64isar2;
			break;
		default:
			return 0;
		}
		break;
	case 7:
		switch (ISS_MSR_OP2(esr)) {
		case 0: /* ID_AA64MMFR0_EL1 */
		case 1: /* ID_AA64MMFR1_EL1 */
		case 2: /* ID_AA64MMFR2_EL1 */
		case 3: /* ID_AA64MMFR3_EL1 */
		case 4: /* ID_AA64MMFR4_EL1 */
			val = 0;
			break;
		default:
			return 0;
		}
		break;
	default:
		return 0;
	}

	if (rt < 30)
		frame->tf_x[rt] = val;
	else if (rt == 30)
		frame->tf_lr = val;
	frame->tf_elr += 4;

	return 1;
}

void
do_el1h_sync(struct trapframe *frame)
{
	uint32_t exception;
	uint64_t esr, far;

	/* Read the ESR and FAR registers to get the exception details */
	esr = READ_SPECIALREG(esr_el1);
	far = READ_SPECIALREG(far_el1);

	intr_enable();
	uvmexp.traps++;

	exception = ESR_ELx_EXCEPTION(esr);
	switch (exception) {
	case EXCP_FP_SIMD:
	case EXCP_TRAP_FP:
		fault("FP exception in kernel");
		goto we_re_toast;
	case EXCP_BRANCH_TGT:
		fault("Branch target exception in kernel");
		goto we_re_toast;
	case EXCP_FPAC:
		fault("Pointher authentication failure in kernel");
		goto we_re_toast;
	case EXCP_INSN_ABORT:
		kdata_abort(frame, esr, far, 1);
		break;
	case EXCP_DATA_ABORT:
		kdata_abort(frame, esr, far, 0);
		break;
	case EXCP_BRK:
	case EXCP_WATCHPT_EL1:
	case EXCP_SOFTSTP_EL1:
#ifdef DDB
		db_ktrap(exception, frame);
		/* Step over permanent breakpoints. */
		if (exception == EXCP_BRK &&
		    (esr & ISS_BRK_COMMENT_MASK) == 0xf000)
			frame->tf_elr += INSN_SIZE;
#else
		panic("No debugger in kernel");
#endif
		break;
	default:
		fault("Unknown kernel exception 0x%02x", exception);
	we_re_toast:
#ifdef DDB
		db_printf("esr 0x%08llx far 0x%016llx elr 0x%016lx",
		    esr, far, frame->tf_elr);
		db_ktrap(exception, frame);
#else
		panic("esr 0x%08llx far 0x%016llx elr 0x%016lx",
		    esr, far, frame->tf_elr);
#endif
		break;
	}
}

void
do_el0_sync(struct trapframe *frame)
{
	struct proc *p = curproc;
	union sigval sv;
	uint32_t exception;
	uint64_t esr, far;

	esr = READ_SPECIALREG(esr_el1);
	exception = ESR_ELx_EXCEPTION(esr);
	far = READ_SPECIALREG(far_el1);

	intr_enable();
	uvmexp.traps++;

	p->p_addr->u_pcb.pcb_tf = frame;
	refreshcreds(p);

	switch (exception) {
	case EXCP_UNKNOWN:
		curcpu()->ci_flush_bp();
		sv.sival_ptr = (void *)frame->tf_elr;
		trapsignal(p, SIGILL, esr, ILL_ILLOPC, sv);
		break;
	case EXCP_SVE:
		sve_load(p);
		break;
	case EXCP_FP_SIMD:
	case EXCP_TRAP_FP:
		fpu_load(p);
		break;
	case EXCP_BRANCH_TGT:
		curcpu()->ci_flush_bp();
		sv.sival_ptr = (void *)frame->tf_elr;
		trapsignal(p, SIGILL, esr, ILL_BTCFI, sv);
		break;
	case EXCP_MSR:
		if (emulate_msr(frame, esr))
			break;
		/* FALLTHROUGH */
	case EXCP_FPAC:
		curcpu()->ci_flush_bp();
		sv.sival_ptr = (void *)frame->tf_elr;
		trapsignal(p, SIGILL, esr, ILL_ILLOPC, sv);
		break;
	case EXCP_SVC:
		svc_handler(frame);
		break;
	case EXCP_INSN_ABORT_L:
		udata_abort(frame, esr, far, 1);
		break;
	case EXCP_PC_ALIGN:
		curcpu()->ci_flush_bp();
		sv.sival_ptr = (void *)frame->tf_elr;
		trapsignal(p, SIGBUS, esr, BUS_ADRALN, sv);
		break;
	case EXCP_SP_ALIGN:
		curcpu()->ci_flush_bp();
		sv.sival_ptr = (void *)frame->tf_sp;
		trapsignal(p, SIGBUS, esr, BUS_ADRALN, sv);
		break;
	case EXCP_DATA_ABORT_L:
		udata_abort(frame, esr, far, 0);
		break;
	case EXCP_BRK:
		sv.sival_ptr = (void *)frame->tf_elr;
		trapsignal(p, SIGTRAP, esr, TRAP_BRKPT, sv);
		break;
	case EXCP_SOFTSTP_EL0:
		sv.sival_ptr = (void *)frame->tf_elr;
		trapsignal(p, SIGTRAP, esr, TRAP_TRACE, sv);
		break;
	default:
		// panic("Unknown userland exception %x esr_el1 %lx", exception,
		//    esr);
		// USERLAND MUST NOT PANIC MACHINE
		{
			// only here to debug !?!?
			printf("exception %x esr_el1 %llx\n", exception, esr);
			dumpregs(frame);
		}
		curcpu()->ci_flush_bp();
		KERNEL_LOCK();
		sigexit(p, SIGILL);
		KERNEL_UNLOCK();
	}

	userret(p);
}

static void
serror(struct trapframe *frame)
{
	struct cpu_info *ci = curcpu();
	uint64_t esr, far;

	esr = READ_SPECIALREG(esr_el1);
	far = READ_SPECIALREG(far_el1);

	printf("SError: %lx esr %llx far %0llx\n",
	    frame->tf_elr, esr, far);

	if (ci->ci_serror)
		ci->ci_serror();
}

void
do_el0_error(struct trapframe *frame)
{
	serror(frame);
	panic("do_el0_error");
}

void
do_el1h_error(struct trapframe *frame)
{
	serror(frame);
	panic("do_el1h_error");
}

void
dumpregs(struct trapframe *frame)
{
	int i;

	for (i = 0; i < 30; i += 2) {
		printf("x%02d: 0x%016lx 0x%016lx\n",
		    i, frame->tf_x[i], frame->tf_x[i+1]);
	}
	printf("sp: 0x%016lx\n", frame->tf_sp);
	printf("lr: 0x%016lx\n", frame->tf_lr);
	printf("pc: 0x%016lx\n", frame->tf_elr);
	printf("spsr: 0x%016lx\n", frame->tf_spsr);
}
