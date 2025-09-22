/*	$OpenBSD: trap.c,v 1.166 2024/04/14 03:26:25 jsg Exp $	*/

/*
 * Copyright (c) 1998-2004 Michael Shalayeff
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR OR HIS RELATIVES BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF MIND, USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

/* #define TRAPDEBUG */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/syscall.h>
#include <sys/proc.h>
#include <sys/signalvar.h>
#include <sys/user.h>
#include <sys/syscall_mi.h>

#include <uvm/uvm_extern.h>

#include <machine/autoconf.h>

#ifdef DDB
#ifdef TRAPDEBUG
#include <ddb/db_output.h>
#else
#include <machine/db_machdep.h>
#endif
#endif

static __inline int inst_store(u_int ins) {
	return (ins & 0xf0000000) == 0x60000000 ||	/* st */
	       (ins & 0xf4000200) == 0x24000200 ||	/* fst/cst */
	       (ins & 0xfc000200) == 0x0c000200 ||	/* stby */
	       (ins & 0xfc0003c0) == 0x0c0001c0;	/* ldcw */
}

int	pcxs_unaligned(u_int opcode, vaddr_t va);
#ifdef PTRACE
void	ss_clear_breakpoints(struct proc *p);
#endif

void	ast(struct proc *);

/* single-step breakpoint */
#define SSBREAKPOINT	(HPPA_BREAK_KERNEL | (HPPA_BREAK_SS << 13))

const char *trap_type[] = {
	"invalid",
	"HPMC",
	"power failure",
	"recovery counter",
	"external interrupt",
	"LPMC",
	"ITLB miss fault",
	"instruction protection",
	"Illegal instruction",
	"break instruction",
	"privileged operation",
	"privileged register",
	"overflow",
	"conditional",
	"assist exception",
	"DTLB miss",
	"ITLB non-access miss",
	"DTLB non-access miss",
	"data protection/rights/alignment",
	"data break",
	"TLB dirty",
	"page reference",
	"assist emulation",
	"higher-priv transfer",
	"lower-priv transfer",
	"taken branch",
	"data access rights",
	"data protection",
	"unaligned data ref",
};
int trap_types = sizeof(trap_type)/sizeof(trap_type[0]);

#define	frame_regmap(tf,r)	(((u_int *)(tf))[hppa_regmap[(r)]])
u_char hppa_regmap[32] = {
	offsetof(struct trapframe, tf_pad[0]) / 4,	/* r0 XXX */
	offsetof(struct trapframe, tf_r1) / 4,
	offsetof(struct trapframe, tf_rp) / 4,
	offsetof(struct trapframe, tf_r3) / 4,
	offsetof(struct trapframe, tf_r4) / 4,
	offsetof(struct trapframe, tf_r5) / 4,
	offsetof(struct trapframe, tf_r6) / 4,
	offsetof(struct trapframe, tf_r7) / 4,
	offsetof(struct trapframe, tf_r8) / 4,
	offsetof(struct trapframe, tf_r9) / 4,
	offsetof(struct trapframe, tf_r10) / 4,
	offsetof(struct trapframe, tf_r11) / 4,
	offsetof(struct trapframe, tf_r12) / 4,
	offsetof(struct trapframe, tf_r13) / 4,
	offsetof(struct trapframe, tf_r14) / 4,
	offsetof(struct trapframe, tf_r15) / 4,
	offsetof(struct trapframe, tf_r16) / 4,
	offsetof(struct trapframe, tf_r17) / 4,
	offsetof(struct trapframe, tf_r18) / 4,
	offsetof(struct trapframe, tf_t4) / 4,
	offsetof(struct trapframe, tf_t3) / 4,
	offsetof(struct trapframe, tf_t2) / 4,
	offsetof(struct trapframe, tf_t1) / 4,
	offsetof(struct trapframe, tf_arg3) / 4,
	offsetof(struct trapframe, tf_arg2) / 4,
	offsetof(struct trapframe, tf_arg1) / 4,
	offsetof(struct trapframe, tf_arg0) / 4,
	offsetof(struct trapframe, tf_dp) / 4,
	offsetof(struct trapframe, tf_ret0) / 4,
	offsetof(struct trapframe, tf_ret1) / 4,
	offsetof(struct trapframe, tf_sp) / 4,
	offsetof(struct trapframe, tf_r31) / 4,
};

void
ast(struct proc *p)
{
	if (p->p_md.md_astpending) {
		p->p_md.md_astpending = 0;
		uvmexp.softs++;
		mi_ast(p, curcpu()->ci_want_resched);
	}

}

void
trap(int type, struct trapframe *frame)
{
	struct proc *p = curproc;
	vaddr_t va;
	struct vm_map *map;
	struct vmspace *vm;
	register vm_prot_t access_type;
	register pa_space_t space;
	union sigval sv;
	u_int opcode;
	int ret, trapnum;
	const char *tts;
#ifdef DIAGNOSTIC
	int oldcpl = curcpu()->ci_cpl;
#endif

	trapnum = type & ~T_USER;
	opcode = frame->tf_iir;
	if (trapnum <= T_EXCEPTION || trapnum == T_HIGHERPL ||
	    trapnum == T_LOWERPL || trapnum == T_TAKENBR ||
	    trapnum == T_IDEBUG || trapnum == T_PERFMON) {
		va = frame->tf_iioq_head;
		space = frame->tf_iisq_head;
		access_type = PROT_EXEC;
	} else {
		va = frame->tf_ior;
		space = frame->tf_isr;
		if (va == frame->tf_iioq_head)
			access_type = PROT_EXEC;
		else if (inst_store(opcode))
			access_type = PROT_WRITE;
		else
			access_type = PROT_READ;
	}

	if (frame->tf_flags & TFF_LAST)
		p->p_md.md_regs = frame;

	if (trapnum > trap_types)
		tts = "reserved";
	else
		tts = trap_type[trapnum];

#ifdef TRAPDEBUG
	if (trapnum != T_INTERRUPT && trapnum != T_IBREAK)
		db_printf("trap: %x, %s for %x:%x at %x:%x, fl=%x, fp=%p\n",
		    type, tts, space, va, frame->tf_iisq_head,
		    frame->tf_iioq_head, frame->tf_flags, frame);
	else if (trapnum  == T_IBREAK)
		db_printf("trap: break instruction %x:%x at %x:%x, fp=%p\n",
		    break5(opcode), break13(opcode),
		    frame->tf_iisq_head, frame->tf_iioq_head, frame);

	{
		extern int etext;
		if (frame < (struct trapframe *)&etext) {
			printf("trap: bogus frame ptr %p\n", frame);
			goto dead_end;
		}
	}
#endif
	if (trapnum != T_INTERRUPT) {
		uvmexp.traps++;
		mtctl(frame->tf_eiem, CR_EIEM);
	}

	if (type & T_USER)
		refreshcreds(p);

	switch (type) {
	case T_NONEXIST:
	case T_NONEXIST | T_USER:
		/* we've got screwed up by the central scrutinizer */
		printf("trap: elvis has just left the building!\n");
		goto dead_end;

	case T_RECOVERY:
	case T_RECOVERY | T_USER:
		/* XXX will implement later */
		printf("trap: handicapped");
		goto dead_end;

#ifdef DIAGNOSTIC
	case T_EXCEPTION:
		panic("FPU/SFU emulation botch");

		/* these just can't happen ever */
	case T_PRIV_OP:
	case T_PRIV_REG:
		/* these just can't make it to the trap() ever */
	case T_HPMC:
	case T_HPMC | T_USER:
#endif
	case T_IBREAK:
	case T_DATALIGN:
	case T_DBREAK:
	dead_end:
#ifdef DDB
		if (db_ktrap(type, va, frame)) {
			if (type == T_IBREAK) {
				/* skip break instruction */
				frame->tf_iioq_head = frame->tf_iioq_tail;
				frame->tf_iioq_tail += 4;
			}
			return;
		}
#else
		if (type == T_DATALIGN || type == T_DPROT)
			panic ("trap: %s at 0x%lx", tts, va);
		else
			panic ("trap: no debugger for \"%s\" (%d)", tts, type);
#endif
		break;

	case T_IBREAK | T_USER:
	case T_DBREAK | T_USER: {
		int code = TRAP_BRKPT;

#ifdef PTRACE
		KERNEL_LOCK();
		ss_clear_breakpoints(p);
		if (opcode == SSBREAKPOINT)
			code = TRAP_TRACE;
		KERNEL_UNLOCK();
#endif
		/* pass to user debugger */
		sv.sival_int = va;
		trapsignal(p, SIGTRAP, type & ~T_USER, code, sv);
		}
		break;

#ifdef PTRACE
	case T_TAKENBR | T_USER:
		KERNEL_LOCK();
		ss_clear_breakpoints(p);
		KERNEL_UNLOCK();
		/* pass to user debugger */
		sv.sival_int = va;
		trapsignal(p, SIGTRAP, type & ~T_USER, TRAP_TRACE, sv);
		break;
#endif

	case T_EXCEPTION | T_USER: {
		struct hppa_fpstate *hfp;
		u_int64_t *fpp;
		u_int32_t *pex;
		int i, flt;

		hfp = (struct hppa_fpstate *)frame->tf_cr30;
		fpp = (u_int64_t *)&hfp->hfp_regs;

		pex = (u_int32_t *)&fpp[0];
		for (i = 0, pex++; i < 7 && !*pex; i++, pex++)
			;
		flt = 0;
		if (i < 7) {
			u_int32_t stat = HPPA_FPU_OP(*pex);
			if (stat & HPPA_FPU_UNMPL)
				flt = FPE_FLTINV;
			else if (stat & (HPPA_FPU_V << 1))
				flt = FPE_FLTINV;
			else if (stat & (HPPA_FPU_Z << 1))
				flt = FPE_FLTDIV;
			else if (stat & (HPPA_FPU_I << 1))
				flt = FPE_FLTRES;
			else if (stat & (HPPA_FPU_O << 1))
				flt = FPE_FLTOVF;
			else if (stat & (HPPA_FPU_U << 1))
				flt = FPE_FLTUND;
			/* still left: under/over-flow w/ inexact */

			/* cleanup exceptions (XXX deliver all ?) */
			while (i++ < 7)
				*pex++ = 0;
		}
		/* reset the trap flag, as if there was none */
		fpp[0] &= ~(((u_int64_t)HPPA_FPU_T) << 32);

		sv.sival_int = va;
		trapsignal(p, SIGFPE, type & ~T_USER, flt, sv);
		}
		break;

	case T_EMULATION:
		panic("trap: emulation trap in the kernel");
		break;

	case T_EMULATION | T_USER:
		sv.sival_int = va;
		trapsignal(p, SIGILL, type & ~T_USER, ILL_COPROC, sv);
		break;

	case T_OVERFLOW | T_USER:
		sv.sival_int = va;
		trapsignal(p, SIGFPE, type & ~T_USER, FPE_INTOVF, sv);
		break;

	case T_CONDITION | T_USER:
		sv.sival_int = va;
		trapsignal(p, SIGFPE, type & ~T_USER, FPE_INTDIV, sv);
		break;

	case T_PRIV_OP | T_USER:
		sv.sival_int = va;
		trapsignal(p, SIGILL, type & ~T_USER, ILL_PRVOPC, sv);
		break;

	case T_PRIV_REG | T_USER:
		/*
		 * On PCXS processors, attempting to read control registers
		 * cr26 and cr27 from userland causes a ``privileged register''
		 * trap.  Later processors do not restrict read accesses to
		 * these registers.
		 */
		if (cpu_type == hpcxs &&
		    (opcode & (0xfc1fffe0 | (0x1e << 21))) ==
		     (0x000008a0 | (0x1a << 21))) { /* mfctl %cr{26,27}, %r# */
			register_t cr;

			if (((opcode >> 21) & 0x1f) == 27)
				cr = frame->tf_cr27;	/* cr27 */
			else
				cr = 0;			/* cr26 */
			frame_regmap(frame, opcode & 0x1f) = cr;
			frame->tf_ipsw |= PSL_N;
		} else {
			sv.sival_int = va;
			trapsignal(p, SIGILL, type & ~T_USER, ILL_PRVREG, sv);
		}
		break;

		/* these should never got here */
	case T_HIGHERPL | T_USER:
	case T_LOWERPL | T_USER:
	case T_DATAPID | T_USER:
		sv.sival_int = va;
		trapsignal(p, SIGSEGV, access_type, SEGV_ACCERR, sv);
		break;

	/*
	 * On PCXS processors, traps T_DATACC, T_DATAPID and T_DATALIGN
	 * are shared.  We need to sort out the unaligned access situation
	 * first, before handling this trap as T_DATACC.
	 */
	case T_DPROT | T_USER:
		if (cpu_type == hpcxs) {
			if (pcxs_unaligned(opcode, va))
				goto datalign_user;
			else
				goto datacc;
		}

		sv.sival_int = va;
		trapsignal(p, SIGSEGV, access_type, SEGV_ACCERR, sv);
		break;

	case T_ITLBMISSNA:
	case T_ITLBMISSNA | T_USER:
	case T_DTLBMISSNA:
	case T_DTLBMISSNA | T_USER:
		if (space == HPPA_SID_KERNEL)
			map = kernel_map;
		else {
			vm = p->p_vmspace;
			map = &vm->vm_map;
		}

		if ((opcode & 0xfc003fc0) == 0x04001340) {
			/* lpa failure case */
			frame_regmap(frame, opcode & 0x1f) = 0;
			frame->tf_ipsw |= PSL_N;
		} else if ((opcode & 0xfc001f80) == 0x04001180) {
			int pl;

			/* dig probe[rw]i? insns */
			if (opcode & 0x2000)
				pl = (opcode >> 16) & 3;
			else
				pl = frame_regmap(frame,
				    (opcode >> 16) & 0x1f) & 3;

			KERNEL_LOCK();

			if ((type & T_USER && space == HPPA_SID_KERNEL) ||
			    (frame->tf_iioq_head & 3) != pl ||
			    (type & T_USER && va >= VM_MAXUSER_ADDRESS) ||
			    uvm_fault(map, trunc_page(va), 0,
			     opcode & 0x40? PROT_WRITE : PROT_READ)) {
				frame_regmap(frame, opcode & 0x1f) = 0;
				frame->tf_ipsw |= PSL_N;
			}

			KERNEL_UNLOCK();
		} else if (type & T_USER) {
			sv.sival_int = va;
			trapsignal(p, SIGILL, type & ~T_USER, ILL_ILLTRP, sv);
		} else
			panic("trap: %s @ 0x%lx:0x%lx for 0x%x:0x%lx irr 0x%08x",
			    tts, frame->tf_iisq_head, frame->tf_iioq_head,
			    space, va, opcode);
		break;

	case T_IPROT | T_USER:
	case T_TLB_DIRTY:
	case T_TLB_DIRTY | T_USER:
	case T_DATACC:
	case T_DATACC | T_USER:
datacc:
	case T_ITLBMISS:
	case T_ITLBMISS | T_USER:
	case T_DTLBMISS:
	case T_DTLBMISS | T_USER:
		if (type & T_USER) {
			if (!uvm_map_inentry(p, &p->p_spinentry, PROC_STACK(p),
			    "[%s]%d/%d sp=%lx inside %lx-%lx: not MAP_STACK\n",
			    uvm_map_inentry_sp, p->p_vmspace->vm_map.sserial))
				goto out;
		}

		/*
		 * it could be a kernel map for exec_map faults
		 */
		if (space == HPPA_SID_KERNEL)
			map = kernel_map;
		else {
			vm = p->p_vmspace;
			map = &vm->vm_map;
		}

		/*
		 * user faults out of user addr space are always a fail,
		 * this happens on va >= VM_MAXUSER_ADDRESS, where
		 * space id will be zero and therefore cause
		 * a misbehave lower in the code.
		 *
		 * also check that faulted space id matches the curproc.
		 */
		if ((type & T_USER && va >= VM_MAXUSER_ADDRESS) ||
		   (type & T_USER && map->pmap->pm_space != space)) {
			sv.sival_int = va;
			trapsignal(p, SIGSEGV, access_type, SEGV_MAPERR, sv);
			break;
		}

		KERNEL_LOCK();
		ret = uvm_fault(map, trunc_page(va), 0, access_type);
		KERNEL_UNLOCK();

		/*
		 * If this was a stack access we keep track of the maximum
		 * accessed stack size.  Also, if uvm_fault gets a protection
		 * failure it is due to accessing the stack region outside
		 * the current limit and we need to reflect that as an access
		 * error.
		 */
		if (ret == 0 && space != HPPA_SID_KERNEL)
			uvm_grow(p, va);

		if (ret != 0) {
			if (type & T_USER) {
				int signal, sicode;

				signal = SIGSEGV;
				sicode = SEGV_MAPERR;
				if (ret == EACCES)
					sicode = SEGV_ACCERR;
				if (ret == EIO) {
					signal = SIGBUS;
					sicode = BUS_OBJERR;
				}
				sv.sival_int = va;
				trapsignal(p, signal, access_type, sicode, sv);
			} else {
				if (p && p->p_addr->u_pcb.pcb_onfault) {
					frame->tf_iioq_tail = 4 +
					    (frame->tf_iioq_head =
						p->p_addr->u_pcb.pcb_onfault);
#ifdef DDB
					frame->tf_iir = 0;
#endif
				} else {
					panic("trap: "
					    "uvm_fault(%p, %lx, 0, %d): %d",
					    map, va, access_type, ret);
				}
			}
		}
		break;

	case T_DATAPID:
		/* This should never happen, unless within spcopy() */
		if (p && p->p_addr->u_pcb.pcb_onfault) {
			frame->tf_iioq_tail = 4 +
			    (frame->tf_iioq_head =
				p->p_addr->u_pcb.pcb_onfault);
#ifdef DDB
			frame->tf_iir = 0;
#endif
		} else
			goto dead_end;
		break;

	case T_DATALIGN | T_USER:
datalign_user:
		sv.sival_int = va;
		trapsignal(p, SIGBUS, access_type, BUS_ADRALN, sv);
		break;

	case T_INTERRUPT:
	case T_INTERRUPT | T_USER:
		cpu_intr(frame);
		break;

	case T_CONDITION:
		panic("trap: divide by zero in the kernel");
		break;

	case T_ILLEGAL:
	case T_ILLEGAL | T_USER:
		/* see if it's a SPOP1,,0 */
		if ((opcode & 0xfffffe00) == 0x10000200) {
			frame_regmap(frame, opcode & 0x1f) = 0;
			frame->tf_ipsw |= PSL_N;
			break;
		}
		if (type & T_USER) {
			sv.sival_int = va;
			trapsignal(p, SIGILL, type & ~T_USER, ILL_ILLOPC, sv);
			break;
		}
		/* FALLTHROUGH */

	/*
	 * On PCXS processors, traps T_DATACC, T_DATAPID and T_DATALIGN
	 * are shared.  We need to sort out the unaligned access situation
	 * first, before handling this trap as T_DATACC.
	 */
	case T_DPROT:
		if (cpu_type == hpcxs) {
			if (pcxs_unaligned(opcode, va))
				goto dead_end;
			else
				goto datacc;
		}
		/* FALLTHROUGH to unimplemented */

	case T_LOWERPL:
	case T_IPROT:
	case T_OVERFLOW:
	case T_HIGHERPL:
	case T_TAKENBR:
	case T_POWERFAIL:
	case T_LPMC:
	case T_PAGEREF:
		/* FALLTHROUGH to unimplemented */
	default:
#ifdef TRAPDEBUG
		if (db_ktrap(type, va, frame))
			return;
#endif
		panic("trap: unimplemented \'%s\' (%d)", tts, trapnum);
	}

#ifdef DIAGNOSTIC
	if (curcpu()->ci_cpl != oldcpl)
		printf("WARNING: SPL (%d) NOT LOWERED ON "
		    "TRAP (%d) EXIT\n", curcpu()->ci_cpl, trapnum);
#endif

	if (trapnum != T_INTERRUPT)
		splx(curcpu()->ci_cpl);	/* process softints */

	/*
	 * in case we were interrupted from the syscall gate page
	 * treat this as we were not really running user code no more
	 * for weird things start to happen on return to the userland
	 * and also see a note in locore.S:TLABEL(all)
	 */
	if ((type & T_USER) && !(frame->tf_iisq_head == HPPA_SID_KERNEL &&
	    (frame->tf_iioq_head & ~PAGE_MASK) == SYSCALLGATE)) {
		ast(p);
out:
		userret(p);
	}
}

void
child_return(void *arg)
{
	struct proc *p = (struct proc *)arg;
	struct trapframe *tf = p->p_md.md_regs;

	/*
	 * Set up return value registers as libc:fork() expects
	 */
	tf->tf_ret0 = 0;
	tf->tf_t1 = 0;		/* errno */

	KERNEL_UNLOCK();

	ast(p);

	mi_child_return(p);
}

#ifdef PTRACE

#include <sys/ptrace.h>

int	ss_get_value(struct proc *p, vaddr_t addr, u_int *value);
int	ss_put_value(struct proc *p, vaddr_t addr, u_int value);

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

void
ss_clear_breakpoints(struct proc *p)
{
	/* Restore original instructions. */
	if (p->p_md.md_bpva != 0) {
		ss_put_value(p, p->p_md.md_bpva, p->p_md.md_bpsave[0]);
		ss_put_value(p, p->p_md.md_bpva + 4, p->p_md.md_bpsave[1]);
		p->p_md.md_bpva = 0;
	}
}

int
process_sstep(struct proc *p, int sstep)
{
	int error;

	ss_clear_breakpoints(p);

	if (sstep == 0) {
		p->p_md.md_regs->tf_ipsw &= ~PSL_T;
		return (0);
	}

	/*
	 * Don't touch the syscall gateway page.  Instead, insert a
	 * breakpoint where we're supposed to return.
	 */
	if ((p->p_md.md_regs->tf_iioq_tail & ~PAGE_MASK) == SYSCALLGATE)
		p->p_md.md_bpva = p->p_md.md_regs->tf_r31 & ~HPPA_PC_PRIV_MASK;
	else
		p->p_md.md_bpva = p->p_md.md_regs->tf_iioq_tail & ~HPPA_PC_PRIV_MASK;

	/*
	 * Insert two breakpoint instructions; the first one might be
	 * nullified.  Of course we need to save two instruction
	 * first.
	 */

	error = ss_get_value(p, p->p_md.md_bpva, &p->p_md.md_bpsave[0]);
	if (error)
		return (error);
	error = ss_get_value(p, p->p_md.md_bpva + 4, &p->p_md.md_bpsave[1]);
	if (error)
		return (error);

	error = ss_put_value(p, p->p_md.md_bpva, SSBREAKPOINT);
	if (error)
		return (error);
	error = ss_put_value(p, p->p_md.md_bpva + 4, SSBREAKPOINT);
	if (error)
		return (error);

	if ((p->p_md.md_regs->tf_iioq_tail & ~PAGE_MASK) != SYSCALLGATE)
		p->p_md.md_regs->tf_ipsw |= PSL_T;
	else
		p->p_md.md_regs->tf_ipsw &= ~PSL_T;

	return (0);
}

#endif	/* PTRACE */

void	syscall(struct trapframe *frame);

/*
 * call actual syscall routine
 */
void
syscall(struct trapframe *frame)
{
	struct proc *p = curproc;
	const struct sysent *callp = sysent;
	int code, argsize, argoff, error;
	register_t args[8], rval[2];
#ifdef DIAGNOSTIC
	int oldcpl = curcpu()->ci_cpl;
#endif

	uvmexp.syscalls++;

	if (!USERMODE(frame->tf_iioq_head))
		panic("syscall");

	p->p_md.md_regs = frame;

	argoff = 4;
	code = frame->tf_t1;
	args[0] = frame->tf_arg0;
	args[1] = frame->tf_arg1;
	args[2] = frame->tf_arg2;
	args[3] = frame->tf_arg3;

	// XXX out of range stays on syscall0, which we assume is enosys
	if (code > 0 && code < SYS_MAXSYSCALL)
		callp += code;

	if ((argsize = callp->sy_argsize)) {
		register_t *s, *e, t;
		int i;

		argsize -= argoff * 4;
		if (argsize > 0) {
			i = argsize / 4;
			if ((error = copyin((void *)(frame->tf_sp +
			    HPPA_FRAME_ARG(4 + i - 1)), args + argoff,
			    argsize)))
				goto bad;
			/* reverse the args[] entries */
			s = args + argoff;
			e = s + i - 1;
			while (s < e) {
				t = *s;
				*s = *e;
				*e = t;
				s++, e--;
			}
		}

		/*
		 * System calls with 64-bit arguments need a word swap
		 * due to the order of the arguments on the stack.
		 */
		i = 0;
		switch (code) {
		case SYS_lseek:
		case SYS_truncate:
		case SYS_ftruncate:	i = 2;	break;
		case SYS_preadv:
		case SYS_pwritev:
		case SYS_pread:
		case SYS_pwrite:	i = 4;	break;
		case SYS_mquery:
		case SYS_mmap:		i = 6;	break;
		}

		if (i) {
			t = args[i];
			args[i] = args[i + 1];
			args[i + 1] = t;
		}
	}

	rval[0] = 0;
	rval[1] = frame->tf_ret1;

	error = mi_syscall(p, code, callp, args, rval);

	switch (error) {
	case 0:
		frame->tf_ret0 = rval[0];
		frame->tf_ret1 = rval[1];
		frame->tf_t1 = 0;
		break;
	case ERESTART:
		frame->tf_iioq_head -= 12;
		frame->tf_iioq_tail -= 12;
	case EJUSTRETURN:
		break;
	default:
	bad:
		frame->tf_t1 = error;
		frame->tf_ret0 = error;
		frame->tf_ret1 = 0;
		break;
	}

	ast(p);		// XXX why?

	mi_syscall_return(p, code, error, rval);

#ifdef DIAGNOSTIC
	if (curcpu()->ci_cpl != oldcpl) {
		printf("WARNING: SPL (0x%x) NOT LOWERED ON "
		    "syscall(0x%x, 0x%lx, 0x%lx, 0x%lx...) EXIT, PID %d\n",
		    curcpu()->ci_cpl, code, args[0], args[1], args[2],
		    p->p_p->ps_pid);
		curcpu()->ci_cpl = oldcpl;
	}
#endif
	splx(curcpu()->ci_cpl);	/* process softints */
}

/*
 * Decide if opcode `opcode' accessing virtual address `va' caused an
 * unaligned trap. Returns zero if the access is correctly aligned.
 * Used on PCXS processors to sort out exception causes.
 */
int
pcxs_unaligned(u_int opcode, vaddr_t va)
{
	u_int mbz_bits;

	/*
	 * Exit early if the va is obviously aligned enough.
	 */
	if ((va & 0x0f) == 0)
		return 0;

	mbz_bits = 0;

	/*
	 * Only load and store instructions can cause unaligned access.
	 * There are three opcode patterns to look for:
	 * - canonical load/store
	 * - load/store short or indexed
	 * - coprocessor load/store
	 */

	if ((opcode & 0xd0000000) == 0x40000000) {
		switch ((opcode >> 26) & 0x03) {
		case 0x00:	/* ldb, stb */
			mbz_bits = 0x00;
			break;
		case 0x01:	/* ldh, sth */
			mbz_bits = 0x01;
			break;
		case 0x02:	/* ldw, stw */
		case 0x03:	/* ldwm, stwm */
			mbz_bits = 0x03;
			break;
		}
	} else

	if ((opcode & 0xfc000000) == 0x0c000000) {
		switch ((opcode >> 6) & 0x0f) {
		case 0x01:	/* ldhx, ldhs */
			mbz_bits = 0x01;
			break;
		case 0x02:	/* ldwx, ldws */
			mbz_bits = 0x03;
			break;
		case 0x07:	/* ldcwx, ldcws */
			mbz_bits = 0x0f;
			break;
		case 0x09:
			if ((opcode & (1 << 12)) != 0)	/* sths */
				mbz_bits = 0x01;
			break;
		case 0x0a:
			if ((opcode & (1 << 12)) != 0)	/* stws */
				mbz_bits = 0x03;
			break;
		}
	} else

	if ((opcode & 0xf4000000) == 0x24000000) {
		if ((opcode & (1 << 27)) != 0) {
			/* cldwx, cstwx, cldws, cstws */
			mbz_bits = 0x03;
		} else {
			/* clddx, cstdx, cldds, cstds */
			mbz_bits = 0x07;
		}
	}

	return (va & mbz_bits);
}
