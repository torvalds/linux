/*	$OpenBSD: trap.c,v 1.174 2024/11/07 16:02:29 miod Exp $	*/

/*
 * Copyright (c) 1988 University of Utah.
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * the Systems Programming Group of the University of Utah Computer
 * Science Department and Ralph Campbell.
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
 * from: Utah Hdr: trap.c 1.32 91/04/06
 *
 *	from: @(#)trap.c	8.5 (Berkeley) 1/11/94
 */


#include <sys/param.h>
#include <sys/systm.h>
#include <sys/exec.h>
#include <sys/proc.h>
#include <sys/kernel.h>
#include <sys/signalvar.h>
#include <sys/user.h>
#include <sys/stacktrace.h>
#include <sys/syscall.h>
#include <sys/syscall_mi.h>
#include <sys/buf.h>
#include <sys/device.h>
#include <sys/atomic.h>
#ifdef PTRACE
#include <sys/ptrace.h>
#endif

#include <uvm/uvm_extern.h>

#include <machine/autoconf.h>
#include <machine/cpu.h>
#include <mips64/mips_cpu.h>
#include <machine/fpu.h>
#include <machine/frame.h>
#include <machine/mips_opcode.h>
#include <machine/regnum.h>
#include <machine/tcb.h>
#include <machine/trap.h>

#ifdef DDB
#include <mips64/db_machdep.h>
#include <ddb/db_access.h>
#include <ddb/db_output.h>
#include <ddb/db_sym.h>
#endif

#include <sys/syslog.h>

#define	USERMODE(ps)	(((ps) & SR_KSU_MASK) == SR_KSU_USER)

const char *trap_type[] = {
	"external interrupt",
	"TLB modification",
	"TLB miss (load or instr. fetch)",
	"TLB miss (store)",
	"address error (load or I-fetch)",
	"address error (store)",
	"bus error (I-fetch)",
	"bus error (load or store)",
	"system call",
	"breakpoint",
	"reserved instruction",
	"coprocessor unusable",
	"arithmetic overflow",
	"trap",
	"virtual coherency instruction",
	"floating point",
	"reserved 16",
	"reserved 17",
	"reserved 18",
	"reserved 19",
	"reserved 20",
	"reserved 21",
	"reserved 22",
	"watch",
	"reserved 24",
	"reserved 25",
	"reserved 26",
	"reserved 27",
	"reserved 28",
	"reserved 29",
	"reserved 30",
	"virtual coherency data"
};

#if defined(DDB) || defined(DEBUG)
struct trapdebug trapdebug[MAXCPUS * TRAPSIZE];
uint trppos[MAXCPUS];

void	stacktrace(struct trapframe *);
uint32_t kdbpeek(vaddr_t);
uint64_t kdbpeekd(vaddr_t);
#endif	/* DDB || DEBUG */

#if defined(DDB)
extern int db_ktrap(int, db_regs_t *);
#endif

void	ast(void);
extern void interrupt(struct trapframe *);
void	itsa(struct trapframe *, struct cpu_info *, struct proc *, int);
void	trap(struct trapframe *);
#ifdef PTRACE
int	ptrace_read_insn(struct proc *, vaddr_t, uint32_t *);
int	ptrace_write_insn(struct proc *, vaddr_t, uint32_t);
int	process_sstep(struct proc *, int);
#endif

/*
 * Handle an AST for the current process.
 */
void
ast(void)
{
	struct proc *p = curproc;

	p->p_md.md_astpending = 0;

	/*
	 * Make sure the AST flag gets cleared before handling the AST.
	 * Otherwise there is a risk of losing an AST that was sent
	 * by another CPU.
	 */
	membar_enter();

	refreshcreds(p);
	atomic_inc_int(&uvmexp.softs);
	mi_ast(p, curcpu()->ci_want_resched);
	userret(p);
}

/*
 * Handle an exception.
 * In the case of a kernel trap, we return the pc where to resume if
 * pcb_onfault is set, otherwise, return old pc.
 */
void
trap(struct trapframe *trapframe)
{
	struct cpu_info *ci = curcpu();
	struct proc *p = ci->ci_curproc;
	int type;

	type = (trapframe->cause & CR_EXC_CODE) >> CR_EXC_CODE_SHIFT;

	trapdebug_enter(ci, trapframe, -1);

	if (type != T_SYSCALL)
		atomic_inc_int(&uvmexp.traps);
	if (USERMODE(trapframe->sr))
		type |= T_USER;

	/*
	 * Enable hardware interrupts if they were on before the trap;
	 * enable IPI interrupts only otherwise.
	 */
	switch (type) {
	case T_BREAK:
		break;
	default:
		if (ISSET(trapframe->sr, SR_INT_ENAB))
			enableintr();
		else {
#ifdef MULTIPROCESSOR
			ENABLEIPI();
#endif
		}
		break;
	}

	if (type & T_USER)
		refreshcreds(p);

	itsa(trapframe, ci, p, type);

	if (type & T_USER)
		userret(p);
}

/*
 * Handle a single exception.
 */
void
itsa(struct trapframe *trapframe, struct cpu_info *ci, struct proc *p,
    int type)
{
	unsigned ucode = 0;
	vm_prot_t access_type;
	extern vaddr_t onfault_table[];
	int onfault;
	int signal, sicode;
	union sigval sv;
	struct pcb *pcb;

	switch (type) {
	case T_TLB_MOD:
		/* check for kernel address */
		if (trapframe->badvaddr < 0) {
			if (pmap_emulate_modify(pmap_kernel(),
			    trapframe->badvaddr)) {
				/* write to read only page in the kernel */
				access_type = PROT_WRITE;
				pcb = &p->p_addr->u_pcb;
				goto kernel_fault;
			}
			return;
		}
		/* FALLTHROUGH */

	case T_TLB_MOD+T_USER:
		if (pmap_emulate_modify(p->p_vmspace->vm_map.pmap,
		    trapframe->badvaddr)) {
			/* write to read only page */
			access_type = PROT_WRITE;
			pcb = &p->p_addr->u_pcb;
			goto fault_common_no_miss;
		}
		return;

	case T_TLB_LD_MISS:
	case T_TLB_ST_MISS:
		if (type == T_TLB_LD_MISS) {
			vaddr_t pc;

			/*
			 * Check if the fault was caused by
			 * an instruction fetch.
			 */
			pc = trapframe->pc;
			if (trapframe->cause & CR_BR_DELAY)
				pc += 4;
			if (pc == trapframe->badvaddr)
				access_type = PROT_EXEC;
			else
				access_type = PROT_READ;
		} else
			access_type = PROT_WRITE;

		pcb = &p->p_addr->u_pcb;
		/* check for kernel address */
		if (trapframe->badvaddr < 0) {
			vaddr_t va;
			int rv;

	kernel_fault:
			va = trunc_page((vaddr_t)trapframe->badvaddr);
			onfault = pcb->pcb_onfault;
			pcb->pcb_onfault = 0;
			rv = uvm_fault(kernel_map, va, 0, access_type);
			pcb->pcb_onfault = onfault;
			if (rv == 0)
				return;
			if (onfault != 0) {
				pcb->pcb_onfault = 0;
				trapframe->pc = onfault_table[onfault];
				return;
			}
			goto err;
		}
		/*
		 * It is an error for the kernel to access user space except
		 * through the copyin/copyout routines.
		 */
		if (pcb->pcb_onfault != 0) {
			/*
			 * We want to resolve the TLB fault before invoking
			 * pcb_onfault if necessary.
			 */
			goto fault_common;
		} else {
			goto err;
		}

	case T_TLB_LD_MISS+T_USER: {
		vaddr_t pc;

		/* Check if the fault was caused by an instruction fetch. */
		pc = trapframe->pc;
		if (trapframe->cause & CR_BR_DELAY)
			pc += 4;
		if (pc == trapframe->badvaddr)
			access_type = PROT_EXEC;
		else
			access_type = PROT_READ;
		pcb = &p->p_addr->u_pcb;
		goto fault_common;
	}

	case T_TLB_ST_MISS+T_USER:
		access_type = PROT_WRITE;
		pcb = &p->p_addr->u_pcb;
fault_common:
		if ((type & T_USER) &&
		    !uvm_map_inentry(p, &p->p_spinentry, PROC_STACK(p),
		    "[%s]%d/%d sp=%lx inside %lx-%lx: not MAP_STACK\n",
		    uvm_map_inentry_sp, p->p_vmspace->vm_map.sserial))
			return;

fault_common_no_miss:
	    {
		vaddr_t va;
		struct vmspace *vm;
		vm_map_t map;
		int rv;

		vm = p->p_vmspace;
		map = &vm->vm_map;
		va = trunc_page((vaddr_t)trapframe->badvaddr);

		onfault = pcb->pcb_onfault;
		pcb->pcb_onfault = 0;
		rv = uvm_fault(map, va, 0, access_type);
		pcb->pcb_onfault = onfault;

		/*
		 * If this was a stack access we keep track of the maximum
		 * accessed stack size.  Also, if vm_fault gets a protection
		 * failure it is due to accessing the stack region outside
		 * the current limit and we need to reflect that as an access
		 * error.
		 */
		if (rv == 0) {
			uvm_grow(p, va);
			return;
		}

		if (!USERMODE(trapframe->sr)) {
			if (onfault != 0) {
				pcb->pcb_onfault = 0;
				trapframe->pc =  onfault_table[onfault];
				return;
			}
			goto err;
		}

		ucode = access_type;
		signal = SIGSEGV;
		sicode = SEGV_MAPERR;
		if (rv == EACCES)
			sicode = SEGV_ACCERR;
		if (rv == EIO) {
			signal = SIGBUS;
			sicode = BUS_OBJERR;
		}
		break;
	    }

	case T_ADDR_ERR_LD+T_USER:	/* misaligned or kseg access */
	case T_ADDR_ERR_ST+T_USER:	/* misaligned or kseg access */
		ucode = 0;		/* XXX should be PROT_something */
		signal = SIGBUS;
		sicode = BUS_ADRALN;
		break;
	case T_BUS_ERR_IFETCH+T_USER:	/* BERR asserted to cpu */
	case T_BUS_ERR_LD_ST+T_USER:	/* BERR asserted to cpu */
		ucode = 0;		/* XXX should be PROT_something */
		signal = SIGBUS;
		sicode = BUS_OBJERR;
		break;

	case T_SYSCALL+T_USER:
	    {
		struct trapframe *locr0 = p->p_md.md_regs;
		const struct sysent *callp = sysent;
		unsigned int code;
		register_t tpc;
		uint32_t branch = 0;
		int error;
		register_t *args, rval[2];

		atomic_inc_int(&uvmexp.syscalls);

		/* compute next PC after syscall instruction */
		tpc = trapframe->pc; /* Remember if restart */
		if (trapframe->cause & CR_BR_DELAY) {
			/* Get the branch instruction. */
			if (copyinsn(p, locr0->pc, &branch) != 0) {
				signal = SIGBUS;
				sicode = BUS_OBJERR;
				break;
			}

			locr0->pc = MipsEmulateBranch(locr0,
			    trapframe->pc, 0, branch);
		} else
			locr0->pc += 4;
		code = locr0->v0;

		// XXX out of range stays on syscall0, which we assume is enosys
		if (code > 0 && code < SYS_MAXSYSCALL)
			callp += code;

		/*
		 * This relies upon a0-a5 being contiguous in struct trapframe.
		 */
		args = &locr0->a0;

		rval[0] = 0;
		rval[1] = 0;

#if defined(DDB) || defined(DEBUG)
		trapdebug[TRAPSIZE * ci->ci_cpuid + (trppos[ci->ci_cpuid] == 0 ?
		    TRAPSIZE : trppos[ci->ci_cpuid]) - 1].code = code;
#endif

		error = mi_syscall(p, code, callp, args, rval);

		switch (error) {
		case 0:
			locr0->v0 = rval[0];
			locr0->a3 = 0;
			break;
		case ERESTART:
			locr0->pc = tpc;
			break;
		case EJUSTRETURN:
			break;	/* nothing to do */
		default:
			locr0->v0 = error;
			locr0->a3 = 1;
		}

		mi_syscall_return(p, code, error, rval);
		return;
	    }

	case T_BREAK:
#ifdef DDB
		db_ktrap(type, trapframe);
#endif
		/* Reenable interrupts if necessary */
		if (trapframe->sr & SR_INT_ENAB) {
			enableintr();
		}
		return;

	case T_BREAK+T_USER:
	    {
		struct trapframe *locr0 = p->p_md.md_regs;
		vaddr_t va;
		uint32_t branch = 0;
		uint32_t instr;

		/* compute address of break instruction */
		va = trapframe->pc;
		if (trapframe->cause & CR_BR_DELAY) {
			va += 4;

			/* Read branch instruction. */
			if (copyinsn(p, trapframe->pc, &branch) != 0) {
				signal = SIGBUS;
				sicode = BUS_OBJERR;
				break;
			}
		}

		/* read break instruction */
		if (copyinsn(p, va, &instr) != 0) {
			signal = SIGBUS;
			sicode = BUS_OBJERR;
			break;
		}

		switch ((instr & BREAK_VAL_MASK) >> BREAK_VAL_SHIFT) {
		case 6:	/* gcc range error */
			signal = SIGFPE;
			sicode = FPE_FLTSUB;
			/* skip instruction */
			if (trapframe->cause & CR_BR_DELAY)
				locr0->pc = MipsEmulateBranch(locr0,
				    trapframe->pc, 0, branch);
			else
				locr0->pc += 4;
			break;
		case 7:	/* gcc3 divide by zero */
			signal = SIGFPE;
			sicode = FPE_INTDIV;
			/* skip instruction */
			if (trapframe->cause & CR_BR_DELAY)
				locr0->pc = MipsEmulateBranch(locr0,
				    trapframe->pc, 0, branch);
			else
				locr0->pc += 4;
			break;
#ifdef PTRACE
		case BREAK_SSTEP_VAL:
			if (p->p_md.md_ss_addr == (long)va) {
#ifdef DEBUG
				printf("trap: %s (%d): breakpoint at %p "
				    "(insn %08x)\n",
				    p->p_p->ps_comm, p->p_p->ps_pid,
				    (void *)p->p_md.md_ss_addr,
				    p->p_md.md_ss_instr);
#endif

				/* Restore original instruction and clear BP */
				KERNEL_LOCK();
				process_sstep(p, 0);
				KERNEL_UNLOCK();
				sicode = TRAP_BRKPT;
			} else {
				sicode = TRAP_TRACE;
			}
			signal = SIGTRAP;
			break;
#endif
#ifdef FPUEMUL
		case BREAK_FPUEMUL_VAL:
			/*
			 * If this is a genuine FP emulation break,
			 * resume execution to our branch destination.
			 */
			if (!CPU_HAS_FPU(ci) &&
			    (p->p_md.md_flags & MDP_FPUSED) != 0 &&
			    p->p_md.md_fppgva + 4 == (vaddr_t)va) {
				struct vm_map *map = &p->p_vmspace->vm_map;

				p->p_md.md_flags &= ~MDP_FPUSED;
				locr0->pc = p->p_md.md_fpbranchva;

				/*
				 * Prevent access to the relocation page.
				 * XXX needs to be fixed to work with rthreads
				 */
				KERNEL_LOCK();
				uvm_fault_unwire(map, p->p_md.md_fppgva,
				    p->p_md.md_fppgva + PAGE_SIZE);
				KERNEL_UNLOCK();
				(void)uvm_map_protect(map, p->p_md.md_fppgva,
				    p->p_md.md_fppgva + PAGE_SIZE,
				    PROT_NONE, 0, FALSE, FALSE);
				return;
			}
			/* FALLTHROUGH */
#endif
		default:
			signal = SIGTRAP;
			sicode = TRAP_TRACE;
			break;
		}
		break;
	    }

	case T_IWATCH+T_USER:
	case T_DWATCH+T_USER:
	    {
		caddr_t va;
		/* compute address of trapped instruction */
		va = (caddr_t)trapframe->pc;
		if (trapframe->cause & CR_BR_DELAY)
			va += 4;
		printf("watch exception @ %p\n", va);
		signal = SIGTRAP;
		sicode = TRAP_BRKPT;
		break;
	    }

	case T_TRAP+T_USER:
	    {
		struct trapframe *locr0 = p->p_md.md_regs;
		vaddr_t va;
		uint32_t branch = 0;
		uint32_t instr;

		/* compute address of trap instruction */
		va = trapframe->pc;
		if (trapframe->cause & CR_BR_DELAY) {
			va += 4;

			/* Read branch instruction. */
			if (copyinsn(p, trapframe->pc, &branch) != 0) {
				signal = SIGBUS;
				sicode = BUS_OBJERR;
				break;
			}
		}

		/* read break instruction */
		if (copyinsn(p, va, &instr) != 0) {
			signal = SIGBUS;
			sicode = BUS_OBJERR;
			break;
		}

		if (trapframe->cause & CR_BR_DELAY)
			locr0->pc = MipsEmulateBranch(locr0,
			    trapframe->pc, 0, branch);
		else
			locr0->pc += 4;
		/*
		 * GCC 4 uses teq with code 7 to signal divide by
	 	 * zero at runtime. This is one instruction shorter
		 * than the BEQ + BREAK combination used by gcc 3.
		 */
		if ((instr & 0xfc00003f) == 0x00000034 /* teq */ &&
		    (instr & 0x001fffc0) == ((ZERO << 16) | (7 << 6))) {
			signal = SIGFPE;
			sicode = FPE_INTDIV;
		} else if (instr == (0x00000034 | (0x52 << 6)) /* teq */) {
			/* trap used by sigfill and similar */
			KERNEL_LOCK();
			sigexit(p, SIGABRT);
			/* NOTREACHED */
		} else if ((instr & 0xfc00003f) == 0x00000036 /* tne */ &&
		    (instr & 0x0000ffc0) == (0x52 << 6)) {
			KERNEL_LOCK();
			log(LOG_ERR, "%s[%d]: retguard trap\n",
			    p->p_p->ps_comm, p->p_p->ps_pid);
			/* Send uncatchable SIGABRT for coredump */
			sigexit(p, SIGABRT);
			/* NOTREACHED */
		} else {
			signal = SIGTRAP;
			sicode = TRAP_BRKPT;
		}
		break;
	    }

	case T_RES_INST+T_USER:
	    {
		register_t *regs = (register_t *)trapframe;
		vaddr_t va;
		uint32_t branch = 0;
		InstFmt inst;

		/* Compute the instruction's address. */
		va = trapframe->pc;
		if (trapframe->cause & CR_BR_DELAY) {
			va += 4;

			/* Get the branch instruction. */
			if (copyinsn(p, trapframe->pc, &branch) != 0) {
				signal = SIGBUS;
				sicode = BUS_OBJERR;
				break;
			}
		}

		/* Get the faulting instruction. */
		if (copyinsn(p, va, &inst.word) != 0) {
			signal = SIGBUS;
			sicode = BUS_OBJERR;
			break;
		}

		/* Emulate "RDHWR rt, UserLocal". */
		if (inst.RType.op == OP_SPECIAL3 &&
		    inst.RType.rs == 0 &&
		    inst.RType.rd == 29 &&
		    inst.RType.shamt == 0 &&
		    inst.RType.func == OP_RDHWR) {
			regs[inst.RType.rt] = (register_t)TCB_GET(p);

			/* Figure out where to continue. */
			if (trapframe->cause & CR_BR_DELAY)
				trapframe->pc = MipsEmulateBranch(trapframe,
				    trapframe->pc, 0, branch);
			else
				trapframe->pc += 4;
			return;
		}

		signal = SIGILL;
		sicode = ILL_ILLOPC;
		break;
	    }

	case T_COP_UNUSABLE+T_USER:
		/*
		 * Note MIPS IV COP1X instructions issued with FPU
		 * disabled correctly report coprocessor 1 as the
		 * unusable coprocessor number.
		 */
		if ((trapframe->cause & CR_COP_ERR) != CR_COP1_ERR) {
			signal = SIGILL; /* only FPU instructions allowed */
			sicode = ILL_ILLOPC;
			break;
		}
		if (CPU_HAS_FPU(ci))
			enable_fpu(p);
		else
			MipsFPTrap(trapframe);
		return;

	case T_FPE:
		printf("FPU Trap: PC %lx CR %lx SR %lx\n",
			trapframe->pc, trapframe->cause, trapframe->sr);
		goto err;

	case T_FPE+T_USER:
		MipsFPTrap(trapframe);
		return;

	case T_OVFLOW+T_USER:
		signal = SIGFPE;
		sicode = FPE_FLTOVF;
		break;

	case T_ADDR_ERR_LD:	/* misaligned access */
	case T_ADDR_ERR_ST:	/* misaligned access */
	case T_BUS_ERR_LD_ST:	/* BERR asserted to cpu */
		pcb = &p->p_addr->u_pcb;
		if ((onfault = pcb->pcb_onfault) != 0) {
			pcb->pcb_onfault = 0;
			trapframe->pc = onfault_table[onfault];
			return;
		}
		goto err;

	default:
	err:
		disableintr();
#if !defined(DDB) && defined(DEBUG)
		trapDump("trap", printf);
#endif
		printf("\nTrap cause = %d Frame %p\n", type, trapframe);
		printf("Trap PC %p RA %p fault %p\n",
		    (void *)trapframe->pc, (void *)trapframe->ra,
		    (void *)trapframe->badvaddr);
#ifdef DDB
		stacktrace(!USERMODE(trapframe->sr) ? trapframe : p->p_md.md_regs);
		db_ktrap(type, trapframe);
#endif
		panic("trap");
	}

#ifdef FPUEMUL
	/*
	 * If a relocated delay slot causes an exception, blame the
	 * original delay slot address - userland is not supposed to
	 * know anything about emulation bowels.
	 */
	if (!CPU_HAS_FPU(ci) && (p->p_md.md_flags & MDP_FPUSED) != 0 &&
	    trapframe->badvaddr == p->p_md.md_fppgva)
		trapframe->badvaddr = p->p_md.md_fpslotva;
#endif
	p->p_md.md_regs->pc = trapframe->pc;
	p->p_md.md_regs->cause = trapframe->cause;
	p->p_md.md_regs->badvaddr = trapframe->badvaddr;
	sv.sival_ptr = (void *)trapframe->badvaddr;
	trapsignal(p, signal, ucode, sicode, sv);
}

void
child_return(void *arg)
{
	struct proc *p = arg;
	struct trapframe *trapframe;

	trapframe = p->p_md.md_regs;
	trapframe->v0 = 0;
	trapframe->a3 = 0;

	KERNEL_UNLOCK();

	mi_child_return(p);
}

int
copyinsn(struct proc *p, vaddr_t uva, uint32_t *insn)
{
	struct vm_map *map = &p->p_vmspace->vm_map;
	int error = 0;

	if (__predict_false(uva >= VM_MAXUSER_ADDRESS || (uva & 3) != 0))
		return EFAULT;

	do {
		if (pmap_copyinsn(map->pmap, uva, insn))
			break;
		error = uvm_fault(map, trunc_page(uva), 0, PROT_EXEC);
	} while (error == 0);

	return error;
}

#if defined(DDB) || defined(DEBUG)
void
trapDump(const char *msg, int (*pr)(const char *, ...))
{
#ifdef MULTIPROCESSOR
	CPU_INFO_ITERATOR cii;
#endif
	struct cpu_info *ci;
	struct trapdebug *base, *ptrp;
	int i;
	uint pos;
	int s;

	s = splhigh();
	(*pr)("trapDump(%s)\n", msg);
#ifndef MULTIPROCESSOR
	ci = curcpu();
#else
	CPU_INFO_FOREACH(cii, ci)
#endif
	{
#ifdef MULTIPROCESSOR
		(*pr)("cpu%d\n", ci->ci_cpuid);
#endif
		/* walk in reverse order */
		pos = trppos[ci->ci_cpuid];
		base = trapdebug + ci->ci_cpuid * TRAPSIZE;
		for (i = TRAPSIZE - 1; i >= 0; i--) {
			if (pos + i >= TRAPSIZE)
				ptrp = base + pos + i - TRAPSIZE;
			else
				ptrp = base + pos + i;

			if (ptrp->cause == 0)
				break;

			(*pr)("%s: PC %p CR 0x%08lx SR 0x%08lx\n",
			    trap_type[(ptrp->cause & CR_EXC_CODE) >>
			      CR_EXC_CODE_SHIFT],
			    ptrp->pc, ptrp->cause & 0xffffffff,
			    ptrp->status & 0xffffffff);
			(*pr)(" RA %p SP %p ADR %p\n",
			    ptrp->ra, ptrp->sp, ptrp->vadr);
		}
	}

	splx(s);
}
#endif


/*
 * Return the resulting PC as if the branch was executed.
 */
register_t
MipsEmulateBranch(struct trapframe *tf, vaddr_t instPC, uint32_t fsr,
    uint32_t curinst)
{
	register_t *regsPtr = (register_t *)tf;
	InstFmt inst;
	vaddr_t retAddr;
	int condition;
	uint cc;

#define	GetBranchDest(InstPtr, inst) \
	    (InstPtr + 4 + ((short)inst.IType.imm << 2))

	inst.word = curinst;

	regsPtr[ZERO] = 0;	/* Make sure zero is 0x0 */

	switch ((int)inst.JType.op) {
	case OP_SPECIAL:
		switch ((int)inst.RType.func) {
		case OP_JR:
		case OP_JALR:
			retAddr = (vaddr_t)regsPtr[inst.RType.rs];
			break;
		default:
			retAddr = instPC + 4;
			break;
		}
		break;
	case OP_BCOND:
		switch ((int)inst.IType.rt) {
		case OP_BLTZ:
		case OP_BLTZL:
		case OP_BLTZAL:
		case OP_BLTZALL:
			if ((int64_t)(regsPtr[inst.RType.rs]) < 0)
				retAddr = GetBranchDest(instPC, inst);
			else
				retAddr = instPC + 8;
			break;
		case OP_BGEZ:
		case OP_BGEZL:
		case OP_BGEZAL:
		case OP_BGEZALL:
			if ((int64_t)(regsPtr[inst.RType.rs]) >= 0)
				retAddr = GetBranchDest(instPC, inst);
			else
				retAddr = instPC + 8;
			break;
		default:
			retAddr = instPC + 4;
			break;
		}
		break;
	case OP_J:
	case OP_JAL:
		retAddr = (inst.JType.target << 2) | (instPC & ~0x0fffffffUL);
		break;
	case OP_BEQ:
	case OP_BEQL:
		if (regsPtr[inst.RType.rs] == regsPtr[inst.RType.rt])
			retAddr = GetBranchDest(instPC, inst);
		else
			retAddr = instPC + 8;
		break;
	case OP_BNE:
	case OP_BNEL:
		if (regsPtr[inst.RType.rs] != regsPtr[inst.RType.rt])
			retAddr = GetBranchDest(instPC, inst);
		else
			retAddr = instPC + 8;
		break;
	case OP_BLEZ:
	case OP_BLEZL:
		if ((int64_t)(regsPtr[inst.RType.rs]) <= 0)
			retAddr = GetBranchDest(instPC, inst);
		else
			retAddr = instPC + 8;
		break;
	case OP_BGTZ:
	case OP_BGTZL:
		if ((int64_t)(regsPtr[inst.RType.rs]) > 0)
			retAddr = GetBranchDest(instPC, inst);
		else
			retAddr = instPC + 8;
		break;
	case OP_COP1:
		switch (inst.RType.rs) {
		case OP_BC:
			cc = (inst.RType.rt & COPz_BC_CC_MASK) >>
			    COPz_BC_CC_SHIFT;
			if ((inst.RType.rt & COPz_BC_TF_MASK) == COPz_BC_TRUE)
				condition = fsr & FPCSR_CONDVAL(cc);
			else
				condition = !(fsr & FPCSR_CONDVAL(cc));
			if (condition)
				retAddr = GetBranchDest(instPC, inst);
			else
				retAddr = instPC + 8;
			break;
		default:
			retAddr = instPC + 4;
		}
		break;
	default:
		retAddr = instPC + 4;
	}

	return (register_t)retAddr;
#undef	GetBranchDest
}

#ifdef PTRACE

int
ptrace_read_insn(struct proc *p, vaddr_t va, uint32_t *insn)
{
	struct iovec iov;
	struct uio uio;

	iov.iov_base = (caddr_t)insn;
	iov.iov_len = sizeof(uint32_t);
	uio.uio_iov = &iov;
	uio.uio_iovcnt = 1;
	uio.uio_offset = (off_t)va;
	uio.uio_resid = sizeof(uint32_t);
	uio.uio_segflg = UIO_SYSSPACE;
	uio.uio_rw = UIO_READ;
	uio.uio_procp = curproc;
	return process_domem(curproc, p->p_p, &uio, PT_READ_I);
}

int
ptrace_write_insn(struct proc *p, vaddr_t va, uint32_t insn)
{
	struct iovec iov;
	struct uio uio;

	iov.iov_base = (caddr_t)&insn;
	iov.iov_len = sizeof(uint32_t);
	uio.uio_iov = &iov;
	uio.uio_iovcnt = 1;
	uio.uio_offset = (off_t)va;
	uio.uio_resid = sizeof(uint32_t);
	uio.uio_segflg = UIO_SYSSPACE;
	uio.uio_rw = UIO_WRITE;
	uio.uio_procp = curproc;
	return process_domem(curproc, p->p_p, &uio, PT_WRITE_I);
}

/*
 * This routine is called by procxmt() to single step one instruction.
 * We do this by storing a break instruction after the current instruction,
 * resuming execution, and then restoring the old instruction.
 */
int
process_sstep(struct proc *p, int sstep)
{
	struct trapframe *locr0 = p->p_md.md_regs;
	int rc;
	uint32_t curinstr;
	vaddr_t va;

	if (sstep == 0) {
		/* clear the breakpoint */
		if (p->p_md.md_ss_addr != 0) {
			rc = ptrace_write_insn(p, p->p_md.md_ss_addr,
			    p->p_md.md_ss_instr);
#ifdef DIAGNOSTIC
			if (rc != 0)
				printf("WARNING: %s (%d): can't restore "
				    "instruction at %p: %08x\n",
				    p->p_p->ps_comm, p->p_p->ps_pid,
				    (void *)p->p_md.md_ss_addr,
				    p->p_md.md_ss_instr);
#endif
			p->p_md.md_ss_addr = 0;
		} else
			rc = 0;
		return rc;
	}

	/* read current instruction */
	rc = ptrace_read_insn(p, locr0->pc, &curinstr);
	if (rc != 0)
		return rc;

	/* compute next address after current location */
	if (curinstr != 0 /* nop */)
		va = (vaddr_t)MipsEmulateBranch(locr0,
		    locr0->pc, locr0->fsr, curinstr);
	else
		va = locr0->pc + 4;
#ifdef DIAGNOSTIC
	/* should not happen */
	if (p->p_md.md_ss_addr != 0) {
		printf("WARNING: %s (%d): breakpoint request "
		    "at %p, already set at %p\n",
		    p->p_p->ps_comm, p->p_p->ps_pid, (void *)va,
		    (void *)p->p_md.md_ss_addr);
		return EFAULT;
	}
#endif

	/* read next instruction */
	rc = ptrace_read_insn(p, va, &p->p_md.md_ss_instr);
	if (rc != 0)
		return rc;

	/* replace with a breakpoint instruction */
	rc = ptrace_write_insn(p, va, BREAK_SSTEP);
	if (rc != 0)
		return rc;

	p->p_md.md_ss_addr = va;

#ifdef DEBUG
	printf("%s (%d): breakpoint set at %p: %08x (pc %p %08x)\n",
		p->p_p->ps_comm, p->p_p->ps_pid, (void *)p->p_md.md_ss_addr,
		p->p_md.md_ss_instr, (void *)locr0->pc, curinstr);
#endif
	return 0;
}

#endif /* PTRACE */

#if defined(DDB) || defined(DEBUG)
#define MIPS_JR_RA	0x03e00008	/* instruction code for jr ra */

/* forward */
#if !defined(DDB)
const char *fn_name(vaddr_t);
#endif
void stacktrace_subr(struct trapframe *, int, int (*)(const char*, ...));

extern char kernel_text[];
extern char etext[];

/*
 * Print a stack backtrace.
 */
void
stacktrace(struct trapframe *regs)
{
	stacktrace_subr(regs, 6, printf);
}

#define	VALID_ADDRESS(va) \
	(((va) >= VM_MIN_KERNEL_ADDRESS && (va) < VM_MAX_KERNEL_ADDRESS) || \
	 IS_XKPHYS(va) || ((va) >= CKSEG0_BASE && (va) < CKSEG1_BASE))

void
stacktrace_subr(struct trapframe *regs, int count,
    int (*pr)(const char*, ...))
{
	vaddr_t pc, sp, ra, va, subr;
	register_t a0, a1, a2, a3;
	uint32_t instr, mask;
	InstFmt i;
	int more, stksize;
	extern char k_intr[];
	extern char k_general[];
#ifdef DDB
	db_expr_t diff;
	Elf_Sym *sym;
	const char *symname;
#endif

	/* get initial values from the exception frame */
	sp = (vaddr_t)regs->sp;
	pc = (vaddr_t)regs->pc;
	ra = (vaddr_t)regs->ra;		/* May be a 'leaf' function */
	a0 = regs->a0;
	a1 = regs->a1;
	a2 = regs->a2;
	a3 = regs->a3;

/* Jump here when done with a frame, to start a new one */
loop:
#ifdef DDB
	symname = NULL;
#endif
	subr = 0;
	stksize = 0;

	if (count-- == 0) {
		ra = 0;
		goto end;
	}

	/* check for bad SP: could foul up next frame */
	if (sp & 3 || !VALID_ADDRESS(sp)) {
		(*pr)("SP %p: not in kernel\n", sp);
		ra = 0;
		goto end;
	}

	/* check for bad PC */
	if (pc & 3 || !VALID_ADDRESS(pc)) {
		(*pr)("PC %p: not in kernel\n", pc);
		ra = 0;
		goto end;
	}

#ifdef DDB
	/*
	 * Dig out the function from the symbol table.
	 * Watch out for function tail optimizations.
	 */
	sym = db_search_symbol(pc, DB_STGY_PROC, &diff);
	if (sym != NULL && diff == 0) {
		instr = kdbpeek(pc - 2 * sizeof(int));
		i.word = instr;
		if (i.JType.op == OP_JAL) {
			sym = db_search_symbol(pc - sizeof(int),
			    DB_STGY_PROC, &diff);
			if (sym != NULL && diff != 0)
				diff += sizeof(int);
		}
	}
	if (sym != NULL) {
		db_symbol_values(sym, &symname, 0);
		subr = pc - (vaddr_t)diff;
	}
#endif

	/*
	 * Find the beginning of the current subroutine by scanning backwards
	 * from the current PC for the end of the previous subroutine.
	 */
	if (!subr) {
		va = pc - sizeof(int);
		while ((instr = kdbpeek(va)) != MIPS_JR_RA)
			va -= sizeof(int);
		va += 2 * sizeof(int);	/* skip back over branch & delay slot */
		/* skip over nulls which might separate .o files */
		while ((instr = kdbpeek(va)) == 0)
			va += sizeof(int);
		subr = va;
	}

	/*
	 * Jump here for locore entry points for which the preceding
	 * function doesn't end in "j ra"
	 */
	/* scan forwards to find stack size and any saved registers */
	stksize = 0;
	more = 3;
	mask = 0;
	for (va = subr; more; va += sizeof(int),
	    more = (more == 3) ? 3 : more - 1) {
		/* stop if hit our current position */
		if (va >= pc)
			break;
		instr = kdbpeek(va);
		i.word = instr;
		switch (i.JType.op) {
		case OP_SPECIAL:
			switch (i.RType.func) {
			case OP_JR:
			case OP_JALR:
				more = 2; /* stop after next instruction */
				break;

			case OP_SYSCALL:
			case OP_BREAK:
				more = 1; /* stop now */
			}
			break;

		case OP_BCOND:
		case OP_J:
		case OP_JAL:
		case OP_BEQ:
		case OP_BNE:
		case OP_BLEZ:
		case OP_BGTZ:
			more = 2; /* stop after next instruction */
			break;

		case OP_COP0:
		case OP_COP1:
		case OP_COP2:
		case OP_COP3:
			switch (i.RType.rs) {
			case OP_BC:
				more = 2; /* stop after next instruction */
			}
			break;

		case OP_SD:
			/* look for saved registers on the stack */
			if (i.IType.rs != SP)
				break;
			/* only restore the first one */
			if (mask & (1 << i.IType.rt))
				break;
			mask |= (1 << i.IType.rt);
			switch (i.IType.rt) {
			case A0:
				a0 = kdbpeekd(sp + (int16_t)i.IType.imm);
				break;
			case A1:
				a1 = kdbpeekd(sp + (int16_t)i.IType.imm);
				break;
			case A2:
				a2 = kdbpeekd(sp + (int16_t)i.IType.imm);
				break;
			case A3:
				a3 = kdbpeekd(sp + (int16_t)i.IType.imm);
				break;
			case RA:
				ra = kdbpeekd(sp + (int16_t)i.IType.imm);
				break;
			}
			break;

		case OP_DADDI:
		case OP_DADDIU:
			/* look for stack pointer adjustment */
			if (i.IType.rs != SP || i.IType.rt != SP)
				break;
			stksize = -((int16_t)i.IType.imm);
		}
	}

#ifdef DDB
	if (symname == NULL)
		(*pr)("%p ", subr);
	else
		(*pr)("%s+%p ", symname, diff);
#else
	(*pr)("%s+%p ", fn_name(subr), pc - subr);
#endif
	(*pr)("(%llx,%llx,%llx,%llx) ", a0, a1, a2, a3);
	(*pr)(" ra %p sp %p, sz %d\n", ra, sp, stksize);

	if (subr == (vaddr_t)k_intr || subr == (vaddr_t)k_general) {
		if (subr == (vaddr_t)k_general)
			(*pr)("(KERNEL TRAP)\n");
		else
			(*pr)("(KERNEL INTERRUPT)\n");
		sp = *(register_t *)sp;
		pc = ((struct trapframe *)sp)->pc;
		ra = ((struct trapframe *)sp)->ra;
		sp = ((struct trapframe *)sp)->sp;
		goto loop;
	}

end:
	if (ra) {
		if (pc == ra && stksize == 0)
			(*pr)("stacktrace: loop!\n");
		else if (ra < (vaddr_t)kernel_text || ra > (vaddr_t)etext)
			(*pr)("stacktrace: ra corrupted!\n");
		else {
			pc = ra;
			sp += stksize;
			ra = 0;
			goto loop;
		}
	} else {
		if (curproc)
			(*pr)("User-level: pid %d\n", curproc->p_p->ps_pid);
		else
			(*pr)("User-level: curproc NULL\n");
	}
}

#ifdef DDB
void
stacktrace_save_at(struct stacktrace *st, unsigned int skip)
{
	extern char k_general[];
	extern char u_general[];
	extern char k_intr[];
	extern char u_intr[];
	db_expr_t diff;
	Elf_Sym *sym;
	struct trapframe *tf;
	vaddr_t pc, ra, sp, subr, va;
	InstFmt inst;
	int first = 1;
	int done, framesize;

	/* Get a pc that comes after the prologue in this subroutine. */
	__asm__ volatile ("1: dla %0, 1b" : "=r" (pc));

	ra = (vaddr_t)__builtin_return_address(0);
	sp = (vaddr_t)__builtin_frame_address(0);

	st->st_count = 0;
	while (st->st_count < STACKTRACE_MAX && pc != 0) {
		if ((pc & 0x3) != 0 ||
		    pc < (vaddr_t)kernel_text || pc >= (vaddr_t)etext)
			break;
		if ((sp & 0x7) != 0 || !VALID_ADDRESS(sp))
			break;

		if (!first) {
			if (skip == 0)
				st->st_pc[st->st_count++] = pc;
			else
				skip--;
		}
		first = 0;

		/* Determine the start address of the current subroutine. */
		sym = db_search_symbol(pc, DB_STGY_PROC, &diff);
		if (sym == NULL)
			break;
		subr = pc - (vaddr_t)diff;

		if ((subr & 0x3) != 0)
			break;
		if (subr == (vaddr_t)u_general || subr == (vaddr_t)u_intr)
			break;
		if (subr == (vaddr_t)k_general || subr == (vaddr_t)k_intr) {
			tf = (struct trapframe *)*(register_t *)sp;
			pc = tf->pc;
			ra = tf->ra;
			sp = tf->sp;
			continue;
		}

		/*
		 * Figure out the return address and the size of the current
		 * stack frame by analyzing the subroutine's prologue.
		 */
		done = 0;
		framesize = 0;
		for (va = subr; va < pc && !done; va += 4) {
			inst.word = *(uint32_t *)va;
			if (inst_call(inst.word) || inst_return(inst.word)) {
				/* Check the delay slot and stop. */
				va += 4;
				inst.word = *(uint32_t *)va;
				done = 1;
			}
			switch (inst.JType.op) {
			case OP_SPECIAL:
				switch (inst.RType.func) {
				case OP_SYSCALL:
				case OP_BREAK:
					done = 1;
				}
				break;
			case OP_SD:
				if (inst.IType.rs == SP &&
				    inst.IType.rt == RA && ra == 0)
					ra = *(uint64_t *)(sp +
					    (int16_t)inst.IType.imm);
				break;
			case OP_DADDI:
			case OP_DADDIU:
				if (inst.IType.rs == SP &&
				    inst.IType.rt == SP &&
				    (int16_t)inst.IType.imm < 0 &&
				    framesize == 0)
					framesize = -(int16_t)inst.IType.imm;
				break;
			}

			if (framesize != 0 && ra != 0)
				break;
		}

		pc = ra;
		ra = 0;
		sp += framesize;
	}
}

void
stacktrace_save_utrace(struct stacktrace *st)
{
	st->st_count = 0;
}
#endif

#undef	VALID_ADDRESS

#if !defined(DDB)
/*
 * Functions ``special'' enough to print by name
 */
#ifdef __STDC__
#define Name(_fn)  { (void*)_fn, # _fn }
#else
#define Name(_fn) { _fn, "_fn"}
#endif
static const struct { void *addr; const char *name;} names[] = {
	Name(trap),
	{ 0, NULL }
};

/*
 * Map a function address to a string name, if known; or a hex string.
 */
const char *
fn_name(vaddr_t addr)
{
	static char buf[19];
	int i = 0;

	for (i = 0; names[i].name != NULL; i++)
		if (names[i].addr == (void*)addr)
			return (names[i].name);
	snprintf(buf, sizeof(buf), "%p", (void *)addr);
	return (buf);
}
#endif	/* !DDB */

#endif /* DDB || DEBUG */

#ifdef FPUEMUL
/*
 * Set up a successful branch emulation.
 * The delay slot instruction is copied to a reserved page, followed by a
 * trap instruction to get control back, and resume at the branch
 * destination.
 */
int
fpe_branch_emulate(struct proc *p, struct trapframe *tf, uint32_t insn,
    vaddr_t dest)
{
	struct vm_map *map = &p->p_vmspace->vm_map;
	InstFmt inst;
	int rc;

	/*
	 * Check the delay slot instruction: since it will run as a
	 * non-delay slot instruction, we want to reject branch instructions
	 * (which behaviour, when in a delay slot, is undefined anyway).
	 */

	inst = *(InstFmt *)&insn;
	rc = 0;
	switch ((int)inst.JType.op) {
	case OP_SPECIAL:
		switch ((int)inst.RType.func) {
		case OP_JR:
		case OP_JALR:
			rc = EINVAL;
			break;
		}
		break;
	case OP_BCOND:
		switch ((int)inst.IType.rt) {
		case OP_BLTZ:
		case OP_BLTZL:
		case OP_BLTZAL:
		case OP_BLTZALL:
		case OP_BGEZ:
		case OP_BGEZL:
		case OP_BGEZAL:
		case OP_BGEZALL:
			rc = EINVAL;
			break;
		}
		break;
	case OP_J:
	case OP_JAL:
	case OP_BEQ:
	case OP_BEQL:
	case OP_BNE:
	case OP_BNEL:
	case OP_BLEZ:
	case OP_BLEZL:
	case OP_BGTZ:
	case OP_BGTZL:
		rc = EINVAL;
		break;
	case OP_COP1:
		if (inst.RType.rs == OP_BC)	/* oh the irony */
			rc = EINVAL;
		break;
	}

	if (rc != 0) {
#ifdef DEBUG
		printf("%s: bogus delay slot insn %08x\n", __func__, insn);
#endif
		return rc;
	}

	/*
	 * Temporarily change protection over the page used to relocate
	 * the delay slot, and fault it in.
	 */

	rc = uvm_map_protect(map, p->p_md.md_fppgva,
	    p->p_md.md_fppgva + PAGE_SIZE, PROT_READ | PROT_WRITE, 0, FALSE,
	    FALSE);
	if (rc != 0) {
#ifdef DEBUG
		printf("%s: uvm_map_protect on %p failed: %d\n",
		    __func__, (void *)p->p_md.md_fppgva, rc);
#endif
		return rc;
	}
	KERNEL_LOCK();
	rc = uvm_fault_wire(map, p->p_md.md_fppgva,
	    p->p_md.md_fppgva + PAGE_SIZE, PROT_READ | PROT_WRITE);
	KERNEL_UNLOCK();
	if (rc != 0) {
#ifdef DEBUG
		printf("%s: uvm_fault_wire on %p failed: %d\n",
		    __func__, (void *)p->p_md.md_fppgva, rc);
#endif
		goto err2;
	}

	rc = copyout(&insn, (void *)p->p_md.md_fppgva, sizeof insn);
	if (rc != 0) {
#ifdef DEBUG
		printf("%s: copyout %p failed %d\n",
		    __func__, (void *)p->p_md.md_fppgva, rc);
#endif
		goto err;
	}
	insn = BREAK_FPUEMUL;
	rc = copyout(&insn, (void *)(p->p_md.md_fppgva + 4), sizeof insn);
	if (rc != 0) {
#ifdef DEBUG
		printf("%s: copyout %p failed %d\n",
		    __func__, (void *)(p->p_md.md_fppgva + 4), rc);
#endif
		goto err;
	}

	(void)uvm_map_protect(map, p->p_md.md_fppgva,
	    p->p_md.md_fppgva + PAGE_SIZE, PROT_READ | PROT_EXEC, 0, FALSE, FALSE);
	p->p_md.md_fpbranchva = dest;
	p->p_md.md_fpslotva = (vaddr_t)tf->pc + 4;
	p->p_md.md_flags |= MDP_FPUSED;
	tf->pc = p->p_md.md_fppgva;

	return 0;

err:
	KERNEL_LOCK();
	uvm_fault_unwire(map, p->p_md.md_fppgva, p->p_md.md_fppgva + PAGE_SIZE);
	KERNEL_UNLOCK();
err2:
	(void)uvm_map_protect(map, p->p_md.md_fppgva,
	    p->p_md.md_fppgva + PAGE_SIZE, PROT_NONE, 0, FALSE, FALSE);
	return rc;
}
#endif
