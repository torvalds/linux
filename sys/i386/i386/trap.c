/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (C) 1994, David Greenman
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 *	from: @(#)trap.c	7.4 (Berkeley) 5/13/91
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * 386 Trap and System call handling
 */

#include "opt_clock.h"
#include "opt_compat.h"
#include "opt_cpu.h"
#include "opt_hwpmc_hooks.h"
#include "opt_isa.h"
#include "opt_kdb.h"
#include "opt_stack.h"
#include "opt_trap.h"

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/pioctl.h>
#include <sys/ptrace.h>
#include <sys/kdb.h>
#include <sys/kernel.h>
#include <sys/ktr.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/resourcevar.h>
#include <sys/signalvar.h>
#include <sys/syscall.h>
#include <sys/sysctl.h>
#include <sys/sysent.h>
#include <sys/uio.h>
#include <sys/vmmeter.h>
#ifdef HWPMC_HOOKS
#include <sys/pmckern.h>
PMC_SOFT_DEFINE( , , page_fault, all);
PMC_SOFT_DEFINE( , , page_fault, read);
PMC_SOFT_DEFINE( , , page_fault, write);
#endif
#include <security/audit/audit.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/pmap.h>
#include <vm/vm_kern.h>
#include <vm/vm_map.h>
#include <vm/vm_page.h>
#include <vm/vm_extern.h>

#include <machine/cpu.h>
#include <machine/intr_machdep.h>
#include <x86/mca.h>
#include <machine/md_var.h>
#include <machine/pcb.h>
#ifdef SMP
#include <machine/smp.h>
#endif
#include <machine/stack.h>
#include <machine/trap.h>
#include <machine/tss.h>
#include <machine/vm86.h>

#ifdef POWERFAIL_NMI
#include <sys/syslog.h>
#include <machine/clock.h>
#endif

#ifdef KDTRACE_HOOKS
#include <sys/dtrace_bsd.h>
#endif

void trap(struct trapframe *frame);
void syscall(struct trapframe *frame);

static int trap_pfault(struct trapframe *, int, vm_offset_t);
static void trap_fatal(struct trapframe *, vm_offset_t);
void dblfault_handler(void);

extern inthand_t IDTVEC(bpt), IDTVEC(dbg), IDTVEC(int0x80_syscall);
extern uint64_t pg_nx;

#define MAX_TRAP_MSG		32

struct trap_data {
	bool		ei;
	const char	*msg;
};

static const struct trap_data trap_data[] = {
	[T_PRIVINFLT] =	{ .ei = true,	.msg = "privileged instruction fault" },
	[T_BPTFLT] =	{ .ei = false,	.msg = "breakpoint instruction fault" },
	[T_ARITHTRAP] =	{ .ei = true,	.msg = "arithmetic trap" },
	[T_PROTFLT] =	{ .ei = true,	.msg = "general protection fault" },
	[T_TRCTRAP] =	{ .ei = false,	.msg = "debug exception" },
	[T_PAGEFLT] =	{ .ei = true,	.msg = "page fault" },
	[T_ALIGNFLT] = 	{ .ei = true,	.msg = "alignment fault" },
	[T_DIVIDE] =	{ .ei = true,	.msg = "integer divide fault" },
	[T_NMI] =	{ .ei = false,	.msg = "non-maskable interrupt trap" },
	[T_OFLOW] =	{ .ei = true,	.msg = "overflow trap" },
	[T_BOUND] =	{ .ei = true,	.msg = "FPU bounds check fault" },
	[T_DNA] =	{ .ei = true,	.msg = "FPU device not available" },
	[T_DOUBLEFLT] =	{ .ei = false,	.msg = "double fault" },
	[T_FPOPFLT] =	{ .ei = true,	.msg = "FPU operand fetch fault" },
	[T_TSSFLT] =	{ .ei = true,	.msg = "invalid TSS fault" },
	[T_SEGNPFLT] =	{ .ei = true,	.msg = "segment not present fault" },
	[T_STKFLT] =	{ .ei = true,	.msg = "stack fault" },
	[T_MCHK] =	{ .ei = true,	.msg = "machine check trap" },
	[T_XMMFLT] =	{ .ei = true,	.msg = "SIMD floating-point exception" },
	[T_DTRACE_RET] ={ .ei = true,	.msg = "DTrace pid return trap" },
};

static bool
trap_enable_intr(int trapno)
{

	MPASS(trapno > 0);
	if (trapno < nitems(trap_data) && trap_data[trapno].msg != NULL)
		return (trap_data[trapno].ei);
	return (false);
}

static const char *
trap_msg(int trapno)
{
	const char *res;
	static const char unkn[] = "UNKNOWN";

	res = NULL;
	if (trapno < nitems(trap_data))
		res = trap_data[trapno].msg;
	if (res == NULL)
		res = unkn;
	return (res);
}

#if defined(I586_CPU) && !defined(NO_F00F_HACK)
int has_f00f_bug = 0;		/* Initialized so that it can be patched. */
#endif

static int prot_fault_translation = 0;
SYSCTL_INT(_machdep, OID_AUTO, prot_fault_translation, CTLFLAG_RW,
	&prot_fault_translation, 0, "Select signal to deliver on protection fault");
static int uprintf_signal;
SYSCTL_INT(_machdep, OID_AUTO, uprintf_signal, CTLFLAG_RW,
    &uprintf_signal, 0,
    "Print debugging information on trap signal to ctty");

/*
 * Exception, fault, and trap interface to the FreeBSD kernel.
 * This common code is called from assembly language IDT gate entry
 * routines that prepare a suitable stack frame, and restore this
 * frame after the exception has been processed.
 */

void
trap(struct trapframe *frame)
{
	ksiginfo_t ksi;
	struct thread *td;
	struct proc *p;
	int signo, ucode;
	u_int type;
	register_t addr, dr6;
	vm_offset_t eva;
#ifdef POWERFAIL_NMI
	static int lastalert = 0;
#endif

	td = curthread;
	p = td->td_proc;
	signo = 0;
	ucode = 0;
	addr = 0;
	dr6 = 0;

	VM_CNT_INC(v_trap);
	type = frame->tf_trapno;

	KASSERT((read_eflags() & PSL_I) == 0,
	    ("trap: interrupts enabled, type %d frame %p", type, frame));

#ifdef SMP
	/* Handler for NMI IPIs used for stopping CPUs. */
	if (type == T_NMI && ipi_nmi_handler() == 0)
		return;
#endif /* SMP */

#ifdef KDB
	if (kdb_active) {
		kdb_reenter();
		return;
	}
#endif

	if (type == T_RESERVED) {
		trap_fatal(frame, 0);
		return;
	}

	if (type == T_NMI) {
#ifdef HWPMC_HOOKS
		/*
		 * CPU PMCs interrupt using an NMI so we check for that first.
		 * If the HWPMC module is active, 'pmc_hook' will point to
		 * the function to be called.  A non-zero return value from the
		 * hook means that the NMI was consumed by it and that we can
		 * return immediately.
		 */
		if (pmc_intr != NULL &&
		    (*pmc_intr)(frame) != 0)
			return;
#endif

#ifdef STACK
		if (stack_nmi_handler(frame) != 0)
			return;
#endif
	}

	if (type == T_MCHK) {
		mca_intr();
		return;
	}

#ifdef KDTRACE_HOOKS
	/*
	 * A trap can occur while DTrace executes a probe. Before
	 * executing the probe, DTrace blocks re-scheduling and sets
	 * a flag in its per-cpu flags to indicate that it doesn't
	 * want to fault. On returning from the probe, the no-fault
	 * flag is cleared and finally re-scheduling is enabled.
	 */
	if ((type == T_PROTFLT || type == T_PAGEFLT) &&
	    dtrace_trap_func != NULL && (*dtrace_trap_func)(frame, type))
		return;
#endif

	/*
	 * We must not allow context switches until %cr2 is read.
	 * Also, for some Cyrix CPUs, %cr2 is clobbered by interrupts.
	 * All faults use interrupt gates, so %cr2 can be safely read
	 * now, before optional enable of the interrupts below.
	 */
	if (type == T_PAGEFLT)
		eva = rcr2();

	/*
	 * Buggy application or kernel code has disabled interrupts
	 * and then trapped.  Enabling interrupts now is wrong, but it
	 * is better than running with interrupts disabled until they
	 * are accidentally enabled later.
	 */
	if ((frame->tf_eflags & PSL_I) == 0 && TRAPF_USERMODE(frame) &&
	    (curpcb->pcb_flags & PCB_VM86CALL) == 0)
		uprintf("pid %ld (%s): trap %d with interrupts disabled\n",
		    (long)curproc->p_pid, curthread->td_name, type);

	/*
	 * Conditionally reenable interrupts.  If we hold a spin lock,
	 * then we must not reenable interrupts.  This might be a
	 * spurious page fault.
	 */
	if (trap_enable_intr(type) && td->td_md.md_spinlock_count == 0 &&
	    frame->tf_eip != (int)cpu_switch_load_gs)
		enable_intr();

        if (TRAPF_USERMODE(frame) && (curpcb->pcb_flags & PCB_VM86CALL) == 0) {
		/* user trap */

		td->td_pticks = 0;
		td->td_frame = frame;
		addr = frame->tf_eip;
		if (td->td_cowgen != p->p_cowgen)
			thread_cow_update(td);

		switch (type) {
		case T_PRIVINFLT:	/* privileged instruction fault */
			signo = SIGILL;
			ucode = ILL_PRVOPC;
			break;

		case T_BPTFLT:		/* bpt instruction fault */
			enable_intr();
#ifdef KDTRACE_HOOKS
			if (dtrace_pid_probe_ptr != NULL &&
			    dtrace_pid_probe_ptr(frame) == 0)
				return;
#endif
			signo = SIGTRAP;
			ucode = TRAP_BRKPT;
			break;

		case T_TRCTRAP:		/* debug exception */
			enable_intr();
user_trctrap_out:
			signo = SIGTRAP;
			ucode = TRAP_TRACE;
			dr6 = rdr6();
			if ((dr6 & DBREG_DR6_BS) != 0) {
				PROC_LOCK(td->td_proc);
				if ((td->td_dbgflags & TDB_STEP) != 0) {
					td->td_frame->tf_eflags &= ~PSL_T;
					td->td_dbgflags &= ~TDB_STEP;
				}
				PROC_UNLOCK(td->td_proc);
			}
			break;

		case T_ARITHTRAP:	/* arithmetic trap */
			ucode = npxtrap_x87();
			if (ucode == -1)
				return;
			signo = SIGFPE;
			break;

		/*
		 * The following two traps can happen in vm86 mode,
		 * and, if so, we want to handle them specially.
		 */
		case T_PROTFLT:		/* general protection fault */
		case T_STKFLT:		/* stack fault */
			if (frame->tf_eflags & PSL_VM) {
				signo = vm86_emulate((struct vm86frame *)frame);
				if (signo == SIGTRAP) {
					load_dr6(rdr6() | 0x4000);
					goto user_trctrap_out;
				}
				if (signo == 0)
					goto user;
				break;
			}
			signo = SIGBUS;
			ucode = (type == T_PROTFLT) ? BUS_OBJERR : BUS_ADRERR;
			break;
		case T_SEGNPFLT:	/* segment not present fault */
			signo = SIGBUS;
			ucode = BUS_ADRERR;
			break;
		case T_TSSFLT:		/* invalid TSS fault */
			signo = SIGBUS;
			ucode = BUS_OBJERR;
			break;
		case T_ALIGNFLT:
			signo = SIGBUS;
			ucode = BUS_ADRALN;
			break;
		case T_DOUBLEFLT:	/* double fault */
		default:
			signo = SIGBUS;
			ucode = BUS_OBJERR;
			break;

		case T_PAGEFLT:		/* page fault */
			signo = trap_pfault(frame, TRUE, eva);
#if defined(I586_CPU) && !defined(NO_F00F_HACK)
			if (signo == -2) {
				/*
				 * The f00f hack workaround has triggered, so
				 * treat the fault as an illegal instruction 
				 * (T_PRIVINFLT) instead of a page fault.
				 */
				type = frame->tf_trapno = T_PRIVINFLT;

				/* Proceed as in that case. */
				ucode = ILL_PRVOPC;
				signo = SIGILL;
				break;
			}
#endif
			if (signo == -1)
				return;
			if (signo == 0)
				goto user;

			if (signo == SIGSEGV)
				ucode = SEGV_MAPERR;
			else if (prot_fault_translation == 0) {
				/*
				 * Autodetect.  This check also covers
				 * the images without the ABI-tag ELF
				 * note.
				 */
				if (SV_CURPROC_ABI() == SV_ABI_FREEBSD &&
				    p->p_osrel >= P_OSREL_SIGSEGV) {
					signo = SIGSEGV;
					ucode = SEGV_ACCERR;
				} else {
					signo = SIGBUS;
					ucode = T_PAGEFLT;
				}
			} else if (prot_fault_translation == 1) {
				/*
				 * Always compat mode.
				 */
				signo = SIGBUS;
				ucode = T_PAGEFLT;
			} else {
				/*
				 * Always SIGSEGV mode.
				 */
				signo = SIGSEGV;
				ucode = SEGV_ACCERR;
			}
			addr = eva;
			break;

		case T_DIVIDE:		/* integer divide fault */
			ucode = FPE_INTDIV;
			signo = SIGFPE;
			break;

#ifdef DEV_ISA
		case T_NMI:
#ifdef POWERFAIL_NMI
#ifndef TIMER_FREQ
#  define TIMER_FREQ 1193182
#endif
			if (time_second - lastalert > 10) {
				log(LOG_WARNING, "NMI: power fail\n");
				sysbeep(880, hz);
				lastalert = time_second;
			}
			return;
#else /* !POWERFAIL_NMI */
			nmi_handle_intr(type, frame);
			return;
#endif /* POWERFAIL_NMI */
#endif /* DEV_ISA */

		case T_OFLOW:		/* integer overflow fault */
			ucode = FPE_INTOVF;
			signo = SIGFPE;
			break;

		case T_BOUND:		/* bounds check fault */
			ucode = FPE_FLTSUB;
			signo = SIGFPE;
			break;

		case T_DNA:
			KASSERT(PCB_USER_FPU(td->td_pcb),
			    ("kernel FPU ctx has leaked"));
			/* transparent fault (due to context switch "late") */
			if (npxdna())
				return;
			uprintf("pid %d killed due to lack of floating point\n",
				p->p_pid);
			signo = SIGKILL;
			ucode = 0;
			break;

		case T_FPOPFLT:		/* FPU operand fetch fault */
			ucode = ILL_COPROC;
			signo = SIGILL;
			break;

		case T_XMMFLT:		/* SIMD floating-point exception */
			ucode = npxtrap_sse();
			if (ucode == -1)
				return;
			signo = SIGFPE;
			break;
#ifdef KDTRACE_HOOKS
		case T_DTRACE_RET:
			enable_intr();
			if (dtrace_return_probe_ptr != NULL)
				dtrace_return_probe_ptr(frame);
			return;
#endif
		}
	} else {
		/* kernel trap */

		KASSERT(cold || td->td_ucred != NULL,
		    ("kernel trap doesn't have ucred"));
		switch (type) {
		case T_PAGEFLT:			/* page fault */
			(void) trap_pfault(frame, FALSE, eva);
			return;

		case T_DNA:
			if (PCB_USER_FPU(td->td_pcb))
				panic("Unregistered use of FPU in kernel");
			if (npxdna())
				return;
			break;

		case T_ARITHTRAP:	/* arithmetic trap */
		case T_XMMFLT:		/* SIMD floating-point exception */
		case T_FPOPFLT:		/* FPU operand fetch fault */
			/*
			 * XXXKIB for now disable any FPU traps in kernel
			 * handler registration seems to be overkill
			 */
			trap_fatal(frame, 0);
			return;

			/*
			 * The following two traps can happen in
			 * vm86 mode, and, if so, we want to handle
			 * them specially.
			 */
		case T_PROTFLT:		/* general protection fault */
		case T_STKFLT:		/* stack fault */
			if (frame->tf_eflags & PSL_VM) {
				signo = vm86_emulate((struct vm86frame *)frame);
				if (signo == SIGTRAP) {
					type = T_TRCTRAP;
					load_dr6(rdr6() | 0x4000);
					goto kernel_trctrap;
				}
				if (signo != 0)
					/*
					 * returns to original process
					 */
					vm86_trap((struct vm86frame *)frame);
				return;
			}
			/* FALL THROUGH */
		case T_SEGNPFLT:	/* segment not present fault */
			if (curpcb->pcb_flags & PCB_VM86CALL)
				break;

			/*
			 * Invalid %fs's and %gs's can be created using
			 * procfs or PT_SETREGS or by invalidating the
			 * underlying LDT entry.  This causes a fault
			 * in kernel mode when the kernel attempts to
			 * switch contexts.  Lose the bad context
			 * (XXX) so that we can continue, and generate
			 * a signal.
			 */
			if (frame->tf_eip == (int)cpu_switch_load_gs) {
				curpcb->pcb_gs = 0;
#if 0				
				PROC_LOCK(p);
				kern_psignal(p, SIGBUS);
				PROC_UNLOCK(p);
#endif				
				return;
			}

			if (td->td_intr_nesting_level != 0)
				break;

			/*
			 * Invalid segment selectors and out of bounds
			 * %eip's and %esp's can be set up in user mode.
			 * This causes a fault in kernel mode when the
			 * kernel tries to return to user mode.  We want
			 * to get this fault so that we can fix the
			 * problem here and not have to check all the
			 * selectors and pointers when the user changes
			 * them.
			 *
			 * N.B. Comparing to long mode, 32-bit mode
			 * does not push %esp on the trap frame,
			 * because iretl faulted while in ring 0.  As
			 * the consequence, there is no need to fixup
			 * the stack pointer for doreti_iret_fault,
			 * the fixup and the complimentary trap() call
			 * are executed on the main thread stack, not
			 * on the trampoline stack.
			 */
			if (frame->tf_eip == (int)doreti_iret + setidt_disp) {
				frame->tf_eip = (int)doreti_iret_fault +
				    setidt_disp;
				return;
			}
			if (type == T_STKFLT)
				break;

			if (frame->tf_eip == (int)doreti_popl_ds +
			    setidt_disp) {
				frame->tf_eip = (int)doreti_popl_ds_fault +
				    setidt_disp;
				return;
			}
			if (frame->tf_eip == (int)doreti_popl_es +
			    setidt_disp) {
				frame->tf_eip = (int)doreti_popl_es_fault +
				    setidt_disp;
				return;
			}
			if (frame->tf_eip == (int)doreti_popl_fs +
			    setidt_disp) {
				frame->tf_eip = (int)doreti_popl_fs_fault +
				    setidt_disp;
				return;
			}
			if (curpcb->pcb_onfault != NULL) {
				frame->tf_eip = (int)curpcb->pcb_onfault;
				return;
			}
			break;

		case T_TSSFLT:
			/*
			 * PSL_NT can be set in user mode and isn't cleared
			 * automatically when the kernel is entered.  This
			 * causes a TSS fault when the kernel attempts to
			 * `iret' because the TSS link is uninitialized.  We
			 * want to get this fault so that we can fix the
			 * problem here and not every time the kernel is
			 * entered.
			 */
			if (frame->tf_eflags & PSL_NT) {
				frame->tf_eflags &= ~PSL_NT;
				return;
			}
			break;

		case T_TRCTRAP:	 /* debug exception */
kernel_trctrap:
			/* Clear any pending debug events. */
			dr6 = rdr6();
			load_dr6(0);

			/*
			 * Ignore debug register exceptions due to
			 * accesses in the user's address space, which
			 * can happen under several conditions such as
			 * if a user sets a watchpoint on a buffer and
			 * then passes that buffer to a system call.
			 * We still want to get TRCTRAPS for addresses
			 * in kernel space because that is useful when
			 * debugging the kernel.
			 */
			if (user_dbreg_trap(dr6) &&
			   !(curpcb->pcb_flags & PCB_VM86CALL))
				return;

			/*
			 * Malicious user code can configure a debug
			 * register watchpoint to trap on data access
			 * to the top of stack and then execute 'pop
			 * %ss; int 3'.  Due to exception deferral for
			 * 'pop %ss', the CPU will not interrupt 'int
			 * 3' to raise the DB# exception for the debug
			 * register but will postpone the DB# until
			 * execution of the first instruction of the
			 * BP# handler (in kernel mode).  Normally the
			 * previous check would ignore DB# exceptions
			 * for watchpoints on user addresses raised in
			 * kernel mode.  However, some CPU errata
			 * include cases where DB# exceptions do not
			 * properly set bits in %dr6, e.g. Haswell
			 * HSD23 and Skylake-X SKZ24.
			 *
			 * A deferred DB# can also be raised on the
			 * first instructions of system call entry
			 * points or single-step traps via similar use
			 * of 'pop %ss' or 'mov xxx, %ss'.
			 */
			if (frame->tf_eip ==
			    (uintptr_t)IDTVEC(int0x80_syscall) + setidt_disp ||
			    frame->tf_eip == (uintptr_t)IDTVEC(bpt) +
			    setidt_disp ||
			    frame->tf_eip == (uintptr_t)IDTVEC(dbg) +
			    setidt_disp)
				return;
			/*
			 * FALLTHROUGH (TRCTRAP kernel mode, kernel address)
			 */
		case T_BPTFLT:
			/*
			 * If KDB is enabled, let it handle the debugger trap.
			 * Otherwise, debugger traps "can't happen".
			 */
#ifdef KDB
			if (kdb_trap(type, dr6, frame))
				return;
#endif
			break;

#ifdef DEV_ISA
		case T_NMI:
#ifdef POWERFAIL_NMI
			if (time_second - lastalert > 10) {
				log(LOG_WARNING, "NMI: power fail\n");
				sysbeep(880, hz);
				lastalert = time_second;
			}
			return;
#else /* !POWERFAIL_NMI */
			nmi_handle_intr(type, frame);
			return;
#endif /* POWERFAIL_NMI */
#endif /* DEV_ISA */
		}

		trap_fatal(frame, eva);
		return;
	}

	/* Translate fault for emulators (e.g. Linux) */
	if (*p->p_sysent->sv_transtrap != NULL)
		signo = (*p->p_sysent->sv_transtrap)(signo, type);

	ksiginfo_init_trap(&ksi);
	ksi.ksi_signo = signo;
	ksi.ksi_code = ucode;
	ksi.ksi_addr = (void *)addr;
	ksi.ksi_trapno = type;
	if (uprintf_signal) {
		uprintf("pid %d comm %s: signal %d err %x code %d type %d "
		    "addr 0x%x ss 0x%04x esp 0x%08x cs 0x%04x eip 0x%08x "
		    "<%02x %02x %02x %02x %02x %02x %02x %02x>\n",
		    p->p_pid, p->p_comm, signo, frame->tf_err, ucode, type,
		    addr, frame->tf_ss, frame->tf_esp, frame->tf_cs,
		    frame->tf_eip,
		    fubyte((void *)(frame->tf_eip + 0)),
		    fubyte((void *)(frame->tf_eip + 1)),
		    fubyte((void *)(frame->tf_eip + 2)),
		    fubyte((void *)(frame->tf_eip + 3)),
		    fubyte((void *)(frame->tf_eip + 4)),
		    fubyte((void *)(frame->tf_eip + 5)),
		    fubyte((void *)(frame->tf_eip + 6)),
		    fubyte((void *)(frame->tf_eip + 7)));
	}
	KASSERT((read_eflags() & PSL_I) != 0, ("interrupts disabled"));
	trapsignal(td, &ksi);

user:
	userret(td, frame);
	KASSERT(PCB_USER_FPU(td->td_pcb),
	    ("Return from trap with kernel FPU ctx leaked"));
}

static int
trap_pfault(struct trapframe *frame, int usermode, vm_offset_t eva)
{
	struct thread *td;
	struct proc *p;
	vm_offset_t va;
	vm_map_t map;
	int rv;
	vm_prot_t ftype;

	td = curthread;
	p = td->td_proc;

	if (__predict_false((td->td_pflags & TDP_NOFAULTING) != 0)) {
		/*
		 * Due to both processor errata and lazy TLB invalidation when
		 * access restrictions are removed from virtual pages, memory
		 * accesses that are allowed by the physical mapping layer may
		 * nonetheless cause one spurious page fault per virtual page. 
		 * When the thread is executing a "no faulting" section that
		 * is bracketed by vm_fault_{disable,enable}_pagefaults(),
		 * every page fault is treated as a spurious page fault,
		 * unless it accesses the same virtual address as the most
		 * recent page fault within the same "no faulting" section.
		 */
		if (td->td_md.md_spurflt_addr != eva ||
		    (td->td_pflags & TDP_RESETSPUR) != 0) {
			/*
			 * Do nothing to the TLB.  A stale TLB entry is
			 * flushed automatically by a page fault.
			 */
			td->td_md.md_spurflt_addr = eva;
			td->td_pflags &= ~TDP_RESETSPUR;
			return (0);
		}
	} else {
		/*
		 * If we get a page fault while in a critical section, then
		 * it is most likely a fatal kernel page fault.  The kernel
		 * is already going to panic trying to get a sleep lock to
		 * do the VM lookup, so just consider it a fatal trap so the
		 * kernel can print out a useful trap message and even get
		 * to the debugger.
		 *
		 * If we get a page fault while holding a non-sleepable
		 * lock, then it is most likely a fatal kernel page fault.
		 * If WITNESS is enabled, then it's going to whine about
		 * bogus LORs with various VM locks, so just skip to the
		 * fatal trap handling directly.
		 */
		if (td->td_critnest != 0 ||
		    WITNESS_CHECK(WARN_SLEEPOK | WARN_GIANTOK, NULL,
		    "Kernel page fault") != 0) {
			trap_fatal(frame, eva);
			return (-1);
		}
	}
	va = trunc_page(eva);
	if (va >= PMAP_TRM_MIN_ADDRESS) {
		/*
		 * Don't allow user-mode faults in kernel address space.
		 * An exception:  if the faulting address is the invalid
		 * instruction entry in the IDT, then the Intel Pentium
		 * F00F bug workaround was triggered, and we need to
		 * treat it is as an illegal instruction, and not a page
		 * fault.
		 */
#if defined(I586_CPU) && !defined(NO_F00F_HACK)
		if ((eva == (unsigned int)&idt[6]) && has_f00f_bug)
			return (-2);
#endif
		if (usermode)
			return (SIGSEGV);
		trap_fatal(frame, eva);
		return (-1);
	} else {
		map = usermode ? &p->p_vmspace->vm_map : kernel_map;

		/*
		 * Kernel cannot access a user-space address directly
		 * because user pages are not mapped.  Also, page
		 * faults must not be caused during the interrupts.
		 */
		if (!usermode && td->td_intr_nesting_level != 0) {
			trap_fatal(frame, eva);
			return (-1);
		}
	}

	/*
	 * If the trap was caused by errant bits in the PTE then panic.
	 */
	if (frame->tf_err & PGEX_RSV) {
		trap_fatal(frame, eva);
		return (-1);
	}

	/*
	 * PGEX_I is defined only if the execute disable bit capability is
	 * supported and enabled.
	 */
	if (frame->tf_err & PGEX_W)
		ftype = VM_PROT_WRITE;
	else if ((frame->tf_err & PGEX_I) && pg_nx != 0)
		ftype = VM_PROT_EXECUTE;
	else
		ftype = VM_PROT_READ;

	/* Fault in the page. */
	rv = vm_fault(map, va, ftype, VM_FAULT_NORMAL);
	if (rv == KERN_SUCCESS) {
#ifdef HWPMC_HOOKS
		if (ftype == VM_PROT_READ || ftype == VM_PROT_WRITE) {
			PMC_SOFT_CALL_TF( , , page_fault, all, frame);
			if (ftype == VM_PROT_READ)
				PMC_SOFT_CALL_TF( , , page_fault, read,
				    frame);
			else
				PMC_SOFT_CALL_TF( , , page_fault, write,
				    frame);
		}
#endif
		return (0);
	}
	if (!usermode) {
		if (td->td_intr_nesting_level == 0 &&
		    curpcb->pcb_onfault != NULL) {
			frame->tf_eip = (int)curpcb->pcb_onfault;
			return (0);
		}
		trap_fatal(frame, eva);
		return (-1);
	}
	return ((rv == KERN_PROTECTION_FAILURE) ? SIGBUS : SIGSEGV);
}

static void
trap_fatal(frame, eva)
	struct trapframe *frame;
	vm_offset_t eva;
{
	int code, ss, esp;
	u_int type;
	struct soft_segment_descriptor softseg;
#ifdef KDB
	bool handled;
#endif

	code = frame->tf_err;
	type = frame->tf_trapno;
	sdtossd(&gdt[IDXSEL(frame->tf_cs & 0xffff)].sd, &softseg);

	printf("\n\nFatal trap %d: %s while in %s mode\n", type, trap_msg(type),
	    frame->tf_eflags & PSL_VM ? "vm86" :
	    ISPL(frame->tf_cs) == SEL_UPL ? "user" : "kernel");
#ifdef SMP
	/* two separate prints in case of a trap on an unmapped page */
	printf("cpuid = %d; ", PCPU_GET(cpuid));
	printf("apic id = %02x\n", PCPU_GET(apic_id));
#endif
	if (type == T_PAGEFLT) {
		printf("fault virtual address	= 0x%x\n", eva);
		printf("fault code		= %s %s%s, %s\n",
			code & PGEX_U ? "user" : "supervisor",
			code & PGEX_W ? "write" : "read",
			pg_nx != 0 ?
			(code & PGEX_I ? " instruction" : " data") :
			"",
			code & PGEX_RSV ? "reserved bits in PTE" :
			code & PGEX_P ? "protection violation" : "page not present");
	} else {
		printf("error code		= %#x\n", code);
	}
	printf("instruction pointer	= 0x%x:0x%x\n",
	       frame->tf_cs & 0xffff, frame->tf_eip);
        if (TF_HAS_STACKREGS(frame)) {
		ss = frame->tf_ss & 0xffff;
		esp = frame->tf_esp;
	} else {
		ss = GSEL(GDATA_SEL, SEL_KPL);
		esp = (int)&frame->tf_esp;
	}
	printf("stack pointer	        = 0x%x:0x%x\n", ss, esp);
	printf("frame pointer	        = 0x%x:0x%x\n", ss, frame->tf_ebp);
	printf("code segment		= base 0x%x, limit 0x%x, type 0x%x\n",
	       softseg.ssd_base, softseg.ssd_limit, softseg.ssd_type);
	printf("			= DPL %d, pres %d, def32 %d, gran %d\n",
	       softseg.ssd_dpl, softseg.ssd_p, softseg.ssd_def32,
	       softseg.ssd_gran);
	printf("processor eflags	= ");
	if (frame->tf_eflags & PSL_T)
		printf("trace trap, ");
	if (frame->tf_eflags & PSL_I)
		printf("interrupt enabled, ");
	if (frame->tf_eflags & PSL_NT)
		printf("nested task, ");
	if (frame->tf_eflags & PSL_RF)
		printf("resume, ");
	if (frame->tf_eflags & PSL_VM)
		printf("vm86, ");
	printf("IOPL = %d\n", (frame->tf_eflags & PSL_IOPL) >> 12);
	printf("current process		= %d (%s)\n",
	    curproc->p_pid, curthread->td_name);

#ifdef KDB
	if (debugger_on_trap) {
		kdb_why = KDB_WHY_TRAP;
		frame->tf_err = eva;	/* smuggle fault address to ddb */
		handled = kdb_trap(type, 0, frame);
		frame->tf_err = code;	/* restore error code */
		kdb_why = KDB_WHY_UNSET;
		if (handled)
			return;
	}
#endif
	printf("trap number		= %d\n", type);
	if (trap_msg(type) != NULL)
		panic("%s", trap_msg(type));
	else
		panic("unknown/reserved trap");
}

/*
 * Double fault handler. Called when a fault occurs while writing
 * a frame for a trap/exception onto the stack. This usually occurs
 * when the stack overflows (such is the case with infinite recursion,
 * for example).
 *
 * XXX Note that the current PTD gets replaced by IdlePTD when the
 * task switch occurs. This means that the stack that was active at
 * the time of the double fault is not available at <kstack> unless
 * the machine was idle when the double fault occurred. The downside
 * of this is that "trace <ebp>" in ddb won't work.
 */
void
dblfault_handler(void)
{
#ifdef KDTRACE_HOOKS
	if (dtrace_doubletrap_func != NULL)
		(*dtrace_doubletrap_func)();
#endif
	printf("\nFatal double fault:\n");
	printf("eip = 0x%x\n", PCPU_GET(common_tssp)->tss_eip);
	printf("esp = 0x%x\n", PCPU_GET(common_tssp)->tss_esp);
	printf("ebp = 0x%x\n", PCPU_GET(common_tssp)->tss_ebp);
#ifdef SMP
	/* two separate prints in case of a trap on an unmapped page */
	printf("cpuid = %d; ", PCPU_GET(cpuid));
	printf("apic id = %02x\n", PCPU_GET(apic_id));
#endif
	panic("double fault");
}

int
cpu_fetch_syscall_args(struct thread *td)
{
	struct proc *p;
	struct trapframe *frame;
	struct syscall_args *sa;
	caddr_t params;
	long tmp;
	int error;
#ifdef COMPAT_43
	u_int32_t eip;
	int cs;
#endif

	p = td->td_proc;
	frame = td->td_frame;
	sa = &td->td_sa;

#ifdef COMPAT_43
	if (__predict_false(frame->tf_cs == 7 && frame->tf_eip == 2)) {
		/*
		 * In lcall $7,$0 after int $0x80.  Convert the user
		 * frame to what it would be for a direct int 0x80 instead
		 * of lcall $7,$0, by popping the lcall return address.
		 */
		error = fueword32((void *)frame->tf_esp, &eip);
		if (error == -1)
			return (EFAULT);
		cs = fuword16((void *)(frame->tf_esp + sizeof(u_int32_t)));
		if (cs == -1)
			return (EFAULT);

		/*
		 * Unwind in-kernel frame after all stack frame pieces
		 * were successfully read.
		 */
		frame->tf_eip = eip;
		frame->tf_cs = cs;
		frame->tf_esp += 2 * sizeof(u_int32_t);
		frame->tf_err = 7;	/* size of lcall $7,$0 */
	}
#endif

	sa->code = frame->tf_eax;
	params = (caddr_t)frame->tf_esp + sizeof(uint32_t);

	/*
	 * Need to check if this is a 32 bit or 64 bit syscall.
	 */
	if (sa->code == SYS_syscall) {
		/*
		 * Code is first argument, followed by actual args.
		 */
		error = fueword(params, &tmp);
		if (error == -1)
			return (EFAULT);
		sa->code = tmp;
		params += sizeof(uint32_t);
	} else if (sa->code == SYS___syscall) {
		/*
		 * Like syscall, but code is a quad, so as to maintain
		 * quad alignment for the rest of the arguments.
		 */
		error = fueword(params, &tmp);
		if (error == -1)
			return (EFAULT);
		sa->code = tmp;
		params += sizeof(quad_t);
	}

 	if (sa->code >= p->p_sysent->sv_size)
 		sa->callp = &p->p_sysent->sv_table[0];
  	else
 		sa->callp = &p->p_sysent->sv_table[sa->code];
	sa->narg = sa->callp->sy_narg;

	if (params != NULL && sa->narg != 0)
		error = copyin(params, (caddr_t)sa->args,
		    (u_int)(sa->narg * sizeof(uint32_t)));
	else
		error = 0;

	if (error == 0) {
		td->td_retval[0] = 0;
		td->td_retval[1] = frame->tf_edx;
	}
		
	return (error);
}

#include "../../kern/subr_syscall.c"

/*
 * syscall - system call request C handler.  A system call is
 * essentially treated as a trap by reusing the frame layout.
 */
void
syscall(struct trapframe *frame)
{
	struct thread *td;
	register_t orig_tf_eflags;
	int error;
	ksiginfo_t ksi;

#ifdef DIAGNOSTIC
	if (!(TRAPF_USERMODE(frame) &&
	    (curpcb->pcb_flags & PCB_VM86CALL) == 0)) {
		panic("syscall");
		/* NOT REACHED */
	}
#endif
	orig_tf_eflags = frame->tf_eflags;

	td = curthread;
	td->td_frame = frame;

	error = syscallenter(td);

	/*
	 * Traced syscall.
	 */
	if ((orig_tf_eflags & PSL_T) && !(orig_tf_eflags & PSL_VM)) {
		frame->tf_eflags &= ~PSL_T;
		ksiginfo_init_trap(&ksi);
		ksi.ksi_signo = SIGTRAP;
		ksi.ksi_code = TRAP_TRACE;
		ksi.ksi_addr = (void *)frame->tf_eip;
		trapsignal(td, &ksi);
	}

	KASSERT(PCB_USER_FPU(td->td_pcb),
	    ("System call %s returning with kernel FPU ctx leaked",
	     syscallname(td->td_proc, td->td_sa.code)));
	KASSERT(td->td_pcb->pcb_save == get_pcb_user_save_td(td),
	    ("System call %s returning with mangled pcb_save",
	     syscallname(td->td_proc, td->td_sa.code)));

	syscallret(td, error);
}
