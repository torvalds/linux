/*	$OpenBSD: trap.c,v 1.19 1998/09/30 12:40:41 pefo Exp $	*/
/* tracked to 1.23 */
/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
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
 * from: Utah Hdr: trap.c 1.32 91/04/06
 *
 *	from: @(#)trap.c	8.5 (Berkeley) 1/11/94
 *	JNPR: trap.c,v 1.13.2.2 2007/08/29 10:03:49 girish
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_ddb.h"
#include "opt_ktrace.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sysent.h>
#include <sys/proc.h>
#include <sys/kernel.h>
#include <sys/signalvar.h>
#include <sys/syscall.h>
#include <sys/lock.h>
#include <vm/vm.h>
#include <vm/vm_extern.h>
#include <vm/vm_kern.h>
#include <vm/vm_page.h>
#include <vm/vm_map.h>
#include <vm/vm_param.h>
#include <sys/vmmeter.h>
#include <sys/ptrace.h>
#include <sys/user.h>
#include <sys/buf.h>
#include <sys/vnode.h>
#include <sys/pioctl.h>
#include <sys/sysctl.h>
#include <sys/syslog.h>
#include <sys/bus.h>
#ifdef KTRACE
#include <sys/ktrace.h>
#endif
#include <net/netisr.h>

#include <machine/trap.h>
#include <machine/cpu.h>
#include <machine/cpuinfo.h>
#include <machine/pte.h>
#include <machine/pmap.h>
#include <machine/md_var.h>
#include <machine/mips_opcode.h>
#include <machine/frame.h>
#include <machine/regnum.h>
#include <machine/tls.h>

#ifdef DDB
#include <machine/db_machdep.h>
#include <ddb/db_sym.h>
#include <ddb/ddb.h>
#include <sys/kdb.h>
#endif

#ifdef KDTRACE_HOOKS
#include <sys/dtrace_bsd.h>
#endif

#ifdef TRAP_DEBUG
int trap_debug = 0;
SYSCTL_INT(_machdep, OID_AUTO, trap_debug, CTLFLAG_RW,
    &trap_debug, 0, "Debug information on all traps");
#endif

#define	lbu_macro(data, addr)						\
	__asm __volatile ("lbu %0, 0x0(%1)"				\
			: "=r" (data)	/* outputs */			\
			: "r" (addr));	/* inputs */

#define	lb_macro(data, addr)						\
	__asm __volatile ("lb %0, 0x0(%1)"				\
			: "=r" (data)	/* outputs */			\
			: "r" (addr));	/* inputs */

#define	lwl_macro(data, addr)						\
	__asm __volatile ("lwl %0, 0x0(%1)"				\
			: "=r" (data)	/* outputs */			\
			: "r" (addr));	/* inputs */

#define	lwr_macro(data, addr)						\
	__asm __volatile ("lwr %0, 0x0(%1)"				\
			: "=r" (data)	/* outputs */			\
			: "r" (addr));	/* inputs */

#define	ldl_macro(data, addr)						\
	__asm __volatile ("ldl %0, 0x0(%1)"				\
			: "=r" (data)	/* outputs */			\
			: "r" (addr));	/* inputs */

#define	ldr_macro(data, addr)						\
	__asm __volatile ("ldr %0, 0x0(%1)"				\
			: "=r" (data)	/* outputs */			\
			: "r" (addr));	/* inputs */

#define	sb_macro(data, addr)						\
	__asm __volatile ("sb %0, 0x0(%1)"				\
			:				/* outputs */	\
			: "r" (data), "r" (addr));	/* inputs */

#define	swl_macro(data, addr)						\
	__asm __volatile ("swl %0, 0x0(%1)"				\
			: 				/* outputs */	\
			: "r" (data), "r" (addr));	/* inputs */

#define	swr_macro(data, addr)						\
	__asm __volatile ("swr %0, 0x0(%1)"				\
			: 				/* outputs */	\
			: "r" (data), "r" (addr));	/* inputs */

#define	sdl_macro(data, addr)						\
	__asm __volatile ("sdl %0, 0x0(%1)"				\
			: 				/* outputs */	\
			: "r" (data), "r" (addr));	/* inputs */

#define	sdr_macro(data, addr)						\
	__asm __volatile ("sdr %0, 0x0(%1)"				\
			:				/* outputs */	\
			: "r" (data), "r" (addr));	/* inputs */

static void log_illegal_instruction(const char *, struct trapframe *);
static void log_bad_page_fault(char *, struct trapframe *, int);
static void log_frame_dump(struct trapframe *frame);
static void get_mapping_info(vm_offset_t, pd_entry_t **, pt_entry_t **);

int (*dtrace_invop_jump_addr)(struct trapframe *);

#ifdef TRAP_DEBUG
static void trap_frame_dump(struct trapframe *frame);
#endif

void (*machExceptionTable[]) (void)= {
/*
 * The kernel exception handlers.
 */
	MipsKernIntr,		/* external interrupt */
	MipsKernGenException,	/* TLB modification */
	MipsTLBInvalidException,/* TLB miss (load or instr. fetch) */
	MipsTLBInvalidException,/* TLB miss (store) */
	MipsKernGenException,	/* address error (load or I-fetch) */
	MipsKernGenException,	/* address error (store) */
	MipsKernGenException,	/* bus error (I-fetch) */
	MipsKernGenException,	/* bus error (load or store) */
	MipsKernGenException,	/* system call */
	MipsKernGenException,	/* breakpoint */
	MipsKernGenException,	/* reserved instruction */
	MipsKernGenException,	/* coprocessor unusable */
	MipsKernGenException,	/* arithmetic overflow */
	MipsKernGenException,	/* trap exception */
	MipsKernGenException,	/* virtual coherence exception inst */
	MipsKernGenException,	/* floating point exception */
	MipsKernGenException,	/* reserved */
	MipsKernGenException,	/* reserved */
	MipsKernGenException,	/* reserved */
	MipsKernGenException,	/* reserved */
	MipsKernGenException,	/* reserved */
	MipsKernGenException,	/* reserved */
	MipsKernGenException,	/* reserved */
	MipsKernGenException,	/* watch exception */
	MipsKernGenException,	/* reserved */
	MipsKernGenException,	/* reserved */
	MipsKernGenException,	/* reserved */
	MipsKernGenException,	/* reserved */
	MipsKernGenException,	/* reserved */
	MipsKernGenException,	/* reserved */
	MipsKernGenException,	/* reserved */
	MipsKernGenException,	/* virtual coherence exception data */
/*
 * The user exception handlers.
 */
	MipsUserIntr,		/* 0 */
	MipsUserGenException,	/* 1 */
	MipsTLBInvalidException,/* 2 */
	MipsTLBInvalidException,/* 3 */
	MipsUserGenException,	/* 4 */
	MipsUserGenException,	/* 5 */
	MipsUserGenException,	/* 6 */
	MipsUserGenException,	/* 7 */
	MipsUserGenException,	/* 8 */
	MipsUserGenException,	/* 9 */
	MipsUserGenException,	/* 10 */
	MipsUserGenException,	/* 11 */
	MipsUserGenException,	/* 12 */
	MipsUserGenException,	/* 13 */
	MipsUserGenException,	/* 14 */
	MipsUserGenException,	/* 15 */
	MipsUserGenException,	/* 16 */
	MipsUserGenException,	/* 17 */
	MipsUserGenException,	/* 18 */
	MipsUserGenException,	/* 19 */
	MipsUserGenException,	/* 20 */
	MipsUserGenException,	/* 21 */
	MipsUserGenException,	/* 22 */
	MipsUserGenException,	/* 23 */
	MipsUserGenException,	/* 24 */
	MipsUserGenException,	/* 25 */
	MipsUserGenException,	/* 26 */
	MipsUserGenException,	/* 27 */
	MipsUserGenException,	/* 28 */
	MipsUserGenException,	/* 29 */
	MipsUserGenException,	/* 20 */
	MipsUserGenException,	/* 31 */
};

char *trap_type[] = {
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
	"virtual coherency data",
};

#if !defined(SMP) && (defined(DDB) || defined(DEBUG))
struct trapdebug trapdebug[TRAPSIZE], *trp = trapdebug;
#endif

#define	KERNLAND(x)	((vm_offset_t)(x) >= VM_MIN_KERNEL_ADDRESS && (vm_offset_t)(x) < VM_MAX_KERNEL_ADDRESS)
#define	DELAYBRANCH(x)	((x) & MIPS_CR_BR_DELAY)

/*
 * MIPS load/store access type
 */
enum {
	MIPS_LHU_ACCESS = 1,
	MIPS_LH_ACCESS,
	MIPS_LWU_ACCESS,
	MIPS_LW_ACCESS,
	MIPS_LD_ACCESS,
	MIPS_SH_ACCESS,
	MIPS_SW_ACCESS,
	MIPS_SD_ACCESS
};

char *access_name[] = {
	"Load Halfword Unsigned",
	"Load Halfword",
	"Load Word Unsigned",
	"Load Word",
	"Load Doubleword",
	"Store Halfword",
	"Store Word",
	"Store Doubleword"
};

#ifdef	CPU_CNMIPS
#include <machine/octeon_cop2.h>
#endif

static int allow_unaligned_acc = 1;

SYSCTL_INT(_vm, OID_AUTO, allow_unaligned_acc, CTLFLAG_RW,
    &allow_unaligned_acc, 0, "Allow unaligned accesses");

/*
 * FP emulation is assumed to work on O32, but the code is outdated and crufty
 * enough that it's a more sensible default to have it disabled when using
 * other ABIs.  At the very least, it needs a lot of help in using
 * type-semantic ABI-oblivious macros for everything it does.
 */
#if defined(__mips_o32)
static int emulate_fp = 1;
#else
static int emulate_fp = 0;
#endif
SYSCTL_INT(_machdep, OID_AUTO, emulate_fp, CTLFLAG_RW,
    &emulate_fp, 0, "Emulate unimplemented FPU instructions");

static int emulate_unaligned_access(struct trapframe *frame, int mode);

extern void fswintrberr(void); /* XXX */

int
cpu_fetch_syscall_args(struct thread *td)
{
	struct trapframe *locr0;
	struct sysentvec *se;
	struct syscall_args *sa;
	int error, nsaved;

	locr0 = td->td_frame;
	sa = &td->td_sa;
	
	bzero(sa->args, sizeof(sa->args));

	/* compute next PC after syscall instruction */
	td->td_pcb->pcb_tpc = sa->trapframe->pc; /* Remember if restart */
	if (DELAYBRANCH(sa->trapframe->cause))	 /* Check BD bit */
		locr0->pc = MipsEmulateBranch(locr0, sa->trapframe->pc, 0, 0);
	else
		locr0->pc += sizeof(int);
	sa->code = locr0->v0;

	switch (sa->code) {
	case SYS___syscall:
	case SYS_syscall:
		/*
		 * This is an indirect syscall, in which the code is the first argument.
		 */
#if (!defined(__mips_n32) && !defined(__mips_n64)) || defined(COMPAT_FREEBSD32)
		if (sa->code == SYS___syscall && SV_PROC_FLAG(td->td_proc, SV_ILP32)) {
			/*
			 * Like syscall, but code is a quad, so as to maintain alignment
			 * for the rest of the arguments.
			 */
			if (_QUAD_LOWWORD == 0)
				sa->code = locr0->a0;
			else
				sa->code = locr0->a1;
			sa->args[0] = locr0->a2;
			sa->args[1] = locr0->a3;
			nsaved = 2;
			break;
		} 
#endif
		/*
		 * This is either not a quad syscall, or is a quad syscall with a
		 * new ABI in which quads fit in a single register.
		 */
		sa->code = locr0->a0;
		sa->args[0] = locr0->a1;
		sa->args[1] = locr0->a2;
		sa->args[2] = locr0->a3;
		nsaved = 3;
#if defined(__mips_n32) || defined(__mips_n64)
#ifdef COMPAT_FREEBSD32
		if (!SV_PROC_FLAG(td->td_proc, SV_ILP32)) {
#endif
			/*
			 * Non-o32 ABIs support more arguments in registers.
			 */
			sa->args[3] = locr0->a4;
			sa->args[4] = locr0->a5;
			sa->args[5] = locr0->a6;
			sa->args[6] = locr0->a7;
			nsaved += 4;
#ifdef COMPAT_FREEBSD32
		}
#endif
#endif
		break;
	default:
		/*
		 * A direct syscall, arguments are just parameters to the syscall.
		 */
		sa->args[0] = locr0->a0;
		sa->args[1] = locr0->a1;
		sa->args[2] = locr0->a2;
		sa->args[3] = locr0->a3;
		nsaved = 4;
#if defined (__mips_n32) || defined(__mips_n64)
#ifdef COMPAT_FREEBSD32
		if (!SV_PROC_FLAG(td->td_proc, SV_ILP32)) {
#endif
			/*
			 * Non-o32 ABIs support more arguments in registers.
			 */
			sa->args[4] = locr0->a4;
			sa->args[5] = locr0->a5;
			sa->args[6] = locr0->a6;
			sa->args[7] = locr0->a7;
			nsaved += 4;
#ifdef COMPAT_FREEBSD32
		}
#endif
#endif
		break;
	}

#ifdef TRAP_DEBUG
	if (trap_debug)
		printf("SYSCALL #%d pid:%u\n", sa->code, td->td_proc->p_pid);
#endif

	se = td->td_proc->p_sysent;
	/*
	 * XXX
	 * Shouldn't this go before switching on the code?
	 */

	if (sa->code >= se->sv_size)
		sa->callp = &se->sv_table[0];
	else
		sa->callp = &se->sv_table[sa->code];

	sa->narg = sa->callp->sy_narg;

	if (sa->narg > nsaved) {
#if defined(__mips_n32) || defined(__mips_n64)
		/*
		 * XXX
		 * Is this right for new ABIs?  I think the 4 there
		 * should be 8, size there are 8 registers to skip,
		 * not 4, but I'm not certain.
		 */
#ifdef COMPAT_FREEBSD32
		if (!SV_PROC_FLAG(td->td_proc, SV_ILP32))
#endif
			printf("SYSCALL #%u pid:%u, narg (%u) > nsaved (%u).\n",
			    sa->code, td->td_proc->p_pid, sa->narg, nsaved);
#endif
#if (defined(__mips_n32) || defined(__mips_n64)) && defined(COMPAT_FREEBSD32)
		if (SV_PROC_FLAG(td->td_proc, SV_ILP32)) {
			unsigned i;
			int32_t arg;

			error = 0; /* XXX GCC is awful.  */
			for (i = nsaved; i < sa->narg; i++) {
				error = copyin((caddr_t)(intptr_t)(locr0->sp +
				    (4 + (i - nsaved)) * sizeof(int32_t)),
				    (caddr_t)&arg, sizeof arg);
				if (error != 0)
					break;
				sa->args[i] = arg;
			}
		} else
#endif
		error = copyin((caddr_t)(intptr_t)(locr0->sp +
		    4 * sizeof(register_t)), (caddr_t)&sa->args[nsaved],
		   (u_int)(sa->narg - nsaved) * sizeof(register_t));
		if (error != 0) {
			locr0->v0 = error;
			locr0->a3 = 1;
		}
	} else
		error = 0;

	if (error == 0) {
		td->td_retval[0] = 0;
		td->td_retval[1] = locr0->v1;
	}

	return (error);
}

#undef __FBSDID
#define __FBSDID(x)
#include "../../kern/subr_syscall.c"

/*
 * Handle an exception.
 * Called from MipsKernGenException() or MipsUserGenException()
 * when a processor trap occurs.
 * In the case of a kernel trap, we return the pc where to resume if
 * p->p_addr->u_pcb.pcb_onfault is set, otherwise, return old pc.
 */
register_t
trap(struct trapframe *trapframe)
{
	int type, usermode;
	int i = 0;
	unsigned ucode = 0;
	struct thread *td = curthread;
	struct proc *p = curproc;
	vm_prot_t ftype;
	pmap_t pmap;
	int access_type;
	ksiginfo_t ksi;
	char *msg = NULL;
	intptr_t addr = 0;
	register_t pc;
	int cop, error;
	register_t *frame_regs;

	trapdebug_enter(trapframe, 0);
#ifdef KDB
	if (kdb_active) {
		kdb_reenter();
		return (0);
	}
#endif
	type = (trapframe->cause & MIPS_CR_EXC_CODE) >> MIPS_CR_EXC_CODE_SHIFT;
	if (TRAPF_USERMODE(trapframe)) {
		type |= T_USER;
		usermode = 1;
	} else {
		usermode = 0;
	}

	/*
	 * Enable hardware interrupts if they were on before the trap. If it
	 * was off disable all so we don't accidently enable it when doing a
	 * return to userland.
	 */
	if (trapframe->sr & MIPS_SR_INT_IE) {
		set_intr_mask(trapframe->sr & MIPS_SR_INT_MASK);
		intr_enable();
	} else {
		intr_disable();
	}

#ifdef TRAP_DEBUG
	if (trap_debug) {
		static vm_offset_t last_badvaddr = 0;
		static vm_offset_t this_badvaddr = 0;
		static int count = 0;
		u_int32_t pid;

		printf("trap type %x (%s - ", type,
		    trap_type[type & (~T_USER)]);

		if (type & T_USER)
			printf("user mode)\n");
		else
			printf("kernel mode)\n");

#ifdef SMP
		printf("cpuid = %d\n", PCPU_GET(cpuid));
#endif
		pid = mips_rd_entryhi() & TLBHI_ASID_MASK;
		printf("badaddr = %#jx, pc = %#jx, ra = %#jx, sp = %#jx, sr = %jx, pid = %d, ASID = %u\n",
		    (intmax_t)trapframe->badvaddr, (intmax_t)trapframe->pc, (intmax_t)trapframe->ra,
		    (intmax_t)trapframe->sp, (intmax_t)trapframe->sr,
		    (curproc ? curproc->p_pid : -1), pid);

		switch (type & ~T_USER) {
		case T_TLB_MOD:
		case T_TLB_LD_MISS:
		case T_TLB_ST_MISS:
		case T_ADDR_ERR_LD:
		case T_ADDR_ERR_ST:
			this_badvaddr = trapframe->badvaddr;
			break;
		case T_SYSCALL:
			this_badvaddr = trapframe->ra;
			break;
		default:
			this_badvaddr = trapframe->pc;
			break;
		}
		if ((last_badvaddr == this_badvaddr) &&
		    ((type & ~T_USER) != T_SYSCALL) &&
		    ((type & ~T_USER) != T_COP_UNUSABLE)) {
			if (++count == 3) {
				trap_frame_dump(trapframe);
				panic("too many faults at %p\n", (void *)last_badvaddr);
			}
		} else {
			last_badvaddr = this_badvaddr;
			count = 0;
		}
	}
#endif

#ifdef KDTRACE_HOOKS
	/*
	 * A trap can occur while DTrace executes a probe. Before
	 * executing the probe, DTrace blocks re-scheduling and sets
	 * a flag in its per-cpu flags to indicate that it doesn't
	 * want to fault. On returning from the probe, the no-fault
	 * flag is cleared and finally re-scheduling is enabled.
	 *
	 * If the DTrace kernel module has registered a trap handler,
	 * call it and if it returns non-zero, assume that it has
	 * handled the trap and modified the trap frame so that this
	 * function can return normally.
	 */
	/*
	 * XXXDTRACE: add pid probe handler here (if ever)
	 */
	if (!usermode) {
		if (dtrace_trap_func != NULL &&
		    (*dtrace_trap_func)(trapframe, type) != 0)
			return (trapframe->pc);
	}
#endif

	switch (type) {
	case T_MCHECK:
#ifdef DDB
		kdb_trap(type, 0, trapframe);
#endif
		panic("MCHECK\n");
		break;
	case T_TLB_MOD:
		/* check for kernel address */
		if (KERNLAND(trapframe->badvaddr)) {
			if (pmap_emulate_modified(kernel_pmap, 
			    trapframe->badvaddr) != 0) {
				ftype = VM_PROT_WRITE;
				goto kernel_fault;
			}
			return (trapframe->pc);
		}
		/* FALLTHROUGH */

	case T_TLB_MOD + T_USER:
		pmap = &p->p_vmspace->vm_pmap;
		if (pmap_emulate_modified(pmap, trapframe->badvaddr) != 0) {
			ftype = VM_PROT_WRITE;
			goto dofault;
		}
		if (!usermode)
			return (trapframe->pc);
		goto out;

	case T_TLB_LD_MISS:
	case T_TLB_ST_MISS:
		ftype = (type == T_TLB_ST_MISS) ? VM_PROT_WRITE : VM_PROT_READ;
		/* check for kernel address */
		if (KERNLAND(trapframe->badvaddr)) {
			vm_offset_t va;
			int rv;

	kernel_fault:
			va = trunc_page((vm_offset_t)trapframe->badvaddr);
			rv = vm_fault(kernel_map, va, ftype, VM_FAULT_NORMAL);
			if (rv == KERN_SUCCESS)
				return (trapframe->pc);
			if (td->td_pcb->pcb_onfault != NULL) {
				pc = (register_t)(intptr_t)td->td_pcb->pcb_onfault;
				td->td_pcb->pcb_onfault = NULL;
				return (pc);
			}
			goto err;
		}

		/*
		 * It is an error for the kernel to access user space except
		 * through the copyin/copyout routines.
		 */
		if (td->td_pcb->pcb_onfault == NULL)
			goto err;

		goto dofault;

	case T_TLB_LD_MISS + T_USER:
		ftype = VM_PROT_READ;
		goto dofault;

	case T_TLB_ST_MISS + T_USER:
		ftype = VM_PROT_WRITE;
dofault:
		{
			vm_offset_t va;
			struct vmspace *vm;
			vm_map_t map;
			int rv = 0;

			vm = p->p_vmspace;
			map = &vm->vm_map;
			va = trunc_page((vm_offset_t)trapframe->badvaddr);
			if (KERNLAND(trapframe->badvaddr)) {
				/*
				 * Don't allow user-mode faults in kernel
				 * address space.
				 */
				goto nogo;
			}

			rv = vm_fault(map, va, ftype, VM_FAULT_NORMAL);
			/*
			 * XXXDTRACE: add dtrace_doubletrap_func here?
			 */
#ifdef VMFAULT_TRACE
			printf("vm_fault(%p (pmap %p), %p (%p), %x, %d) -> %x at pc %p\n",
			    map, &vm->vm_pmap, (void *)va, (void *)(intptr_t)trapframe->badvaddr,
			    ftype, VM_FAULT_NORMAL, rv, (void *)(intptr_t)trapframe->pc);
#endif

			if (rv == KERN_SUCCESS) {
				if (!usermode) {
					return (trapframe->pc);
				}
				goto out;
			}
	nogo:
			if (!usermode) {
				if (td->td_pcb->pcb_onfault != NULL) {
					pc = (register_t)(intptr_t)td->td_pcb->pcb_onfault;
					td->td_pcb->pcb_onfault = NULL;
					return (pc);
				}
				goto err;
			}
			i = SIGSEGV;
			if (rv == KERN_PROTECTION_FAILURE)
				ucode = SEGV_ACCERR;
			else
				ucode = SEGV_MAPERR;
			addr = trapframe->pc;

			msg = "BAD_PAGE_FAULT";
			log_bad_page_fault(msg, trapframe, type);

			break;
		}

	case T_ADDR_ERR_LD + T_USER:	/* misaligned or kseg access */
	case T_ADDR_ERR_ST + T_USER:	/* misaligned or kseg access */
		if (trapframe->badvaddr < 0 ||
		    trapframe->badvaddr >= VM_MAXUSER_ADDRESS) {
			msg = "ADDRESS_SPACE_ERR";
		} else if (allow_unaligned_acc) {
			int mode;

			if (type == (T_ADDR_ERR_LD + T_USER))
				mode = VM_PROT_READ;
			else
				mode = VM_PROT_WRITE;

			access_type = emulate_unaligned_access(trapframe, mode);
			if (access_type != 0)
				goto out;
			msg = "ALIGNMENT_FIX_ERR";
		} else {
			msg = "ADDRESS_ERR";
		}

		/* FALL THROUGH */

	case T_BUS_ERR_IFETCH + T_USER:	/* BERR asserted to cpu */
	case T_BUS_ERR_LD_ST + T_USER:	/* BERR asserted to cpu */
		ucode = 0;	/* XXX should be VM_PROT_something */
		i = SIGBUS;
		addr = trapframe->pc;
		if (!msg)
			msg = "BUS_ERR";
		log_bad_page_fault(msg, trapframe, type);
		break;

	case T_SYSCALL + T_USER:
		{
			int error;

			td->td_sa.trapframe = trapframe;
			error = syscallenter(td);

#if !defined(SMP) && (defined(DDB) || defined(DEBUG))
			if (trp == trapdebug)
				trapdebug[TRAPSIZE - 1].code = td->td_sa.code;
			else
				trp[-1].code = td->td_sa.code;
#endif
			trapdebug_enter(td->td_frame, -td->td_sa.code);

			/*
			 * The sync'ing of I & D caches for SYS_ptrace() is
			 * done by procfs_domem() through procfs_rwmem()
			 * instead of being done here under a special check
			 * for SYS_ptrace().
			 */
			syscallret(td, error);
			return (trapframe->pc);
		}

#if defined(KDTRACE_HOOKS) || defined(DDB)
	case T_BREAK:
#ifdef KDTRACE_HOOKS
		if (!usermode && dtrace_invop_jump_addr != 0) {
			dtrace_invop_jump_addr(trapframe);
			return (trapframe->pc);
		}
#endif
#ifdef DDB
		kdb_trap(type, 0, trapframe);
		return (trapframe->pc);
#endif
#endif

	case T_BREAK + T_USER:
		{
			intptr_t va;
			uint32_t instr;

			i = SIGTRAP;
			ucode = TRAP_BRKPT;
			addr = trapframe->pc;

			/* compute address of break instruction */
			va = trapframe->pc;
			if (DELAYBRANCH(trapframe->cause))
				va += sizeof(int);

			if (td->td_md.md_ss_addr != va)
				break;

			/* read break instruction */
			instr = fuword32((caddr_t)va);

			if (instr != MIPS_BREAK_SSTEP)
				break;

			CTR3(KTR_PTRACE,
			    "trap: tid %d, single step at %#lx: %#08x",
			    td->td_tid, va, instr);
			PROC_LOCK(p);
			_PHOLD(p);
			error = ptrace_clear_single_step(td);
			_PRELE(p);
			PROC_UNLOCK(p);
			if (error == 0)
				ucode = TRAP_TRACE;
			break;
		}

	case T_IWATCH + T_USER:
	case T_DWATCH + T_USER:
		{
			intptr_t va;

			/* compute address of trapped instruction */
			va = trapframe->pc;
			if (DELAYBRANCH(trapframe->cause))
				va += sizeof(int);
			printf("watch exception @ %p\n", (void *)va);
			i = SIGTRAP;
			ucode = TRAP_BRKPT;
			addr = va;
			break;
		}

	case T_TRAP + T_USER:
		{
			intptr_t va;
			struct trapframe *locr0 = td->td_frame;

			/* compute address of trap instruction */
			va = trapframe->pc;
			if (DELAYBRANCH(trapframe->cause))
				va += sizeof(int);

			if (DELAYBRANCH(trapframe->cause)) {	/* Check BD bit */
				locr0->pc = MipsEmulateBranch(locr0, trapframe->pc, 0,
				    0);
			} else {
				locr0->pc += sizeof(int);
			}
			addr = va;
			i = SIGEMT;	/* Stuff it with something for now */
			break;
		}

	case T_RES_INST + T_USER:
		{
			InstFmt inst;
			inst = *(InstFmt *)(intptr_t)trapframe->pc;
			switch (inst.RType.op) {
			case OP_SPECIAL3:
				switch (inst.RType.func) {
				case OP_RDHWR:
					/* Register 29 used for TLS */
					if (inst.RType.rd == 29) {
						frame_regs = &(trapframe->zero);
						frame_regs[inst.RType.rt] = (register_t)(intptr_t)td->td_md.md_tls;
						frame_regs[inst.RType.rt] += td->td_md.md_tls_tcb_offset;
						trapframe->pc += sizeof(int);
						goto out;
					}
				break;
				}
			break;
			}

			log_illegal_instruction("RES_INST", trapframe);
			i = SIGILL;
			addr = trapframe->pc;
		}
		break;
	case T_C2E:
	case T_C2E + T_USER:
		goto err;
		break;
	case T_COP_UNUSABLE:
#ifdef	CPU_CNMIPS
		cop = (trapframe->cause & MIPS_CR_COP_ERR) >> MIPS_CR_COP_ERR_SHIFT;
		/* Handle only COP2 exception */
		if (cop != 2)
			goto err;

		addr = trapframe->pc;
		/* save userland cop2 context if it has been touched */
		if ((td->td_md.md_flags & MDTD_COP2USED) &&
		    (td->td_md.md_cop2owner == COP2_OWNER_USERLAND)) {
			if (td->td_md.md_ucop2)
				octeon_cop2_save(td->td_md.md_ucop2);
			else
				panic("COP2 was used in user mode but md_ucop2 is NULL");
		}

		if (td->td_md.md_cop2 == NULL) {
			td->td_md.md_cop2 = octeon_cop2_alloc_ctx();
			if (td->td_md.md_cop2 == NULL)
				panic("Failed to allocate COP2 context");
			memset(td->td_md.md_cop2, 0, sizeof(*td->td_md.md_cop2));
		}

		octeon_cop2_restore(td->td_md.md_cop2);
		
		/* Make userland re-request its context */
		td->td_frame->sr &= ~MIPS_SR_COP_2_BIT;
		td->td_md.md_flags |= MDTD_COP2USED;
		td->td_md.md_cop2owner = COP2_OWNER_KERNEL;
		/* Enable COP2, it will be disabled in cpu_switch */
		mips_wr_status(mips_rd_status() | MIPS_SR_COP_2_BIT);
		return (trapframe->pc);
#else
		goto err;
		break;
#endif

	case T_COP_UNUSABLE + T_USER:
		cop = (trapframe->cause & MIPS_CR_COP_ERR) >> MIPS_CR_COP_ERR_SHIFT;
		if (cop == 1) {
			/* FP (COP1) instruction */
			if (cpuinfo.fpu_id == 0) {
				log_illegal_instruction("COP1_UNUSABLE",
				    trapframe);
				i = SIGILL;
				break;
			}
			addr = trapframe->pc;
			MipsSwitchFPState(PCPU_GET(fpcurthread), td->td_frame);
			PCPU_SET(fpcurthread, td);
#if defined(__mips_n32) || defined(__mips_n64)
			td->td_frame->sr |= MIPS_SR_COP_1_BIT | MIPS_SR_FR;
#else
			td->td_frame->sr |= MIPS_SR_COP_1_BIT;
#endif
			td->td_md.md_flags |= MDTD_FPUSED;
			goto out;
		}
#ifdef	CPU_CNMIPS
		else  if (cop == 2) {
			addr = trapframe->pc;
			if ((td->td_md.md_flags & MDTD_COP2USED) &&
			    (td->td_md.md_cop2owner == COP2_OWNER_KERNEL)) {
				if (td->td_md.md_cop2)
					octeon_cop2_save(td->td_md.md_cop2);
				else
					panic("COP2 was used in kernel mode but md_cop2 is NULL");
			}

			if (td->td_md.md_ucop2 == NULL) {
				td->td_md.md_ucop2 = octeon_cop2_alloc_ctx();
				if (td->td_md.md_ucop2 == NULL)
					panic("Failed to allocate userland COP2 context");
				memset(td->td_md.md_ucop2, 0, sizeof(*td->td_md.md_ucop2));
			}

			octeon_cop2_restore(td->td_md.md_ucop2);

			td->td_frame->sr |= MIPS_SR_COP_2_BIT;
			td->td_md.md_flags |= MDTD_COP2USED;
			td->td_md.md_cop2owner = COP2_OWNER_USERLAND;
			goto out;
		}
#endif
		else {
			log_illegal_instruction("COPn_UNUSABLE", trapframe);
			i = SIGILL;	/* only FPU instructions allowed */
			break;
		}

	case T_FPE:
#if !defined(SMP) && (defined(DDB) || defined(DEBUG))
		trapDump("fpintr");
#else
		printf("FPU Trap: PC %#jx CR %x SR %x\n",
		    (intmax_t)trapframe->pc, (unsigned)trapframe->cause, (unsigned)trapframe->sr);
		goto err;
#endif

	case T_FPE + T_USER:
		if (!emulate_fp) {
			i = SIGFPE;
			addr = trapframe->pc;
			break;
		}
		MipsFPTrap(trapframe->sr, trapframe->cause, trapframe->pc);
		goto out;

	case T_OVFLOW + T_USER:
		i = SIGFPE;
		addr = trapframe->pc;
		break;

	case T_ADDR_ERR_LD:	/* misaligned access */
	case T_ADDR_ERR_ST:	/* misaligned access */
#ifdef TRAP_DEBUG
		if (trap_debug) {
			printf("+++ ADDR_ERR: type = %d, badvaddr = %#jx\n", type,
			    (intmax_t)trapframe->badvaddr);
		}
#endif
		/* Only allow emulation on a user address */
		if (allow_unaligned_acc &&
		    ((vm_offset_t)trapframe->badvaddr < VM_MAXUSER_ADDRESS)) {
			int mode;

			if (type == T_ADDR_ERR_LD)
				mode = VM_PROT_READ;
			else
				mode = VM_PROT_WRITE;

			access_type = emulate_unaligned_access(trapframe, mode);
			if (access_type != 0)
				return (trapframe->pc);
		}
		/* FALLTHROUGH */

	case T_BUS_ERR_LD_ST:	/* BERR asserted to cpu */
		if (td->td_pcb->pcb_onfault != NULL) {
			pc = (register_t)(intptr_t)td->td_pcb->pcb_onfault;
			td->td_pcb->pcb_onfault = NULL;
			return (pc);
		}

		/* FALLTHROUGH */

	default:
err:

#if !defined(SMP) && defined(DEBUG)
		trapDump("trap");
#endif
#ifdef SMP
		printf("cpu:%d-", PCPU_GET(cpuid));
#endif
		printf("Trap cause = %d (%s - ", type,
		    trap_type[type & (~T_USER)]);

		if (type & T_USER)
			printf("user mode)\n");
		else
			printf("kernel mode)\n");

#ifdef TRAP_DEBUG
		if (trap_debug)
			printf("badvaddr = %#jx, pc = %#jx, ra = %#jx, sr = %#jxx\n",
			       (intmax_t)trapframe->badvaddr, (intmax_t)trapframe->pc, (intmax_t)trapframe->ra,
			       (intmax_t)trapframe->sr);
#endif

#ifdef KDB
		if (debugger_on_trap) {
			kdb_why = KDB_WHY_TRAP;
			kdb_trap(type, 0, trapframe);
			kdb_why = KDB_WHY_UNSET;
		}
#endif
		panic("trap");
	}
	td->td_frame->pc = trapframe->pc;
	td->td_frame->cause = trapframe->cause;
	td->td_frame->badvaddr = trapframe->badvaddr;
	ksiginfo_init_trap(&ksi);
	ksi.ksi_signo = i;
	ksi.ksi_code = ucode;
	ksi.ksi_addr = (void *)addr;
	ksi.ksi_trapno = type;
	trapsignal(td, &ksi);
out:

	/*
	 * Note: we should only get here if returning to user mode.
	 */
	userret(td, trapframe);
	return (trapframe->pc);
}

#if !defined(SMP) && (defined(DDB) || defined(DEBUG))
void
trapDump(char *msg)
{
	register_t s;
	int i;

	s = intr_disable();
	printf("trapDump(%s)\n", msg);
	for (i = 0; i < TRAPSIZE; i++) {
		if (trp == trapdebug) {
			trp = &trapdebug[TRAPSIZE - 1];
		} else {
			trp--;
		}

		if (trp->cause == 0)
			break;

		printf("%s: ADR %jx PC %jx CR %jx SR %jx\n",
		    trap_type[(trp->cause & MIPS_CR_EXC_CODE) >> 
			MIPS_CR_EXC_CODE_SHIFT],
		    (intmax_t)trp->vadr, (intmax_t)trp->pc,
		    (intmax_t)trp->cause, (intmax_t)trp->status);

		printf("   RA %jx SP %jx code %d\n", (intmax_t)trp->ra,
		    (intmax_t)trp->sp, (int)trp->code);
	}
	intr_restore(s);
}
#endif


/*
 * Return the resulting PC as if the branch was executed.
 */
uintptr_t
MipsEmulateBranch(struct trapframe *framePtr, uintptr_t instPC, int fpcCSR,
    uintptr_t instptr)
{
	InstFmt inst;
	register_t *regsPtr = (register_t *) framePtr;
	uintptr_t retAddr = 0;
	int condition;

#define	GetBranchDest(InstPtr, inst) \
	(InstPtr + 4 + ((short)inst.IType.imm << 2))


	if (instptr) {
		if (instptr < MIPS_KSEG0_START)
			inst.word = fuword32((void *)instptr);
		else
			inst = *(InstFmt *) instptr;
	} else {
		if ((vm_offset_t)instPC < MIPS_KSEG0_START)
			inst.word = fuword32((void *)instPC);
		else
			inst = *(InstFmt *) instPC;
	}

	switch ((int)inst.JType.op) {
	case OP_SPECIAL:
		switch ((int)inst.RType.func) {
		case OP_JR:
		case OP_JALR:
			retAddr = regsPtr[inst.RType.rs];
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
			if ((int)(regsPtr[inst.RType.rs]) < 0)
				retAddr = GetBranchDest(instPC, inst);
			else
				retAddr = instPC + 8;
			break;

		case OP_BGEZ:
		case OP_BGEZL:
		case OP_BGEZAL:
		case OP_BGEZALL:
			if ((int)(regsPtr[inst.RType.rs]) >= 0)
				retAddr = GetBranchDest(instPC, inst);
			else
				retAddr = instPC + 8;
			break;

		case OP_TGEI:
		case OP_TGEIU:
		case OP_TLTI:
		case OP_TLTIU:
		case OP_TEQI:
		case OP_TNEI:
			retAddr = instPC + 4;	/* Like syscall... */
			break;

		default:
			panic("MipsEmulateBranch: Bad branch cond");
		}
		break;

	case OP_J:
	case OP_JAL:
		retAddr = (inst.JType.target << 2) |
		    ((unsigned)(instPC + 4) & 0xF0000000);
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
		if ((int)(regsPtr[inst.RType.rs]) <= 0)
			retAddr = GetBranchDest(instPC, inst);
		else
			retAddr = instPC + 8;
		break;

	case OP_BGTZ:
	case OP_BGTZL:
		if ((int)(regsPtr[inst.RType.rs]) > 0)
			retAddr = GetBranchDest(instPC, inst);
		else
			retAddr = instPC + 8;
		break;

	case OP_COP1:
		switch (inst.RType.rs) {
		case OP_BCx:
		case OP_BCy:
			if ((inst.RType.rt & COPz_BC_TF_MASK) == COPz_BC_TRUE)
				condition = fpcCSR & MIPS_FPU_COND_BIT;
			else
				condition = !(fpcCSR & MIPS_FPU_COND_BIT);
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
	return (retAddr);
}

static void
log_frame_dump(struct trapframe *frame)
{
	log(LOG_ERR, "Trapframe Register Dump:\n");
	log(LOG_ERR, "\tzero: %#jx\tat: %#jx\tv0: %#jx\tv1: %#jx\n",
	    (intmax_t)0, (intmax_t)frame->ast, (intmax_t)frame->v0, (intmax_t)frame->v1);

	log(LOG_ERR, "\ta0: %#jx\ta1: %#jx\ta2: %#jx\ta3: %#jx\n",
	    (intmax_t)frame->a0, (intmax_t)frame->a1, (intmax_t)frame->a2, (intmax_t)frame->a3);

#if defined(__mips_n32) || defined(__mips_n64)
	log(LOG_ERR, "\ta4: %#jx\ta5: %#jx\ta6: %#jx\ta6: %#jx\n",
	    (intmax_t)frame->a4, (intmax_t)frame->a5, (intmax_t)frame->a6, (intmax_t)frame->a7);

	log(LOG_ERR, "\tt0: %#jx\tt1: %#jx\tt2: %#jx\tt3: %#jx\n",
	    (intmax_t)frame->t0, (intmax_t)frame->t1, (intmax_t)frame->t2, (intmax_t)frame->t3);
#else
	log(LOG_ERR, "\tt0: %#jx\tt1: %#jx\tt2: %#jx\tt3: %#jx\n",
	    (intmax_t)frame->t0, (intmax_t)frame->t1, (intmax_t)frame->t2, (intmax_t)frame->t3);

	log(LOG_ERR, "\tt4: %#jx\tt5: %#jx\tt6: %#jx\tt7: %#jx\n",
	    (intmax_t)frame->t4, (intmax_t)frame->t5, (intmax_t)frame->t6, (intmax_t)frame->t7);
#endif
	log(LOG_ERR, "\tt8: %#jx\tt9: %#jx\ts0: %#jx\ts1: %#jx\n",
	    (intmax_t)frame->t8, (intmax_t)frame->t9, (intmax_t)frame->s0, (intmax_t)frame->s1);

	log(LOG_ERR, "\ts2: %#jx\ts3: %#jx\ts4: %#jx\ts5: %#jx\n",
	    (intmax_t)frame->s2, (intmax_t)frame->s3, (intmax_t)frame->s4, (intmax_t)frame->s5);

	log(LOG_ERR, "\ts6: %#jx\ts7: %#jx\tk0: %#jx\tk1: %#jx\n",
	    (intmax_t)frame->s6, (intmax_t)frame->s7, (intmax_t)frame->k0, (intmax_t)frame->k1);

	log(LOG_ERR, "\tgp: %#jx\tsp: %#jx\ts8: %#jx\tra: %#jx\n",
	    (intmax_t)frame->gp, (intmax_t)frame->sp, (intmax_t)frame->s8, (intmax_t)frame->ra);

	log(LOG_ERR, "\tsr: %#jx\tmullo: %#jx\tmulhi: %#jx\tbadvaddr: %#jx\n",
	    (intmax_t)frame->sr, (intmax_t)frame->mullo, (intmax_t)frame->mulhi, (intmax_t)frame->badvaddr);

	log(LOG_ERR, "\tcause: %#jx\tpc: %#jx\n",
	    (intmax_t)frame->cause, (intmax_t)frame->pc);
}

#ifdef TRAP_DEBUG
static void
trap_frame_dump(struct trapframe *frame)
{
	printf("Trapframe Register Dump:\n");
	printf("\tzero: %#jx\tat: %#jx\tv0: %#jx\tv1: %#jx\n",
	    (intmax_t)0, (intmax_t)frame->ast, (intmax_t)frame->v0, (intmax_t)frame->v1);

	printf("\ta0: %#jx\ta1: %#jx\ta2: %#jx\ta3: %#jx\n",
	    (intmax_t)frame->a0, (intmax_t)frame->a1, (intmax_t)frame->a2, (intmax_t)frame->a3);
#if defined(__mips_n32) || defined(__mips_n64)
	printf("\ta4: %#jx\ta5: %#jx\ta6: %#jx\ta7: %#jx\n",
	    (intmax_t)frame->a4, (intmax_t)frame->a5, (intmax_t)frame->a6, (intmax_t)frame->a7);

	printf("\tt0: %#jx\tt1: %#jx\tt2: %#jx\tt3: %#jx\n",
	    (intmax_t)frame->t0, (intmax_t)frame->t1, (intmax_t)frame->t2, (intmax_t)frame->t3);
#else
	printf("\tt0: %#jx\tt1: %#jx\tt2: %#jx\tt3: %#jx\n",
	    (intmax_t)frame->t0, (intmax_t)frame->t1, (intmax_t)frame->t2, (intmax_t)frame->t3);

	printf("\tt4: %#jx\tt5: %#jx\tt6: %#jx\tt7: %#jx\n",
	    (intmax_t)frame->t4, (intmax_t)frame->t5, (intmax_t)frame->t6, (intmax_t)frame->t7);
#endif
	printf("\tt8: %#jx\tt9: %#jx\ts0: %#jx\ts1: %#jx\n",
	    (intmax_t)frame->t8, (intmax_t)frame->t9, (intmax_t)frame->s0, (intmax_t)frame->s1);

	printf("\ts2: %#jx\ts3: %#jx\ts4: %#jx\ts5: %#jx\n",
	    (intmax_t)frame->s2, (intmax_t)frame->s3, (intmax_t)frame->s4, (intmax_t)frame->s5);

	printf("\ts6: %#jx\ts7: %#jx\tk0: %#jx\tk1: %#jx\n",
	    (intmax_t)frame->s6, (intmax_t)frame->s7, (intmax_t)frame->k0, (intmax_t)frame->k1);

	printf("\tgp: %#jx\tsp: %#jx\ts8: %#jx\tra: %#jx\n",
	    (intmax_t)frame->gp, (intmax_t)frame->sp, (intmax_t)frame->s8, (intmax_t)frame->ra);

	printf("\tsr: %#jx\tmullo: %#jx\tmulhi: %#jx\tbadvaddr: %#jx\n",
	    (intmax_t)frame->sr, (intmax_t)frame->mullo, (intmax_t)frame->mulhi, (intmax_t)frame->badvaddr);

	printf("\tcause: %#jx\tpc: %#jx\n",
	    (intmax_t)frame->cause, (intmax_t)frame->pc);
}

#endif


static void
get_mapping_info(vm_offset_t va, pd_entry_t **pdepp, pt_entry_t **ptepp)
{
	pt_entry_t *ptep;
	pd_entry_t *pdep;
	struct proc *p = curproc;

	pdep = (&(p->p_vmspace->vm_pmap.pm_segtab[(va >> SEGSHIFT) & (NPDEPG - 1)]));
	if (*pdep)
		ptep = pmap_pte(&p->p_vmspace->vm_pmap, va);
	else
		ptep = (pt_entry_t *)0;

	*pdepp = pdep;
	*ptepp = ptep;
}

static void
log_illegal_instruction(const char *msg, struct trapframe *frame)
{
	pt_entry_t *ptep;
	pd_entry_t *pdep;
	unsigned int *addr;
	struct thread *td;
	struct proc *p;
	register_t pc;

	td = curthread;
	p = td->td_proc;

#ifdef SMP
	printf("cpuid = %d\n", PCPU_GET(cpuid));
#endif
	pc = frame->pc + (DELAYBRANCH(frame->cause) ? 4 : 0);
	log(LOG_ERR, "%s: pid %d tid %ld (%s), uid %d: pc %#jx ra %#jx\n",
	    msg, p->p_pid, (long)td->td_tid, p->p_comm,
	    p->p_ucred ? p->p_ucred->cr_uid : -1,
	    (intmax_t)pc,
	    (intmax_t)frame->ra);

	/* log registers in trap frame */
	log_frame_dump(frame);

	get_mapping_info((vm_offset_t)pc, &pdep, &ptep);

	/*
	 * Dump a few words around faulting instruction, if the addres is
	 * valid.
	 */
	if (!(pc & 3) &&
	    useracc((caddr_t)(intptr_t)pc, sizeof(int) * 4, VM_PROT_READ)) {
		/* dump page table entry for faulting instruction */
		log(LOG_ERR, "Page table info for pc address %#jx: pde = %p, pte = %#jx\n",
		    (intmax_t)pc, (void *)(intptr_t)*pdep, (uintmax_t)(ptep ? *ptep : 0));

		addr = (unsigned int *)(intptr_t)pc;
		log(LOG_ERR, "Dumping 4 words starting at pc address %p: \n",
		    addr);
		log(LOG_ERR, "%08x %08x %08x %08x\n",
		    addr[0], addr[1], addr[2], addr[3]);
	} else {
		log(LOG_ERR, "pc address %#jx is inaccessible, pde = %p, pte = %#jx\n",
		    (intmax_t)pc, (void *)(intptr_t)*pdep, (uintmax_t)(ptep ? *ptep : 0));
	}
}

static void
log_bad_page_fault(char *msg, struct trapframe *frame, int trap_type)
{
	pt_entry_t *ptep;
	pd_entry_t *pdep;
	unsigned int *addr;
	struct thread *td;
	struct proc *p;
	char *read_or_write;
	register_t pc;

	trap_type &= ~T_USER;

	td = curthread;
	p = td->td_proc;

#ifdef SMP
	printf("cpuid = %d\n", PCPU_GET(cpuid));
#endif
	switch (trap_type) {
	case T_TLB_MOD:
	case T_TLB_ST_MISS:
	case T_ADDR_ERR_ST:
		read_or_write = "write";
		break;
	case T_TLB_LD_MISS:
	case T_ADDR_ERR_LD:
	case T_BUS_ERR_IFETCH:
		read_or_write = "read";
		break;
	default:
		read_or_write = "unknown";
	}

	pc = frame->pc + (DELAYBRANCH(frame->cause) ? 4 : 0);
	log(LOG_ERR, "%s: pid %d tid %ld (%s), uid %d: pc %#jx got a %s fault "
	    "(type %#x) at %#jx\n",
	    msg, p->p_pid, (long)td->td_tid, p->p_comm,
	    p->p_ucred ? p->p_ucred->cr_uid : -1,
	    (intmax_t)pc,
	    read_or_write,
	    trap_type,
	    (intmax_t)frame->badvaddr);

	/* log registers in trap frame */
	log_frame_dump(frame);

	get_mapping_info((vm_offset_t)pc, &pdep, &ptep);

	/*
	 * Dump a few words around faulting instruction, if the addres is
	 * valid.
	 */
	if (!(pc & 3) && (pc != frame->badvaddr) &&
	    (trap_type != T_BUS_ERR_IFETCH) &&
	    useracc((caddr_t)(intptr_t)pc, sizeof(int) * 4, VM_PROT_READ)) {
		/* dump page table entry for faulting instruction */
		log(LOG_ERR, "Page table info for pc address %#jx: pde = %p, pte = %#jx\n",
		    (intmax_t)pc, (void *)(intptr_t)*pdep, (uintmax_t)(ptep ? *ptep : 0));

		addr = (unsigned int *)(intptr_t)pc;
		log(LOG_ERR, "Dumping 4 words starting at pc address %p: \n",
		    addr);
		log(LOG_ERR, "%08x %08x %08x %08x\n",
		    addr[0], addr[1], addr[2], addr[3]);
	} else {
		log(LOG_ERR, "pc address %#jx is inaccessible, pde = %p, pte = %#jx\n",
		    (intmax_t)pc, (void *)(intptr_t)*pdep, (uintmax_t)(ptep ? *ptep : 0));
	}

	get_mapping_info((vm_offset_t)frame->badvaddr, &pdep, &ptep);
	log(LOG_ERR, "Page table info for bad address %#jx: pde = %p, pte = %#jx\n",
	    (intmax_t)frame->badvaddr, (void *)(intptr_t)*pdep, (uintmax_t)(ptep ? *ptep : 0));
}


/*
 * Unaligned load/store emulation
 */
static int
mips_unaligned_load_store(struct trapframe *frame, int mode, register_t addr, register_t pc)
{
	register_t *reg = (register_t *) frame;
	u_int32_t inst = *((u_int32_t *)(intptr_t)pc);
	register_t value_msb, value;
	unsigned size;

	/*
	 * ADDR_ERR faults have higher priority than TLB
	 * Miss faults.  Therefore, it is necessary to
	 * verify that the faulting address is a valid
	 * virtual address within the process' address space
	 * before trying to emulate the unaligned access.
	 */
	switch (MIPS_INST_OPCODE(inst)) {
	case OP_LHU: case OP_LH:
	case OP_SH:
		size = 2;
		break;
	case OP_LWU: case OP_LW:
	case OP_SW:
		size = 4;
		break;
	case OP_LD:
	case OP_SD:
		size = 8;
		break;
	default:
		printf("%s: unhandled opcode in address error: %#x\n", __func__, MIPS_INST_OPCODE(inst));
		return (0);
	}

	if (!useracc((void *)rounddown2((vm_offset_t)addr, size), size * 2, mode))
		return (0);

	/*
	 * XXX
	 * Handle LL/SC LLD/SCD.
	 */
	switch (MIPS_INST_OPCODE(inst)) {
	case OP_LHU:
		KASSERT(mode == VM_PROT_READ, ("access mode must be read for load instruction."));
		lbu_macro(value_msb, addr);
		addr += 1;
		lbu_macro(value, addr);
		value |= value_msb << 8;
		reg[MIPS_INST_RT(inst)] = value;
		return (MIPS_LHU_ACCESS);

	case OP_LH:
		KASSERT(mode == VM_PROT_READ, ("access mode must be read for load instruction."));
		lb_macro(value_msb, addr);
		addr += 1;
		lbu_macro(value, addr);
		value |= value_msb << 8;
		reg[MIPS_INST_RT(inst)] = value;
		return (MIPS_LH_ACCESS);

	case OP_LWU:
		KASSERT(mode == VM_PROT_READ, ("access mode must be read for load instruction."));
		lwl_macro(value, addr);
		addr += 3;
		lwr_macro(value, addr);
		value &= 0xffffffff;
		reg[MIPS_INST_RT(inst)] = value;
		return (MIPS_LWU_ACCESS);

	case OP_LW:
		KASSERT(mode == VM_PROT_READ, ("access mode must be read for load instruction."));
		lwl_macro(value, addr);
		addr += 3;
		lwr_macro(value, addr);
		reg[MIPS_INST_RT(inst)] = value;
		return (MIPS_LW_ACCESS);

#if defined(__mips_n32) || defined(__mips_n64)
	case OP_LD:
		KASSERT(mode == VM_PROT_READ, ("access mode must be read for load instruction."));
		ldl_macro(value, addr);
		addr += 7;
		ldr_macro(value, addr);
		reg[MIPS_INST_RT(inst)] = value;
		return (MIPS_LD_ACCESS);
#endif

	case OP_SH:
		KASSERT(mode == VM_PROT_WRITE, ("access mode must be write for store instruction."));
		value = reg[MIPS_INST_RT(inst)];
		value_msb = value >> 8;
		sb_macro(value_msb, addr);
		addr += 1;
		sb_macro(value, addr);
		return (MIPS_SH_ACCESS);

	case OP_SW:
		KASSERT(mode == VM_PROT_WRITE, ("access mode must be write for store instruction."));
		value = reg[MIPS_INST_RT(inst)];
		swl_macro(value, addr);
		addr += 3;
		swr_macro(value, addr);
		return (MIPS_SW_ACCESS);

#if defined(__mips_n32) || defined(__mips_n64)
	case OP_SD:
		KASSERT(mode == VM_PROT_WRITE, ("access mode must be write for store instruction."));
		value = reg[MIPS_INST_RT(inst)];
		sdl_macro(value, addr);
		addr += 7;
		sdr_macro(value, addr);
		return (MIPS_SD_ACCESS);
#endif
	}
	panic("%s: should not be reached.", __func__);
}


/*
 * XXX TODO: SMP?
 */
static struct timeval unaligned_lasterr;
static int unaligned_curerr;

static int unaligned_pps_log_limit = 4;

SYSCTL_INT(_machdep, OID_AUTO, unaligned_log_pps_limit, CTLFLAG_RWTUN,
    &unaligned_pps_log_limit, 0,
    "limit number of userland unaligned log messages per second");

static int
emulate_unaligned_access(struct trapframe *frame, int mode)
{
	register_t pc;
	int access_type = 0;
	struct thread *td = curthread;
	struct proc *p = curproc;

	pc = frame->pc + (DELAYBRANCH(frame->cause) ? 4 : 0);

	/*
	 * Fall through if it's instruction fetch exception
	 */
	if (!((pc & 3) || (pc == frame->badvaddr))) {

		/*
		 * Handle unaligned load and store
		 */

		/*
		 * Return access type if the instruction was emulated.
		 * Otherwise restore pc and fall through.
		 */
		access_type = mips_unaligned_load_store(frame,
		    mode, frame->badvaddr, pc);

		if (access_type) {
			if (DELAYBRANCH(frame->cause))
				frame->pc = MipsEmulateBranch(frame, frame->pc,
				    0, 0);
			else
				frame->pc += 4;

			if (ppsratecheck(&unaligned_lasterr,
			    &unaligned_curerr, unaligned_pps_log_limit)) {
				/* XXX TODO: keep global/tid/pid counters? */
				log(LOG_INFO,
				    "Unaligned %s: pid=%ld (%s), tid=%ld, "
				    "pc=%#jx, badvaddr=%#jx\n",
				    access_name[access_type - 1],
				    (long) p->p_pid,
				    p->p_comm,
				    (long) td->td_tid,
				    (intmax_t)pc,
				    (intmax_t)frame->badvaddr);
			}
		}
	}
	return access_type;
}
