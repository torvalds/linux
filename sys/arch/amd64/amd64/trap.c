/*	$OpenBSD: trap.c,v 1.114 2025/09/17 18:37:44 sf Exp $	*/
/*	$NetBSD: trap.c,v 1.2 2003/05/04 23:51:56 fvdl Exp $	*/

/*-
 * Copyright (c) 1998, 2000 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Charles M. Hannum.
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

/*-
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
 * amd64 Trap and System call handling
 */
#undef	TRAP_SIGDEBUG

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/signalvar.h>
#include <sys/user.h>
#include <sys/signal.h>
#include <sys/syscall.h>
#include <sys/syscall_mi.h>
#include <sys/stdarg.h>

#include <uvm/uvm_extern.h>

#include <machine/cpu.h>
#include <machine/cpufunc.h>
#include <machine/fpu.h>
#include <machine/psl.h>
#include <machine/trap.h>
#include <machine/ghcb.h>
#include <machine/vmmvar.h>
#ifdef DDB
#include <ddb/db_output.h>
#include <machine/db_machdep.h>
#endif

#include "isa.h"

int	upageflttrap(struct trapframe *, uint64_t);
int	kpageflttrap(struct trapframe *, uint64_t);
int	vctrap(struct trapframe *, int);
void	kerntrap(struct trapframe *);
void	usertrap(struct trapframe *);
void	ast(struct trapframe *);
void	syscall(struct trapframe *);

const char * const trap_type[] = {
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
	"FPU operand fetch fault",		/* 14 T_FPOPFLT */
	"invalid TSS fault",			/* 15 T_TSSFLT */
	"segment not present fault",		/* 16 T_SEGNPFLT */
	"stack fault",				/* 17 T_STKFLT */
	"machine check",			/* 18 T_MCA */
	"SSE FP exception",			/* 19 T_XMM */
	"virtualization exception",		/* 20 T_VE */
	"control protection exception",		/* 21 T_CP */
	"VMM communication exception",		/* 29 T_VC */
};
const int	trap_types = nitems(trap_type);

#ifdef DEBUG
int	trapdebug = 0;
#endif

static void trap_print(struct trapframe *, int _type);
static inline void frame_dump(struct trapframe *_tf, struct proc *_p,
    const char *_sig, uint64_t _cr2);
static inline void verify_smap(const char *_func);
static inline int verify_pkru(struct proc *);
static inline void debug_trap(struct trapframe *_frame, struct proc *_p,
    long _type);

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
	printf("%s\n", ci->ci_panicbuf);
#endif
}

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
upageflttrap(struct trapframe *frame, uint64_t cr2)
{
	struct proc *p = curproc;
	vaddr_t va = trunc_page((vaddr_t)cr2);
	vm_prot_t access_type = pgex2access(frame->tf_err);
	union sigval sv;
	int signal, sicode, error;

	/*
	 * If NX is not enabled, we can't distinguish between PROT_READ
	 * and PROT_EXEC access, so try both.
	 */
	error = uvm_fault(&p->p_vmspace->vm_map, va, 0, access_type);
	if (pg_nx == 0 && error == EACCES && access_type == PROT_READ)
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
 * Returns non-zero if the fault was handled (possibly by generating a signal).
 * Returns zero if something was so broken that we should panic.
 */
int
kpageflttrap(struct trapframe *frame, uint64_t cr2)
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
	if (pcb->pcb_onfault != NULL) {
		extern caddr_t __nofault_start[], __nofault_end[];
		caddr_t *nf = __nofault_start;
		while (*nf++ != pcb->pcb_onfault) {
			if (nf >= __nofault_end) {
				fault("invalid pcb_nofault=%lx",
				    (long)pcb->pcb_onfault);
				return 0;
			}
		}
	}

	/* This will only trigger if SMEP is enabled */
	if (pcb->pcb_onfault == NULL && cr2 <= VM_MAXUSER_ADDRESS &&
	    frame->tf_err & PGEX_I) {
		fault("attempt to execute user address %p "
		    "in supervisor mode", (void *)cr2);
		return 0;
	}
	/* This will only trigger if SMAP is enabled */
	if (pcb->pcb_onfault == NULL && cr2 <= VM_MAXUSER_ADDRESS &&
	    frame->tf_err & PGEX_P) {
		fault("attempt to access user address %p "
		    "in supervisor mode", (void *)cr2);
		return 0;
	}

	/*
	 * It is only a kernel address space fault iff:
	 *	1. when running in ring 0 and
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
			fault("uvm_fault(%p, 0x%llx, 0, %d) -> %x",
			    map, cr2, access_type, error);
			return 0;
		}
		frame->tf_rip = (u_int64_t)pcb->pcb_onfault;
	}

	return 1;
}

int
vctrap(struct trapframe *frame, int user)
{
	uint8_t		*rip = (uint8_t *)(frame->tf_rip);
	uint64_t	 port;
	struct ghcb_sync syncout, syncin;
	struct ghcb_sa	*ghcb;
	struct ghcb_extra_regs	ghcb_regs;

	KASSERT((read_rflags() & PSL_I) == 0);

	memset(&syncout, 0, sizeof(syncout));
	memset(&syncin, 0, sizeof(syncin));
	memset(&ghcb_regs, 0, sizeof(ghcb_regs));

	ghcb_regs.exitcode = frame->tf_err;

	/*
	 * The #VC trap occurs when the guest (us) performs an
	 * operation which requires sharing data with the host. In
	 * order to ascertain which instruction caused the #VC,
	 * examine the instruction by reading %rip, Then, sync the
	 * appropriate values out (to the host), perform VMGEXIT
	 * to request that the host handle the operation which
	 * caused the #VC, then sync the returned values back in
	 * (from the host).
	 */
	switch (ghcb_regs.exitcode) {
	case SVM_VMEXIT_CPUID:
		ghcb_sync_val(GHCB_RAX, GHCB_SZ32, &syncout);
		ghcb_sync_val(GHCB_RCX, GHCB_SZ32, &syncout);
		ghcb_sync_val(GHCB_RAX, GHCB_SZ32, &syncin);
		ghcb_sync_val(GHCB_RBX, GHCB_SZ32, &syncin);
		ghcb_sync_val(GHCB_RCX, GHCB_SZ32, &syncin);
		ghcb_sync_val(GHCB_RDX, GHCB_SZ32, &syncin);
		frame->tf_rip += 2;
		break;
	case SVM_VMEXIT_MSR: {
		if (user)
			return 0;	/* not allowed from userspace */
		if (*rip == 0x0f && *(rip + 1) == 0x30) {
			/* WRMSR */
			ghcb_sync_val(GHCB_RAX, GHCB_SZ32, &syncout);
			ghcb_sync_val(GHCB_RCX, GHCB_SZ32, &syncout);
			ghcb_sync_val(GHCB_RDX, GHCB_SZ32, &syncout);
			ghcb_regs.exitinfo1 = 1;
		} else if (*rip == 0x0f && *(rip + 1) == 0x32) {
			/* RDMSR */
			ghcb_sync_val(GHCB_RCX, GHCB_SZ32, &syncout);
			ghcb_sync_val(GHCB_RAX, GHCB_SZ32, &syncin);
			ghcb_sync_val(GHCB_RDX, GHCB_SZ32, &syncin);
		} else
			panic("failed to decode MSR");
		frame->tf_rip += 2;
		break;
	    }
	case SVM_VMEXIT_IOIO: {
		if (user)
			return 0;	/* not allowed from userspace */
		switch (*rip) {
		case 0x66: {
			switch (*(rip + 1)) {
			case 0xef:	/* out %ax,(%dx) */
				ghcb_sync_val(GHCB_RAX, GHCB_SZ16, &syncout);
				port = frame->tf_rdx & 0xffff;
				ghcb_regs.exitinfo1 = (port << 16) |
				    (1ULL << 5);
				frame->tf_rip += 2;
				break;
			case 0xed:	/* in (%dx),%ax */
				ghcb_sync_val(GHCB_RAX, GHCB_SZ16, &syncin);
				port = frame->tf_rdx & 0xffff;
				ghcb_regs.exitinfo1 = (port << 16) |
				    (1ULL << 5) | (1ULL << 0);
				frame->tf_rip += 2;
				break;
			default:
				panic("failed to decode prefixed IOIO");
			}
			break;
		    }
		case 0xe4:	/* in $port,%al */
			ghcb_sync_val(GHCB_RAX, GHCB_SZ8, &syncin);
			port = *(rip + 1) & 0xff;
			ghcb_regs.exitinfo1 = (port << 16) | (1ULL << 4) |
			    (1ULL << 0);
			frame->tf_rip += 2;
			break;
		case 0xe6:	/* outb %al,$port */
			ghcb_sync_val(GHCB_RAX, GHCB_SZ8, &syncout);
			port = *(rip + 1) & 0xff;
			ghcb_regs.exitinfo1 = (port << 16) | (1ULL << 4);
			frame->tf_rip += 2;
			break;
		case 0xec:	/* in (%dx),%al */
			ghcb_sync_val(GHCB_RAX, GHCB_SZ8, &syncin);
			port = frame->tf_rdx & 0xffff;
			ghcb_regs.exitinfo1 = (port << 16) | (1ULL << 4) |
			    (1ULL << 0);
			frame->tf_rip += 1;
			break;
		case 0xed:	/* in (%dx),%eax */
			ghcb_sync_val(GHCB_RAX, GHCB_SZ32, &syncin);
			port = frame->tf_rdx & 0xffff;
			ghcb_regs.exitinfo1 = (port << 16) | (1ULL << 6) |
			    (1ULL << 0);
			frame->tf_rip += 1;
			break;
		case 0xee:	/* out %al,(%dx) */
			ghcb_sync_val(GHCB_RAX, GHCB_SZ8, &syncout);
			port = frame->tf_rdx & 0xffff;
			ghcb_regs.exitinfo1 = (port << 16) | (1ULL << 4);
			frame->tf_rip += 1;
			break;
		case 0xef:	/* out %eax,(%dx) */
			ghcb_sync_val(GHCB_RAX, GHCB_SZ32, &syncout);
			port = frame->tf_rdx & 0xffff;
			ghcb_regs.exitinfo1 = (port << 16) | (1ULL << 6);
			frame->tf_rip += 1;
			break;
		default:
			panic("failed to decode IOIO");
		}
		break;
	    }
	default:
		panic("invalid exit code 0x%llx", ghcb_regs.exitcode);
	}

	/* Always required */
	ghcb_sync_val(GHCB_SW_EXITCODE, GHCB_SZ64, &syncout);
	ghcb_sync_val(GHCB_SW_EXITINFO1, GHCB_SZ64, &syncout);
	ghcb_sync_val(GHCB_SW_EXITINFO2, GHCB_SZ64, &syncout);

	/* Sync out to GHCB */
	ghcb = (struct ghcb_sa *)ghcb_vaddr;
	ghcb_sync_out(frame, &ghcb_regs, ghcb, &syncout);

	wrmsr(MSR_SEV_GHCB, ghcb_paddr);

	/* Call hypervisor. */
	vmgexit();

	/* Verify response */
	if (ghcb_verify_bm(ghcb->valid_bitmap, syncin.valid_bitmap)) {
		ghcb_clear(ghcb);
		panic("invalid hypervisor response");
	}

	/* Sync in from GHCB */
	ghcb_sync_in(frame, NULL, ghcb, &syncin);

	return 1;
}


/*
 * kerntrap(frame):
 *	Exception, fault, and trap interface to BSD kernel. This
 * common code is called from assembly language IDT gate entry
 * routines that prepare a suitable stack frame, and restore this
 * frame after the exception has been processed.
 */
void
kerntrap(struct trapframe *frame)
{
	int type = (int)frame->tf_trapno;
	uint64_t cr2 = rcr2();

	verify_smap(__func__);
	uvmexp.traps++;
	debug_trap(frame, curproc, type);

	switch (type) {

	default:
	we_re_toast:
#ifdef DDB
		if (db_ktrap(type, frame->tf_err, frame))
			return;
#endif
		trap_print(frame, type);
		panic("trap type %d, code=%llx, pc=%llx",
		    type, frame->tf_err, frame->tf_rip);
		/*NOTREACHED*/

	case T_PAGEFLT:			/* allow page faults in kernel mode */
		if (kpageflttrap(frame, cr2))
			return;
		goto we_re_toast;

#if NISA > 0
	case T_NMI:
#ifdef DDB
		/* NMI can be hooked up to a pushbutton for debugging */
		printf ("NMI ... going to debugger\n");
		if (db_ktrap(type, 0, frame))
			return;
#endif
		/* machine/parity/power fail/"kitchen sink" faults */

		if (x86_nmi() != 0)
			goto we_re_toast;
		else
			return;
#endif /* NISA > 0 */

	case T_VC:
		if (vctrap(frame, 0))
			return;
		goto we_re_toast;
	}
}

/* If we find out userland changed the pkru register, punish the process */
static inline int
verify_pkru(struct proc *p)
{
	if (pg_xo == 0 || rdpkru(0) == PGK_VALUE)
		return 0;
	KERNEL_LOCK();
	sigabort(p);
	KERNEL_UNLOCK();
	return 1;
}

/*
 * usertrap(frame): handler for exceptions, faults, and traps from userspace
 *	This is called from the assembly language IDT gate entries
 * which prepare a suitable stack frame and restores the CPU state
 * after the fault has been processed.
 */
void
usertrap(struct trapframe *frame)
{
	struct proc *p = curproc;
	int type = (int)frame->tf_trapno;
	uint64_t cr2 = rcr2();
	union sigval sv;
	int sig, code;

	verify_smap(__func__);
	uvmexp.traps++;
	debug_trap(frame, p, type);

	p->p_md.md_regs = frame;
	refreshcreds(p);

	if (verify_pkru(p))
		goto out;

	switch (type) {
	case T_TSSFLT:
		sig = SIGBUS;
		code = BUS_OBJERR;
		break;
	case T_PROTFLT:			/* protection fault */
	case T_SEGNPFLT:
	case T_STKFLT:
		frame_dump(frame, p, "SEGV", 0);
		sig = SIGSEGV;
		code = SEGV_MAPERR;
		break;
	case T_ALIGNFLT:
		sig = SIGBUS;
		code = BUS_ADRALN;
		break;
	case T_PRIVINFLT:		/* privileged instruction fault */
		sig = SIGILL;
		code = ILL_PRVOPC;
		break;
	case T_DIVIDE:
		sig = SIGFPE;
		code = FPE_INTDIV;
		break;
	case T_ARITHTRAP:
	case T_XMM:			/* real arithmetic exceptions */
		sig = SIGFPE;
		code = fputrap(type);
		break;
	case T_BPTFLT:			/* bpt instruction fault */
	case T_TRCTRAP:			/* trace trap */
		sig = SIGTRAP;
		code = TRAP_BRKPT;
		break;
	case T_CP:
		sig = SIGILL;
		code = (frame->tf_err & 0x7fff) < 4 ? ILL_BTCFI
		    : ILL_BADSTK;
		break;
	case T_VC:
		if (vctrap(frame, 1))
			goto out;
		sig = SIGILL;
		code = ILL_PRVOPC;
		break;
	case T_PAGEFLT:			/* page fault */
		if (!uvm_map_inentry(p, &p->p_spinentry, PROC_STACK(p),
		    "[%s]%d/%d sp=%lx inside %lx-%lx: not MAP_STACK\n",
		    uvm_map_inentry_sp, p->p_vmspace->vm_map.sserial))
			goto out;
		if (upageflttrap(frame, cr2))
			goto out;
		/* FALLTHROUGH */

	default:
		trap_print(frame, type);
		panic("impossible trap");
	}

	sv.sival_ptr = (void *)frame->tf_rip;
	trapsignal(p, sig, type, code, sv);

out:
	userret(p);
}


static void
trap_print(struct trapframe *frame, int type)
{
	if (type < trap_types)
		printf("fatal %s", trap_type[type]);
	else
		printf("unknown trap %d", type);
	printf(" in %s mode\n", KERNELMODE(frame->tf_cs, frame->tf_rflags) ?
	    "supervisor" : "user");
	printf("trap type %d code %llx rip %llx cs %llx rflags %llx cr2 "
	       "%llx cpl %x rsp %llx\n",
	    type, frame->tf_err, frame->tf_rip, frame->tf_cs,
	    frame->tf_rflags, rcr2(), curcpu()->ci_ilevel, frame->tf_rsp);
	printf("gsbase %p  kgsbase %p\n",
	    (void *)rdmsr(MSR_GSBASE), (void *)rdmsr(MSR_KERNELGSBASE));
	if (type == T_TRCTRAP)
		printf("dr6 %llx dr7 %llx\n", rdr6(), rdr7());
}


static inline void
frame_dump(struct trapframe *tf, struct proc *p, const char *sig, uint64_t cr2)
{
#ifdef TRAP_SIGDEBUG
	printf("pid %d (%s): %s at rip %llx addr %llx\n",
	    p->p_p->ps_pid, p->p_p->ps_comm, sig, tf->tf_rip, cr2);
	printf("rip %p  cs 0x%x  rfl %p  rsp %p  ss 0x%x\n",
	    (void *)tf->tf_rip, (unsigned)tf->tf_cs & 0xffff,
	    (void *)tf->tf_rflags,
	    (void *)tf->tf_rsp, (unsigned)tf->tf_ss & 0xffff);
	printf("err 0x%llx  trapno 0x%llx\n",
	    tf->tf_err, tf->tf_trapno);
	printf("rdi %p  rsi %p  rdx %p\n",
	    (void *)tf->tf_rdi, (void *)tf->tf_rsi, (void *)tf->tf_rdx);
	printf("rcx %p  r8  %p  r9  %p\n",
	    (void *)tf->tf_rcx, (void *)tf->tf_r8, (void *)tf->tf_r9);
	printf("r10 %p  r11 %p  r12 %p\n",
	    (void *)tf->tf_r10, (void *)tf->tf_r11, (void *)tf->tf_r12);
	printf("r13 %p  r14 %p  r15 %p\n",
	    (void *)tf->tf_r13, (void *)tf->tf_r14, (void *)tf->tf_r15);
	printf("rbp %p  rbx %p  rax %p\n",
	    (void *)tf->tf_rbp, (void *)tf->tf_rbx, (void *)tf->tf_rax);
#endif
}

static inline void
verify_smap(const char *func)
{
#ifdef DIAGNOSTIC
	if (curcpu()->ci_feature_sefflags_ebx & SEFF0EBX_SMAP) {
		u_long rf = read_rflags();
		if (rf & PSL_AC) {
			write_rflags(rf & ~PSL_AC);
			panic("%s: AC set on entry", func);
		}
	}
#endif
}

static inline void
debug_trap(struct trapframe *frame, struct proc *p, long type)
{
#ifdef DEBUG
	if (trapdebug) {
		printf("trap %ld code %llx rip %llx cs %llx rflags %llx "
		       "cr2 %llx cpl %x\n",
		    type, frame->tf_err, frame->tf_rip, frame->tf_cs,
		    frame->tf_rflags, rcr2(), curcpu()->ci_ilevel);
		printf("curproc %p\n", (void *)p);
		if (p != NULL)
			printf("pid %d\n", p->p_p->ps_pid);
	}
#endif
}

/*
 * ast(frame):
 *	AST handler.  This is called from assembly language stubs when
 *	returning to userspace after a syscall or interrupt.
 */
void
ast(struct trapframe *frame)
{
	struct proc *p = curproc;

	uvmexp.traps++;
	KASSERT(!KERNELMODE(frame->tf_cs, frame->tf_rflags));
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
	const struct sysent *callp;
	struct proc *p;
	int error = ENOSYS;
	register_t code, *args, rval[2];

	verify_smap(__func__);
	uvmexp.syscalls++;
	p = curproc;

	if (verify_pkru(p)) {
		userret(p);
		return;
	}

	code = frame->tf_rax;
	args = (register_t *)&frame->tf_rdi;

	if (code <= 0 || code >= SYS_MAXSYSCALL)
		goto bad;
	callp = sysent + code;

	rval[0] = 0;
	rval[1] = 0;

	error = mi_syscall(p, code, callp, args, rval);

	switch (error) {
	case 0:
		frame->tf_rax = rval[0];
		frame->tf_rflags &= ~PSL_C;	/* carry bit */
		break;
	case ERESTART:
		/* Back up over the syscall instruction (2 bytes) */
		frame->tf_rip -= 2;
		break;
	case EJUSTRETURN:
		/* nothing to do */
		break;
	default:
	bad:
		frame->tf_rax = error;
		frame->tf_rflags |= PSL_C;	/* carry bit */
		break;
	}

	mi_syscall_return(p, code, error, rval);
}

void
child_return(void *arg)
{
	struct proc *p = arg;
	struct trapframe *tf = p->p_md.md_regs;

	tf->tf_rax = 0;
	tf->tf_rflags &= ~PSL_C;

	KERNEL_UNLOCK();

	mi_child_return(p);
}

