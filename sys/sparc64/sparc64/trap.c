/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2001, Jake Burkholder
 * Copyright (C) 1994, David Greenman
 * Copyright (c) 1990, 1993
 *      The Regents of the University of California.  All rights reserved.
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
 *	from: @(#)trap.c        7.4 (Berkeley) 5/13/91
 *	from: FreeBSD: src/sys/i386/i386/trap.c,v 1.197 2001/07/19
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_ddb.h"
#include "opt_ktr.h"

#include <sys/param.h>
#include <sys/kdb.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/interrupt.h>
#include <sys/ktr.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/systm.h>
#include <sys/pcpu.h>
#include <sys/pioctl.h>
#include <sys/ptrace.h>
#include <sys/proc.h>
#include <sys/smp.h>
#include <sys/signalvar.h>
#include <sys/syscall.h>
#include <sys/sysctl.h>
#include <sys/sysent.h>
#include <sys/vmmeter.h>
#include <security/audit/audit.h>

#include <dev/ofw/openfirm.h>

#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/vm_extern.h>
#include <vm/vm_param.h>
#include <vm/vm_kern.h>
#include <vm/vm_map.h>
#include <vm/vm_page.h>

#include <machine/cpu.h>
#include <machine/frame.h>
#include <machine/intr_machdep.h>
#include <machine/ofw_machdep.h>
#include <machine/pcb.h>
#include <machine/smp.h>
#include <machine/trap.h>
#include <machine/tstate.h>
#include <machine/tte.h>
#include <machine/tlb.h>
#include <machine/tsb.h>
#include <machine/watch.h>

void trap(struct trapframe *tf);
void syscall(struct trapframe *tf);

static int trap_cecc(void);
static int trap_pfault(struct thread *td, struct trapframe *tf);

extern char copy_fault[];
extern char copy_nofault_begin[];
extern char copy_nofault_end[];

extern char fs_fault[];
extern char fs_nofault_begin[];
extern char fs_nofault_end[];

extern char fas_fault[];
extern char fas_nofault_begin[];
extern char fas_nofault_end[];

const char *const trap_msg[] = {
	"reserved",
	"instruction access exception",
	"instruction access error",
	"instruction access protection",
	"illtrap instruction",
	"illegal instruction",
	"privileged opcode",
	"floating point disabled",
	"floating point exception ieee 754",
	"floating point exception other",
	"tag overflow",
	"division by zero",
	"data access exception",
	"data access error",
	"data access protection",
	"memory address not aligned",
	"privileged action",
	"async data error",
	"trap instruction 16",
	"trap instruction 17",
	"trap instruction 18",
	"trap instruction 19",
	"trap instruction 20",
	"trap instruction 21",
	"trap instruction 22",
	"trap instruction 23",
	"trap instruction 24",
	"trap instruction 25",
	"trap instruction 26",
	"trap instruction 27",
	"trap instruction 28",
	"trap instruction 29",
	"trap instruction 30",
	"trap instruction 31",
	"fast instruction access mmu miss",
	"fast data access mmu miss",
	"interrupt",
	"physical address watchpoint",
	"virtual address watchpoint",
	"corrected ecc error",
	"spill",
	"fill",
	"fill",
	"breakpoint",
	"clean window",
	"range check",
	"fix alignment",
	"integer overflow",
	"syscall",
	"restore physical watchpoint",
	"restore virtual watchpoint",
	"kernel stack fault",
};

static const int trap_sig[] = {
	SIGILL,			/* reserved */
	SIGILL,			/* instruction access exception */
	SIGILL,			/* instruction access error */
	SIGILL,			/* instruction access protection */
	SIGILL,			/* illtrap instruction */
	SIGILL,			/* illegal instruction */
	SIGBUS,			/* privileged opcode */
	SIGFPE,			/* floating point disabled */
	SIGFPE,			/* floating point exception ieee 754 */
	SIGFPE,			/* floating point exception other */
	SIGEMT,			/* tag overflow */
	SIGFPE,			/* division by zero */
	SIGILL,			/* data access exception */
	SIGILL,			/* data access error */
	SIGBUS,			/* data access protection */
	SIGBUS,			/* memory address not aligned */
	SIGBUS,			/* privileged action */
	SIGBUS,			/* async data error */
	SIGILL,			/* trap instruction 16 */
	SIGILL,			/* trap instruction 17 */
	SIGILL,			/* trap instruction 18 */
	SIGILL,			/* trap instruction 19 */
	SIGILL,			/* trap instruction 20 */
	SIGILL,			/* trap instruction 21 */
	SIGILL,			/* trap instruction 22 */
	SIGILL,			/* trap instruction 23 */
	SIGILL,			/* trap instruction 24 */
	SIGILL,			/* trap instruction 25 */
	SIGILL,			/* trap instruction 26 */
	SIGILL,			/* trap instruction 27 */
	SIGILL,			/* trap instruction 28 */
	SIGILL,			/* trap instruction 29 */
	SIGILL,			/* trap instruction 30 */
	SIGILL,			/* trap instruction 31 */
	SIGSEGV,		/* fast instruction access mmu miss */
	SIGSEGV,		/* fast data access mmu miss */
	-1,			/* interrupt */
	-1,			/* physical address watchpoint */
	-1,			/* virtual address watchpoint */
	-1,			/* corrected ecc error */
	SIGILL,			/* spill */
	SIGILL,			/* fill */
	SIGILL,			/* fill */
	SIGTRAP,		/* breakpoint */
	SIGILL,			/* clean window */
	SIGILL,			/* range check */
	SIGILL,			/* fix alignment */
	SIGILL,			/* integer overflow */
	SIGSYS,			/* syscall */
	-1,			/* restore physical watchpoint */
	-1,			/* restore virtual watchpoint */
	-1,			/* kernel stack fault */
};

CTASSERT(nitems(trap_msg) == T_MAX);
CTASSERT(nitems(trap_sig) == T_MAX);

CTASSERT(sizeof(struct trapframe) == 256);

int debugger_on_signal = 0;
SYSCTL_INT(_debug, OID_AUTO, debugger_on_signal, CTLFLAG_RW,
    &debugger_on_signal, 0, "");

u_int corrected_ecc = 0;
SYSCTL_UINT(_machdep, OID_AUTO, corrected_ecc, CTLFLAG_RD, &corrected_ecc, 0,
    "corrected ECC errors");

/*
 * SUNW,set-trap-table allows to take over %tba from the PROM, which
 * will turn off interrupts and handle outstanding ones while doing so,
 * in a safe way.
 */
void
sun4u_set_traptable(void *tba_addr)
{
	static struct {
		cell_t name;
		cell_t nargs;
		cell_t nreturns;
		cell_t tba_addr;
	} args = {
		(cell_t)"SUNW,set-trap-table",
		1,
		0,
	};

	args.tba_addr = (cell_t)tba_addr;
	ofw_entry(&args);
}

void
trap(struct trapframe *tf)
{
	struct thread *td;
	struct proc *p;
	int error;
	int sig, ucode;
	register_t addr;
	ksiginfo_t ksi;

	td = curthread;

	CTR4(KTR_TRAP, "trap: %p type=%s (%s) pil=%#lx", td,
	    trap_msg[tf->tf_type & ~T_KERNEL],
	    (TRAPF_USERMODE(tf) ? "user" : "kernel"), rdpr(pil));

	VM_CNT_INC(v_trap);

	if ((tf->tf_tstate & TSTATE_PRIV) == 0) {
		KASSERT(td != NULL, ("trap: curthread NULL"));
		KASSERT(td->td_proc != NULL, ("trap: curproc NULL"));

		p = td->td_proc;
		td->td_pticks = 0;
		td->td_frame = tf;
		addr = tf->tf_tpc;
		ucode = (int)tf->tf_type; /* XXX not POSIX */
		if (td->td_cowgen != p->p_cowgen)
			thread_cow_update(td);

		switch (tf->tf_type) {
		case T_DATA_MISS:
		case T_DATA_PROTECTION:
			addr = tf->tf_sfar;
			/* FALLTHROUGH */
		case T_INSTRUCTION_MISS:
			sig = trap_pfault(td, tf);
			break;
		case T_FILL:
			sig = rwindow_load(td, tf, 2);
			break;
		case T_FILL_RET:
			sig = rwindow_load(td, tf, 1);
			break;
		case T_SPILL:
			sig = rwindow_save(td);
			break;
		case T_CORRECTED_ECC_ERROR:
			sig = trap_cecc();
			break;
		case T_BREAKPOINT:
			sig = SIGTRAP;
			ucode = TRAP_BRKPT;
			break;
		default:
			if (tf->tf_type > T_MAX)
				panic("trap: bad trap type %#lx (user)",
				    tf->tf_type);
			else if (trap_sig[tf->tf_type] == -1)
				panic("trap: %s (user)",
				    trap_msg[tf->tf_type]);
			sig = trap_sig[tf->tf_type];
			break;
		}

		if (sig != 0) {
			/* Translate fault for emulators. */
			if (p->p_sysent->sv_transtrap != NULL) {
				sig = p->p_sysent->sv_transtrap(sig,
				    tf->tf_type);
			}
			if (debugger_on_signal &&
			    (sig == 4 || sig == 10 || sig == 11))
				kdb_enter(KDB_WHY_TRAPSIG, "trapsig");
			ksiginfo_init_trap(&ksi);
			ksi.ksi_signo = sig;
			ksi.ksi_code = ucode;
			ksi.ksi_addr = (void *)addr;
			ksi.ksi_trapno = (int)tf->tf_type;
			trapsignal(td, &ksi);
		}

		userret(td, tf);
	} else {
		KASSERT((tf->tf_type & T_KERNEL) != 0,
		    ("trap: kernel trap isn't"));

		if (kdb_active) {
			kdb_reenter();
			return;
		}

		switch (tf->tf_type & ~T_KERNEL) {
		case T_BREAKPOINT:
		case T_KSTACK_FAULT:
			error = (kdb_trap(tf->tf_type, 0, tf) == 0);
			TF_DONE(tf);
			break;
#ifdef notyet
		case T_PA_WATCHPOINT:
		case T_VA_WATCHPOINT:
			error = db_watch_trap(tf);
			break;
#endif
		case T_DATA_MISS:
		case T_DATA_PROTECTION:
		case T_INSTRUCTION_MISS:
			error = trap_pfault(td, tf);
			break;
		case T_DATA_EXCEPTION:
		case T_MEM_ADDRESS_NOT_ALIGNED:
			if ((tf->tf_sfsr & MMU_SFSR_FV) != 0 &&
			    MMU_SFSR_GET_ASI(tf->tf_sfsr) == ASI_AIUP) {
				if (tf->tf_tpc >= (u_long)copy_nofault_begin &&
				    tf->tf_tpc <= (u_long)copy_nofault_end) {
					tf->tf_tpc = (u_long)copy_fault;
					tf->tf_tnpc = tf->tf_tpc + 4;
					error = 0;
					break;
				}
				if (tf->tf_tpc >= (u_long)fs_nofault_begin &&
				    tf->tf_tpc <= (u_long)fs_nofault_end) {
					tf->tf_tpc = (u_long)fs_fault;
					tf->tf_tnpc = tf->tf_tpc + 4;
					error = 0;
					break;
				}
			}
			error = 1;
			break;
		case T_DATA_ERROR:
			/*
			 * Handle PCI poke/peek as per UltraSPARC IIi
			 * User's Manual 16.2.1, modulo checking the
			 * TPC as USIII CPUs generate a precise trap
			 * instead of a special deferred one.
			 */
			if (tf->tf_tpc > (u_long)fas_nofault_begin &&
			    tf->tf_tpc < (u_long)fas_nofault_end) {
				cache_flush();
				cache_enable(PCPU_GET(impl));
				tf->tf_tpc = (u_long)fas_fault;
				tf->tf_tnpc = tf->tf_tpc + 4;
				error = 0;
				break;
			}
			error = 1;
			break;
		case T_CORRECTED_ECC_ERROR:
			error = trap_cecc();
			break;
		default:
			error = 1;
			break;
		}

		if (error != 0) {
			tf->tf_type &= ~T_KERNEL;
			if (tf->tf_type > T_MAX)
				panic("trap: bad trap type %#lx (kernel)",
				    tf->tf_type);
			panic("trap: %s (kernel)", trap_msg[tf->tf_type]);
		}
	}
	CTR1(KTR_TRAP, "trap: td=%p return", td);
}

static int
trap_cecc(void)
{
	u_long eee;

	/*
	 * Turn off (non-)correctable error reporting while we're dealing
	 * with the error.
	 */
	eee = ldxa(0, ASI_ESTATE_ERROR_EN_REG);
	stxa_sync(0, ASI_ESTATE_ERROR_EN_REG, eee & ~(AA_ESTATE_NCEEN |
	    AA_ESTATE_CEEN));
	/* Flush the caches in order ensure no corrupt data got installed. */
	cache_flush();
	/* Ensure the caches are still turned on (should be). */
	cache_enable(PCPU_GET(impl));
	/* Clear the error from the AFSR. */
	stxa_sync(0, ASI_AFSR, ldxa(0, ASI_AFSR));
	corrected_ecc++;
	printf("corrected ECC error\n");
	/* Turn (non-)correctable error reporting back on. */
	stxa_sync(0, ASI_ESTATE_ERROR_EN_REG, eee);
	return (0);
}

static int
trap_pfault(struct thread *td, struct trapframe *tf)
{
	vm_map_t map;
	struct proc *p;
	vm_offset_t va;
	vm_prot_t prot;
	vm_map_entry_t entry;
	u_long ctx;
	int type;
	int rv;

	if (td == NULL)
		return (-1);
	KASSERT(td->td_pcb != NULL, ("trap_pfault: pcb NULL"));
	KASSERT(td->td_proc != NULL, ("trap_pfault: curproc NULL"));
	KASSERT(td->td_proc->p_vmspace != NULL, ("trap_pfault: vmspace NULL"));

	p = td->td_proc;

	rv = KERN_SUCCESS;
	ctx = TLB_TAR_CTX(tf->tf_tar);
	type = tf->tf_type & ~T_KERNEL;
	va = TLB_TAR_VA(tf->tf_tar);

	CTR4(KTR_TRAP, "trap_pfault: td=%p pm_ctx=%#lx va=%#lx ctx=%#lx",
	    td, p->p_vmspace->vm_pmap.pm_context[curcpu], va, ctx);

	if (type == T_DATA_PROTECTION)
		prot = VM_PROT_WRITE;
	else {
		if (type == T_DATA_MISS)
			prot = VM_PROT_READ;
		else
			prot = VM_PROT_READ | VM_PROT_EXECUTE;
	}

	if (ctx != TLB_CTX_KERNEL) {
		/* This is a fault on non-kernel virtual memory. */
		map = &p->p_vmspace->vm_map;
	} else {
		/*
		 * This is a fault on kernel virtual memory.  Attempts to
		 * access kernel memory from user mode cause privileged
		 * action traps, not page fault.
		 */
		KASSERT(tf->tf_tstate & TSTATE_PRIV,
		    ("trap_pfault: fault on nucleus context from user mode"));

		if (tf->tf_tpc >= (u_long)copy_nofault_begin &&
		    tf->tf_tpc <= (u_long)copy_nofault_end) {
			vm_map_lock_read(kernel_map);
			if (vm_map_lookup_entry(kernel_map, va, &entry) &&
			    (entry->eflags & MAP_ENTRY_NOFAULT) != 0) {
				tf->tf_tpc = (u_long)copy_fault;
				tf->tf_tnpc = tf->tf_tpc + 4;
				vm_map_unlock_read(kernel_map);
				return (0);
			}
			vm_map_unlock_read(kernel_map);
		}
		map = kernel_map;
	}

	/* Fault in the page. */
	rv = vm_fault(map, va, prot, VM_FAULT_NORMAL);

	CTR3(KTR_TRAP, "trap_pfault: return td=%p va=%#lx rv=%d",
	    td, va, rv);
	if (rv == KERN_SUCCESS)
		return (0);
	if (ctx != TLB_CTX_KERNEL && (tf->tf_tstate & TSTATE_PRIV) != 0) {
		if (tf->tf_tpc >= (u_long)fs_nofault_begin &&
		    tf->tf_tpc <= (u_long)fs_nofault_end) {
			tf->tf_tpc = (u_long)fs_fault;
			tf->tf_tnpc = tf->tf_tpc + 4;
			return (0);
		}
		if (tf->tf_tpc >= (u_long)copy_nofault_begin &&
		    tf->tf_tpc <= (u_long)copy_nofault_end) {
			tf->tf_tpc = (u_long)copy_fault;
			tf->tf_tnpc = tf->tf_tpc + 4;
			return (0);
		}
	}
	return ((rv == KERN_PROTECTION_FAILURE) ? SIGBUS : SIGSEGV);
}

/* Maximum number of arguments that can be passed via the out registers. */
#define	REG_MAXARGS	6

int
cpu_fetch_syscall_args(struct thread *td)
{
	struct trapframe *tf;
	struct proc *p;
	register_t *argp;
	struct syscall_args *sa;
	int reg;
	int regcnt;
	int error;

	p = td->td_proc;
	tf = td->td_frame;
	sa = &td->td_sa;
	reg = 0;
	regcnt = REG_MAXARGS;

	sa->code = tf->tf_global[1];

	if (sa->code == SYS_syscall || sa->code == SYS___syscall) {
		sa->code = tf->tf_out[reg++];
		regcnt--;
	}

	if (sa->code >= p->p_sysent->sv_size)
		sa->callp = &p->p_sysent->sv_table[0];
	else
		sa->callp = &p->p_sysent->sv_table[sa->code];

	sa->narg = sa->callp->sy_narg;
	KASSERT(sa->narg <= sizeof(sa->args) / sizeof(sa->args[0]),
	    ("Too many syscall arguments!"));
	error = 0;
	argp = sa->args;
	bcopy(&tf->tf_out[reg], sa->args, sizeof(sa->args[0]) * regcnt);
	if (sa->narg > regcnt)
		error = copyin((void *)(tf->tf_out[6] + SPOFF +
		    offsetof(struct frame, fr_pad[6])), &sa->args[regcnt],
		    (sa->narg - regcnt) * sizeof(sa->args[0]));
	if (error == 0) {
		td->td_retval[0] = 0;
		td->td_retval[1] = 0;
	}

	return (error);
}

#include "../../kern/subr_syscall.c"

/*
 * Syscall handler
 * The arguments to the syscall are passed in the out registers by the caller,
 * and are saved in the trap frame.  The syscall number is passed in %g1 (and
 * also saved in the trap frame).
 */
void
syscall(struct trapframe *tf)
{
	struct thread *td;
	int error;

	td = curthread;
	td->td_frame = tf;

	KASSERT(td != NULL, ("trap: curthread NULL"));
	KASSERT(td->td_proc != NULL, ("trap: curproc NULL"));

	/*
	 * For syscalls, we don't want to retry the faulting instruction
	 * (usually), instead we need to advance one instruction.
	 */
	td->td_pcb->pcb_tpc = tf->tf_tpc;
	TF_DONE(tf);

	error = syscallenter(td);
	syscallret(td, error);
}
