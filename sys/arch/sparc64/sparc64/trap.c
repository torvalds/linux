/*	$OpenBSD: trap.c,v 1.127 2024/11/08 08:43:38 miod Exp $	*/
/*	$NetBSD: trap.c,v 1.73 2001/08/09 01:03:01 eeh Exp $ */

/*
 * Copyright (c) 1996
 *	The President and Fellows of Harvard College. All rights reserved.
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This software was developed by the Computer Systems Engineering group
 * at Lawrence Berkeley Laboratory under DARPA contract BG 91-66 and
 * contributed to Berkeley.
 *
 * All advertising materials mentioning features or use of this software
 * must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Lawrence Berkeley Laboratory.
 *	This product includes software developed by Harvard University.
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
 *	This product includes software developed by Harvard University.
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
 *	@(#)trap.c	8.4 (Berkeley) 9/23/93
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/signalvar.h>
#include <sys/user.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/resource.h>
#include <sys/signal.h>
#include <sys/wait.h>
#include <sys/syscall.h>
#include <sys/syscall_mi.h>
#include <sys/syslog.h>

#include <uvm/uvm_extern.h>

#include <machine/cpu.h>
#include <machine/ctlreg.h>
#include <machine/fsr.h>
#include <machine/trap.h>
#include <machine/instr.h>
#include <machine/pmap.h>

#ifdef DDB
#include <machine/db_machdep.h>
#else
#include <machine/frame.h>
#endif

#include <sparc64/fpu/fpu_extern.h>
#include <sparc64/sparc64/cache.h>

/*
 * Initial FPU state is all registers == all 1s, everything else == all 0s.
 * This makes every floating point register a signalling NaN, with sign bit
 * set, no matter how it is interpreted.  Appendix N of the Sparc V8 document
 * seems to imply that we should do this, and it does make sense.
 */
const struct fpstate initfpstate = {
	{ ~0, ~0, ~0, ~0, ~0, ~0, ~0, ~0, ~0, ~0, ~0, ~0, ~0, ~0, ~0, ~0,
	  ~0, ~0, ~0, ~0, ~0, ~0, ~0, ~0, ~0, ~0, ~0, ~0, ~0, ~0, ~0, ~0 }
};

/*
 * There are more than 100 trap types, but most are unused.
 *
 * Trap type 0 is taken over as an `Asynchronous System Trap'.
 * This is left-over Vax emulation crap that should be fixed.
 *
 * Traps not supported on the spitfire are marked with `*',
 * and additions are marked with `+'
 */
static const char T[] = "*trap";
const char *trap_type[] = {
	/* non-user vectors */
	"ast",			/* 0 */
	"power on reset",	/* 1 */
	"watchdog reset",	/* 2 */
	"externally initiated reset",/*3 */
	"software initiated reset",/* 4 */
	"RED state exception",	/* 5 */
	T, T,			/* 6..7 */
	"instruction access exception",	/* 8 */
	"*instruction MMU miss",/* 9 */
	"instruction access error",/* 0a */
	T, T, T, T, T,		/* 0b..0f */
	"illegal instruction",	/* 10 */
	"privileged opcode",	/* 11 */
	"*unimplemented LDD",	/* 12 */
	"*unimplemented STD",	/* 13 */
	T, T, T, T,		/* 14..17 */
	T, T, T, T, T, T, T, T, /* 18..1f */
	"fp disabled",		/* 20 */
	"fp exception ieee 754",/* 21 */
	"fp exception other",	/* 22 */
	"tag overflow",		/* 23 */
	"clean window",		/* 24 */
	T, T, T,		/* 25..27 -- trap continues */
	"division by zero",	/* 28 */
	"*internal processor error",/* 29 */
	T, T, T, T, T, T,	/* 2a..2f */
	"data access exception",/* 30 */
	"*data access MMU miss",/* 31 */
	"data access error",	/* 32 */
	"*data access protection",/* 33 */
	"mem address not aligned",	/* 34 */
	"LDDF mem address not aligned",/* 35 */
	"STDF mem address not aligned",/* 36 */
	"privileged action",	/* 37 */
	"LDQF mem address not aligned",/* 38 */
	"STQF mem address not aligned",/* 39 */
	T, T, T, T, T, T,	/* 3a..3f */
	"*async data error",	/* 40 */
	"level 1 int",		/* 41 */
	"level 2 int",		/* 42 */
	"level 3 int",		/* 43 */
	"level 4 int",		/* 44 */
	"level 5 int",		/* 45 */
	"level 6 int",		/* 46 */
	"level 7 int",		/* 47 */
	"level 8 int",		/* 48 */
	"level 9 int",		/* 49 */
	"level 10 int",		/* 4a */
	"level 11 int",		/* 4b */
	"level 12 int",		/* 4c */
	"level 13 int",		/* 4d */
	"level 14 int",		/* 4e */
	"level 15 int",		/* 4f */
	T, T, T, T, T, T, T, T, /* 50..57 */
	T, T, T, T, T, T, T, T, /* 58..5f */
	"+interrupt vector",	/* 60 */
	"+PA_watchpoint",	/* 61 */
	"+VA_watchpoint",	/* 62 */
	"+corrected ECC error",	/* 63 */
	"+fast instruction access MMU miss",/* 64 */
	T, T, T,		/* 65..67 -- trap continues */
	"+fast data access MMU miss",/* 68 */
	T, T, T,		/* 69..6b -- trap continues */
	"+fast data access protection",/* 6c */
	T, T, T,		/* 6d..6f -- trap continues */
	T, T, T, T, T, T, T, T, /* 70..77 */
	T, T, T, T, T, T, T, T, /* 78..7f */
	"spill 0 normal",	/* 80 */
	T, T, T,		/* 81..83 -- trap continues */
	"spill 1 normal",	/* 84 */
	T, T, T,		/* 85..87 -- trap continues */
	"spill 2 normal",	/* 88 */
	T, T, T,		/* 89..8b -- trap continues */
	"spill 3 normal",	/* 8c */
	T, T, T,		/* 8d..8f -- trap continues */
	"spill 4 normal",	/* 90 */
	T, T, T,		/* 91..93 -- trap continues */
	"spill 5 normal",	/* 94 */
	T, T, T,		/* 95..97 -- trap continues */
	"spill 6 normal",	/* 98 */
	T, T, T,		/* 99..9b -- trap continues */
	"spill 7 normal",	/* 9c */
	T, T, T,		/* 9c..9f -- trap continues */
	"spill 0 other",	/* a0 */
	T, T, T,		/* a1..a3 -- trap continues */
	"spill 1 other",	/* a4 */
	T, T, T,		/* a5..a7 -- trap continues */
	"spill 2 other",	/* a8 */
	T, T, T,		/* a9..ab -- trap continues */
	"spill 3 other",	/* ac */
	T, T, T,		/* ad..af -- trap continues */
	"spill 4 other",	/* b0 */
	T, T, T,		/* b1..b3 -- trap continues */
	"spill 5 other",	/* b4 */
	T, T, T,		/* b5..b7 -- trap continues */
	"spill 6 other",	/* b8 */
	T, T, T,		/* b9..bb -- trap continues */
	"spill 7 other",	/* bc */
	T, T, T,		/* bc..bf -- trap continues */
	"fill 0 normal",	/* c0 */
	T, T, T,		/* c1..c3 -- trap continues */
	"fill 1 normal",	/* c4 */
	T, T, T,		/* c5..c7 -- trap continues */
	"fill 2 normal",	/* c8 */
	T, T, T,		/* c9..cb -- trap continues */
	"fill 3 normal",	/* cc */
	T, T, T,		/* cd..cf -- trap continues */
	"fill 4 normal",	/* d0 */
	T, T, T,		/* d1..d3 -- trap continues */
	"fill 5 normal",	/* d4 */
	T, T, T,		/* d5..d7 -- trap continues */
	"fill 6 normal",	/* d8 */
	T, T, T,		/* d9..db -- trap continues */
	"fill 7 normal",	/* dc */
	T, T, T,		/* dc..df -- trap continues */
	"fill 0 other",		/* e0 */
	T, T, T,		/* e1..e3 -- trap continues */
	"fill 1 other",		/* e4 */
	T, T, T,		/* e5..e7 -- trap continues */
	"fill 2 other",		/* e8 */
	T, T, T,		/* e9..eb -- trap continues */
	"fill 3 other",		/* ec */
	T, T, T,		/* ed..ef -- trap continues */
	"fill 4 other",		/* f0 */
	T, T, T,		/* f1..f3 -- trap continues */
	"fill 5 other",		/* f4 */
	T, T, T,		/* f5..f7 -- trap continues */
	"fill 6 other",		/* f8 */
	T, T, T,		/* f9..fb -- trap continues */
	"fill 7 other",		/* fc */
	T, T, T,		/* fc..ff -- trap continues */

	/* user (software trap) vectors */
	"syscall",		/* 100 */
	"breakpoint",		/* 101 */
	"zero divide",		/* 102 */
	"flush windows",	/* 103 */
	"clean windows",	/* 104 */
	"range check",		/* 105 */
	"fix align",		/* 106 */
	"integer overflow",	/* 107 */
	"svr4 syscall",		/* 108 */
	"4.4 syscall",		/* 109 */
	"kgdb exec",		/* 10a */
	T, T, T, T, T,		/* 10b..10f */
	T, T, T, T, T, T, T, T,	/* 11a..117 */
	T, T, T, T, T, T, T, T,	/* 118..11f */
	"svr4 getcc",		/* 120 */
	"svr4 setcc",		/* 121 */
	"svr4 getpsr",		/* 122 */
	"svr4 setpsr",		/* 123 */
	"svr4 gethrtime",	/* 124 */
	"svr4 gethrvtime",	/* 125 */
	T,			/* 126 */
	"svr4 gethrestime",	/* 127 */
	T, T, T, T, T, T, T, T, /* 128..12f */
	T, T,			/* 130..131 */
	"get condition codes",	/* 132 */
	"set condition codes",	/* 133 */
	T, T, T, T,		/* 134..137 */
	T, T, T, T, T, T, T, T, /* 138..13f */
	T, T, T, T, T, T, T, T, /* 140..147 */
	T, T, T, T, T, T, T, T, /* 148..14f */
	T, T, T, T, T, T, T, T, /* 150..157 */
	T, T, T, T, T, T, T, T, /* 158..15f */
	T, T, T, T,		/* 160..163 */
	"SVID syscall64",	/* 164 */
	"SPARC Intl syscall64",	/* 165 */
	"OS vendor spec syscall",	/* 166 */
	"HW OEM syscall",	/* 167 */
	"ret from deferred trap",	/* 168 */
};

#define	N_TRAP_TYPES	(sizeof trap_type / sizeof *trap_type)

static inline void share_fpu(struct proc *, struct trapframe *);

void trap(struct trapframe *tf, unsigned type, vaddr_t pc, long tstate);
void data_access_fault(struct trapframe *tf, unsigned type, vaddr_t pc,
	vaddr_t va, vaddr_t sfva, u_long sfsr);
void data_access_error(struct trapframe *tf, unsigned type,
	vaddr_t afva, u_long afsr, vaddr_t sfva, u_long sfsr);
void text_access_fault(struct trapframe *tf, unsigned type,
	vaddr_t pc, u_long sfsr);
void text_access_error(struct trapframe *tf, unsigned type,
	vaddr_t pc, u_long sfsr, vaddr_t afva, u_long afsr);
void syscall(struct trapframe *, register_t code, register_t pc);

int	copyinsn(struct proc *p, vaddr_t uva, int *insn);

/*
 * If someone stole the FPU while we were away, do not enable it
 * on return.  This is not done in userret() above as it must follow
 * the ktrsysret() in syscall().  Actually, it is likely that the
 * ktrsysret should occur before the call to userret.
 *
 * Oh, and don't touch the FPU bit if we're returning to the kernel.
 */
static inline void
share_fpu(struct proc *p, struct trapframe *tf)
{
	if (!(tf->tf_tstate & TSTATE_PRIV) &&
	    (tf->tf_tstate & TSTATE_PEF) && fpproc != p)
		tf->tf_tstate &= ~TSTATE_PEF;
}

/*
 * Called from locore.s trap handling, for non-MMU-related traps.
 * (MMU-related traps go through mem_access_fault, below.)
 */
void
trap(struct trapframe *tf, unsigned type, vaddr_t pc, long tstate)
{
	struct proc *p;
	struct pcb *pcb;
	int pstate = (tstate>>TSTATE_PSTATE_SHIFT);
	u_int64_t s;
	int64_t n;
	union sigval sv;

	sv.sival_ptr = (void *)pc;

	/* This steps the PC over the trap. */
#define	ADVANCE (n = tf->tf_npc, tf->tf_pc = n, tf->tf_npc = n + 4)

	uvmexp.traps++;
	/*
	 * Generally, kernel traps cause a panic.  Any exceptions are
	 * handled early here.
	 */
	if (pstate & PSTATE_PRIV) {
#ifdef DDB
		if (type == T_BREAKPOINT) {
			write_all_windows();
			if (db_ktrap(type, tf)) {
				/* ADVANCE; */
				return;
			}
		}
		if (type == T_PA_WATCHPT || type == T_VA_WATCHPT) {
			if (db_ktrap(type, tf)) {
				/* DDB must turn off watchpoints or something */
				return;
			}
		}
#endif
		/*
		 * The kernel needs to use FPU registers for block
		 * load/store.  If we trap in privileged code, save
		 * the FPU state if there is any and enable the FPU.
		 *
		 * We rely on the kernel code properly enabling the FPU
		 * in %fprs, otherwise we'll hang here trying to enable
		 * the FPU.
		 */
		if (type == T_FPDISABLED) {
			struct proc *newfpproc;

			if (CLKF_INTR((struct clockframe *)tf) || !curproc)
				newfpproc = &proc0;
			else {
				newfpproc = curproc;
				/* force other cpus to give up this fpstate */
				if (newfpproc->p_md.md_fpstate)
					fpusave_proc(newfpproc, 1);
			}
			if (fpproc != newfpproc) {
				s = intr_disable();
				if (fpproc != NULL) {
					/* someone else had it, maybe? */
					savefpstate(fpproc->p_md.md_fpstate);
					fpproc = NULL;
				}
				intr_restore(s);

				/* If we have an allocated fpstate, load it */
				if (newfpproc->p_md.md_fpstate != 0) {
					fpproc = newfpproc;
					loadfpstate(fpproc->p_md.md_fpstate);
				} else
					fpproc = NULL;
			}
			/* Enable the FPU */
			tf->tf_tstate |= (PSTATE_PEF<<TSTATE_PSTATE_SHIFT);
			return;
		}
		if (type != T_SPILL_N_NORM && type != T_FILL_N_NORM)
			goto dopanic;
	}
	if ((p = curproc) == NULL)
		p = &proc0;
	pcb = &p->p_addr->u_pcb;
	p->p_md.md_tf = tf;	/* for ptrace/signals */
	refreshcreds(p);

	switch (type) {

	default:
		if (type < 0x100) {
dopanic:
			panic("trap type 0x%x (%s): pc=%lx npc=%lx pstate=%b",
			    type, type < N_TRAP_TYPES ? trap_type[type] : T,
			    pc, (long)tf->tf_npc, pstate, PSTATE_BITS);
			/* NOTREACHED */
		}
		trapsignal(p, SIGILL, type, ILL_ILLOPC, sv);
		break;

	case T_AST:
		p->p_md.md_astpending = 0;
		uvmexp.softs++;
		mi_ast(p, curcpu()->ci_want_resched);
		break;

	case T_RWRET:
		/*
		 * XXX Flushing the user windows here should not be
		 * necessary, but not doing so here causes corruption
		 * of user windows on sun4v.  Flushing them shouldn't
		 * be much of a performance penalty since we're
		 * probably going to spill any remaining user windows
		 * anyhow.
		 */
		write_user_windows();
		if (rwindow_save(p) == -1) {
			trapsignal(p, SIGILL, 0, ILL_BADSTK, sv);
		}
		break;

	case T_ILLINST:
	{
		union instr ins;

		if (copyinsn(p, pc, &ins.i_int) != 0) {
			/* XXX Can this happen? */
			trapsignal(p, SIGILL, 0, ILL_ILLOPC, sv);
			break;
		}
		if (ins.i_any.i_op == IOP_mem &&
		    (ins.i_op3.i_op3 == IOP3_LDQF ||
		     ins.i_op3.i_op3 == IOP3_STQF ||
		     ins.i_op3.i_op3 == IOP3_LDQFA ||
		     ins.i_op3.i_op3 == IOP3_STQFA)) {
			if (emul_qf(ins.i_int, p, sv, tf))
				ADVANCE;
			break;
		}
		if (ins.i_any.i_op == IOP_reg &&
		    ins.i_op3.i_op3 == IOP3_POPC &&
		    ins.i_op3.i_rs1 == 0) {
			if (emul_popc(ins.i_int, p, sv, tf))
				ADVANCE;
			break;
		}
		trapsignal(p, SIGILL, 0, ILL_ILLOPC, sv);	/* XXX code? */
		break;
	}

	case T_INST_EXCEPT:
	case T_TEXTFAULT:
	case T_PRIVINST:
	case T_PRIVACT:
		trapsignal(p, SIGILL, 0, ILL_ILLOPC, sv);	/* XXX code? */
		break;

	case T_FPDISABLED: {
		struct fpstate *fs = p->p_md.md_fpstate;

		if (fs == NULL) {
			KERNEL_LOCK();
			fs = malloc((sizeof *fs), M_SUBPROC, M_WAITOK);
			*fs = initfpstate;
			p->p_md.md_fpstate = fs;
			KERNEL_UNLOCK();
		}
		if (fpproc != p) {		/* we do not have it */
			/* but maybe another CPU has it? */
			fpusave_proc(p, 1);
			s = intr_disable();
			if (fpproc != NULL)	/* someone else had it */
				savefpstate(fpproc->p_md.md_fpstate);
			loadfpstate(fs);
			fpproc = p;		/* now we do have it */
			intr_restore(s);
			uvmexp.fpswtch++;
		}
		tf->tf_tstate |= (PSTATE_PEF<<TSTATE_PSTATE_SHIFT);
		sparc_wr(fprs, FPRS_FEF, 0);
		break;
	}

	case T_LDQF_ALIGN:
	case T_STQF_ALIGN:
	{
		union instr ins;

		if (copyinsn(p, pc, &ins.i_int) != 0) {
			/* XXX Can this happen? */
			trapsignal(p, SIGILL, 0, ILL_ILLOPC, sv);
			break;
		}
		if (ins.i_any.i_op == IOP_mem &&
		    (ins.i_op3.i_op3 == IOP3_LDQF ||
		     ins.i_op3.i_op3 == IOP3_STQF ||
		     ins.i_op3.i_op3 == IOP3_LDQFA ||
		     ins.i_op3.i_op3 == IOP3_STQFA)) {
			if (emul_qf(ins.i_int, p, sv, tf))
				ADVANCE;
		} else {
			trapsignal(p, SIGILL, 0, ILL_ILLOPC, sv);
		}
		break;
	}

	case T_SPILL_N_NORM:
	case T_FILL_N_NORM:
		/*
		 * We got an alignment trap in the spill/fill handler.
		 *
		 * XXX We really should generate a bus error here, but
		 * we could be on the interrupt stack, and dumping
		 * core from the interrupt stack is not a good idea.
		 * It causes random crashes.
		 */
		KERNEL_LOCK();
		sigexit(p, SIGKILL);
		/* NOTREACHED */
		break;

	case T_ALIGN:
	case T_LDDF_ALIGN:
	case T_STDF_ALIGN:
		/*
		 * If we're busy doing copyin/copyout continue
		 */
		if (p->p_addr->u_pcb.pcb_onfault) {
			tf->tf_pc = (vaddr_t)p->p_addr->u_pcb.pcb_onfault;
			tf->tf_npc = tf->tf_pc + 4;
			break;
		}

		/* XXX sv.sival_ptr should be the fault address! */
		trapsignal(p, SIGBUS, 0, BUS_ADRALN, sv);	/* XXX code? */
		break;

	case T_FP_IEEE_754:
	case T_FP_OTHER:
	{
		union instr ins;

		/*
		 * Clean up after a floating point exception.
		 * fpu_cleanup can (and usually does) modify the
		 * state we save here, so we must `give up' the FPU
		 * chip context.  (The software and hardware states
		 * will not match once fpu_cleanup does its job, so
		 * we must not save again later.)
		 */
		if (p != fpproc)
			panic("fpe without being the FP user");
		s = intr_disable();
		savefpstate(p->p_md.md_fpstate);
		fpproc = NULL;
		intr_restore(s);
		/* tf->tf_psr &= ~PSR_EF; */	/* share_fpu will do this */
		if (type == T_FP_OTHER) {
			/*
			 * Read the faulting instruction;
			 * we might need to emulate it.
			 */
			(void)copyinsn(p, pc, &ins.i_int);
		} else
			ins.i_int = 0;
		ADVANCE;
		fpu_cleanup(p, p->p_md.md_fpstate, ins, sv);
		/* fpu_cleanup posts signals if needed */
		break;
	}

	case T_TAGOF:
	case T_BREAKPOINT:
		trapsignal(p, SIGTRAP, 0, TRAP_BRKPT, sv);
		break;

	case T_DIV0:
		ADVANCE;
		trapsignal(p, SIGFPE, 0, FPE_INTDIV, sv);
		break;

	case T_CLEANWIN:
		uprintf("T_CLEANWIN\n");	/* XXX Should not get this */
		ADVANCE;
		break;

	case T_FLUSHWIN:
		/* Software window flush for v8 software */
		write_all_windows();
		ADVANCE;
		break;

	case T_RANGECHECK:
		ADVANCE;
		trapsignal(p, SIGILL, 0, ILL_ILLOPN, sv);	/* XXX code? */
		break;

	case T_FIXALIGN:
		uprintf("T_FIXALIGN\n");
		ADVANCE;
		trapsignal(p, SIGILL, 0, ILL_ILLOPN, sv);	/* XXX code? */
		break;

	case T_INTOF:
		uprintf("T_INTOF\n");		/* XXX */
		ADVANCE;
		trapsignal(p, SIGFPE, FPE_INTOVF_TRAP, FPE_INTOVF, sv);
		break;
	}

	userret(p);
	share_fpu(p, tf);
#undef ADVANCE
}

/*
 * Save windows from PCB into user stack, and return 0.  This is used on
 * window overflow pseudo-traps (from locore.s, just before returning to
 * user mode) and when ptrace or sendsig needs a consistent state.
 * As a side effect, rwindow_save() always sets pcb_nsaved to 0.
 *
 * If the windows cannot be saved, pcb_nsaved is restored and we return -1.
 */
int
rwindow_save(struct proc *p)
{
	struct pcb *pcb = &p->p_addr->u_pcb;
	int i;

	for (i = 0; i < pcb->pcb_nsaved; i++) {
		pcb->pcb_rw[i].rw_in[7] ^= pcb->pcb_wcookie;
		if (copyout(&pcb->pcb_rw[i], (void *)(pcb->pcb_rwsp[i] + BIAS),
		    sizeof(struct rwindow)))
			return (-1);
	}

	pcb->pcb_nsaved = 0;
	return (0);
}

/*
 * Kill user windows (before exec) by writing back to stack or pcb
 * and then erasing any pcb tracks.  Otherwise we might try to write
 * the registers into the new process after the exec.
 */
void
pmap_unuse_final(struct proc *p)
{

	write_user_windows();
	p->p_addr->u_pcb.pcb_nsaved = 0;
}

static inline int
accesstype(unsigned int type, u_long sfsr)
{
	/*
	 * If it was a FAST_DATA_ACCESS_MMU_MISS we have no idea what the
	 * access was since the SFSR is not set.  But we should never get
	 * here from there.
	 */
	if (type == T_FDMMU_MISS || (sfsr & SFSR_FV) == 0)
		return PROT_READ;
	else if (sfsr & SFSR_W)
		return PROT_WRITE;
	return PROT_READ;
}

/*
 * This routine handles MMU generated faults.
 */
void
data_access_fault(struct trapframe *tf, unsigned type, vaddr_t pc,
    vaddr_t addr, vaddr_t sfva, u_long sfsr)
{
	struct proc *p = curproc;
	vaddr_t va = trunc_page(addr);
	vm_prot_t access_type = accesstype(type, sfsr);
	vaddr_t onfault;
	union sigval sv;
	int signal, sicode, error;

	uvmexp.traps++;
	if (p == NULL)		/* safety check */
		p = &proc0;

	if (tf->tf_tstate & TSTATE_PRIV) {
#ifdef DDB
		extern char Lfsprobe[];
		/*
		 * If this was an access that we shouldn't try to page in,
		 * resume at the fault handler without any action.
		 */
		if (p->p_addr->u_pcb.pcb_onfault == Lfsprobe)
			goto kfault;
#endif

		/*
		 * During autoconfiguration, faults are never OK unless
		 * pcb_onfault is set.  Once running normally we must allow
		 * exec() to cause copy-on-write faults to kernel addresses.
		 */
		if (cold)
			goto kfault;
		if (!(addr & TLB_TAG_ACCESS_CTX)) {
			/* CTXT == NUCLEUS */

			error = uvm_fault(kernel_map, va, 0, access_type);
			if (error == 0)
				return;
			goto kfault;
		}
	} else {
		p->p_md.md_tf = tf;
		refreshcreds(p);
		if (!uvm_map_inentry(p, &p->p_spinentry, PROC_STACK(p),
		    "[%s]%d/%d sp=%lx inside %lx-%lx: not MAP_STACK\n",
		    uvm_map_inentry_sp, p->p_vmspace->vm_map.sserial))
			goto out;
	}

	onfault = (vaddr_t)p->p_addr->u_pcb.pcb_onfault;
	p->p_addr->u_pcb.pcb_onfault = NULL;
	error = uvm_fault(&p->p_vmspace->vm_map, (vaddr_t)va, 0, access_type);
	p->p_addr->u_pcb.pcb_onfault = (void *)onfault;

	/*
	 * If this was a stack access we keep track of the maximum
	 * accessed stack size.  Also, if uvm_fault gets a protection
	 * failure it is due to accessing the stack region outside
	 * the current limit and we need to reflect that as an access
	 * error.
	 */
	if (error == 0) {
		uvm_grow(p, va);
		goto out;
	}

	/*
	 * Pagein failed.  If doing copyin/out, return to onfault
	 * address.  Any other page fault in kernel, die; if user
	 * fault, deliver SIGSEGV.
	 */
	if (tf->tf_tstate & TSTATE_PRIV) {
kfault:
		onfault = (long)p->p_addr->u_pcb.pcb_onfault;
		if (!onfault) {
			(void) splhigh();
			panic("kernel data fault: pc=%lx addr=%lx",
			    pc, addr);
			/* NOTREACHED */
		}
		tf->tf_pc = onfault;
		tf->tf_npc = onfault + 4;
		return;
	}

	if (type == T_FDMMU_MISS || (sfsr & SFSR_FV) == 0)
		sv.sival_ptr = (void *)va;
	else
		sv.sival_ptr = (void *)sfva;

	signal = SIGSEGV;
	sicode = SEGV_MAPERR;
	if (error == ENOMEM) {
		printf("UVM: pid %d (%s), uid %d killed: out of swap\n",
		    p->p_p->ps_pid, p->p_p->ps_comm,
		    p->p_ucred ? (int)p->p_ucred->cr_uid : -1);
		signal = SIGKILL;
	} else if (error == EACCES)
		sicode = SEGV_ACCERR;
	else if (error == EIO) {
		signal = SIGBUS;
		sicode = BUS_OBJERR;
	}
	trapsignal(p, signal, access_type, sicode, sv);

out:
	if ((tf->tf_tstate & TSTATE_PRIV) == 0) {
		userret(p);
		share_fpu(p, tf);
	}
}

/*
 * This routine handles deferred errors caused by the memory
 * or I/O bus subsystems.  Most of these are fatal, and even
 * if they are not, recovery is painful.  Also, the TPC and
 * TNPC values are probably not valid if we're not doing a
 * special PEEK/POKE code sequence.
 */
void
data_access_error(struct trapframe *tf, unsigned type, vaddr_t afva,
    u_long afsr, vaddr_t sfva, u_long sfsr)
{
	struct proc *p = curproc;
	u_long pc;
	vaddr_t onfault;
	union sigval sv;

	uvmexp.traps++;
	if (p == NULL)		/* safety check */
		p = &proc0;

	/*
	 * Catch PCI config space reads.
	 */
	if (curcpu()->ci_pci_probe) {
		curcpu()->ci_pci_fault = 1;
		goto out;
	}

	pc = tf->tf_pc;

	onfault = (long)p->p_addr->u_pcb.pcb_onfault;
	printf("data error type %x sfsr=%lx sfva=%lx afsr=%lx afva=%lx tf=%p\n",
	    type, sfsr, sfva, afsr, afva, tf);

	if (afsr == 0 && sfsr == 0) {
		printf("data_access_error: no fault\n");
		goto out;	/* No fault. Why were we called? */
	}

	if (tf->tf_tstate & TSTATE_PRIV) {
		if (!onfault) {
			(void) splhigh();
			panic("data fault: pc=%lx addr=%lx sfsr=%lb",
				(u_long)pc, (long)sfva, sfsr, SFSR_BITS);
			/* NOTREACHED */
		}

		/*
		 * If this was a privileged error but not a probe, we
		 * cannot recover, so panic.
		 */
		if (afsr & ASFR_PRIV) {
			panic("Privileged Async Fault: AFAR %p AFSR %lx\n%lb",
				(void *)afva, afsr, afsr, AFSR_BITS);
			/* NOTREACHED */
		}
		tf->tf_pc = onfault;
		tf->tf_npc = onfault + 4;
		return;
	}

	sv.sival_ptr = (void *)pc;
	trapsignal(p, SIGSEGV, PROT_READ | PROT_WRITE, SEGV_MAPERR, sv);

out:
	if ((tf->tf_tstate & TSTATE_PRIV) == 0) {
		userret(p);
		share_fpu(p, tf);
	}
}

/*
 * This routine handles MMU generated faults.
 */
void
text_access_fault(struct trapframe *tf, unsigned type, vaddr_t pc,
    u_long sfsr)
{
	struct proc *p = curproc;
	vaddr_t va = trunc_page(pc);
	vm_prot_t access_type = PROT_EXEC;
	union sigval sv;
	int signal, sicode, error;

	uvmexp.traps++;
	if (p == NULL)		/* safety check */
		panic("text_access_fault: no curproc");

	sv.sival_ptr = (void *)pc;

	if (tf->tf_tstate & TSTATE_PRIV) {
		(void) splhigh();
		panic("kernel text_access_fault: pc=%lx va=%lx", pc, va);
		/* NOTREACHED */
	}

	p->p_md.md_tf = tf;
	refreshcreds(p);
	if (!uvm_map_inentry(p, &p->p_spinentry, PROC_STACK(p),
	    "[%s]%d/%d sp=%lx inside %lx-%lx: not MAP_STACK\n",
	    uvm_map_inentry_sp, p->p_vmspace->vm_map.sserial))
		goto out;

	error = uvm_fault(&p->p_vmspace->vm_map, va, 0, access_type);
	if (error == 0)
		goto out;

	signal = SIGSEGV;
	sicode = SEGV_MAPERR;
	if (error == EACCES)
		sicode = SEGV_ACCERR;
	else if (error == EIO) {
		signal = SIGBUS;
		sicode = BUS_OBJERR;
	}
	trapsignal(p, signal, access_type, sicode, sv);

out:
	userret(p);
	share_fpu(p, tf);
}


/*
 * This routine handles deferred errors caused by the memory
 * or I/O bus subsystems.  Most of these are fatal, and even
 * if they are not, recovery is painful.  Also, the TPC and
 * TNPC values are probably not valid if we're not doing a
 * special PEEK/POKE code sequence.
 */
void
text_access_error(struct trapframe *tf, unsigned type, vaddr_t pc,
    u_long sfsr, vaddr_t afva, u_long afsr)
{
	struct proc *p = curproc;
	vaddr_t va = trunc_page(pc);
	vm_prot_t access_type = PROT_EXEC;
	union sigval sv;
	int signal, sicode, error;

	uvmexp.traps++;
	if (p == NULL)		/* safety check */
		p = &proc0;

	sv.sival_ptr = (void *)pc;

	if ((afsr) != 0) {
		printf("text_access_error: memory error...\n");
		printf("type %d sfsr=%lx sfva=%lx afsr=%lx afva=%lx tf=%p\n",
		    type, sfsr, pc, afsr, afva, tf);

		if (tf->tf_tstate & TSTATE_PRIV)
			panic("text_access_error: kernel memory error");

		/* User fault -- Berr */
		trapsignal(p, SIGBUS, 0, BUS_ADRALN, sv);
	}

	if ((sfsr & SFSR_FV) == 0 || (sfsr & SFSR_FT) == 0)
		goto out;	/* No fault. Why were we called? */

	if (tf->tf_tstate & TSTATE_PRIV) {
		(void) splhigh();
		panic("kernel text error: pc=%lx sfsr=%lb", pc,
		    sfsr, SFSR_BITS);
		/* NOTREACHED */
	}

	p->p_md.md_tf = tf;
	refreshcreds(p);
	if (!uvm_map_inentry(p, &p->p_spinentry, PROC_STACK(p),
	    "[%s]%d/%d sp=%lx inside %lx-%lx: not MAP_STACK\n",
	    uvm_map_inentry_sp, p->p_vmspace->vm_map.sserial))
		goto out;

	error = uvm_fault(&p->p_vmspace->vm_map, va, 0, access_type);
	if (error == 0)
		goto out;

	signal = SIGSEGV;
	sicode = SEGV_MAPERR;
	if (error == EACCES)
		sicode = SEGV_ACCERR;
	else if (error == EIO) {
		signal = SIGBUS;
		sicode = BUS_OBJERR;
	}
	trapsignal(p, signal, access_type, sicode, sv);

out:
	if ((tf->tf_tstate & TSTATE_PRIV) == 0) {
		userret(p);
		share_fpu(p, tf);
	}
}

/*
 * System calls.  `pc' is just a copy of tf->tf_pc.
 *
 * Note that the things labelled `out' registers in the trapframe were the
 * `in' registers within the syscall trap code (because of the automatic
 * `save' effect of each trap).  They are, however, the %o registers of the
 * thing that made the system call, and are named that way here.
 */
void
syscall(struct trapframe *tf, register_t code, register_t pc)
{
	const struct sysent *callp;
	struct proc *p = curproc;
	int error = ENOSYS, new;
	register_t rval[2];
	register_t *args;

	if ((tf->tf_out[6] & 1) == 0)
		sigexit(p, SIGILL);

	uvmexp.syscalls++;
#ifdef DIAGNOSTIC
	if (tf->tf_tstate & TSTATE_PRIV)
		panic("syscall from kernel");
	if (curpcb != &p->p_addr->u_pcb)
		panic("syscall: cpcb/ppcb mismatch");
	if (tf != (struct trapframe *)((caddr_t)curpcb + USPACE) - 1)
		panic("syscall: trapframe");
#endif
	p->p_md.md_tf = tf;
	new = code & SYSCALL_G2RFLAG;
	code &= ~SYSCALL_G2RFLAG;

	if (code <= 0 || code >= SYS_MAXSYSCALL)
		goto bad;
	callp = sysent + code;

	/*
	 * The first six system call arguments are in the six %o registers.
	 * Any arguments beyond that are in the `argument extension' area
	 * of the user's stack frame (see <machine/frame.h>), but no system
	 * call currently uses more than six arguments.
	 */
	args = (register_t *)&tf->tf_out[0];

	rval[0] = 0;
	rval[1] = 0;

	error = mi_syscall(p, code, callp, args, rval);

	switch (error) {
		vaddr_t dest;
	case 0:
		/* Note: fork() does not return here in the child */
		tf->tf_out[0] = rval[0];
		if (new) {
			/* jmp %g2 on success */
			dest = tf->tf_global[2];
			if (dest & 3) {
				error = EINVAL;
				goto bad;
			}
		} else {
			/* old system call convention: clear C on success */
			tf->tf_tstate &= ~(((int64_t)(ICC_C|XCC_C))<<TSTATE_CCR_SHIFT);	/* success */
			dest = tf->tf_npc;
		}
		tf->tf_pc = dest;
		tf->tf_npc = dest + 4;
		break;

	case ERESTART:
	case EJUSTRETURN:
		/* nothing to do */
		break;

	default:
	bad:
		tf->tf_out[0] = error;
		tf->tf_tstate |= (((int64_t)(ICC_C|XCC_C))<<TSTATE_CCR_SHIFT);	/* fail */
		dest = tf->tf_npc;
		tf->tf_pc = dest;
		tf->tf_npc = dest + 4;
		break;
	}

	mi_syscall_return(p, code, error, rval);
	share_fpu(p, tf);
}

/*
 * Process the tail end of a fork() for the child.
 */
void
child_return(void *arg)
{
	struct proc *p = arg;
	struct trapframe *tf = p->p_md.md_tf;
	vaddr_t dest;

	/* Duplicate efforts of syscall(), but slightly differently */
	if (tf->tf_global[1] & SYSCALL_G2RFLAG) {
		/* jmp %g2 on success */
		dest = tf->tf_global[2];
	} else {
		dest = tf->tf_npc;
		tf->tf_tstate &= ~(((int64_t)(ICC_C|XCC_C))<<TSTATE_CCR_SHIFT);
	}

	/* Skip trap instruction. */
	tf->tf_pc = dest;
	tf->tf_npc = dest + 4;

	/*
	 * Return values in the frame set by cpu_fork().
	 */
	tf->tf_out[0] = 0;

	KERNEL_UNLOCK();

	mi_child_return(p);
}

int
copyinsn(struct proc *p, vaddr_t uva, int *insn)
{
	struct vm_map *map = &p->p_vmspace->vm_map;
	int error = 0;

	if (__predict_false((uva & 3) != 0))
		return EFAULT;

	do {
		if (pmap_copyinsn(map->pmap, uva, (uint32_t *)insn) == 0)
			break;
		error = uvm_fault(map, trunc_page(uva), 0, PROT_EXEC);
	} while (error == 0);

	return error;
}
