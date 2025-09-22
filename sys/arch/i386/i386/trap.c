/*	$OpenBSD: trap.c,v 1.165 2024/09/04 07:54:51 mglocker Exp $	*/
/*	$NetBSD: trap.c,v 1.95 1996/05/05 06:50:02 mycroft Exp $	*/

/*-
 * Copyright (c) 1995 Charles M. Hannum.  All rights reserved.
 * Copyright (c) 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * the University of Utah, and William Jolitz.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
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
 *	@(#)trap.c	7.4 (Berkeley) 5/13/91
 */

/*
 * 386 Trap and System call handling
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/signalvar.h>
#include <sys/user.h>
#include <sys/signal.h>
#include <sys/syscall.h>
#include <sys/syscall_mi.h>

#include <uvm/uvm_extern.h>

#include <machine/cpufunc.h>
#include <machine/psl.h>
#include <machine/trap.h>
#ifdef DDB
#include <machine/db_machdep.h>
#endif

#include "isa.h"

int upageflttrap(struct trapframe *, uint32_t);
int kpageflttrap(struct trapframe *, uint32_t);
void trap(struct trapframe *);
void ast(struct trapframe *);
void syscall(struct trapframe *);

char	*trap_type[] = {
	"privileged instruction fault",		/*  0 T_PRIVINFLT */
	"breakpoint trap",			/*  1 T_BPTFLT */
	"arithmetic trap",			/*  2 T_ARITHTRAP */
	"reserved trap",			/*  3 T_RESERVED */
	"protection fault",			/*  4 T_PROTFLT */
	"trace trap",				/*  5 T_TRCTRAP */
	"page fault",				/*  6 T_PAGEFLT */
	"alignment fault",			/*  7 T_ALIGNFLT */
	"integer divide fault",			/*  8 T_DIVIDE */
	"non-maskable interrupt",		/*  9 T_NMI */
	"overflow trap",			/* 10 T_OFLOW */
	"bounds check fault",			/* 11 T_BOUND */
	"FPU not available fault",		/* 12 T_DNA */
	"double fault",				/* 13 T_DOUBLEFLT */
	"FPU operand fetch fault",		/* 14 T_FPOPFLT (![P]Pro) */
	"invalid TSS fault",			/* 15 T_TSSFLT */
	"segment not present fault",		/* 16 T_SEGNPFLT */
	"stack fault",				/* 17 T_STKFLT */
	"machine check",			/* 18 T_MACHK ([P]Pro) */
	"SIMD FP fault",			/* 19 T_XFTRAP */
};
int	trap_types = sizeof trap_type / sizeof trap_type[0];

#ifdef DEBUG
int	trapdebug = 0;
#endif

static inline int
pgex2access(int pgex)
{
	if (pgex & PGEX_W)
		return PROT_WRITE;
	else if (pgex & PGEX_I)
		return PROT_EXEC;
	return PROT_READ;
}

/*
 * upageflttrap(frame, usermode): page fault handler
 * Returns non-zero if the fault was handled (possibly by generating
 * a signal).  Returns zero, possibly still holding the kernel lock,
 * if something was so broken that we should panic.
 */
int
upageflttrap(struct trapframe *frame, uint32_t cr2)
{
	struct proc *p = curproc;
	vaddr_t va = trunc_page((vaddr_t)cr2);
	vm_prot_t access_type = pgex2access(frame->tf_err);
	union sigval sv;
	int signal, sicode, error;

	/*
	 * cpu_pae is true if system has PAE + NX.
	 * If NX is not enabled, we can't distinguish between PROT_READ
	 * and PROT_EXEC access, so try both.
	 */
	error = uvm_fault(&p->p_vmspace->vm_map, va, 0, access_type);
	if (cpu_pae == 0 && error == EACCES && access_type == PROT_READ)
		error = uvm_fault(&p->p_vmspace->vm_map, va, 0, PROT_EXEC);

	if (error == 0) {
		uvm_grow(p, va);
		return 1;
	}

	signal = SIGSEGV;
	sicode = SEGV_MAPERR;
	if (error == ENOMEM) {
		printf("UVM: pid %d (%s), uid %d killed:"
		    " out of swap\n", p->p_p->ps_pid, p->p_p->ps_comm,
		    p->p_ucred ? (int)p->p_ucred->cr_uid : -1);
		signal = SIGKILL;
	} else {
		if (error == EACCES)
			sicode = SEGV_ACCERR;
		else if (error == EIO) {
			signal = SIGBUS;
			sicode = BUS_OBJERR;
		}
	}
	sv.sival_ptr = (void *)cr2;
	trapsignal(p, signal, T_PAGEFLT, sicode, sv);
	return 1;
}

/*
 * kpageflttrap(frame, usermode): page fault handler
 * Returns non-zero if the fault was handled (possibly by generating
 * a signal).  Returns zero, possibly still holding the kernel lock,
 * if something was so broken that we should panic.
 */
int
kpageflttrap(struct trapframe *frame, uint32_t cr2)
{
	struct proc *p = curproc;
	struct pcb *pcb;
	vaddr_t va = trunc_page((vaddr_t)cr2);
	struct vm_map *map;
	vm_prot_t access_type = pgex2access(frame->tf_err);
	caddr_t onfault;
	int error;

	if (p == NULL || p->p_addr == NULL || p->p_vmspace == NULL)
		return 0;

	pcb = &p->p_addr->u_pcb;

	/* This will only trigger if SMEP is enabled */
	if (cr2 <= VM_MAXUSER_ADDRESS && frame->tf_err & PGEX_I)
		panic("attempt to execute user address %p "
		    "in supervisor mode", (void *)cr2);

	/* This will only trigger if SMAP is enabled */
	if (pcb->pcb_onfault == NULL && cr2 <= VM_MAXUSER_ADDRESS &&
	    frame->tf_err & PGEX_P)
		panic("attempt to access user address %p "
		    "in supervisor mode", (void *)cr2);

	/*
	 * It is only a kernel address space fault iff:
	 *	1. (type & T_USER) == 0  and
	 *	2. pcb_onfault not set or
	 *	3. pcb_onfault set but supervisor space fault
	 * The last can occur during an exec() copyin where the
	 * argument space is lazy-allocated.
	 */
	map = &p->p_vmspace->vm_map;
	if (va >= VM_MIN_KERNEL_ADDRESS)
		map = kernel_map;

	if (curcpu()->ci_inatomic == 0 || map == kernel_map) {
		onfault = pcb->pcb_onfault;
		pcb->pcb_onfault = NULL;
		error = uvm_fault(map, va, 0, access_type);
		pcb->pcb_onfault = onfault;

		if (error == 0 && map != kernel_map)
			uvm_grow(p, va);
	} else
		error = EFAULT;

	if (error) {
		if (pcb->pcb_onfault == NULL) {
			/* bad memory access in the kernel */
			panic("uvm_fault(%p, 0x%lx, 0, %d) -> %x",
			    map, va, access_type, error);
			return 0;
		}
		frame->tf_eip = (u_int32_t)pcb->pcb_onfault;
	}
	return 1;
}

/*
 * trap(frame):
 *	Exception, fault, and trap interface to BSD kernel. This
 * common code is called from assembly language IDT gate entry
 * routines that prepare a suitable stack frame, and restore this
 * frame after the exception has been processed.
 */
void
trap(struct trapframe *frame)
{
	struct proc *p = curproc;
	int type = frame->tf_trapno;
	struct pcb *pcb = NULL;
	extern char resume_iret[], resume_pop_ds[], resume_pop_es[],
	    resume_pop_fs[], resume_pop_gs[];
	struct trapframe *vframe;
	int resume;
	union sigval sv;
	vaddr_t gdt_cs = SEGDESC_LIMIT(curcpu()->ci_gdt[GUCODE_SEL].sd);
	uint32_t cr2 = rcr2();

	uvmexp.traps++;

#ifdef DEBUG
	if (trapdebug) {
		printf("trap %d code %x eip %x cs %x eflags %x cr2 %x cpl %x\n",
		    frame->tf_trapno, frame->tf_err, frame->tf_eip,
		    frame->tf_cs, frame->tf_eflags, cr2, lapic_tpr);
		printf("curproc %p\n", curproc);
	}
#endif
#ifdef DIAGNOSTIC
	if (curcpu()->ci_feature_sefflags_ebx & SEFF0EBX_SMAP) {
		u_int ef = read_eflags();
		if (ef & PSL_AC) {
			write_eflags(ef & ~PSL_AC);
			panic("%s: AC set on entry", "trap");
		}
	}
#endif

	if (!KERNELMODE(frame->tf_cs, frame->tf_eflags)) {
		type |= T_USER;
		p->p_md.md_regs = frame;
		refreshcreds(p);
	}

	switch (type) {

	/* trace trap */
	case T_PROTFLT:
	case T_SEGNPFLT:
	case T_ALIGNFLT:
		/* Check for copyin/copyout fault. */
		if (p && p->p_addr) {
			pcb = &p->p_addr->u_pcb;
			if (pcb->pcb_onfault != 0) {
				frame->tf_eip = (int)pcb->pcb_onfault;
				return;
			}
		}

		/*
		 * Check for failure during return to user mode.
		 *
		 * We do this by looking at the instruction we faulted on.  The
		 * specific instructions we recognize only happen when
		 * returning from a trap, syscall, or interrupt.
		 *
		 * XXX
		 * The heuristic used here will currently fail for the case of
		 * one of the 2 pop instructions faulting when returning from a
		 * a fast interrupt.  This should not be possible.  It can be
		 * fixed by rearranging the trap frame so that the stack format
		 * at this point is the same as on exit from a `slow'
		 * interrupt.
		 */
		switch (*(u_char *)frame->tf_eip) {
		case 0xcf:	/* iret */
			vframe = (void *)((int)&frame->tf_esp -
			    offsetof(struct trapframe, tf_eip));
			resume = (int)resume_iret;
			break;
		case 0x1f:	/* popl %ds */
			vframe = (void *)((int)&frame->tf_esp -
			    offsetof(struct trapframe, tf_ds));
			resume = (int)resume_pop_ds;
			break;
		case 0x07:	/* popl %es */
			vframe = (void *)((int)&frame->tf_esp -
			    offsetof(struct trapframe, tf_es));
			resume = (int)resume_pop_es;
			break;
		case 0x0f:	/* 0x0f prefix */
			switch (*(u_char *)(frame->tf_eip + 1)) {
			case 0xa1:		/* popl %fs */
				vframe = (void *)((int)&frame->tf_esp -
				    offsetof(struct trapframe, tf_fs));
				resume = (int)resume_pop_fs;
				break;
			case 0xa9:		/* popl %gs */
				vframe = (void *)((int)&frame->tf_esp -
				    offsetof(struct trapframe, tf_gs));
				resume = (int)resume_pop_gs;
				break;
			default:
				goto we_re_toast;
			}
			break;
		default:
			goto we_re_toast;
		}
		if (KERNELMODE(vframe->tf_cs, vframe->tf_eflags))
			goto we_re_toast;

		frame->tf_eip = resume;
		return;

	case T_PROTFLT|T_USER:		/* protection fault */
		/* If pmap_exec_fixup does something, let's retry the trap. */
		if (cpu_pae == 0 &&
		    pmap_exec_fixup(&p->p_vmspace->vm_map, frame, gdt_cs,
		    &p->p_addr->u_pcb))
			goto out;

		sv.sival_int = frame->tf_eip;
		trapsignal(p, SIGSEGV, type &~ T_USER, SEGV_MAPERR, sv);
		goto out;

	case T_TSSFLT|T_USER:
		sv.sival_int = frame->tf_eip;
		trapsignal(p, SIGBUS, type &~ T_USER, BUS_OBJERR, sv);
		goto out;

	case T_SEGNPFLT|T_USER:
	case T_STKFLT|T_USER:
		sv.sival_int = frame->tf_eip;
		trapsignal(p, SIGSEGV, type &~ T_USER, SEGV_MAPERR, sv);
		goto out;

	case T_ALIGNFLT|T_USER:
		sv.sival_int = frame->tf_eip;
		trapsignal(p, SIGBUS, type &~ T_USER, BUS_ADRALN, sv);
		goto out;

	case T_PRIVINFLT|T_USER:	/* privileged instruction fault */
		sv.sival_int = frame->tf_eip;
		trapsignal(p, SIGILL, type &~ T_USER, ILL_PRVOPC, sv);
		goto out;

	case T_FPOPFLT|T_USER:		/* coprocessor operand fault */
		sv.sival_int = frame->tf_eip;
		trapsignal(p, SIGILL, type &~ T_USER, ILL_COPROC, sv);
		goto out;

	case T_DNA|T_USER: {
		printf("pid %d killed due to lack of floating point\n",
		    p->p_p->ps_pid);
		sv.sival_int = frame->tf_eip;
		trapsignal(p, SIGKILL, type &~ T_USER, FPE_FLTINV, sv);
		goto out;
	}

	case T_BOUND|T_USER:
		sv.sival_int = frame->tf_eip;
		trapsignal(p, SIGFPE, type &~ T_USER, FPE_FLTSUB, sv);
		goto out;
	case T_OFLOW|T_USER:
		sv.sival_int = frame->tf_eip;
		trapsignal(p, SIGFPE, type &~ T_USER, FPE_INTOVF, sv);
		goto out;
	case T_DIVIDE|T_USER:
		sv.sival_int = frame->tf_eip;
		trapsignal(p, SIGFPE, type &~ T_USER, FPE_INTDIV, sv);
		goto out;

	case T_ARITHTRAP|T_USER:
		sv.sival_int = frame->tf_eip;
		trapsignal(p, SIGFPE, frame->tf_err, FPE_INTOVF, sv);
		goto out;

	case T_XFTRAP|T_USER:
		npxtrap(frame);
		goto out;

	case T_PAGEFLT:
		if (kpageflttrap(frame, cr2))
			return;
		goto we_re_toast;

	case T_PAGEFLT|T_USER: {	/* page fault */
		if (!uvm_map_inentry(p, &p->p_spinentry, PROC_STACK(p),
		    "[%s]%d/%d sp=%lx inside %lx-%lx: not MAP_STACK\n",
		    uvm_map_inentry_sp, p->p_vmspace->vm_map.sserial))
			goto out;
		if (upageflttrap(frame, cr2))
			goto out;
		goto we_re_toast;
	}

#if 0  /* Should this be left out?  */
#if !defined(DDB)
	/* XXX need to deal with this when DDB is present, too */
	case T_TRCTRAP: /* kernel trace trap; someone single stepping lcall's */
			/* syscall has to turn off the trace bit itself */
		return;
#endif
#endif

	case T_BPTFLT|T_USER:		/* bpt instruction fault */
		sv.sival_int = cr2;
		trapsignal(p, SIGTRAP, type &~ T_USER, TRAP_BRKPT, sv);
		break;
	case T_TRCTRAP|T_USER:		/* trace trap */
		sv.sival_int = cr2;
		trapsignal(p, SIGTRAP, type &~ T_USER, TRAP_TRACE, sv);
		break;

#if NISA > 0
	case T_NMI:
	case T_NMI|T_USER:
#ifdef DDB
		/* NMI can be hooked up to a pushbutton for debugging */
		printf ("NMI ... going to debugger\n");
		if (db_ktrap(type, 0, frame))
			return;
#endif
		/* machine/parity/power fail/"kitchen sink" faults */
		if (isa_nmi() == 0)
			return;
		else
			goto we_re_toast;
#endif

	case T_TRCTRAP:
#ifndef DDB
		return;	/* Just return if no kernel debugger */
#endif
		/* FALLTHROUGH */
	default:
	we_re_toast:
#ifdef DDB
		if (db_ktrap(type, frame->tf_err, frame))
			return;
#endif
		if (frame->tf_trapno < trap_types)
			printf("fatal %s (%d)", trap_type[frame->tf_trapno],
			    frame->tf_trapno);
		else
			printf("unknown trap %d", frame->tf_trapno);
		printf(" in %s mode\n", (type & T_USER) ? "user" : "supervisor");
		printf("trap type %d code %x eip %x cs %x eflags %x cr2 %x cpl %x\n",
		    type, frame->tf_err, frame->tf_eip, frame->tf_cs,
		    frame->tf_eflags, cr2, lapic_tpr);

		panic("trap type %d, code=%x, pc=%x",
		    type, frame->tf_err, frame->tf_eip);
		/*NOTREACHED*/
	}

	if ((type & T_USER) == 0)
		return;
out:
	userret(p);
}


/*
 * ast(frame):
 *	AST handler.  This is called from assembly language stubs when
 *	returning to userspace after a syscall, trap, or interrupt.
 */
void
ast(struct trapframe *frame)
{
	struct proc *p = curproc;

	uvmexp.traps++;
	KASSERT(!KERNELMODE(frame->tf_cs, frame->tf_eflags));
	p->p_md.md_regs = frame;
	refreshcreds(p);
	uvmexp.softs++;
	mi_ast(p, curcpu()->ci_want_resched);
	userret(p);
}


/*
 * syscall(frame):
 *	System call request from POSIX system call gate interface to kernel.
 */
void
syscall(struct trapframe *frame)
{
	caddr_t params;
	const struct sysent *callp = sysent;
	struct proc *p = curproc;
	int error;
	register_t code, args[8], rval[2];
#ifdef DIAGNOSTIC
	int ocpl = lapic_tpr;
#endif
	short argsize;

	uvmexp.syscalls++;
#ifdef DIAGNOSTIC
	if (!USERMODE(frame->tf_cs, frame->tf_eflags))
		panic("syscall");
#endif
#ifdef DIAGNOSTIC
	if (curcpu()->ci_feature_sefflags_ebx & SEFF0EBX_SMAP) {
		u_int ef = read_eflags();
		if (ef & PSL_AC) {
			write_eflags(ef & ~PSL_AC);
			panic("%s: AC set on entry", "syscall");
		}
	}
#endif

	p->p_md.md_regs = frame;

	code = frame->tf_eax;
	// XXX out of range stays on syscall0, which we assume is enosys
	if (code > 0 && code < SYS_MAXSYSCALL)
		callp += code;

	argsize = callp->sy_argsize;
	params = (caddr_t)frame->tf_esp + sizeof(int);
	if (argsize && (error = copyin(params, args, argsize)))
		goto bad;

	rval[0] = 0;
	rval[1] = frame->tf_edx;

	error = mi_syscall(p, code, callp, args, rval);

	switch (error) {
	case 0:
		frame->tf_eax = rval[0];
		frame->tf_edx = rval[1];
		frame->tf_eflags &= ~PSL_C;	/* carry bit */
		break;
	case ERESTART:
		/* Back up over the int$80 (2 bytes) that made the syscall */
		frame->tf_eip -= 2;
		break;
	case EJUSTRETURN:
		/* nothing to do */
		break;
	default:
	bad:
		frame->tf_eax = error;
		frame->tf_eflags |= PSL_C;	/* carry bit */
		break;
	}

	mi_syscall_return(p, code, error, rval);

#ifdef DIAGNOSTIC
	if (lapic_tpr != ocpl) {
		printf("WARNING: SPL (0x%x) NOT LOWERED ON "
		    "syscall(0x%lx, 0x%lx, 0x%lx, 0x%lx...) EXIT, PID %d\n",
		    lapic_tpr, code, args[0], args[1], args[2],
		    p->p_p->ps_pid);
		lapic_tpr = ocpl;
	}
#endif
}

void
child_return(void *arg)
{
	struct proc *p = (struct proc *)arg;
	struct trapframe *tf = p->p_md.md_regs;

	tf->tf_eax = 0;
	tf->tf_eflags &= ~PSL_C;

	KERNEL_UNLOCK();

	mi_child_return(p);
}
