/*-
 * Copyright (c) 1989, 1990 William F. Jolitz.
 * Copyright (c) 1990 The Regents of the University of California.
 * Copyright (c) 2007, 2018 The FreeBSD Foundation
 * All rights reserved.
 *
 * Portions of this software were developed by A. Joseph Koshy under
 * sponsorship from the FreeBSD Foundation and Google, Inc.
 * Portions of this software were developed by Konstantin Belousov
 * <kib@FreeBSD.org> under sponsorship from the FreeBSD Foundation.
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
 * $FreeBSD$
 */

#include "opt_apic.h"
#include "opt_atpic.h"
#include "opt_hwpmc_hooks.h"

#include "assym.inc"

#include <machine/psl.h>
#include <machine/asmacros.h>
#include <machine/trap.h>

#ifdef KDTRACE_HOOKS
	.bss
	.globl	dtrace_invop_jump_addr
	.align	4
	.type	dtrace_invop_jump_addr, @object
	.size	dtrace_invop_jump_addr, 4
dtrace_invop_jump_addr:
	.zero	4
	.globl	dtrace_invop_calltrap_addr
	.align	4
	.type	dtrace_invop_calltrap_addr, @object
	.size	dtrace_invop_calltrap_addr, 4
dtrace_invop_calltrap_addr:
	.zero	8
#endif
	.text
ENTRY(start_exceptions)
	.globl	tramp_idleptd
tramp_idleptd:	.long	0

/*****************************************************************************/
/* Trap handling                                                             */
/*****************************************************************************/
/*
 * Trap and fault vector routines.
 *
 * All traps are 'interrupt gates', SDT_SYS386IGT.  Interrupts are disabled
 * by hardware to not allow interrupts until code switched to the kernel
 * address space and the kernel thread stack.
 *
 * The cpu will push a certain amount of state onto the kernel stack for
 * the current process.  The amount of state depends on the type of trap
 * and whether the trap crossed rings or not.  See i386/include/frame.h.
 * At the very least the current EFLAGS (status register, which includes
 * the interrupt disable state prior to the trap), the code segment register,
 * and the return instruction pointer are pushed by the cpu.  The cpu
 * will also push an 'error' code for certain traps.  We push a dummy
 * error code for those traps where the cpu doesn't in order to maintain
 * a consistent frame.  We also push a contrived 'trap number'.
 *
 * The cpu does not push the general registers, we must do that, and we
 * must restore them prior to calling 'iret'.  The cpu adjusts the %cs and
 * %ss segment registers, but does not mess with %ds, %es, or %fs.  Thus we
 * must load them with appropriate values for supervisor mode operation.
 *
 * This code is not executed at the linked address, it is copied to the
 * trampoline area.  As the consequence, all code there and in included files
 * must be PIC.
 */

MCOUNT_LABEL(user)
MCOUNT_LABEL(btrap)

#define	TRAP(a)		pushl $(a) ; jmp alltraps

IDTVEC(div)
	pushl $0; TRAP(T_DIVIDE)
IDTVEC(bpt)
	pushl $0; TRAP(T_BPTFLT)
IDTVEC(dtrace_ret)
	pushl $0; TRAP(T_DTRACE_RET)
IDTVEC(ofl)
	pushl $0; TRAP(T_OFLOW)
IDTVEC(bnd)
	pushl $0; TRAP(T_BOUND)
#ifndef KDTRACE_HOOKS
IDTVEC(ill)
	pushl $0; TRAP(T_PRIVINFLT)
#endif
IDTVEC(dna)
	pushl $0; TRAP(T_DNA)
IDTVEC(fpusegm)
	pushl $0; TRAP(T_FPOPFLT)
IDTVEC(tss)
	TRAP(T_TSSFLT)
IDTVEC(missing)
	pushl	$T_SEGNPFLT
	jmp	irettraps
IDTVEC(stk)
	pushl	$T_STKFLT
	jmp	irettraps
IDTVEC(prot)
	pushl	$T_PROTFLT
	jmp	irettraps
IDTVEC(page)
	testl	$PSL_VM, TF_EFLAGS-TF_ERR(%esp)
	jnz	1f
	testb	$SEL_RPL_MASK, TF_CS-TF_ERR(%esp)
	jnz	1f
	cmpl	$PMAP_TRM_MIN_ADDRESS, TF_EIP-TF_ERR(%esp)
	jb	1f
	movl	%ebx, %cr3
	movl	%edx, TF_EIP-TF_ERR(%esp)
	addl	$4, %esp
	iret
1:	pushl	$T_PAGEFLT
	jmp	alltraps
IDTVEC(rsvd_pti)
IDTVEC(rsvd)
	pushl $0; TRAP(T_RESERVED)
IDTVEC(fpu)
	pushl $0; TRAP(T_ARITHTRAP)
IDTVEC(align)
	TRAP(T_ALIGNFLT)
IDTVEC(xmm)
	pushl $0; TRAP(T_XMMFLT)

	/*
	 * All traps except ones for syscalls or invalid segment,
	 * jump to alltraps.  If
	 * interrupts were enabled when the trap occurred, then interrupts
	 * are enabled now if the trap was through a trap gate, else
	 * disabled if the trap was through an interrupt gate.  Note that
	 * int0x80_syscall is a trap gate.   Interrupt gates are used by
	 * page faults, non-maskable interrupts, debug and breakpoint
	 * exceptions.
	 */
	SUPERALIGN_TEXT
	.globl	alltraps
	.type	alltraps,@function
alltraps:
	PUSH_FRAME2
alltraps_with_regs_pushed:
	SET_KERNEL_SREGS
	cld
	KENTER
	FAKE_MCOUNT(TF_EIP(%esp))
calltrap:
	pushl	%esp
	movl	$trap,%eax
	call	*%eax
	add	$4, %esp

	/*
	 * Return via doreti to handle ASTs.
	 */
	MEXITCOUNT
	jmp	doreti

	.globl	irettraps
	.type	irettraps,@function
irettraps:
	testl	$PSL_VM, TF_EFLAGS-TF_TRAPNO(%esp)
	jnz	alltraps
	testb	$SEL_RPL_MASK, TF_CS-TF_TRAPNO(%esp)
	jnz	alltraps

	/*
	 * Kernel mode.
	 * The special case there is the kernel mode with user %cr3 and
	 * trampoline stack. We need to copy both current frame and the
	 * hardware portion of the frame we tried to return to, to the
	 * normal stack.  This logic must follow the stack unwind order
	 * in doreti.
	 */
	PUSH_FRAME2
	SET_KERNEL_SREGS
	cld
	call	1f
1:	popl	%ebx
	leal	(doreti_iret - 1b)(%ebx), %edx
	cmpl	%edx, TF_EIP(%esp)
	jne	2f
	movl	$(2 * TF_SZ - TF_EIP), %ecx
	jmp	6f
2:	leal	(doreti_popl_ds - 1b)(%ebx), %edx
	cmpl	%edx, TF_EIP(%esp)
	jne	3f
	movl	$(2 * TF_SZ - TF_DS), %ecx
	jmp	6f
3:	leal	(doreti_popl_es - 1b)(%ebx), %edx
	cmpl	%edx, TF_EIP(%esp)
	jne	4f
	movl	$(2 * TF_SZ - TF_ES), %ecx
	jmp	6f
4:	leal	(doreti_popl_fs - 1b)(%ebx), %edx
	cmpl	%edx, TF_EIP(%esp)
	jne	5f
	movl	$(2 * TF_SZ - TF_FS), %ecx
	jmp	6f
	/* kernel mode, normal */
5:	FAKE_MCOUNT(TF_EIP(%esp))
	jmp	calltrap
6:	cmpl	$PMAP_TRM_MIN_ADDRESS, %esp	/* trampoline stack ? */
	jb	5b	/* if not, no need to change stacks */
	movl	(tramp_idleptd - 1b)(%ebx), %eax
	movl	%eax, %cr3
	movl	PCPU(KESP0), %edx
	subl	%ecx, %edx
	movl	%edx, %edi
	movl	%esp, %esi
	rep; movsb
	movl	%edx, %esp
	FAKE_MCOUNT(TF_EIP(%esp))
	jmp	calltrap

/*
 * Privileged instruction fault.
 */
#ifdef KDTRACE_HOOKS
	SUPERALIGN_TEXT
IDTVEC(ill)
	/*
	 * Check if this is a user fault.  If so, just handle it as a normal
	 * trap.
	 */
	testl	$PSL_VM, 8(%esp)	/* and vm86 mode. */
	jnz	norm_ill
	cmpl	$GSEL_KPL, 4(%esp)	/* Check the code segment */
	jne	norm_ill

	/*
	 * Check if a DTrace hook is registered.  The trampoline cannot
	 * be instrumented.
	 */
	cmpl	$0, dtrace_invop_jump_addr
	je	norm_ill

	/*
	 * This is a kernel instruction fault that might have been caused
	 * by a DTrace provider.
	 */
	pushal
	cld

	/*
	 * Set our jump address for the jump back in the event that
	 * the exception wasn't caused by DTrace at all.
	 */
	movl	$norm_ill, dtrace_invop_calltrap_addr

	/* Jump to the code hooked in by DTrace. */
	jmpl	*dtrace_invop_jump_addr

	/*
	 * Process the instruction fault in the normal way.
	 */
norm_ill:
	pushl	$0
	pushl	$T_PRIVINFLT
	jmp	alltraps
#endif

/*
 * See comment in the handler for the kernel case T_TRCTRAP in trap.c.
 * The exception handler must be ready to execute with wrong %cr3.
 * We save original %cr3 in frame->tf_err, similarly to NMI and MCE
 * handlers.
 */
IDTVEC(dbg)
	pushl	$0
	pushl	$T_TRCTRAP
	PUSH_FRAME2
	SET_KERNEL_SREGS
	cld
	movl	%cr3, %eax
	movl	%eax, TF_ERR(%esp)
	call	1f
1:	popl	%eax
	movl	(tramp_idleptd - 1b)(%eax), %eax
	movl	%eax, %cr3
	FAKE_MCOUNT(TF_EIP(%esp))
	testl	$PSL_VM, TF_EFLAGS(%esp)
	jnz	dbg_user
	testb	$SEL_RPL_MASK,TF_CS(%esp)
	jz	calltrap
dbg_user:
	NMOVE_STACKS
	movl	$handle_ibrs_entry,%eax
	call	*%eax
	pushl	%esp
	movl	$trap,%eax
	call	*%eax
	add	$4, %esp
	movl	$T_RESERVED, TF_TRAPNO(%esp)
	MEXITCOUNT
	jmp	doreti

IDTVEC(mchk)
	pushl	$0
	pushl	$T_MCHK
	jmp	nmi_mchk_common

IDTVEC(nmi)
	pushl	$0
	pushl	$T_NMI
nmi_mchk_common:
	PUSH_FRAME2
	SET_KERNEL_SREGS
	cld
	/*
	 * Save %cr3 into tf_err.  There is no good place to put it.
	 * Always reload %cr3, since we might have interrupted the
	 * kernel entry or exit.
	 * Do not switch to the thread kernel stack, otherwise we might
	 * obliterate the previous context partially copied from the
	 * trampoline stack.
	 * Do not re-enable IBRS, there is no good place to store
	 * previous state if we come from the kernel.
	 */
	movl	%cr3, %eax
	movl	%eax, TF_ERR(%esp)
	call	1f
1:	popl	%eax
	movl	(tramp_idleptd - 1b)(%eax), %eax
	movl	%eax, %cr3
	FAKE_MCOUNT(TF_EIP(%esp))
	jmp	calltrap

/*
 * Trap gate entry for syscalls (int 0x80).
 * This is used by FreeBSD ELF executables, "new" a.out executables, and all
 * Linux executables.
 *
 * Even though the name says 'int0x80', this is actually a trap gate, not an
 * interrupt gate.  Thus interrupts are enabled on entry just as they are for
 * a normal syscall.
 */
	SUPERALIGN_TEXT
IDTVEC(int0x80_syscall)
	pushl	$2			/* sizeof "int 0x80" */
	pushl	$0			/* tf_trapno */
	PUSH_FRAME2
	SET_KERNEL_SREGS
	cld
	MOVE_STACKS
	movl	$handle_ibrs_entry,%eax
	call	*%eax
	sti
	FAKE_MCOUNT(TF_EIP(%esp))
	pushl	%esp
	movl	$syscall, %eax
	call	*%eax
	add	$4, %esp
	MEXITCOUNT
	jmp	doreti

ENTRY(fork_trampoline)
	pushl	%esp			/* trapframe pointer */
	pushl	%ebx			/* arg1 */
	pushl	%esi			/* function */
	movl	$fork_exit, %eax
	call	*%eax
	addl	$12,%esp
	/* cut from syscall */

	/*
	 * Return via doreti to handle ASTs.
	 */
	MEXITCOUNT
	jmp	doreti


/*
 * To efficiently implement classification of trap and interrupt handlers
 * for profiling, there must be only trap handlers between the labels btrap
 * and bintr, and only interrupt handlers between the labels bintr and
 * eintr.  This is implemented (partly) by including files that contain
 * some of the handlers.  Before including the files, set up a normal asm
 * environment so that the included files doen't need to know that they are
 * included.
 */

	.data
	.p2align 4
	.text
	SUPERALIGN_TEXT
MCOUNT_LABEL(bintr)

#ifdef DEV_ATPIC
#include <i386/i386/atpic_vector.s>
#endif

#if defined(DEV_APIC) && defined(DEV_ATPIC)
	.data
	.p2align 4
	.text
	SUPERALIGN_TEXT
#endif

#ifdef DEV_APIC
#include <i386/i386/apic_vector.s>
#endif

	.data
	.p2align 4
	.text
	SUPERALIGN_TEXT
#include <i386/i386/vm86bios.s>

	.text
MCOUNT_LABEL(eintr)

#include <i386/i386/copyout_fast.s>

/*
 * void doreti(struct trapframe)
 *
 * Handle return from interrupts, traps and syscalls.
 */
	.text
	SUPERALIGN_TEXT
	.type	doreti,@function
	.globl	doreti
doreti:
	FAKE_MCOUNT($bintr)		/* init "from" bintr -> doreti */
doreti_next:
	/*
	 * Check if ASTs can be handled now.  ASTs cannot be safely
	 * processed when returning from an NMI.
	 */
	cmpb	$T_NMI,TF_TRAPNO(%esp)
#ifdef HWPMC_HOOKS
	je	doreti_nmi
#else
	je	doreti_exit
#endif
	/*
	 * PSL_VM must be checked first since segment registers only
	 * have an RPL in non-VM86 mode.
	 * ASTs can not be handled now if we are in a vm86 call.
	 */
	testl	$PSL_VM,TF_EFLAGS(%esp)
	jz	doreti_notvm86
	movl	PCPU(CURPCB),%ecx
	testl	$PCB_VM86CALL,PCB_FLAGS(%ecx)
	jz	doreti_ast
	jmp	doreti_popl_fs

doreti_notvm86:
	testb	$SEL_RPL_MASK,TF_CS(%esp) /* are we returning to user mode? */
	jz	doreti_exit		/* can't handle ASTs now if not */

doreti_ast:
	/*
	 * Check for ASTs atomically with returning.  Disabling CPU
	 * interrupts provides sufficient locking even in the SMP case,
	 * since we will be informed of any new ASTs by an IPI.
	 */
	cli
	movl	PCPU(CURTHREAD),%eax
	testl	$TDF_ASTPENDING | TDF_NEEDRESCHED,TD_FLAGS(%eax)
	je	doreti_exit
	sti
	pushl	%esp			/* pass a pointer to the trapframe */
	movl	$ast, %eax
	call	*%eax
	add	$4,%esp
	jmp	doreti_ast

	/*
	 * doreti_exit:	pop registers, iret.
	 *
	 *	The segment register pop is a special case, since it may
	 *	fault if (for example) a sigreturn specifies bad segment
	 *	registers.  The fault is handled in trap.c.
	 */
doreti_exit:
	MEXITCOUNT

	cmpl	$T_NMI, TF_TRAPNO(%esp)
	je	doreti_iret_nmi
	cmpl	$T_MCHK, TF_TRAPNO(%esp)
	je	doreti_iret_nmi
	cmpl	$T_TRCTRAP, TF_TRAPNO(%esp)
	je	doreti_iret_nmi
	movl	$TF_SZ, %ecx
	testl	$PSL_VM,TF_EFLAGS(%esp)
	jz	1f			/* PCB_VM86CALL is not set */
	addl	$VM86_STACK_SPACE, %ecx
	jmp	2f
1:	testl	$SEL_RPL_MASK, TF_CS(%esp)
	jz	doreti_popl_fs
2:	movl	$handle_ibrs_exit,%eax
	pushl	%ecx			/* preserve enough call-used regs */
	call	*%eax
	popl	%ecx
	movl	%esp, %esi
	movl	PCPU(TRAMPSTK), %edx
	subl	%ecx, %edx
	movl	%edx, %edi
	rep; movsb
	movl	%edx, %esp
	movl	PCPU(CURPCB),%eax
	movl	PCB_CR3(%eax), %eax
	movl	%eax, %cr3

	.globl	doreti_popl_fs
doreti_popl_fs:
	popl	%fs
	.globl	doreti_popl_es
doreti_popl_es:
	popl	%es
	.globl	doreti_popl_ds
doreti_popl_ds:
	popl	%ds
	popal
	addl	$8,%esp
	.globl	doreti_iret
doreti_iret:
	iret

doreti_iret_nmi:
	movl	TF_ERR(%esp), %eax
	movl	%eax, %cr3
	jmp	doreti_popl_fs

	/*
	 * doreti_iret_fault and friends.  Alternative return code for
	 * the case where we get a fault in the doreti_exit code
	 * above.  trap() (i386/i386/trap.c) catches this specific
	 * case, and continues in the corresponding place in the code
	 * below.
	 *
	 * If the fault occured during return to usermode, we recreate
	 * the trap frame and call trap() to send a signal.  Otherwise
	 * the kernel was tricked into fault by attempt to restore invalid
	 * usermode segment selectors on return from nested fault or
	 * interrupt, where interrupted kernel entry code not yet loaded
	 * kernel selectors.  In the latter case, emulate iret and zero
	 * the invalid selector.
	 */
	ALIGN_TEXT
	.globl	doreti_iret_fault
doreti_iret_fault:
	pushl	$0	/* tf_err */
	pushl	$0	/* tf_trapno XXXKIB: provide more useful value ? */
	pushal
	pushl	$0
	movw	%ds,(%esp)
	.globl	doreti_popl_ds_fault
doreti_popl_ds_fault:
	testb	$SEL_RPL_MASK,TF_CS-TF_DS(%esp)
	jz	doreti_popl_ds_kfault
	pushl	$0
	movw	%es,(%esp)
	.globl	doreti_popl_es_fault
doreti_popl_es_fault:
	testb	$SEL_RPL_MASK,TF_CS-TF_ES(%esp)
	jz	doreti_popl_es_kfault
	pushl	$0
	movw	%fs,(%esp)
	.globl	doreti_popl_fs_fault
doreti_popl_fs_fault:
	testb	$SEL_RPL_MASK,TF_CS-TF_FS(%esp)
	jz	doreti_popl_fs_kfault
	movl	$0,TF_ERR(%esp)	/* XXX should be the error code */
	movl	$T_PROTFLT,TF_TRAPNO(%esp)
	SET_KERNEL_SREGS
	jmp	calltrap

doreti_popl_ds_kfault:
	movl	$0,(%esp)
	jmp	doreti_popl_ds
doreti_popl_es_kfault:
	movl	$0,(%esp)
	jmp	doreti_popl_es
doreti_popl_fs_kfault:
	movl	$0,(%esp)
	jmp	doreti_popl_fs

#ifdef HWPMC_HOOKS
doreti_nmi:
	/*
	 * Since we are returning from an NMI, check if the current trap
	 * was from user mode and if so whether the current thread
	 * needs a user call chain capture.
	 */
	testl	$PSL_VM, TF_EFLAGS(%esp)
	jnz	doreti_exit
	testb	$SEL_RPL_MASK,TF_CS(%esp)
	jz	doreti_exit
	movl	PCPU(CURTHREAD),%eax	/* curthread present? */
	orl	%eax,%eax
	jz	doreti_exit
	testl	$TDP_CALLCHAIN,TD_PFLAGS(%eax) /* flagged for capture? */
	jz	doreti_exit
	/*
	 * Switch to thread stack.  Reset tf_trapno to not indicate NMI,
	 * to cause normal userspace exit.
	 */
	movl	$T_RESERVED, TF_TRAPNO(%esp)
	NMOVE_STACKS
	/*
	 * Take the processor out of NMI mode by executing a fake "iret".
	 */
	pushfl
	pushl	%cs
	call	1f
1:	popl	%eax
	leal	(outofnmi-1b)(%eax),%eax
	pushl	%eax
	iret
outofnmi:
	/*
	 * Call the callchain capture hook after turning interrupts back on.
	 */
	movl	pmc_hook,%ecx
	orl	%ecx,%ecx
	jz	doreti_exit
	pushl	%esp			/* frame pointer */
	pushl	$PMC_FN_USER_CALLCHAIN	/* command */
	movl	PCPU(CURTHREAD),%eax
	pushl	%eax			/* curthread */
	sti
	call	*%ecx
	addl	$12,%esp
	jmp	doreti_ast
#endif

ENTRY(end_exceptions)
