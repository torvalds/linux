/*	$OpenBSD: trap.c,v 1.139 2025/07/25 16:55:00 miod Exp $	*/
/*
 * Copyright (c) 2004, Miodrag Vallat.
 * Copyright (c) 1998 Steve Murphree, Jr.
 * Copyright (c) 1996 Nivas Madhur
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Nivas Madhur.
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
 *
 */
/*
 * Mach Operating System
 * Copyright (c) 1991 Carnegie Mellon University
 * Copyright (c) 1991 OMRON Corporation
 * All Rights Reserved.
 *
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 */

#include <sys/param.h>
#include <sys/proc.h>
#include <sys/signalvar.h>
#include <sys/user.h>
#include <sys/syscall.h>
#include <sys/systm.h>
#include <sys/syscall_mi.h>

#include <uvm/uvm_extern.h>

#include <machine/asm_macro.h>
#include <machine/cmmu.h>
#include <machine/cpu.h>
#ifdef M88100
#include <machine/m88100.h>
#include <machine/m8820x.h>
#endif
#ifdef M88110
#include <machine/m88110.h>
#endif
#include <machine/fpu.h>
#include <machine/pcb.h>
#include <machine/psl.h>
#include <machine/trap.h>

#include <machine/db_machdep.h>

#define SSBREAKPOINT (0xF000D1F8U) /* Single Step Breakpoint */

#define USERMODE(PSR)   (((PSR) & PSR_MODE) == 0)
#define SYSTEMMODE(PSR) (((PSR) & PSR_MODE) != 0)

void printtrap(int, struct trapframe *);
__dead void panictrap(int, struct trapframe *);
__dead void error_fatal(struct trapframe *);
int double_reg_fixup(struct trapframe *, int);
int ss_put_value(struct proc *, vaddr_t, u_int);

extern void regdump(struct trapframe *f);

const char *trap_type[] = {
	"Reset",
	"Interrupt Exception",
	"Instruction Access",
	"Data Access Exception",
	"Misaligned Access",
	"Unimplemented Opcode",
	"Privilege Violation",
	"Bounds Check Violation",
	"Illegal Integer Divide",
	"Integer Overflow",
	"Error Exception",
	"Non-Maskable Exception",
};

const int trap_types = sizeof trap_type / sizeof trap_type[0];

#ifdef M88100
const char *pbus_exception_type[] = {
	"Success (No Fault)",
	"unknown 1",
	"unknown 2",
	"Bus Error",
	"Segment Fault",
	"Page Fault",
	"Supervisor Violation",
	"Write Violation",
};
#endif

void
printtrap(int type, struct trapframe *frame)
{
#ifdef M88100
	if (CPU_IS88100) {
		if (type == 2) {
			/* instruction exception */
			printf("\nInstr access fault (%s) v = %lx, frame %p\n",
			    pbus_exception_type[
			      CMMU_PFSR_FAULT(frame->tf_ipfsr)],
			    frame->tf_sxip & XIP_ADDR, frame);
		} else if (type == 3) {
			/* data access exception */
			printf("\nData access fault (%s) v = %lx, frame %p\n",
			    pbus_exception_type[
			      CMMU_PFSR_FAULT(frame->tf_dpfsr)],
			    frame->tf_sxip & XIP_ADDR, frame);
		} else
			printf("\nTrap type %d, v = %lx, frame %p\n",
			    type, frame->tf_sxip & XIP_ADDR, frame);
	}
#endif
#ifdef M88110
	if (CPU_IS88110) {
		printf("\nTrap type %d, v = %lx, frame %p\n",
		    type, frame->tf_exip, frame);
	}
#endif
#ifdef DDB
	regdump(frame);
#endif
}

__dead void
panictrap(int type, struct trapframe *frame)
{
	static int panicing = 0;

	if (panicing++ == 0)
		printtrap(type, frame);
	if ((u_int)type < trap_types)
		panic("%s", trap_type[type]);
	else
		panic("trap %d", type);
	/*NOTREACHED*/
}

/*
 * Handle external interrupts.
 */
void
interrupt(struct trapframe *frame)
{
	struct cpu_info *ci = curcpu();

	ci->ci_idepth++;
	md_interrupt_func(frame);
	ci->ci_idepth--;
}

#ifdef M88110
/*
 * Handle non-maskable interrupts.
 */
int
nmi(struct trapframe *frame)
{
	return md_nmi_func(frame);
}

/*
 * Reenable non-maskable interrupts.
 */
void
nmi_wrapup(struct trapframe *frame)
{
	md_nmi_wrapup_func(frame);
}
#endif

/*
 * Handle asynchronous software traps.
 */
void
ast(struct trapframe *frame)
{
	struct cpu_info *ci = curcpu();
	struct proc *p = ci->ci_curproc;

	p->p_md.md_astpending = 0;

	uvmexp.softs++;
	mi_ast(p, ci->ci_want_resched);
	userret(p);
}

#ifdef M88100
void
m88100_trap(u_int type, struct trapframe *frame)
{
	struct proc *p;
	struct vm_map *map;
	vaddr_t va, pcb_onfault;
	vm_prot_t access_type;
	int fault_type, pbus_type;
	u_long fault_code;
	vaddr_t fault_addr;
	struct vmspace *vm;
	union sigval sv;
	int result;
#ifdef DDB
	int s;
	u_int psr;
#endif
	int sig = 0;

	uvmexp.traps++;
	if ((p = curproc) == NULL)
		p = &proc0;

	if (USERMODE(frame->tf_epsr)) {
		type |= T_USER;
		p->p_md.md_tf = frame;	/* for ptrace/signals */
		refreshcreds(p);
	}
	fault_type = SI_NOINFO;
	fault_code = 0;
	fault_addr = frame->tf_sxip & XIP_ADDR;

	switch (type) {
	default:
	case T_ILLFLT:
lose:
		panictrap(frame->tf_vector, frame);
		break;
		/*NOTREACHED*/

#if defined(DDB)
	case T_KDB_BREAK:
		s = splhigh();
		set_psr((psr = get_psr()) & ~PSR_IND);
		ddb_break_trap(T_KDB_BREAK, (db_regs_t*)frame);
		set_psr(psr);
		splx(s);
		return;
	case T_KDB_ENTRY:
		s = splhigh();
		set_psr((psr = get_psr()) & ~PSR_IND);
		ddb_entry_trap(T_KDB_ENTRY, (db_regs_t*)frame);
		set_psr(psr);
		splx(s);
		return;
#endif /* DDB */
	case T_MISALGNFLT:
		printf("kernel misaligned access exception @0x%08lx\n",
		    frame->tf_sxip);
		goto lose;
	case T_INSTFLT:
		/* kernel mode instruction access fault.
		 * Should never, never happen for a non-paged kernel.
		 */
#ifdef TRAPDEBUG
		pbus_type = CMMU_PFSR_FAULT(frame->tf_ipfsr);
		printf("Kernel Instruction fault #%d (%s) v = 0x%x, frame 0x%x cpu %p\n",
		    pbus_type, pbus_exception_type[pbus_type],
		    fault_addr, frame, frame->tf_cpu);
#endif
		goto lose;
	case T_DATAFLT:
		/* kernel mode data fault */

		/* data fault on the user address? */
		if ((frame->tf_dmt0 & DMT_DAS) == 0)
			goto user_fault;

		fault_addr = frame->tf_dma0;
		if (frame->tf_dmt0 & (DMT_WRITE|DMT_LOCKBAR)) {
			access_type = PROT_READ | PROT_WRITE;
			fault_code = PROT_WRITE;
		} else {
			access_type = PROT_READ;
			fault_code = PROT_READ;
		}

		va = trunc_page((vaddr_t)fault_addr);

		vm = p->p_vmspace;
		map = kernel_map;

		pbus_type = CMMU_PFSR_FAULT(frame->tf_dpfsr);
#ifdef TRAPDEBUG
		printf("Kernel Data access fault #%d (%s) v = 0x%x, frame 0x%x cpu %p\n",
		    pbus_type, pbus_exception_type[pbus_type],
		    fault_addr, frame, frame->tf_cpu);
#endif

		pcb_onfault = p->p_addr->u_pcb.pcb_onfault;
		switch (pbus_type) {
		case CMMU_PFSR_SUCCESS:
			/*
			 * The fault was resolved. Call data_access_emulation
			 * to drain the data unit pipe line and reset dmt0
			 * so that trap won't get called again.
			 */
			p->p_addr->u_pcb.pcb_onfault = 0;
			KERNEL_LOCK();
			data_access_emulation((u_int *)frame);
			KERNEL_UNLOCK();
			p->p_addr->u_pcb.pcb_onfault = pcb_onfault;
			frame->tf_dmt0 = 0;
			frame->tf_dpfsr = 0;
			return;
		case CMMU_PFSR_SFAULT:
		case CMMU_PFSR_PFAULT:
			p->p_addr->u_pcb.pcb_onfault = 0;
			KERNEL_LOCK();
			result = uvm_fault(map, va, 0, access_type);
			p->p_addr->u_pcb.pcb_onfault = pcb_onfault;
			if (result == 0) {
				/*
				 * We could resolve the fault. Call
				 * data_access_emulation to drain the data
				 * unit pipe line and reset dmt0 so that trap
				 * won't get called again.
				 */
				p->p_addr->u_pcb.pcb_onfault = 0;
				data_access_emulation((u_int *)frame);
				KERNEL_UNLOCK();
				p->p_addr->u_pcb.pcb_onfault = pcb_onfault;
				frame->tf_dmt0 = 0;
				frame->tf_dpfsr = 0;
				return;
			} else if (pcb_onfault != 0) {
				KERNEL_UNLOCK();
				/*
				 * This could be a fault caused in copyout*()
				 * while accessing kernel space.
				 */
				frame->tf_snip = pcb_onfault | NIP_V;
				frame->tf_sfip = (pcb_onfault + 4) | FIP_V;
				/*
				 * Continue as if the fault had been resolved,
				 * but do not try to complete the faulting
				 * access.
				 */
				frame->tf_dmt0 = 0;
				frame->tf_dpfsr = 0;
				return;
			}
			KERNEL_UNLOCK();
			break;
		}
#ifdef TRAPDEBUG
		printf("PBUS Fault %d (%s) va = 0x%x\n", pbus_type,
		    pbus_exception_type[pbus_type], va);
#endif
		goto lose;
		/* NOTREACHED */
	case T_INSTFLT+T_USER:
		/* User mode instruction access fault */
		/* FALLTHROUGH */
	case T_DATAFLT+T_USER:
		if (!uvm_map_inentry(p, &p->p_spinentry, PROC_STACK(p),
		    "[%s]%d/%d sp=%lx inside %lx-%lx: not MAP_STACK\n",
		    uvm_map_inentry_sp, p->p_vmspace->vm_map.sserial))
			goto userexit;
user_fault:
		if (type == T_INSTFLT + T_USER) {
			pbus_type = CMMU_PFSR_FAULT(frame->tf_ipfsr);
#ifdef TRAPDEBUG
			printf("User Instruction fault #%d (%s) v = 0x%x, frame 0x%x cpu %p\n",
			    pbus_type, pbus_exception_type[pbus_type],
			    fault_addr, frame, frame->tf_cpu);
#endif
			access_type = PROT_EXEC;
			fault_code = PROT_EXEC;
		} else {
			fault_addr = frame->tf_dma0;
			pbus_type = CMMU_PFSR_FAULT(frame->tf_dpfsr);
#ifdef TRAPDEBUG
			printf("User Data access fault #%d (%s) v = 0x%x, frame 0x%x cpu %p\n",
			    pbus_type, pbus_exception_type[pbus_type],
			    fault_addr, frame, frame->tf_cpu);
#endif
			if (frame->tf_dmt0 & (DMT_WRITE | DMT_LOCKBAR)) {
				access_type = PROT_READ | PROT_WRITE;
				fault_code = PROT_WRITE;
			} else {
				access_type = PROT_READ;
				fault_code = PROT_READ;
			}
		}

		va = trunc_page((vaddr_t)fault_addr);

		vm = p->p_vmspace;
		map = &vm->vm_map;
		if ((pcb_onfault = p->p_addr->u_pcb.pcb_onfault) != 0)
			p->p_addr->u_pcb.pcb_onfault = 0;

		/* Call uvm_fault() to resolve non-bus error faults */
		switch (pbus_type) {
		case CMMU_PFSR_SUCCESS:
			result = 0;
			break;
		case CMMU_PFSR_BERROR:
			result = EACCES;
			break;
		default:
			KERNEL_LOCK();
			result = uvm_fault(map, va, 0, access_type);
			KERNEL_UNLOCK();
			if (result == EACCES)
				result = EFAULT;
			break;
		}

		p->p_addr->u_pcb.pcb_onfault = pcb_onfault;

		if (result == 0) {
			uvm_grow(p, va);

			if (type == T_INSTFLT + T_USER) {
				m88100_rewind_insn(&(frame->tf_regs));
				/* clear the error bit */
				frame->tf_sfip &= ~FIP_E;
				frame->tf_snip &= ~NIP_E;
				frame->tf_ipfsr = 0;
			} else {
				/*
			 	 * We could resolve the fault. Call
			 	 * data_access_emulation to drain the data unit
			 	 * pipe line and reset dmt0 so that trap won't
			 	 * get called again.
			 	 */
				p->p_addr->u_pcb.pcb_onfault = 0;
				KERNEL_LOCK();
				data_access_emulation((u_int *)frame);
				KERNEL_UNLOCK();
				p->p_addr->u_pcb.pcb_onfault = pcb_onfault;
				frame->tf_dmt0 = 0;
				frame->tf_dpfsr = 0;
			}
		} else {
			/*
			 * This could be a fault caused in copyin*()
			 * while accessing user space.
			 */
			if (pcb_onfault != 0) {
				frame->tf_snip = pcb_onfault | NIP_V;
				frame->tf_sfip = (pcb_onfault + 4) | FIP_V;
				/*
				 * Continue as if the fault had been resolved,
				 * but do not try to complete the faulting
				 * access.
				 */
				frame->tf_dmt0 = 0;
				frame->tf_dpfsr = 0;
			} else {
				sig = result == EACCES ? SIGBUS : SIGSEGV;
				fault_type = result == EACCES ?
				    BUS_ADRERR : SEGV_MAPERR;
			}
		}
		break;
	case T_MISALGNFLT+T_USER:
		/* Fix any misaligned ld.d or st.d instructions */
		sig = double_reg_fixup(frame, T_MISALGNFLT);
		fault_type = BUS_ADRALN;
		break;
	case T_PRIVINFLT+T_USER:
	case T_ILLFLT+T_USER:
#ifndef DDB
	case T_KDB_BREAK:
	case T_KDB_ENTRY:
#endif
	case T_KDB_BREAK+T_USER:
	case T_KDB_ENTRY+T_USER:
	case T_KDB_TRACE:
	case T_KDB_TRACE+T_USER:
		sig = SIGILL;
		break;
	case T_BNDFLT+T_USER:
		sig = SIGFPE;
		break;
	case T_ZERODIV+T_USER:
		sig = SIGFPE;
		fault_type = FPE_INTDIV;
		break;
	case T_OVFFLT+T_USER:
		sig = SIGFPE;
		fault_type = FPE_INTOVF;
		break;
	case T_FPEPFLT+T_USER:
		m88100_fpu_precise_exception(frame);
		goto userexit;
	case T_FPEIFLT:
		/*
		 * Although the kernel does not use FPU instructions,
		 * userland-triggered FPU imprecise exceptions may be
		 * raised during exception processing, when the FPU gets
		 * reenabled (i.e. immediately when returning to
		 * m88100_fpu_enable).
		 */
		/* FALLTHROUGH */
	case T_FPEIFLT+T_USER:
		m88100_fpu_imprecise_exception(frame);
		goto userexit;
	case T_SIGSYS+T_USER:
		sig = SIGSYS;
		break;
	case T_STEPBPT+T_USER:
#ifdef PTRACE
		/*
		 * This trap is used by the kernel to support single-step
		 * debugging (although any user could generate this trap
		 * which should probably be handled differently). When a
		 * process is continued by a debugger with the PT_STEP
		 * function of ptrace (single step), the kernel inserts
		 * one or two breakpoints in the user process so that only
		 * one instruction (or two in the case of a delayed branch)
		 * is executed.  When this breakpoint is hit, we get the
		 * T_STEPBPT trap.
		 */
		{
			u_int instr;
			vaddr_t pc = PC_REGS(&frame->tf_regs);

			/* read break instruction */
			copyinsn(p, (u_int32_t *)pc, (u_int32_t *)&instr);

			/* check and see if we got here by accident */
			if ((p->p_md.md_bp0va != pc &&
			     p->p_md.md_bp1va != pc) ||
			    instr != SSBREAKPOINT) {
				sig = SIGTRAP;
				fault_type = TRAP_TRACE;
				break;
			}

			/* restore original instruction and clear breakpoint */
			KERNEL_LOCK();
			if (p->p_md.md_bp0va == pc) {
				ss_put_value(p, pc, p->p_md.md_bp0save);
				p->p_md.md_bp0va = 0;
			}
			if (p->p_md.md_bp1va == pc) {
				ss_put_value(p, pc, p->p_md.md_bp1save);
				p->p_md.md_bp1va = 0;
			}
			KERNEL_UNLOCK();

			frame->tf_sxip = pc | NIP_V;
			sig = SIGTRAP;
			fault_type = TRAP_BRKPT;
		}
#else
		sig = SIGTRAP;
		fault_type = TRAP_TRACE;
#endif
		break;

	case T_USERBPT+T_USER:
		/*
		 * This trap is meant to be used by debuggers to implement
		 * breakpoint debugging.  When we get this trap, we just
		 * return a signal which gets caught by the debugger.
		 */
		sig = SIGTRAP;
		fault_type = TRAP_BRKPT;
		break;

	}

	/*
	 * If trap from supervisor mode, just return
	 */
	if (type < T_USER)
		return;

	if (sig) {
		sv.sival_ptr = (void *)fault_addr;
		trapsignal(p, sig, fault_code, fault_type, sv);
		/*
		 * don't want multiple faults - we are going to
		 * deliver signal.
		 */
		frame->tf_dmt0 = 0;
		frame->tf_ipfsr = frame->tf_dpfsr = 0;
	}

userexit:
	userret(p);
}
#endif /* M88100 */

#ifdef M88110
void
m88110_trap(u_int type, struct trapframe *frame)
{
	struct proc *p;
	struct vm_map *map;
	vaddr_t va, pcb_onfault;
	vm_prot_t access_type;
	int fault_type;
	u_long fault_code;
	vaddr_t fault_addr;
	struct vmspace *vm;
	union sigval sv;
	int result;
#ifdef DDB
        int s;
	u_int psr;
#endif
	int sig = 0;

	uvmexp.traps++;
	if ((p = curproc) == NULL)
		p = &proc0;

	fault_type = SI_NOINFO;
	fault_code = 0;
	fault_addr = frame->tf_exip & XIP_ADDR;

	/*
	 * 88110 errata #16 (4.2) or #3 (5.1.1):
	 * ``bsr, br, bcnd, jsr and jmp instructions with the .n extension
	 *   can cause the enip value to be incremented by 4 incorrectly
	 *   if the instruction in the delay slot is the first word of a
	 *   page which misses in the mmu and results in a hardware
	 *   tablewalk which encounters an exception or an invalid
	 *   descriptor.  The exip value in this case will point to the
	 *   first word of the page, and the D bit will be set.
	 *
	 *   Note: if the instruction is a jsr.n r1, r1 will be overwritten
	 *   with erroneous data.  Therefore, no recovery is possible. Do
	 *   not allow this instruction to occupy the last word of a page.
	 *
	 *   Suggested fix: recover in general by backing up the exip by 4
	 *   and clearing the delay bit before an rte when the lower 3 hex
	 *   digits of the exip are 001.''
	 */
	if ((frame->tf_exip & PAGE_MASK) == 0x00000001 && type == T_INSTFLT) {
		u_int instr;

		/*
		 * Note that we have initialized fault_addr above, so that
		 * signals provide the correct address if necessary.
		 */
		frame->tf_exip = (frame->tf_exip & ~1) - 4;

		/*
		 * Check the instruction at the (backed up) exip.
		 * If it is a jsr.n, abort.
		 */
		if (!USERMODE(frame->tf_epsr)) {
			instr = *(u_int *)fault_addr;
			if (instr == 0xf400cc01)
				panic("mc88110 errata #16, exip 0x%lx enip 0x%lx",
				    (frame->tf_exip + 4) | 1, frame->tf_enip);
		} else {
			/* copyin here should not fail */
			if (copyinsn(p, (u_int32_t *)frame->tf_exip,
			    (u_int32_t *)&instr) == 0 &&
			    instr == 0xf400cc01) {
				uprintf("mc88110 errata #16, exip 0x%lx enip 0x%lx",
				    (frame->tf_exip + 4) | 1, frame->tf_enip);
				sig = SIGILL;
			}
		}
	}

	if (USERMODE(frame->tf_epsr)) {
		type |= T_USER;
		p->p_md.md_tf = frame;	/* for ptrace/signals */
		refreshcreds(p);
	}

	if (sig != 0)
		goto deliver;

	switch (type) {
	default:
lose:
		panictrap(frame->tf_vector, frame);
		break;
		/*NOTREACHED*/

#ifdef DEBUG
	case T_110_DRM+T_USER:
	case T_110_DRM:
		printf("DMMU read miss: Hardware Table Searches should be enabled!\n");
		goto lose;
	case T_110_DWM+T_USER:
	case T_110_DWM:
		printf("DMMU write miss: Hardware Table Searches should be enabled!\n");
		goto lose;
	case T_110_IAM+T_USER:
	case T_110_IAM:
		printf("IMMU miss: Hardware Table Searches should be enabled!\n");
		goto lose;
#endif

#ifdef DDB
	case T_KDB_TRACE:
		s = splhigh();
		set_psr((psr = get_psr()) & ~PSR_IND);
		ddb_break_trap(T_KDB_TRACE, (db_regs_t*)frame);
		set_psr(psr);
		splx(s);
		return;
	case T_KDB_BREAK:
		s = splhigh();
		set_psr((psr = get_psr()) & ~PSR_IND);
		ddb_break_trap(T_KDB_BREAK, (db_regs_t*)frame);
		set_psr(psr);
		splx(s);
		return;
	case T_KDB_ENTRY:
		s = splhigh();
		set_psr((psr = get_psr()) & ~PSR_IND);
		ddb_entry_trap(T_KDB_ENTRY, (db_regs_t*)frame);
		set_psr(psr);
		/* skip trap instruction */
		m88110_skip_insn(frame);
		splx(s);
		return;
#endif /* DDB */
	case T_ILLFLT:
		/*
		 * The 88110 seems to trigger an instruction fault in
		 * supervisor mode when running the following sequence:
		 *
		 *	bcnd.n cond, reg, 1f
		 *	arithmetic insn
		 *	...
		 *  	the same exact arithmetic insn
		 *  1:	another arithmetic insn stalled by the previous one
		 *	...
		 *
		 * The exception is reported with exip pointing to the
		 * branch address. I don't know, at this point, if there
		 * is any better workaround than the aggressive one
		 * implemented below; I don't see how this could relate to
		 * any of the 88110 errata (although it might be related to
		 * branch prediction).
		 *
		 * For the record, the exact sequence triggering the
		 * spurious exception is:
		 *
		 *	bcnd.n	eq0, r2,  1f
		 *	 or	r25, r0,  r22
		 *	bsr	somewhere
		 *	or	r25, r0,  r22
		 *  1:	cmp	r13, r25, r20
		 *
		 * within the same cache line.
		 *
		 * Simply ignoring the exception and returning does not
		 * cause the exception to disappear. Clearing the
		 * instruction cache works, but on 88110+88410 systems,
		 * the 88410 needs to be invalidated as well. (note that
		 * the size passed to the flush routines does not matter
		 * since there is no way to flush a subset of the 88110
		 * I$ anyway)
		 */
	    {
		extern void *kernel_text, *etext;

		if (fault_addr >= (vaddr_t)&kernel_text &&
		    fault_addr < (vaddr_t)&etext) {
			cmmu_icache_inv(curcpu()->ci_cpuid,
			    trunc_page(fault_addr), PAGE_SIZE);
			cmmu_cache_wbinv(curcpu()->ci_cpuid,
			    trunc_page(fault_addr), PAGE_SIZE);
			return;
		}
	    }
		goto lose;
	case T_MISALGNFLT:
		printf("kernel misaligned access exception @%p\n",
		    (void *)frame->tf_exip);
		goto lose;
	case T_INSTFLT:
		/* kernel mode instruction access fault.
		 * Should never, never happen for a non-paged kernel.
		 */
#ifdef TRAPDEBUG
		printf("Kernel Instruction fault exip %x isr %x ilar %x\n",
		    frame->tf_exip, frame->tf_isr, frame->tf_ilar);
#endif
		goto lose;

	case T_DATAFLT:
		/* kernel mode data fault */

		/* data fault on the user address? */
		if ((frame->tf_dsr & CMMU_DSR_SU) == 0)
			goto m88110_user_fault;

#ifdef TRAPDEBUG
		printf("Kernel Data access fault exip %x dsr %x dlar %x\n",
		    frame->tf_exip, frame->tf_dsr, frame->tf_dlar);
#endif

		fault_addr = frame->tf_dlar;
		if (frame->tf_dsr & CMMU_DSR_RW) {
			access_type = PROT_READ;
			fault_code = PROT_READ;
		} else {
			access_type = PROT_READ | PROT_WRITE;
			fault_code = PROT_WRITE;
		}

		va = trunc_page((vaddr_t)fault_addr);

		vm = p->p_vmspace;
		map = kernel_map;

		if (frame->tf_dsr & (CMMU_DSR_SI | CMMU_DSR_PI)) {
			/*
			 * On a segment or a page fault, call uvm_fault() to
			 * resolve the fault.
			 */
			if ((pcb_onfault = p->p_addr->u_pcb.pcb_onfault) != 0)
				p->p_addr->u_pcb.pcb_onfault = 0;
			KERNEL_LOCK();
			result = uvm_fault(map, va, 0, access_type);
			KERNEL_UNLOCK();
			p->p_addr->u_pcb.pcb_onfault = pcb_onfault;
			/*
			 * This could be a fault caused in copyout*()
			 * while accessing kernel space.
			 */
			if (result != 0 && pcb_onfault != 0) {
				frame->tf_exip = pcb_onfault;
				/*
				 * Continue as if the fault had been resolved.
				 */
				result = 0;
			}
			if (result == 0)
				return;
		}
		goto lose;
	case T_INSTFLT+T_USER:
		/* User mode instruction access fault */
		/* FALLTHROUGH */
	case T_DATAFLT+T_USER:
		if (!uvm_map_inentry(p, &p->p_spinentry, PROC_STACK(p),
		    "[%s]%d/%d sp=%lx inside %lx-%lx: not MAP_STACK\n",
		    uvm_map_inentry_sp, p->p_vmspace->vm_map.sserial))
			goto userexit;
m88110_user_fault:
		if (type == T_INSTFLT+T_USER) {
			access_type = PROT_EXEC;
			fault_code = PROT_EXEC;
#ifdef TRAPDEBUG
			printf("User Instruction fault exip %lx isr %lx ilar %lx\n",
			    frame->tf_exip, frame->tf_isr, frame->tf_ilar);
#endif
		} else {
			fault_addr = frame->tf_dlar;
			/*
			 * Unlike the 88100, there is no specific bit telling
			 * us this is the read part of an xmem operation.
			 * However, if the WE (Write Exception) bit is set,
			 * then obviously this is not a read fault.
			 * But the value of this bit can not be relied upon
			 * if either PI or SI are set...
			 */
			if ((frame->tf_dsr & CMMU_DSR_RW) != 0 &&
			    ((frame->tf_dsr & (CMMU_DSR_PI|CMMU_DSR_SI)) != 0 ||
			     (frame->tf_dsr & CMMU_DSR_WE) == 0)) {
				access_type = PROT_READ;
				fault_code = PROT_READ;
			} else {
				access_type = PROT_READ | PROT_WRITE;
				fault_code = PROT_WRITE;
			}
#ifdef TRAPDEBUG
			printf("User Data access fault exip %lx dsr %lx dlar %lx\n",
			    frame->tf_exip, frame->tf_dsr, frame->tf_dlar);
#endif
		}

		va = trunc_page((vaddr_t)fault_addr);

		vm = p->p_vmspace;
		map = &vm->vm_map;
		if ((pcb_onfault = p->p_addr->u_pcb.pcb_onfault) != 0)
			p->p_addr->u_pcb.pcb_onfault = 0;

		/*
		 * Call uvm_fault() to resolve non-bus error faults
		 * whenever possible.
		 */
		if (type == T_INSTFLT+T_USER) {
			/* instruction faults */
			if (frame->tf_isr &
			    (CMMU_ISR_BE | CMMU_ISR_SP | CMMU_ISR_TBE)) {
				/* bus error, supervisor protection */
				result = EACCES;
			} else
			if (frame->tf_isr & (CMMU_ISR_SI | CMMU_ISR_PI)) {
				/* segment or page fault */
				KERNEL_LOCK();
				result = uvm_fault(map, va, 0, access_type);
				KERNEL_UNLOCK();
				if (result == EACCES)
					result = EFAULT;
			} else {
#ifdef TRAPDEBUG
				printf("Unexpected Instruction fault isr %lx\n",
				    frame->tf_isr);
#endif
				goto lose;
			}
		} else {
			/* data faults */
			if (frame->tf_dsr & CMMU_DSR_BE) {
				/* bus error */
				result = EACCES;
			} else
			if (frame->tf_dsr & (CMMU_DSR_SI | CMMU_DSR_PI)) {
				/* segment or page fault */
				KERNEL_LOCK();
				result = uvm_fault(map, va, 0, access_type);
				KERNEL_UNLOCK();
				if (result == EACCES)
					result = EFAULT;
			} else
			if (frame->tf_dsr & (CMMU_DSR_CP | CMMU_DSR_WA)) {
				/* copyback or write allocate error */
				result = EACCES;
			} else
			if (frame->tf_dsr & CMMU_DSR_WE) {
				/* write fault  */
				/* This could be a write protection fault or an
				 * exception to set the used and modified bits
				 * in the pte. Basically, if we got a write
				 * error, then we already have a pte entry that
				 * faulted in from a previous seg fault or page
				 * fault.
				 * Get the pte and check the status of the
				 * modified and valid bits to determine if this
				 * indeed a real write fault.  XXX smurph
				 */
				if (pmap_set_modify(map->pmap, va)) {
#ifdef TRAPDEBUG
					printf("Corrected userland write fault, pmap %p va %p\n",
					    map->pmap, (void *)va);
#endif
					result = 0;
				} else {
					/* must be a real wp fault */
#ifdef TRAPDEBUG
					printf("Uncorrected userland write fault, pmap %p va %p\n",
					    map->pmap, (void *)va);
#endif
					result = uvm_fault(map, va, 0, access_type);
					if (result == EACCES)
						result = EFAULT;
				}
			} else {
#ifdef TRAPDEBUG
				printf("Unexpected Data access fault dsr %lx\n",
				    frame->tf_dsr);
#endif
				goto lose;
			}
		}
		p->p_addr->u_pcb.pcb_onfault = pcb_onfault;

		if (result == 0)
			uvm_grow(p, va);

		/*
		 * This could be a fault caused in copyin*()
		 * while accessing user space.
		 */
		if (result != 0 && pcb_onfault != 0) {
			frame->tf_exip = pcb_onfault;
			/*
			 * Continue as if the fault had been resolved.
			 */
			result = 0;
		}

		if (result != 0) {
			sig = result == EACCES ? SIGBUS : SIGSEGV;
			fault_type = result == EACCES ?
			    BUS_ADRERR : SEGV_MAPERR;
		}
		break;
	case T_MISALGNFLT+T_USER:
		/* Fix any misaligned ld.d or st.d instructions */
		sig = double_reg_fixup(frame, T_MISALGNFLT);
		fault_type = BUS_ADRALN;
		if (sig == 0) {
			/* skip recovered instruction */
			m88110_skip_insn(frame);
			goto userexit;
		}
		break;
	case T_ILLFLT+T_USER:
		/* Fix any ld.d or st.d instruction with an odd register */
		sig = double_reg_fixup(frame, T_ILLFLT);
		fault_type = ILL_PRVREG;
		if (sig == 0) {
			/* skip recovered instruction */
			m88110_skip_insn(frame);
			goto userexit;
		}
		break;
	case T_PRIVINFLT+T_USER:
		fault_type = ILL_PRVREG;
		/* FALLTHROUGH */
#ifndef DDB
	case T_KDB_BREAK:
	case T_KDB_ENTRY:
	case T_KDB_TRACE:
#endif
	case T_KDB_BREAK+T_USER:
	case T_KDB_ENTRY+T_USER:
	case T_KDB_TRACE+T_USER:
		sig = SIGILL;
		break;
	case T_BNDFLT+T_USER:
		sig = SIGFPE;
		/* skip trap instruction */
		m88110_skip_insn(frame);
		break;
	case T_ZERODIV+T_USER:
		sig = SIGFPE;
		fault_type = FPE_INTDIV;
		/* skip trap instruction */
		m88110_skip_insn(frame);
		break;
	case T_OVFFLT+T_USER:
		sig = SIGFPE;
		fault_type = FPE_INTOVF;
		/* skip trap instruction */
		m88110_skip_insn(frame);
		break;
	case T_FPEPFLT+T_USER:
		m88110_fpu_exception(frame);
		goto userexit;
	case T_SIGSYS+T_USER:
		sig = SIGSYS;
		break;
	case T_STEPBPT+T_USER:
#ifdef PTRACE
		/*
		 * This trap is used by the kernel to support single-step
		 * debugging (although any user could generate this trap
		 * which should probably be handled differently). When a
		 * process is continued by a debugger with the PT_STEP
		 * function of ptrace (single step), the kernel inserts
		 * one or two breakpoints in the user process so that only
		 * one instruction (or two in the case of a delayed branch)
		 * is executed.  When this breakpoint is hit, we get the
		 * T_STEPBPT trap.
		 */
		{
			u_int instr;
			vaddr_t pc = PC_REGS(&frame->tf_regs);

			/* read break instruction */
			copyinsn(p, (u_int32_t *)pc, (u_int32_t *)&instr);

			/* check and see if we got here by accident */
			if ((p->p_md.md_bp0va != pc &&
			     p->p_md.md_bp1va != pc) ||
			    instr != SSBREAKPOINT) {
				sig = SIGTRAP;
				fault_type = TRAP_TRACE;
				break;
			}

			/* restore original instruction and clear breakpoint */
			KERNEL_LOCK();
			if (p->p_md.md_bp0va == pc) {
				ss_put_value(p, pc, p->p_md.md_bp0save);
				p->p_md.md_bp0va = 0;
			}
			if (p->p_md.md_bp1va == pc) {
				ss_put_value(p, pc, p->p_md.md_bp1save);
				p->p_md.md_bp1va = 0;
			}
			KERNEL_UNLOCK();

			sig = SIGTRAP;
			fault_type = TRAP_BRKPT;
		}
#else
		sig = SIGTRAP;
		fault_type = TRAP_TRACE;
#endif
		break;
	case T_USERBPT+T_USER:
		/*
		 * This trap is meant to be used by debuggers to implement
		 * breakpoint debugging.  When we get this trap, we just
		 * return a signal which gets caught by the debugger.
		 */
		sig = SIGTRAP;
		fault_type = TRAP_BRKPT;
		break;
	}

	/*
	 * If trap from supervisor mode, just return
	 */
	if (type < T_USER)
		return;

	if (sig) {
deliver:
		sv.sival_ptr = (void *)fault_addr;
		trapsignal(p, sig, fault_code, fault_type, sv);
	}

userexit:
	userret(p);
}
#endif /* M88110 */

__dead void
error_fatal(struct trapframe *frame)
{
	if (frame->tf_vector == 0)
		printf("\nCPU %d Reset Exception\n", cpu_number());
	else
		printf("\nCPU %d Error Exception\n", cpu_number());

#ifdef DDB
	regdump((struct trapframe*)frame);
#endif
	panic("unrecoverable exception %ld", frame->tf_vector);
}

#ifdef M88100
void
m88100_syscall(register_t code, struct trapframe *tf)
{
	const struct sysent *callp = sysent;
	struct proc *p = curproc;
	int error;
	register_t *args;
	register_t rval[2] __aligned(8);

	uvmexp.syscalls++;

	p->p_md.md_tf = tf;

	// XXX out of range stays on syscall0, which we assume is enosys
	if (code > 0 && code < SYS_MAXSYSCALL)
		callp += code;

	/*
	 * For 88k, all the arguments are passed in the registers (r2-r9).
	 */
	args = &tf->tf_r[2];

	rval[0] = 0;
	rval[1] = tf->tf_r[3];

	error = mi_syscall(p, code, callp, args, rval);

	/*
	 * system call will look like:
	 *	 or r13, r0, <code>
	 *       tb0 0, r0, <128> <- sxip
	 *	 br err 	  <- snip
	 *       jmp r1 	  <- sfip
	 *  err: or.u r3, r0, hi16(errno)
	 *	 st r2, r3, lo16(errno)
	 *	 subu r2, r0, 1
	 *	 jmp r1
	 *
	 * So, when we take syscall trap, sxip/snip/sfip will be as
	 * shown above.
	 * Given this,
	 * 1. If the system call returned 0, need to skip nip.
	 *	nip = fip, fip += 4
	 *    (doesn't matter what fip + 4 will be but we will never
	 *    execute this since jmp r1 at nip will change the execution flow.)
	 * 2. If the system call returned an errno > 0, plug the value
	 *    in r2, and leave nip and fip unchanged. This will have us
	 *    executing "br err" on return to user space.
	 * 3. If the system call code returned ERESTART,
	 *    we need to rexecute the trap instruction. Back up the pipe
	 *    line.
	 *     fip = nip, nip = xip
	 * 4. If the system call returned EJUSTRETURN, don't need to adjust
	 *    any pointers.
	 */

	switch (error) {
	case 0:
		tf->tf_r[2] = rval[0];
		tf->tf_r[3] = rval[1];
		tf->tf_epsr &= ~PSR_C;
		tf->tf_snip = tf->tf_sfip & ~NIP_E;
		tf->tf_sfip = tf->tf_snip + 4;
		break;
	case ERESTART:
		m88100_rewind_insn(&(tf->tf_regs));
		/* clear the error bit */
		tf->tf_sfip &= ~FIP_E;
		tf->tf_snip &= ~NIP_E;
		break;
	case EJUSTRETURN:
		break;
	default:
		tf->tf_r[2] = error;
		tf->tf_epsr |= PSR_C;   /* fail */
		tf->tf_snip = tf->tf_snip & ~NIP_E;
		tf->tf_sfip = tf->tf_sfip & ~FIP_E;
		break;
	}

	mi_syscall_return(p, code, error, rval);
}
#endif /* M88100 */

#ifdef M88110
/* Instruction pointers operate differently on mc88110 */
void
m88110_syscall(register_t code, struct trapframe *tf)
{
	const struct sysent *callp = sysent;
	struct proc *p = curproc;
	int error;
	register_t rval[2] __aligned(8);
	register_t *args;

	uvmexp.syscalls++;

	p->p_md.md_tf = tf;

	// XXX out of range stays on syscall0, which we assume is enosys
	if (code > 0 && code < SYS_MAXSYSCALL)
		callp += code;

	/*
	 * For 88k, all the arguments are passed in the registers (r2-r9).
	 */
	args = &tf->tf_r[2];

	rval[0] = 0;
	rval[1] = tf->tf_r[3];

	error = mi_syscall(p, code, callp, args, rval);

	/*
	 * system call will look like:
	 *	 or r13, r0, <code>
	 *       tb0 0, r0, <128> <- exip
	 *	 br err 	  <- enip
	 *       jmp r1
	 *  err: or.u r3, r0, hi16(errno)
	 *	 st r2, r3, lo16(errno)
	 *	 subu r2, r0, 1
	 *	 jmp r1
	 *
	 * So, when we take syscall trap, exip/enip will be as
	 * shown above.
	 * Given this,
	 * 1. If the system call returned 0, need to jmp r1.
	 *    exip += 8
	 * 2. If the system call returned an errno > 0, increment
	 *    exip += 4 and plug the value in r2. This will have us
	 *    executing "br err" on return to user space.
	 * 3. If the system call code returned ERESTART,
	 *    we need to rexecute the trap instruction. leave exip as is.
	 * 4. If the system call returned EJUSTRETURN, just return.
	 *    exip += 4
	 */

	switch (error) {
	case 0:
		tf->tf_r[2] = rval[0];
		tf->tf_r[3] = rval[1];
		tf->tf_epsr &= ~PSR_C;
		/* skip two instructions */
		m88110_skip_insn(tf);
		m88110_skip_insn(tf);
		break;
	case ERESTART:
		/*
		 * Reexecute the trap.
		 * exip is already at the trap instruction, so
		 * there is nothing to do.
		 */
		break;
	case EJUSTRETURN:
		/* skip one instruction */
		m88110_skip_insn(tf);
		break;
	default:
		tf->tf_r[2] = error;
		tf->tf_epsr |= PSR_C;   /* fail */
		/* skip one instruction */
		m88110_skip_insn(tf);
		break;
	}

	mi_syscall_return(p, code, error, rval);
}
#endif	/* M88110 */

/*
 * Set up return-value registers as fork() libc stub expects,
 * and do normal return-to-user-mode stuff.
 */
void
child_return(void *arg)
{
	struct proc *p = arg;
	struct trapframe *tf;

	tf = (struct trapframe *)USER_REGS(p);
	tf->tf_r[2] = 0;
	tf->tf_epsr &= ~PSR_C;
	/* skip br instruction as in syscall() */
#ifdef M88100
	if (CPU_IS88100) {
		tf->tf_snip = (tf->tf_sfip & XIP_ADDR) | XIP_V;
		tf->tf_sfip = tf->tf_snip + 4;
	}
#endif
#ifdef M88110
	if (CPU_IS88110) {
		/* skip two instructions */
		m88110_skip_insn(tf);
		m88110_skip_insn(tf);
	}
#endif

	KERNEL_UNLOCK();

	mi_child_return(p);
}

#ifdef PTRACE

/*
 * User Single Step Debugging Support
 */

#include <sys/ptrace.h>

vaddr_t	ss_branch_taken(u_int, vaddr_t, struct reg *);
int	ss_get_value(struct proc *, vaddr_t, u_int *);
int	ss_inst_branch_or_call(u_int);
int	ss_put_breakpoint(struct proc *, vaddr_t, vaddr_t *, u_int *);

#define	SYSCALL_INSTR	0xf000d080	/* tb0 0,r0,128 */

int
ss_get_value(struct proc *p, vaddr_t addr, u_int *value)
{
	struct uio uio;
	struct iovec iov;

	iov.iov_base = (caddr_t)value;
	iov.iov_len = sizeof(u_int);
	uio.uio_iov = &iov;
	uio.uio_iovcnt = 1;
	uio.uio_offset = (off_t)addr;
	uio.uio_resid = sizeof(u_int);
	uio.uio_segflg = UIO_SYSSPACE;
	uio.uio_rw = UIO_READ;
	uio.uio_procp = curproc;
	return (process_domem(curproc, p->p_p, &uio, PT_READ_I));
}

int
ss_put_value(struct proc *p, vaddr_t addr, u_int value)
{
	struct uio uio;
	struct iovec iov;

	iov.iov_base = (caddr_t)&value;
	iov.iov_len = sizeof(u_int);
	uio.uio_iov = &iov;
	uio.uio_iovcnt = 1;
	uio.uio_offset = (off_t)addr;
	uio.uio_resid = sizeof(u_int);
	uio.uio_segflg = UIO_SYSSPACE;
	uio.uio_rw = UIO_WRITE;
	uio.uio_procp = curproc;
	return (process_domem(curproc, p->p_p, &uio, PT_WRITE_I));
}

/*
 * ss_branch_taken(instruction, pc, regs)
 *
 * instruction will be a control flow instruction location at address pc.
 * Branch taken is supposed to return the address to which the instruction
 * would jump if the branch is taken.
 *
 * This is different from branch_taken() in ddb, as we also need to process
 * system calls.
 */
vaddr_t
ss_branch_taken(u_int inst, vaddr_t pc, struct reg *regs)
{
	u_int regno;

	/*
	 * Quick check of the instruction. Note that we know we are only
	 * invoked if ss_inst_branch_or_call() returns TRUE, so we do not
	 * need to repeat the jpm, jsr and syscall stricter checks here.
	 */
	switch (inst >> (32 - 5)) {
	case 0x18:	/* br */
	case 0x19:	/* bsr */
		/* signed 26 bit pc relative displacement, shift left 2 bits */
		inst = (inst & 0x03ffffff) << 2;
		/* check if sign extension is needed */
		if (inst & 0x08000000)
			inst |= 0xf0000000;
		return (pc + inst);

	case 0x1a:	/* bb0 */
	case 0x1b:	/* bb1 */
	case 0x1d:	/* bcnd */
		/* signed 16 bit pc relative displacement, shift left 2 bits */
		inst = (inst & 0x0000ffff) << 2;
		/* check if sign extension is needed */
		if (inst & 0x00020000)
			inst |= 0xfffc0000;
		return (pc + inst);

	case 0x1e:	/* jmp or jsr */
		regno = inst & 0x1f;	/* get the register value */
		return (regno == 0 ? 0 : regs->r[regno]);

	default:	/* system call */
		/*
		 * The regular (pc + 4) breakpoint will match the error
		 * return. Successful system calls return at (pc + 8),
		 * so we'll set up a branch breakpoint there.
		 */
		return (pc + 8);
	}
}

int
ss_inst_branch_or_call(u_int ins)
{
	/* check high five bits */
	switch (ins >> (32 - 5)) {
	case 0x18: /* br */
	case 0x19: /* bsr */
	case 0x1a: /* bb0 */
	case 0x1b: /* bb1 */
	case 0x1d: /* bcnd */
		return (TRUE);
	case 0x1e: /* could be jmp or jsr */
		if ((ins & 0xfffff3e0) == 0xf400c000)
			return (TRUE);
	}

	return (FALSE);
}

int
ss_put_breakpoint(struct proc *p, vaddr_t va, vaddr_t *bpva, u_int *bpsave)
{
	int rc;

	/* Restore previous breakpoint if we did not trigger it. */
	if (*bpva != 0) {
		ss_put_value(p, *bpva, *bpsave);
		*bpva = 0;
	}

	/* Save instruction. */
	if ((rc = ss_get_value(p, va, bpsave)) != 0)
		return (rc);

	/* Store breakpoint instruction at the location now. */
	*bpva = va;
	return (ss_put_value(p, va, SSBREAKPOINT));
}

int
process_sstep(struct proc *p, int sstep)
{
	struct reg *sstf = USER_REGS(p);
	vaddr_t pc, brpc;
	u_int32_t instr;
	int rc;

	if (sstep == 0) {
		/* Restore previous breakpoints if any. */
		if (p->p_md.md_bp0va != 0) {
			ss_put_value(p, p->p_md.md_bp0va, p->p_md.md_bp0save);
			p->p_md.md_bp0va = 0;
		}
		if (p->p_md.md_bp1va != 0) {
			ss_put_value(p, p->p_md.md_bp1va, p->p_md.md_bp1save);
			p->p_md.md_bp1va = 0;
		}

		return (0);
	}

	/*
	 * User was stopped at pc, e.g. the instruction at pc was not executed.
	 * Fetch what's at the current location.
	 */
	pc = PC_REGS(sstf);
	if ((rc = ss_get_value(p, pc, &instr)) != 0)
		return (rc);

	/*
	 * Find if this instruction may cause a branch, and set up a breakpoint
	 * at the branch location.
	 */
	if (ss_inst_branch_or_call(instr) || instr == SYSCALL_INSTR) {
		brpc = ss_branch_taken(instr, pc, sstf);

		/* self-branches are hopeless */
		if (brpc != pc && brpc != 0) {
			if ((rc = ss_put_breakpoint(p, brpc,
			    &p->p_md.md_bp1va, &p->p_md.md_bp1save)) != 0)
				return (rc);
		}
	}

	if ((rc = ss_put_breakpoint(p, pc + 4,
	    &p->p_md.md_bp0va, &p->p_md.md_bp0save)) != 0)
		return (rc);

	return (0);
}

#endif	/* PTRACE */

#ifdef DIAGNOSTIC
void
splassert_check(int wantipl, const char *func)
{
	int oldipl;

	oldipl = getipl();

	if (oldipl < wantipl) {
		splassert_fail(wantipl, oldipl, func);
		/*
		 * This will raise the spl,
		 * in a feeble attempt to reduce further damage.
		 */
		(void)splraise(wantipl);
	}
}
#endif

/*
 * ld.d and st.d instructions referencing long aligned but not long long
 * aligned addresses will trigger a misaligned address exception.
 *
 * This routine attempts to recover these (valid) statements, by simulating
 * the split form of the instruction. If it fails, it returns the appropriate
 * signal number to deliver.
 *
 * Note that we do not attempt to do anything for .d.usr instructions - the
 * kernel never issues such instructions, and they cause a privileged
 * instruction exception from userland.
 */
int
double_reg_fixup(struct trapframe *frame, int fault)
{
	uint32_t pc, instr, value;
	int regno, store;
	vaddr_t addr;

	/*
	 * Decode the faulting instruction.
	 */

	pc = PC_REGS(&frame->tf_regs);
	if (copyinsn(NULL, (u_int32_t *)pc, (u_int32_t *)&instr) != 0)
		return SIGSEGV;

	switch (instr & 0xfc00ffe0) {
	case 0xf4001000:	/* ld.d rD, rS1, rS2 */
		addr = frame->tf_r[(instr >> 16) & 0x1f]
		    + frame->tf_r[(instr & 0x1f)];
		store = 0;
		break;
	case 0xf4001200:	/* ld.d rD, rS1[rS2] */
		addr = frame->tf_r[(instr >> 16) & 0x1f]
		    + 8 * frame->tf_r[(instr & 0x1f)];
		store = 0;
		break;
	case 0xf4002000:	/* st.d rD, rS1, rS2 */
		addr = frame->tf_r[(instr >> 16) & 0x1f]
		    + frame->tf_r[(instr & 0x1f)];
		store = 1;
		break;
	case 0xf4002200:	/* st.d rD, rS1[rS2] */
		addr = frame->tf_r[(instr >> 16) & 0x1f]
		    + 8 * frame->tf_r[(instr & 0x1f)];
		store = 1;
		break;
	default:
		switch (instr >> 26) {
		case 0x10000000 >> 26:	/* ld.d rD, rS, imm16 */
			addr = (instr & 0x0000ffff) +
			    frame->tf_r[(instr >> 16) & 0x1f];
			store = 0;
			break;
		case 0x20000000 >> 26:	/* st.d rD, rS, imm16 */
			addr = (instr & 0x0000ffff) +
			    frame->tf_r[(instr >> 16) & 0x1f];
			store = 1;
			break;
		default:
			return SIGBUS;
		}
		break;
	}

	regno = (instr >> 21) & 0x1f;

	switch (fault) {
	case T_MISALGNFLT:
		/* We only handle long but not long long aligned access here */
		if ((addr & 0x07) != 4)
			return SIGBUS;
		break;
	case T_ILLFLT:
		/* We only handle odd register pair number here */
		if ((regno & 0x01) == 0)
			return SIGILL;
		/* We only handle long aligned access here */
		if ((addr & 0x03) != 0)
			return SIGBUS;
		break;
	}

	if (store) {
		/*
		 * Two word stores.
		 */
		if (regno == 0)
			value = 0;
		else
			value = frame->tf_r[regno];
		if (copyout(&value, (void *)addr, sizeof(u_int32_t)) != 0)
			return SIGSEGV;
		if (regno == 31)
			value = 0;
		else
			value = frame->tf_r[regno + 1];
		if (copyout(&value, (void *)(addr + 4), sizeof(u_int32_t)) != 0)
			return SIGSEGV;
	} else {
		/*
		 * Two word loads. r0 should be left unaltered, but the
		 * value should still be fetched even if it is discarded.
		 * We can use copyin32 here as the address is guaranteed
		 * to be properly aligned on a 32-bit boundary.
		 */
		if (copyin32((const uint32_t *)addr, &value) != 0)
			return SIGSEGV;
		if (regno != 0)
			frame->tf_r[regno] = value;
		if (copyin32((const uint32_t *)(addr + 4), &value) != 0)
			return SIGSEGV;
		if (regno != 31)
			frame->tf_r[regno + 1] = value;
	}

	return 0;
}

void
cache_flush(struct trapframe *tf)
{
	struct proc *p = curproc;
	struct pmap *pmap;
	paddr_t pa;
	vaddr_t va;
	vsize_t len, count;

	p->p_md.md_tf = tf;

	pmap = vm_map_pmap(&p->p_vmspace->vm_map);
	va = tf->tf_r[2];
	len = tf->tf_r[3];

	if (va < VM_MIN_ADDRESS || va >= VM_MAXUSER_ADDRESS ||
	    va + len <= va || va + len >= VM_MAXUSER_ADDRESS)
		len = 0;

	while (len != 0) {
		count = min(len, PAGE_SIZE - (va & PAGE_MASK));
		if (pmap_extract(pmap, va, &pa) != FALSE)	
			dma_cachectl(pa, count, DMA_CACHE_SYNC);
		va += count;
		len -= count;
	}

#ifdef M88100
	if (CPU_IS88100) {
		/* clear the error bit */
		tf->tf_sfip &= ~FIP_E;
		tf->tf_snip &= ~NIP_E;
	}
#endif
#ifdef M88110
	if (CPU_IS88110) {
		/* skip instruction */
		m88110_skip_insn(tf);
	}
#endif

	userret(p);
}
