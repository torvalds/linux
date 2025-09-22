/* $OpenBSD: trap.c,v 1.112 2025/06/28 16:04:09 miod Exp $ */
/* $NetBSD: trap.c,v 1.52 2000/05/24 16:48:33 thorpej Exp $ */

/*-
 * Copyright (c) 2000 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe of the Numerical Aerospace Simulation Facility,
 * NASA Ames Research Center.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Copyright (c) 1999 Christopher G. Demetriou.  All rights reserved.
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
 *      This product includes software developed by Christopher G. Demetriou
 *	for the NetBSD Project.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
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
#include <sys/signalvar.h>
#include <sys/user.h>
#include <sys/syscall.h>
#include <sys/syscall_mi.h>
#include <sys/buf.h>
#ifndef NO_IEEE
#include <sys/device.h>
#endif
#include <sys/ptrace.h>

#include <uvm/uvm_extern.h>

#include <machine/cpu.h>
#include <machine/reg.h>
#ifdef DDB
#include <machine/db_machdep.h>
#endif
#include <alpha/alpha/db_instruction.h>

int		handle_opdec(struct proc *p, u_int64_t *ucodep);

#ifndef NO_IEEE
struct device fpevent_use;
struct device fpevent_reuse;
#endif

void	printtrap(const unsigned long, const unsigned long, const unsigned long,
	    const unsigned long, struct trapframe *, int, int);

/*
 * Initialize the trap vectors for the current processor.
 */
void
trap_init(void)
{

	/*
	 * Point interrupt/exception vectors to our own.
	 */
	alpha_pal_wrent(XentInt, ALPHA_KENTRY_INT);
	alpha_pal_wrent(XentArith, ALPHA_KENTRY_ARITH);
	alpha_pal_wrent(XentMM, ALPHA_KENTRY_MM);
	alpha_pal_wrent(XentIF, ALPHA_KENTRY_IF);
	alpha_pal_wrent(XentUna, ALPHA_KENTRY_UNA);
	alpha_pal_wrent(XentSys, ALPHA_KENTRY_SYS);

	/*
	 * Clear pending machine checks and error reports, and enable
	 * system- and processor-correctable error reporting.
	 */
	alpha_pal_wrmces(alpha_pal_rdmces() &
	    ~(ALPHA_MCES_DSC|ALPHA_MCES_DPC));
}

void
printtrap(const unsigned long a0, const unsigned long a1,
    const unsigned long a2, const unsigned long entry, struct trapframe *framep,
    int isfatal, int user)
{
	char ubuf[64];
	const char *entryname;

	switch (entry) {
	case ALPHA_KENTRY_INT:
		entryname = "interrupt";
		break;
	case ALPHA_KENTRY_ARITH:
		entryname = "arithmetic trap";
		break;
	case ALPHA_KENTRY_MM:
		entryname = "memory management fault";
		break;
	case ALPHA_KENTRY_IF:
		entryname = "instruction fault";
		break;
	case ALPHA_KENTRY_UNA:
		entryname = "unaligned access fault";
		break;
	case ALPHA_KENTRY_SYS:
		entryname = "system call";
		break;
	default:
		snprintf(ubuf, sizeof ubuf, "type %lx", entry);
		entryname = (const char *) ubuf;
		break;
	}

	printf("\n");
	printf("%s %s trap:\n", isfatal? "fatal" : "handled",
	       user ? "user" : "kernel");
	printf("\n");
	printf("    trap entry = 0x%lx (%s)\n", entry, entryname);
	printf("    a0         = 0x%lx\n", a0);
	printf("    a1         = 0x%lx\n", a1);
	printf("    a2         = 0x%lx\n", a2);
	printf("    pc         = 0x%lx\n", framep->tf_regs[FRAME_PC]);
	printf("    ra         = 0x%lx\n", framep->tf_regs[FRAME_RA]);
	printf("    curproc    = %p\n", curproc);
	if (curproc != NULL)
		printf("        pid = %d, comm = %s\n",
		    curproc->p_p->ps_pid, curproc->p_p->ps_comm);
	printf("\n");
}

/*
 * Trap is called from locore to handle most types of processor traps.
 * System calls are broken out for efficiency and ASTs are broken out
 * to make the code a bit cleaner and more representative of the
 * Alpha architecture.
 */
void
trap(const unsigned long a0, const unsigned long a1, const unsigned long a2,
    const unsigned long entry, struct trapframe *framep)
{
	struct proc *p;
	int i;
	u_int64_t ucode;
	int user;
#if defined(DDB)
	int call_debugger = 1;
#endif
	caddr_t v;
	int typ;
	union sigval sv;
	vm_prot_t access_type;
	unsigned long onfault;

	atomic_add_int(&uvmexp.traps, 1);
	p = curproc;
	ucode = 0;
	v = 0;
	typ = SI_NOINFO;
	framep->tf_regs[FRAME_SP] = alpha_pal_rdusp();
	user = (framep->tf_regs[FRAME_PS] & ALPHA_PSL_USERMODE) != 0;
	if (user) {
		p->p_md.md_tf = framep;
		refreshcreds(p);
	}

	switch (entry) {
	case ALPHA_KENTRY_UNA:
		/*
		 * If user-land, deliver SIGBUS unconditionally.
		 */
		if (user) {
			i = SIGBUS;
			ucode = ILL_ILLADR;
			v = (caddr_t)a0;
			break;
		}

		/*
		 * Unaligned access from kernel mode is always an error,
		 * EVEN IF A COPY FAULT HANDLER IS SET!
		 *
		 * It's an error if a copy fault handler is set because
		 * the various routines which do user-initiated copies
		 * do so in a bcopy-like manner.  In other words, the
		 * kernel never assumes that pointers provided by the
		 * user are properly aligned, and so if the kernel
		 * does cause an unaligned access it's a kernel bug.
		 */
		goto dopanic;

	case ALPHA_KENTRY_ARITH:
		/*
		 * Resolve trap shadows, interpret FP ops requiring infinities,
		 * NaNs, or denorms, and maintain FPCR corrections.
		 */
		if (user) {
#ifndef NO_IEEE
			i = alpha_fp_complete(a0, a1, p, &ucode);
			if (i == 0)
				goto out;
#else
			i = SIGFPE;
			ucode = FPE_FLTINV;
#endif
			v = (caddr_t)framep->tf_regs[FRAME_PC];
			break;
		}

		/* Always fatal in kernel.  Should never happen. */
		goto dopanic;

	case ALPHA_KENTRY_IF:
		/*
		 * These are always fatal in kernel, and should never
		 * happen.  (Debugger entry is handled in XentIF.)
		 */
		if (!user) {
#if defined(DDB)
			/*
			 * ...unless a debugger is configured.  It will
			 * inform us if the trap was handled.
			 */
			if (alpha_debug(a0, a1, a2, entry, framep))
				goto out;

			/*
			 * Debugger did NOT handle the trap, don't
			 * call the debugger again!
			 */
			call_debugger = 0;
#endif
			goto dopanic;
		}
		i = 0;
		switch (a0) {
		case ALPHA_IF_CODE_GENTRAP:
			if (framep->tf_regs[FRAME_A0] == -2) { /* weird! */
				i = SIGFPE;
				ucode =  a0;	/* exception summary */
				break;
			}
			/* FALLTHROUGH */
		case ALPHA_IF_CODE_BPT:
		case ALPHA_IF_CODE_BUGCHK:
#ifdef PTRACE
			if (p->p_md.md_flags & (MDP_STEP1|MDP_STEP2)) {
				KERNEL_LOCK();
				process_sstep(p, 0);
				KERNEL_UNLOCK();
				p->p_md.md_tf->tf_regs[FRAME_PC] -= 4;
			}
#endif
			ucode = a0;		/* trap type */
			i = SIGTRAP;
			break;

		case ALPHA_IF_CODE_OPDEC:
			KERNEL_LOCK();
			i = handle_opdec(p, &ucode);
			KERNEL_UNLOCK();
			if (i == 0)
				goto out;
			break;

		case ALPHA_IF_CODE_FEN:
			alpha_enable_fp(p, 0);
			goto out;

		default:
			printf("trap: unknown IF type 0x%lx\n", a0);
			goto dopanic;
		}
		v = (caddr_t)framep->tf_regs[FRAME_PC];
		break;

	case ALPHA_KENTRY_MM:
		if (user &&
		    !uvm_map_inentry(p, &p->p_spinentry, PROC_STACK(p),
		   "[%s]%d/%d sp=%lx inside %lx-%lx: not MAP_STACK\n",
		    uvm_map_inentry_sp, p->p_vmspace->vm_map.sserial))
			goto out;

		switch (a1) {
		case ALPHA_MMCSR_FOR:
		case ALPHA_MMCSR_FOE:
		case ALPHA_MMCSR_FOW:
			if (pmap_emulate_reference(p, a0, user, a1)) {
				access_type = PROT_EXEC;
				goto do_fault;
			}
			goto out;

		case ALPHA_MMCSR_INVALTRANS:
		case ALPHA_MMCSR_ACCESS:
	    	    {
			vaddr_t va;
			struct vmspace *vm = NULL;
			struct vm_map *map;
			int rv;
			extern struct vm_map *kernel_map;

			switch (a2) {
			case -1:		/* instruction fetch fault */
				access_type = PROT_EXEC;
				break;
			case 0:			/* load instruction */
				access_type = PROT_READ;
				break;
			case 1:			/* store instruction */
				access_type = PROT_READ | PROT_WRITE;
				break;
			}
do_fault:
			/*
			 * It is only a kernel address space fault iff:
			 *	1. !user and
			 *	2. pcb_onfault not set or
			 *	3. pcb_onfault set but kernel space data fault
			 * The last can occur during an exec() copyin where the
			 * argument space is lazy-allocated.
			 */
			if (!user && (a0 >= VM_MIN_KERNEL_ADDRESS ||
			    p->p_addr->u_pcb.pcb_onfault == 0)) {
				vm = NULL;
				map = kernel_map;
			} else {
				vm = p->p_vmspace;
				map = &vm->vm_map;
			}

			va = trunc_page((vaddr_t)a0);
			onfault = p->p_addr->u_pcb.pcb_onfault;
			p->p_addr->u_pcb.pcb_onfault = 0;

			KERNEL_LOCK();
			rv = uvm_fault(map, va, 0, access_type);
			KERNEL_UNLOCK();

			p->p_addr->u_pcb.pcb_onfault = onfault;

			/*
			 * If this was a stack access we keep track of the
			 * maximum accessed stack size.  Also, if vm_fault
			 * gets a protection failure it is due to accessing
			 * the stack region outside the current limit and
			 * we need to reflect that as an access error.
			 */
			if (rv == 0) {
				if (map != kernel_map)
					uvm_grow(p, va);
				goto out;
			}

			if (!user) {
				/* Check for copyin/copyout fault */
				if (p->p_addr->u_pcb.pcb_onfault != 0) {
					framep->tf_regs[FRAME_PC] =
					    p->p_addr->u_pcb.pcb_onfault;
					goto out;
				}
				goto dopanic;
			}
			ucode = access_type;
			v = (caddr_t)a0;
			typ = SEGV_MAPERR;
			if (rv == ENOMEM) {
				printf("UVM: pid %u (%s), uid %d killed: "
				   "out of swap\n", p->p_p->ps_pid,
				   p->p_p->ps_comm,
				   p->p_ucred ? (int)p->p_ucred->cr_uid : -1);
				i = SIGKILL;
			} else {
				i = SIGSEGV;
			}
			break;
		    }

		default:
			printf("trap: unknown MMCSR value 0x%lx\n", a1);
			goto dopanic;
		}
		break;

	default:
		goto dopanic;
	}

#ifdef DEBUG
	printtrap(a0, a1, a2, entry, framep, 1, user);
#endif
	sv.sival_ptr = v;
	trapsignal(p, i, ucode, typ, sv);
out:
	if (user) {
		/* Do any deferred user pmap operations. */
		PMAP_USERRET(vm_map_pmap(&p->p_vmspace->vm_map));

		userret(p);
	}
	return;

dopanic:
	printtrap(a0, a1, a2, entry, framep, 1, user);
	/* XXX dump registers */

#if defined(DDB)
	if (call_debugger && alpha_debug(a0, a1, a2, entry, framep)) {
		/*
		 * The debugger has handled the trap; just return.
		 */
		goto out;
	}
#endif

	panic("trap");
}

/*
 * Process a system call.
 *
 * System calls are strange beasts.  They are passed the syscall number
 * in v0, and the arguments in the registers (as normal).  They return
 * an error flag in a3 (if a3 != 0 on return, the syscall had an error),
 * and the return value (if any) in v0.
 *
 * The assembly stub takes care of moving the call number into a register
 * we can get to, and moves all of the argument registers into their places
 * in the trap frame.  On return, it restores the callee-saved registers,
 * a3, and v0 from the frame before returning to the user process.
 */
void
syscall(u_int64_t code, struct trapframe *framep)
{
	const struct sysent *callp = sysent;
	struct proc *p;
	int error = ENOSYS;
	u_int64_t opc;
	u_long rval[2];
	u_long args[6];
	u_int nargs;

	atomic_add_int(&uvmexp.syscalls, 1);
	p = curproc;
	p->p_md.md_tf = framep;
	framep->tf_regs[FRAME_SP] = alpha_pal_rdusp();
	opc = framep->tf_regs[FRAME_PC] - 4;

	if (code <= 0 || code >= SYS_MAXSYSCALL)
		goto bad;

	callp += code;

	nargs = callp->sy_narg;
	switch (nargs) {
	case 6:
		args[5] = framep->tf_regs[FRAME_A5];
	case 5:
		args[4] = framep->tf_regs[FRAME_A4];
	case 4:
		args[3] = framep->tf_regs[FRAME_A3];
	case 3:
		args[2] = framep->tf_regs[FRAME_A2];
	case 2:
		args[1] = framep->tf_regs[FRAME_A1];
	case 1:
		args[0] = framep->tf_regs[FRAME_A0];
	case 0:
		break;
	}

	rval[0] = 0;
	rval[1] = 0;

	error = mi_syscall(p, code, callp, args, rval);

	switch (error) {
	case 0:
		framep->tf_regs[FRAME_V0] = rval[0];
		framep->tf_regs[FRAME_A3] = 0;
		break;
	case ERESTART:
		framep->tf_regs[FRAME_PC] = opc;
		break;
	case EJUSTRETURN:
		break;
	default:
	bad:
		framep->tf_regs[FRAME_V0] = error;
		framep->tf_regs[FRAME_A3] = 1;
		break;
	}

	/* Do any deferred user pmap operations. */
	PMAP_USERRET(vm_map_pmap(&p->p_vmspace->vm_map));

	mi_syscall_return(p, code, error, rval);
}

/*
 * Process the tail end of a fork() for the child.
 */
void
child_return(void *arg)
{
	struct proc *p = arg;
	struct trapframe *framep = p->p_md.md_tf;

	/*
	 * Return values in the frame set by cpu_fork().
	 */
	framep->tf_regs[FRAME_V0] = 0;
	framep->tf_regs[FRAME_A3] = 0;

	KERNEL_UNLOCK();

	/* Do any deferred user pmap operations. */
	PMAP_USERRET(vm_map_pmap(&p->p_vmspace->vm_map));

	mi_child_return(p);
}

/*
 * Set the float-point enable for the current process, and return
 * the FPU context to the named process. If check == 0, it is an
 * error for the named process to already be fpcurproc.
 */
void
alpha_enable_fp(struct proc *p, int check)
{
	struct cpu_info *ci = curcpu();
#if defined(MULTIPROCESSOR)
	int s;
#endif

	if (check && ci->ci_fpcurproc == p) {
		alpha_pal_wrfen(1);
		return;
	}
	if (ci->ci_fpcurproc == p)
		panic("trap: fp disabled for fpcurproc == %p", p);

	if (ci->ci_fpcurproc != NULL)
		fpusave_cpu(ci, 1);

	KDASSERT(ci->ci_fpcurproc == NULL);

#if defined(MULTIPROCESSOR)
	if (p->p_addr->u_pcb.pcb_fpcpu != NULL)
		fpusave_proc(p, 1);
#else
	KDASSERT(p->p_addr->u_pcb.pcb_fpcpu == NULL);
#endif

#if defined(MULTIPROCESSOR)
	/* Need to block IPIs */
	s = splipi();
#endif
	p->p_addr->u_pcb.pcb_fpcpu = ci;
	ci->ci_fpcurproc = p;
	atomic_add_int(&uvmexp.fpswtch, 1);

	p->p_md.md_flags |= MDP_FPUSED;
	alpha_pal_wrfen(1);
	restorefpstate(&p->p_addr->u_pcb.pcb_fp);
	alpha_pal_wrfen(0);

#if defined(MULTIPROCESSOR)
	alpha_pal_swpipl(s);
#endif
}

/*
 * Process an asynchronous software trap.
 * This is relatively easy.
 */
void
ast(struct trapframe *framep)
{
	struct proc *p = curproc;

	p->p_md.md_tf = framep;
	p->p_md.md_astpending = 0;

#ifdef DIAGNOSTIC
	if ((framep->tf_regs[FRAME_PS] & ALPHA_PSL_USERMODE) == 0)
		panic("ast and not user");
#endif

	refreshcreds(p);
	atomic_add_int(&uvmexp.softs, 1);
	mi_ast(p, curcpu()->ci_want_resched);

	/* Do any deferred user pmap operations. */
	PMAP_USERRET(vm_map_pmap(&p->p_vmspace->vm_map));

	userret(p);
}

static const int reg_to_framereg[32] = {
	FRAME_V0,	FRAME_T0,	FRAME_T1,	FRAME_T2,
	FRAME_T3,	FRAME_T4,	FRAME_T5,	FRAME_T6,
	FRAME_T7,	FRAME_S0,	FRAME_S1,	FRAME_S2,
	FRAME_S3,	FRAME_S4,	FRAME_S5,	FRAME_S6,
	FRAME_A0,	FRAME_A1,	FRAME_A2,	FRAME_A3,
	FRAME_A4,	FRAME_A5,	FRAME_T8,	FRAME_T9,
	FRAME_T10,	FRAME_T11,	FRAME_RA,	FRAME_T12,
	FRAME_AT,	FRAME_GP,	FRAME_SP,	-1,
};

#define	irp(p, reg)							\
	((reg_to_framereg[(reg)] == -1) ? NULL :			\
	    &(p)->p_md.md_tf->tf_regs[reg_to_framereg[(reg)]])

/*
 * Reserved/unimplemented instruction (opDec fault) handler
 *
 * Argument is the process that caused it.  No useful information
 * is passed to the trap handler other than the fault type.  The
 * address of the instruction that caused the fault is 4 less than
 * the PC stored in the trap frame.
 *
 * If the instruction is emulated successfully, this function returns 0.
 * Otherwise, this function returns the signal to deliver to the process,
 * and fills in *ucodep with the code to be delivered.
 */
int
handle_opdec(struct proc *p, u_int64_t *ucodep)
{
	alpha_instruction inst;
	register_t *regptr, memaddr;
	u_int64_t inst_pc;
	int sig;

	/*
	 * Read USP into frame in case it's going to be used or modified.
	 * This keeps us from having to check for it in lots of places
	 * later.
	 */
	p->p_md.md_tf->tf_regs[FRAME_SP] = alpha_pal_rdusp();

	inst_pc = memaddr = p->p_md.md_tf->tf_regs[FRAME_PC] - 4;
	if (copyinsn(p, (u_int32_t *)inst_pc, (u_int32_t *)&inst) != 0) {
		/*
		 * really, this should never happen, but in case it
		 * does we handle it.
		 */
		printf("WARNING: handle_opdec() couldn't fetch instruction\n");
		goto sigsegv;
	}

	switch (inst.generic_format.opcode) {
	case op_ldbu:
	case op_ldwu:
	case op_stw:
	case op_stb:
		regptr = irp(p, inst.mem_format.rb);
		if (regptr != NULL)
			memaddr = *regptr;
		else
			memaddr = 0;
		memaddr += inst.mem_format.displacement;

		regptr = irp(p, inst.mem_format.ra);

		if (inst.mem_format.opcode == op_ldwu ||
		    inst.mem_format.opcode == op_stw) {
			if (memaddr & 0x01) {	/* misaligned address */
				sig = SIGBUS;
				goto sigbus;
			}
		}

		if (inst.mem_format.opcode == op_ldbu) {
			u_int8_t b;

			/* XXX ONLY WORKS ON LITTLE-ENDIAN ALPHA */
			if (copyin((caddr_t)memaddr, &b, sizeof (b)) != 0)
				goto sigsegv;
			if (regptr != NULL)
				*regptr = b;
		} else if (inst.mem_format.opcode == op_ldwu) {
			u_int16_t w;

			/* XXX ONLY WORKS ON LITTLE-ENDIAN ALPHA */
			if (copyin((caddr_t)memaddr, &w, sizeof (w)) != 0)
				goto sigsegv;
			if (regptr != NULL)
				*regptr = w;
		} else if (inst.mem_format.opcode == op_stw) {
			u_int16_t w;

			/* XXX ONLY WORKS ON LITTLE-ENDIAN ALPHA */
			w = (regptr != NULL) ? *regptr : 0;
			if (copyout(&w, (caddr_t)memaddr, sizeof (w)) != 0)
				goto sigsegv;
		} else if (inst.mem_format.opcode == op_stb) {
			u_int8_t b;

			/* XXX ONLY WORKS ON LITTLE-ENDIAN ALPHA */
			b = (regptr != NULL) ? *regptr : 0;
			if (copyout(&b, (caddr_t)memaddr, sizeof (b)) != 0)
				goto sigsegv;
		}
		break;

	case op_intmisc:
		if (inst.operate_generic_format.function == op_sextb &&
		    inst.operate_generic_format.ra == 31) {
			int8_t b;

			if (inst.operate_generic_format.is_lit) {
				b = inst.operate_lit_format.literal;
			} else {
				if (inst.operate_reg_format.sbz != 0)
					goto sigill;
				regptr = irp(p, inst.operate_reg_format.rb);
				b = (regptr != NULL) ? *regptr : 0;
			}

			regptr = irp(p, inst.operate_generic_format.rc);
			if (regptr != NULL)
				*regptr = b;
			break;
		}
		if (inst.operate_generic_format.function == op_sextw &&
		    inst.operate_generic_format.ra == 31) {
			int16_t w;

			if (inst.operate_generic_format.is_lit) {
				w = inst.operate_lit_format.literal;
			} else {
				if (inst.operate_reg_format.sbz != 0)
					goto sigill;
				regptr = irp(p, inst.operate_reg_format.rb);
				w = (regptr != NULL) ? *regptr : 0;
			}

			regptr = irp(p, inst.operate_generic_format.rc);
			if (regptr != NULL)
				*regptr = w;
			break;
		}
		goto sigill;

#ifndef NO_IEEE
	/* case op_fix_float: */
	/* case op_vax_float: */
	case op_ieee_float:
	/* case op_any_float: */
		/*
		 * EV4 processors do not implement dynamic rounding
		 * instructions at all.
		 */
		if (cpu_implver <= ALPHA_IMPLVER_EV4) {
			sig = alpha_fp_complete_at(inst_pc, p, ucodep);
			if (sig)
				return sig;
			break;
		}
		goto sigill;
#endif

	default:
		goto sigill;
	}

	/*
	 * Write back USP.  Note that in the error cases below,
	 * nothing will have been successfully modified so we don't
	 * have to write it out.
	 */
	alpha_pal_wrusp(p->p_md.md_tf->tf_regs[FRAME_SP]);

	return (0);

sigill:
	*ucodep = ALPHA_IF_CODE_OPDEC;			/* trap type */
	return (SIGILL);

sigsegv:
	sig = SIGSEGV;
	p->p_md.md_tf->tf_regs[FRAME_PC] = inst_pc;	/* re-run instr. */
sigbus:
	*ucodep = memaddr;				/* faulting address */
	return (sig);
}
