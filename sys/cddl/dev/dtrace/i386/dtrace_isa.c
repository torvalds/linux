/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 *
 * $FreeBSD$
 */
/*
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */
#include <sys/cdefs.h>

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/stack.h>
#include <sys/pcpu.h>

#include <machine/frame.h>
#include <machine/md_var.h>
#include <machine/pcb.h>
#include <machine/stack.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/pmap.h>

#include "regset.h"

extern uintptr_t kernbase;
uintptr_t kernelbase = (uintptr_t) &kernbase;

uint8_t dtrace_fuword8_nocheck(void *);
uint16_t dtrace_fuword16_nocheck(void *);
uint32_t dtrace_fuword32_nocheck(void *);
uint64_t dtrace_fuword64_nocheck(void *);

int	dtrace_ustackdepth_max = 2048;

void
dtrace_getpcstack(pc_t *pcstack, int pcstack_limit, int aframes,
    uint32_t *intrpc)
{
	int depth = 0;
	register_t ebp;
	struct i386_frame *frame;
	vm_offset_t callpc;
	pc_t caller = (pc_t) solaris_cpu[curcpu].cpu_dtrace_caller;

	if (intrpc != 0)
		pcstack[depth++] = (pc_t) intrpc;

	aframes++;

	__asm __volatile("movl %%ebp,%0" : "=r" (ebp));

	frame = (struct i386_frame *)ebp;
	while (depth < pcstack_limit) {
		if (!INKERNEL(frame))
			break;

		callpc = frame->f_retaddr;

		if (!INKERNEL(callpc))
			break;

		if (aframes > 0) {
			aframes--;
			if ((aframes == 0) && (caller != 0)) {
				pcstack[depth++] = caller;
			}
		}
		else {
			pcstack[depth++] = callpc;
		}

		if (frame->f_frame <= frame ||
		    (vm_offset_t)frame->f_frame >= curthread->td_kstack +
		    curthread->td_kstack_pages * PAGE_SIZE)
			break;
		frame = frame->f_frame;
	}

	for (; depth < pcstack_limit; depth++) {
		pcstack[depth] = 0;
	}
}

static int
dtrace_getustack_common(uint64_t *pcstack, int pcstack_limit, uintptr_t pc,
    uintptr_t sp)
{
#ifdef notyet
	proc_t *p = curproc;
	uintptr_t oldcontext = lwp->lwp_oldcontext; /* XXX signal stack. */
	size_t s1, s2;
#endif
	uintptr_t oldsp;
	volatile uint16_t *flags =
	    (volatile uint16_t *)&cpu_core[curcpu].cpuc_dtrace_flags;
	int ret = 0;

	ASSERT(pcstack == NULL || pcstack_limit > 0);
	ASSERT(dtrace_ustackdepth_max > 0);

#ifdef notyet /* XXX signal stack. */
	if (p->p_model == DATAMODEL_NATIVE) {
		s1 = sizeof (struct frame) + 2 * sizeof (long);
		s2 = s1 + sizeof (siginfo_t);
	} else {
		s1 = sizeof (struct frame32) + 3 * sizeof (int);
		s2 = s1 + sizeof (siginfo32_t);
	}
#endif

	while (pc != 0) {
		/*
		 * We limit the number of times we can go around this
		 * loop to account for a circular stack.
		 */
		if (ret++ >= dtrace_ustackdepth_max) {
			*flags |= CPU_DTRACE_BADSTACK;
			cpu_core[curcpu].cpuc_dtrace_illval = sp;
			break;
		}

		if (pcstack != NULL) {
			*pcstack++ = (uint64_t)pc;
			pcstack_limit--;
			if (pcstack_limit <= 0)
				break;
		}

		if (sp == 0)
			break;

		oldsp = sp;

#ifdef notyet /* XXX signal stack. */ 
		if (oldcontext == sp + s1 || oldcontext == sp + s2) {
			if (p->p_model == DATAMODEL_NATIVE) {
				ucontext_t *ucp = (ucontext_t *)oldcontext;
				greg_t *gregs = ucp->uc_mcontext.gregs;

				sp = dtrace_fulword(&gregs[REG_FP]);
				pc = dtrace_fulword(&gregs[REG_PC]);

				oldcontext = dtrace_fulword(&ucp->uc_link);
			} else {
				ucontext32_t *ucp = (ucontext32_t *)oldcontext;
				greg32_t *gregs = ucp->uc_mcontext.gregs;

				sp = dtrace_fuword32(&gregs[EBP]);
				pc = dtrace_fuword32(&gregs[EIP]);

				oldcontext = dtrace_fuword32(&ucp->uc_link);
			}
		} else {
			if (p->p_model == DATAMODEL_NATIVE) {
				struct frame *fr = (struct frame *)sp;

				pc = dtrace_fulword(&fr->fr_savpc);
				sp = dtrace_fulword(&fr->fr_savfp);
			} else {
				struct frame32 *fr = (struct frame32 *)sp;

				pc = dtrace_fuword32(&fr->fr_savpc);
				sp = dtrace_fuword32(&fr->fr_savfp);
			}
		}
#else
		pc = dtrace_fuword32((void *)(sp +
			offsetof(struct i386_frame, f_retaddr)));
		sp = dtrace_fuword32((void *)sp);
#endif /* ! notyet */

		if (sp == oldsp) {
			*flags |= CPU_DTRACE_BADSTACK;
			cpu_core[curcpu].cpuc_dtrace_illval = sp;
			break;
		}

		/*
		 * This is totally bogus:  if we faulted, we're going to clear
		 * the fault and break.  This is to deal with the apparently
		 * broken Java stacks on x86.
		 */
		if (*flags & CPU_DTRACE_FAULT) {
			*flags &= ~CPU_DTRACE_FAULT;
			break;
		}
	}

	return (ret);
}

void
dtrace_getupcstack(uint64_t *pcstack, int pcstack_limit)
{
	proc_t *p = curproc;
	struct trapframe *tf;
	uintptr_t pc, sp, fp;
	volatile uint16_t *flags =
	    (volatile uint16_t *)&cpu_core[curcpu].cpuc_dtrace_flags;
	int n;

	if (*flags & CPU_DTRACE_FAULT)
		return;

	if (pcstack_limit <= 0)
		return;

	/*
	 * If there's no user context we still need to zero the stack.
	 */
	if (p == NULL || (tf = curthread->td_frame) == NULL)
		goto zero;

	*pcstack++ = (uint64_t)p->p_pid;
	pcstack_limit--;

	if (pcstack_limit <= 0)
		return;

	pc = tf->tf_eip;
	fp = tf->tf_ebp;
	sp = tf->tf_esp;

	if (DTRACE_CPUFLAG_ISSET(CPU_DTRACE_ENTRY)) {
		/*
		 * In an entry probe.  The frame pointer has not yet been
		 * pushed (that happens in the function prologue).  The
		 * best approach is to add the current pc as a missing top
		 * of stack and back the pc up to the caller, which is stored
		 * at the current stack pointer address since the call 
		 * instruction puts it there right before the branch.
		 */

		*pcstack++ = (uint64_t)pc;
		pcstack_limit--;
		if (pcstack_limit <= 0)
			return;

		pc = dtrace_fuword32((void *) sp);
	}

	n = dtrace_getustack_common(pcstack, pcstack_limit, pc, sp);
	ASSERT(n >= 0);
	ASSERT(n <= pcstack_limit);

	pcstack += n;
	pcstack_limit -= n;

zero:
	while (pcstack_limit-- > 0)
		*pcstack++ = 0;
}

int
dtrace_getustackdepth(void)
{
	proc_t *p = curproc;
	struct trapframe *tf;
	uintptr_t pc, fp, sp;
	int n = 0;

	if (p == NULL || (tf = curthread->td_frame) == NULL)
		return (0);

	if (DTRACE_CPUFLAG_ISSET(CPU_DTRACE_FAULT))
		return (-1);

	pc = tf->tf_eip;
	fp = tf->tf_ebp;
	sp = tf->tf_esp;

	if (DTRACE_CPUFLAG_ISSET(CPU_DTRACE_ENTRY)) {
		/*
		 * In an entry probe.  The frame pointer has not yet been
		 * pushed (that happens in the function prologue).  The
		 * best approach is to add the current pc as a missing top
		 * of stack and back the pc up to the caller, which is stored
		 * at the current stack pointer address since the call 
		 * instruction puts it there right before the branch.
		 */

		pc = dtrace_fuword32((void *) sp);
		n++;
	}

	n += dtrace_getustack_common(NULL, 0, pc, fp);

	return (n);
}

void
dtrace_getufpstack(uint64_t *pcstack, uint64_t *fpstack, int pcstack_limit)
{
	proc_t *p = curproc;
	struct trapframe *tf;
	uintptr_t pc, sp, fp;
	volatile uint16_t *flags =
	    (volatile uint16_t *)&cpu_core[curcpu].cpuc_dtrace_flags;
#ifdef notyet /* XXX signal stack */
	uintptr_t oldcontext;
	size_t s1, s2;
#endif

	if (*flags & CPU_DTRACE_FAULT)
		return;

	if (pcstack_limit <= 0)
		return;

	/*
	 * If there's no user context we still need to zero the stack.
	 */
	if (p == NULL || (tf = curthread->td_frame) == NULL)
		goto zero;

	*pcstack++ = (uint64_t)p->p_pid;
	pcstack_limit--;

	if (pcstack_limit <= 0)
		return;

	pc = tf->tf_eip;
	fp = tf->tf_ebp;
	sp = tf->tf_esp;

#ifdef notyet /* XXX signal stack */
	oldcontext = lwp->lwp_oldcontext;

	if (p->p_model == DATAMODEL_NATIVE) {
		s1 = sizeof (struct frame) + 2 * sizeof (long);
		s2 = s1 + sizeof (siginfo_t);
	} else {
		s1 = sizeof (struct frame32) + 3 * sizeof (int);
		s2 = s1 + sizeof (siginfo32_t);
	}
#endif

	if (DTRACE_CPUFLAG_ISSET(CPU_DTRACE_ENTRY)) {
		*pcstack++ = (uint64_t)pc;
		*fpstack++ = 0;
		pcstack_limit--;
		if (pcstack_limit <= 0)
			return;

		pc = dtrace_fuword32((void *)sp);
	}

	while (pc != 0) {
		*pcstack++ = (uint64_t)pc;
		*fpstack++ = fp;
		pcstack_limit--;
		if (pcstack_limit <= 0)
			break;

		if (fp == 0)
			break;

#ifdef notyet /* XXX signal stack */
		if (oldcontext == sp + s1 || oldcontext == sp + s2) {
			if (p->p_model == DATAMODEL_NATIVE) {
				ucontext_t *ucp = (ucontext_t *)oldcontext;
				greg_t *gregs = ucp->uc_mcontext.gregs;

				sp = dtrace_fulword(&gregs[REG_FP]);
				pc = dtrace_fulword(&gregs[REG_PC]);

				oldcontext = dtrace_fulword(&ucp->uc_link);
			} else {
				ucontext_t *ucp = (ucontext_t *)oldcontext;
				greg_t *gregs = ucp->uc_mcontext.gregs;

				sp = dtrace_fuword32(&gregs[EBP]);
				pc = dtrace_fuword32(&gregs[EIP]);

				oldcontext = dtrace_fuword32(&ucp->uc_link);
			}
		} else
#endif /* XXX */
		{
			pc = dtrace_fuword32((void *)(fp +
				offsetof(struct i386_frame, f_retaddr)));
			fp = dtrace_fuword32((void *)fp);
		}

		/*
		 * This is totally bogus:  if we faulted, we're going to clear
		 * the fault and break.  This is to deal with the apparently
		 * broken Java stacks on x86.
		 */
		if (*flags & CPU_DTRACE_FAULT) {
			*flags &= ~CPU_DTRACE_FAULT;
			break;
		}
	}

zero:
	while (pcstack_limit-- > 0)
		*pcstack++ = 0;
}

uint64_t
dtrace_getarg(int arg, int aframes)
{
	struct trapframe *frame;
	struct i386_frame *fp = (struct i386_frame *)dtrace_getfp();
	uintptr_t *stack, val;
	int i;

	for (i = 1; i <= aframes; i++) {
		fp = fp->f_frame;

		if (P2ROUNDUP(fp->f_retaddr, 4) ==
		    (long)dtrace_invop_callsite) {
			/*
			 * If we pass through the invalid op handler, we will
			 * use the trap frame pointer that it pushed on the
			 * stack as the second argument to dtrace_invop() as
			 * the pointer to the stack.  When using this stack, we
			 * must skip the third argument to dtrace_invop(),
			 * which is included in the i386_frame.
			 */
			frame = (struct trapframe *)(((uintptr_t **)&fp[1])[0]);
			/*
			 * Skip the three hardware-saved registers and the
			 * return address.
			 */
			stack = (uintptr_t *)frame->tf_isp + 4;
			goto load;
		}

	}

	/*
	 * We know that we did not come through a trap to get into
	 * dtrace_probe() -- the provider simply called dtrace_probe()
	 * directly.  As this is the case, we need to shift the argument
	 * that we're looking for:  the probe ID is the first argument to
	 * dtrace_probe(), so the argument n will actually be found where
	 * one would expect to find argument (n + 1).
	 */
	arg++;

	stack = (uintptr_t *)fp + 2;

load:
	DTRACE_CPUFLAG_SET(CPU_DTRACE_NOFAULT);
	val = stack[arg];
	DTRACE_CPUFLAG_CLEAR(CPU_DTRACE_NOFAULT);

	return (val);
}

int
dtrace_getstackdepth(int aframes)
{
	int depth = 0;
	struct i386_frame *frame;
	vm_offset_t ebp;

	aframes++;
	ebp = dtrace_getfp();
	frame = (struct i386_frame *)ebp;
	depth++;
	for(;;) {
		if (!INKERNEL((long) frame))
			break;
		if (!INKERNEL((long) frame->f_frame))
			break;
		depth++;
		if (frame->f_frame <= frame ||
		    (vm_offset_t)frame->f_frame >= curthread->td_kstack +
		    curthread->td_kstack_pages * PAGE_SIZE)
			break;
		frame = frame->f_frame;
	}
	if (depth < aframes)
		return 0;
	else
		return depth - aframes;
}

ulong_t
dtrace_getreg(struct trapframe *rp, uint_t reg)
{
	struct pcb *pcb;
	int regmap[] = {  /* Order is dependent on reg.d */
		REG_GS,		/* 0  GS */
		REG_FS,		/* 1  FS */
		REG_ES,		/* 2  ES */
		REG_DS,		/* 3  DS */
		REG_RDI,	/* 4  EDI */
		REG_RSI,	/* 5  ESI */
		REG_RBP,	/* 6  EBP, REG_FP */
		REG_RSP,	/* 7  ESP */
		REG_RBX,	/* 8  EBX */
		REG_RDX,	/* 9  EDX, REG_R1 */
		REG_RCX,	/* 10 ECX */
		REG_RAX,	/* 11 EAX, REG_R0 */
		REG_TRAPNO,	/* 12 TRAPNO */
		REG_ERR,	/* 13 ERR */
		REG_RIP,	/* 14 EIP, REG_PC */
		REG_CS,		/* 15 CS */
		REG_RFL,	/* 16 EFL, REG_PS */
		REG_RSP,	/* 17 UESP, REG_SP */
		REG_SS		/* 18 SS */
	};

	if (reg > SS) {
		DTRACE_CPUFLAG_SET(CPU_DTRACE_ILLOP);
		return (0);
	}

	if (reg >= sizeof (regmap) / sizeof (int)) {
		DTRACE_CPUFLAG_SET(CPU_DTRACE_ILLOP);
		return (0);
	}

	reg = regmap[reg];

	switch(reg) {
	case REG_GS:
		if ((pcb = curthread->td_pcb) == NULL) {
			DTRACE_CPUFLAG_SET(CPU_DTRACE_ILLOP);
			return (0);
		}
		return (pcb->pcb_gs);
	case REG_FS:
		return (rp->tf_fs);
	case REG_ES:
		return (rp->tf_es);
	case REG_DS:
		return (rp->tf_ds);
	case REG_RDI:
		return (rp->tf_edi);
	case REG_RSI:
		return (rp->tf_esi);
	case REG_RBP:
		return (rp->tf_ebp);
	case REG_RSP:
		return (rp->tf_isp);
	case REG_RBX:
		return (rp->tf_ebx);
	case REG_RCX:
		return (rp->tf_ecx);
	case REG_RAX:
		return (rp->tf_eax);
	case REG_TRAPNO:
		return (rp->tf_trapno);
	case REG_ERR:
		return (rp->tf_err);
	case REG_RIP:
		return (rp->tf_eip);
	case REG_CS:
		return (rp->tf_cs);
	case REG_RFL:
		return (rp->tf_eflags);
#if 0
	case REG_RSP:
		return (rp->tf_esp);
#endif
	case REG_SS:
		return (rp->tf_ss);
	default:
		DTRACE_CPUFLAG_SET(CPU_DTRACE_ILLOP);
		return (0);
	}
}

static int
dtrace_copycheck(uintptr_t uaddr, uintptr_t kaddr, size_t size)
{
	ASSERT(kaddr >= kernelbase && kaddr + size >= kaddr);

	if (uaddr + size >= kernelbase || uaddr + size < uaddr) {
		DTRACE_CPUFLAG_SET(CPU_DTRACE_BADADDR);
		cpu_core[curcpu].cpuc_dtrace_illval = uaddr;
		return (0);
	}

	return (1);
}

void
dtrace_copyin(uintptr_t uaddr, uintptr_t kaddr, size_t size,
    volatile uint16_t *flags)
{
	if (dtrace_copycheck(uaddr, kaddr, size))
		dtrace_copy(uaddr, kaddr, size);
}

void
dtrace_copyout(uintptr_t kaddr, uintptr_t uaddr, size_t size,
    volatile uint16_t *flags)
{
	if (dtrace_copycheck(uaddr, kaddr, size))
		dtrace_copy(kaddr, uaddr, size);
}

void
dtrace_copyinstr(uintptr_t uaddr, uintptr_t kaddr, size_t size,
    volatile uint16_t *flags)
{
	if (dtrace_copycheck(uaddr, kaddr, size))
		dtrace_copystr(uaddr, kaddr, size, flags);
}

void
dtrace_copyoutstr(uintptr_t kaddr, uintptr_t uaddr, size_t size,
    volatile uint16_t *flags)
{
	if (dtrace_copycheck(uaddr, kaddr, size))
		dtrace_copystr(kaddr, uaddr, size, flags);
}

uint8_t
dtrace_fuword8(void *uaddr)
{
	if ((uintptr_t)uaddr >= kernelbase) {
		DTRACE_CPUFLAG_SET(CPU_DTRACE_BADADDR);
		cpu_core[curcpu].cpuc_dtrace_illval = (uintptr_t)uaddr;
		return (0);
	}
	return (dtrace_fuword8_nocheck(uaddr));
}

uint16_t
dtrace_fuword16(void *uaddr)
{
	if ((uintptr_t)uaddr >= kernelbase) {
		DTRACE_CPUFLAG_SET(CPU_DTRACE_BADADDR);
		cpu_core[curcpu].cpuc_dtrace_illval = (uintptr_t)uaddr;
		return (0);
	}
	return (dtrace_fuword16_nocheck(uaddr));
}

uint32_t
dtrace_fuword32(void *uaddr)
{
	if ((uintptr_t)uaddr >= kernelbase) {
		DTRACE_CPUFLAG_SET(CPU_DTRACE_BADADDR);
		cpu_core[curcpu].cpuc_dtrace_illval = (uintptr_t)uaddr;
		return (0);
	}
	return (dtrace_fuword32_nocheck(uaddr));
}

uint64_t
dtrace_fuword64(void *uaddr)
{
	if ((uintptr_t)uaddr >= kernelbase) {
		DTRACE_CPUFLAG_SET(CPU_DTRACE_BADADDR);
		cpu_core[curcpu].cpuc_dtrace_illval = (uintptr_t)uaddr;
		return (0);
	}
	return (dtrace_fuword64_nocheck(uaddr));
}
