/*-
 * Copyright (c) 1989, 1990 William F. Jolitz.
 * Copyright (c) 1990 The Regents of the University of California.
 * Copyright (c) 2007-2018 The FreeBSD Foundation
 * All rights reserved.
 *
 * Portions of this software were developed by A. Joseph Koshy under
 * sponsorship from the FreeBSD Foundation and Google, Inc.
 *
 * Portions of this software were developed by
 * Konstantin Belousov <kib@FreeBSD.org> under sponsorship from
 * the FreeBSD Foundation.
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

#include "opt_atpic.h"
#include "opt_hwpmc_hooks.h"

#include "assym.inc"

#include <machine/psl.h>
#include <machine/asmacros.h>
#include <machine/trap.h>
#include <machine/specialreg.h>

#ifdef KDTRACE_HOOKS
	.bss
	.globl	dtrace_invop_jump_addr
	.align	8
	.type	dtrace_invop_jump_addr,@object
	.size	dtrace_invop_jump_addr,8
dtrace_invop_jump_addr:
	.zero	8
	.globl	dtrace_invop_calltrap_addr
	.align	8
	.type	dtrace_invop_calltrap_addr,@object
	.size	dtrace_invop_calltrap_addr,8
dtrace_invop_calltrap_addr:
	.zero	8
#endif
	.text
#ifdef HWPMC_HOOKS
	ENTRY(start_exceptions)
#endif

/*****************************************************************************/
/* Trap handling                                                             */
/*****************************************************************************/
/*
 * Trap and fault vector routines.
 *
 * All traps are 'interrupt gates', SDT_SYSIGT.  An interrupt gate pushes
 * state on the stack but also disables interrupts.  This is important for
 * us for the use of the swapgs instruction.  We cannot be interrupted
 * until the GS.base value is correct.  For most traps, we automatically
 * then enable interrupts if the interrupted context had them enabled.
 * This is equivalent to the i386 port's use of SDT_SYS386TGT.
 *
 * The cpu will push a certain amount of state onto the kernel stack for
 * the current process.  See amd64/include/frame.h.
 * This includes the current RFLAGS (status register, which includes
 * the interrupt disable state prior to the trap), the code segment register,
 * and the return instruction pointer are pushed by the cpu.  The cpu
 * will also push an 'error' code for certain traps.  We push a dummy
 * error code for those traps where the cpu doesn't in order to maintain
 * a consistent frame.  We also push a contrived 'trap number'.
 *
 * The CPU does not push the general registers, so we must do that, and we
 * must restore them prior to calling 'iret'.  The CPU adjusts %cs and %ss
 * but does not mess with %ds, %es, %gs or %fs.  We swap the %gs base for
 * for the kernel mode operation shortly, without changes to the selector
 * loaded.  Since superuser long mode works with any selectors loaded into
 * segment registers other then %cs, which makes them mostly unused in long
 * mode, and kernel does not reference %fs, leave them alone.  The segment
 * registers are reloaded on return to the usermode.
 */

MCOUNT_LABEL(user)
MCOUNT_LABEL(btrap)

/* Traps that we leave interrupts disabled for. */
	.macro	TRAP_NOEN	l, trapno
	PTI_ENTRY	\l,X\l
	.globl	X\l
	.type	X\l,@function
X\l:	subq $TF_RIP,%rsp
	movl $\trapno,TF_TRAPNO(%rsp)
	movq $0,TF_ADDR(%rsp)
	movq $0,TF_ERR(%rsp)
	jmp alltraps_noen
	.endm

	TRAP_NOEN	bpt, T_BPTFLT
#ifdef KDTRACE_HOOKS
	TRAP_NOEN	dtrace_ret, T_DTRACE_RET
#endif

/* Regular traps; The cpu does not supply tf_err for these. */
	.macro	TRAP	l, trapno
	PTI_ENTRY	\l,X\l
	.globl	X\l
	.type	X\l,@function
X\l:
	subq $TF_RIP,%rsp
	movl $\trapno,TF_TRAPNO(%rsp)
	movq $0,TF_ADDR(%rsp)
	movq $0,TF_ERR(%rsp)
	jmp alltraps
	.endm

	TRAP	div, T_DIVIDE
	TRAP	ofl, T_OFLOW
	TRAP	bnd, T_BOUND
	TRAP	ill, T_PRIVINFLT
	TRAP	dna, T_DNA
	TRAP	fpusegm, T_FPOPFLT
	TRAP	rsvd, T_RESERVED
	TRAP	fpu, T_ARITHTRAP
	TRAP	xmm, T_XMMFLT

/* This group of traps have tf_err already pushed by the cpu. */
	.macro	TRAP_ERR	l, trapno
	PTI_ENTRY	\l,X\l,has_err=1
	.globl	X\l
	.type	X\l,@function
X\l:
	subq $TF_ERR,%rsp
	movl $\trapno,TF_TRAPNO(%rsp)
	movq $0,TF_ADDR(%rsp)
	jmp alltraps
	.endm

	TRAP_ERR	tss, T_TSSFLT
	TRAP_ERR	align, T_ALIGNFLT

	/*
	 * alltraps entry point.  Use swapgs if this is the first time in the
	 * kernel from userland.  Reenable interrupts if they were enabled
	 * before the trap.  This approximates SDT_SYS386TGT on the i386 port.
	 */
	SUPERALIGN_TEXT
	.globl	alltraps
	.type	alltraps,@function
alltraps:
	movq	%rdi,TF_RDI(%rsp)
	testb	$SEL_RPL_MASK,TF_CS(%rsp) /* Did we come from kernel? */
	jz	1f		/* already running with kernel GS.base */
	swapgs
	movq	PCPU(CURPCB),%rdi
	andl	$~PCB_FULL_IRET,PCB_FLAGS(%rdi)
1:	SAVE_SEGS
	movq	%rdx,TF_RDX(%rsp)
	movq	%rax,TF_RAX(%rsp)
	movq	%rcx,TF_RCX(%rsp)
	testb	$SEL_RPL_MASK,TF_CS(%rsp)
	jz	2f
	call	handle_ibrs_entry
2:	testl	$PSL_I,TF_RFLAGS(%rsp)
	jz	alltraps_pushregs_no_rax
	sti
alltraps_pushregs_no_rax:
	movq	%rsi,TF_RSI(%rsp)
	movq	%r8,TF_R8(%rsp)
	movq	%r9,TF_R9(%rsp)
	movq	%rbx,TF_RBX(%rsp)
	movq	%rbp,TF_RBP(%rsp)
	movq	%r10,TF_R10(%rsp)
	movq	%r11,TF_R11(%rsp)
	movq	%r12,TF_R12(%rsp)
	movq	%r13,TF_R13(%rsp)
	movq	%r14,TF_R14(%rsp)
	movq	%r15,TF_R15(%rsp)
	movl	$TF_HASSEGS,TF_FLAGS(%rsp)
	pushfq
	andq	$~(PSL_D | PSL_AC),(%rsp)
	popfq
	FAKE_MCOUNT(TF_RIP(%rsp))
#ifdef KDTRACE_HOOKS
	/*
	 * DTrace Function Boundary Trace (fbt) probes are triggered
	 * by int3 (0xcc) which causes the #BP (T_BPTFLT) breakpoint
	 * interrupt. For all other trap types, just handle them in
	 * the usual way.
	 */
	testb	$SEL_RPL_MASK,TF_CS(%rsp) /* Did we come from kernel? */
	jnz	calltrap		/* ignore userland traps */
	cmpl	$T_BPTFLT,TF_TRAPNO(%rsp)
	jne	calltrap

	/* Check if there is no DTrace hook registered. */
	cmpq	$0,dtrace_invop_jump_addr
	je	calltrap

	/*
	 * Set our jump address for the jump back in the event that
	 * the breakpoint wasn't caused by DTrace at all.
	 */
	movq	$calltrap,dtrace_invop_calltrap_addr(%rip)

	/* Jump to the code hooked in by DTrace. */
	jmpq	*dtrace_invop_jump_addr
#endif
	.globl	calltrap
	.type	calltrap,@function
calltrap:
	movq	%rsp,%rdi
	call	trap_check
	MEXITCOUNT
	jmp	doreti			/* Handle any pending ASTs */

	/*
	 * alltraps_noen entry point.  Unlike alltraps above, we want to
	 * leave the interrupts disabled.  This corresponds to
	 * SDT_SYS386IGT on the i386 port.
	 */
	SUPERALIGN_TEXT
	.globl	alltraps_noen
	.type	alltraps_noen,@function
alltraps_noen:
	movq	%rdi,TF_RDI(%rsp)
	testb	$SEL_RPL_MASK,TF_CS(%rsp) /* Did we come from kernel? */
	jz	1f /* already running with kernel GS.base */
	swapgs
	movq	PCPU(CURPCB),%rdi
	andl	$~PCB_FULL_IRET,PCB_FLAGS(%rdi)
1:	SAVE_SEGS
	movq	%rdx,TF_RDX(%rsp)
	movq	%rax,TF_RAX(%rsp)
	movq	%rcx,TF_RCX(%rsp)
	testb	$SEL_RPL_MASK,TF_CS(%rsp)
	jz	alltraps_pushregs_no_rax
	call	handle_ibrs_entry
	jmp	alltraps_pushregs_no_rax

IDTVEC(dblfault)
	subq	$TF_ERR,%rsp
	movl	$T_DOUBLEFLT,TF_TRAPNO(%rsp)
	movq	$0,TF_ADDR(%rsp)
	movq	$0,TF_ERR(%rsp)
	movq	%rdi,TF_RDI(%rsp)
	movq	%rsi,TF_RSI(%rsp)
	movq	%rdx,TF_RDX(%rsp)
	movq	%rcx,TF_RCX(%rsp)
	movq	%r8,TF_R8(%rsp)
	movq	%r9,TF_R9(%rsp)
	movq	%rax,TF_RAX(%rsp)
	movq	%rbx,TF_RBX(%rsp)
	movq	%rbp,TF_RBP(%rsp)
	movq	%r10,TF_R10(%rsp)
	movq	%r11,TF_R11(%rsp)
	movq	%r12,TF_R12(%rsp)
	movq	%r13,TF_R13(%rsp)
	movq	%r14,TF_R14(%rsp)
	movq	%r15,TF_R15(%rsp)
	SAVE_SEGS
	movl	$TF_HASSEGS,TF_FLAGS(%rsp)
	pushfq
	andq	$~(PSL_D | PSL_AC),(%rsp)
	popfq
	testb	$SEL_RPL_MASK,TF_CS(%rsp) /* Did we come from kernel? */
	jz	1f			/* already running with kernel GS.base */
	swapgs
1:
	movq	PCPU(KCR3),%rax
	cmpq	$~0,%rax
	je	2f
	movq	%rax,%cr3
2:	movq	%rsp,%rdi
	call	dblfault_handler
3:	hlt
	jmp	3b

	ALIGN_TEXT
IDTVEC(page_pti)
	testb	$SEL_RPL_MASK,PTI_CS-2*8(%rsp)
	jz	Xpage
	swapgs
	pushq	%rax
	movq	%cr3,%rax
	movq	%rax,PCPU(SAVED_UCR3)
	cmpq	$~0,PCPU(UCR3)
	jne	1f
	popq	%rax
	jmp	2f
1:	pushq	%rdx
	PTI_UUENTRY has_err=1
2:	subq	$TF_ERR,%rsp
	movq	%rdi,TF_RDI(%rsp)
	movq	%rax,TF_RAX(%rsp)
	movq	%rdx,TF_RDX(%rsp)
	movq	%rcx,TF_RCX(%rsp)
	jmp	page_u
IDTVEC(page)
	subq	$TF_ERR,%rsp
	movq	%rdi,TF_RDI(%rsp)	/* free up GP registers */
	movq	%rax,TF_RAX(%rsp)
	movq	%rdx,TF_RDX(%rsp)
	movq	%rcx,TF_RCX(%rsp)
	testb	$SEL_RPL_MASK,TF_CS(%rsp) /* Did we come from kernel? */
	jz	page_cr2		/* already running with kernel GS.base */
	swapgs
page_u:	movq	PCPU(CURPCB),%rdi
	andl	$~PCB_FULL_IRET,PCB_FLAGS(%rdi)
	movq	PCPU(SAVED_UCR3),%rax
	movq	%rax,PCB_SAVED_UCR3(%rdi)
	call	handle_ibrs_entry
page_cr2:
	movq	%cr2,%rdi		/* preserve %cr2 before ..  */
	movq	%rdi,TF_ADDR(%rsp)	/* enabling interrupts. */
	SAVE_SEGS
	movl	$T_PAGEFLT,TF_TRAPNO(%rsp)
	testl	$PSL_I,TF_RFLAGS(%rsp)
	jz	alltraps_pushregs_no_rax
	sti
	jmp	alltraps_pushregs_no_rax

	/*
	 * We have to special-case this one.  If we get a trap in doreti() at
	 * the iretq stage, we'll reenter with the wrong gs state.  We'll have
	 * to do a special the swapgs in this case even coming from the kernel.
	 * XXX linux has a trap handler for their equivalent of load_gs().
	 *
	 * On the stack, we have the hardware interrupt frame to return
	 * to usermode (faulted) and another frame with error code, for
	 * fault.  For PTI, copy both frames to the main thread stack.
	 * Handle the potential 16-byte alignment adjustment incurred
	 * during the second fault by copying both frames independently
	 * while unwinding the stack in between.
	 */
	.macro PROTF_ENTRY name,trapno
\name\()_pti_doreti:
	swapgs
	cmpq	$~0,PCPU(UCR3)
	je	1f
	pushq	%rax
	pushq	%rdx
	movq	PCPU(KCR3),%rax
	movq	%rax,%cr3
	movq	PCPU(RSP0),%rax
	subq	$2*PTI_SIZE-3*8,%rax /* no err, %rax, %rdx in faulted frame */
	MOVE_STACKS	(PTI_SIZE / 8)
	addq	$PTI_SIZE,%rax
	movq	PTI_RSP(%rsp),%rsp
	MOVE_STACKS	(PTI_SIZE / 8 - 3)
	subq	$PTI_SIZE,%rax
	movq	%rax,%rsp
	popq	%rdx
	popq	%rax
1:	swapgs
	jmp	X\name
IDTVEC(\name\()_pti)
	cmpq	$doreti_iret,PTI_RIP-2*8(%rsp)
	je	\name\()_pti_doreti
	testb	$SEL_RPL_MASK,PTI_CS-2*8(%rsp) /* %rax, %rdx not yet pushed */
	jz	X\name
	PTI_UENTRY has_err=1
	swapgs
IDTVEC(\name)
	subq	$TF_ERR,%rsp
	movl	$\trapno,TF_TRAPNO(%rsp)
	jmp	prot_addrf
	.endm

	PROTF_ENTRY	missing, T_SEGNPFLT
	PROTF_ENTRY	stk, T_STKFLT
	PROTF_ENTRY	prot, T_PROTFLT

prot_addrf:
	movq	$0,TF_ADDR(%rsp)
	movq	%rdi,TF_RDI(%rsp)	/* free up a GP register */
	movq	%rax,TF_RAX(%rsp)
	movq	%rdx,TF_RDX(%rsp)
	movq	%rcx,TF_RCX(%rsp)
	movw	%fs,TF_FS(%rsp)
	movw	%gs,TF_GS(%rsp)
	leaq	doreti_iret(%rip),%rdi
	cmpq	%rdi,TF_RIP(%rsp)
	je	5f			/* kernel but with user gsbase!! */
	testb	$SEL_RPL_MASK,TF_CS(%rsp) /* Did we come from kernel? */
	jz	6f			/* already running with kernel GS.base */
	testb	$CPUID_STDEXT_FSGSBASE,cpu_stdext_feature(%rip)
	jz	2f
	cmpw	$KUF32SEL,TF_FS(%rsp)
	jne	1f
	rdfsbase %rax
1:	cmpw	$KUG32SEL,TF_GS(%rsp)
	jne	2f
	rdgsbase %rdx
2:	swapgs
	movq	PCPU(CURPCB),%rdi
	testb	$CPUID_STDEXT_FSGSBASE,cpu_stdext_feature(%rip)
	jz	4f
	cmpw	$KUF32SEL,TF_FS(%rsp)
	jne	3f
	movq	%rax,PCB_FSBASE(%rdi)
3:	cmpw	$KUG32SEL,TF_GS(%rsp)
	jne	4f
	movq	%rdx,PCB_GSBASE(%rdi)
4:	call	handle_ibrs_entry
	orl	$PCB_FULL_IRET,PCB_FLAGS(%rdi)	/* always full iret from GPF */
	movw	%es,TF_ES(%rsp)
	movw	%ds,TF_DS(%rsp)
	testl	$PSL_I,TF_RFLAGS(%rsp)
	jz	alltraps_pushregs_no_rax
	sti
	jmp	alltraps_pushregs_no_rax

5:	swapgs
6:	movq	PCPU(CURPCB),%rdi
	jmp	4b

/*
 * Fast syscall entry point.  We enter here with just our new %cs/%ss set,
 * and the new privilige level.  We are still running on the old user stack
 * pointer.  We have to juggle a few things around to find our stack etc.
 * swapgs gives us access to our PCPU space only.
 *
 * We do not support invoking this from a custom segment registers,
 * esp. %cs, %ss, %fs, %gs, e.g. using entries from an LDT.
 */
	SUPERALIGN_TEXT
IDTVEC(fast_syscall_pti)
	swapgs
	movq	%rax,PCPU(SCRATCH_RAX)
	cmpq	$~0,PCPU(UCR3)
	je	fast_syscall_common
	movq	PCPU(KCR3),%rax
	movq	%rax,%cr3
	jmp	fast_syscall_common
	SUPERALIGN_TEXT
IDTVEC(fast_syscall)
	swapgs
	movq	%rax,PCPU(SCRATCH_RAX)
fast_syscall_common:
	movq	%rsp,PCPU(SCRATCH_RSP)
	movq	PCPU(RSP0),%rsp
	/* Now emulate a trapframe. Make the 8 byte alignment odd for call. */
	subq	$TF_SIZE,%rsp
	/* defer TF_RSP till we have a spare register */
	movq	%r11,TF_RFLAGS(%rsp)
	movq	%rcx,TF_RIP(%rsp)	/* %rcx original value is in %r10 */
	movq	PCPU(SCRATCH_RSP),%r11	/* %r11 already saved */
	movq	%r11,TF_RSP(%rsp)	/* user stack pointer */
	movq	PCPU(SCRATCH_RAX),%rax
	/*
	 * Save a few arg registers early to free them for use in
	 * handle_ibrs_entry().  %r10 is especially tricky.  It is not an
	 * arg register, but it holds the arg register %rcx.  Profiling
	 * preserves %rcx, but may clobber %r10.  Profiling may also
	 * clobber %r11, but %r11 (original %eflags) has been saved.
	 */
	movq	%rax,TF_RAX(%rsp)	/* syscall number */
	movq	%rdx,TF_RDX(%rsp)	/* arg 3 */
	movq	%r10,TF_RCX(%rsp)	/* arg 4 */
	SAVE_SEGS
	call	handle_ibrs_entry
	movq	PCPU(CURPCB),%r11
	andl	$~PCB_FULL_IRET,PCB_FLAGS(%r11)
	sti
	movq	$KUDSEL,TF_SS(%rsp)
	movq	$KUCSEL,TF_CS(%rsp)
	movq	$2,TF_ERR(%rsp)
	movq	%rdi,TF_RDI(%rsp)	/* arg 1 */
	movq	%rsi,TF_RSI(%rsp)	/* arg 2 */
	movq	%r8,TF_R8(%rsp)		/* arg 5 */
	movq	%r9,TF_R9(%rsp)		/* arg 6 */
	movq	%rbx,TF_RBX(%rsp)	/* C preserved */
	movq	%rbp,TF_RBP(%rsp)	/* C preserved */
	movq	%r12,TF_R12(%rsp)	/* C preserved */
	movq	%r13,TF_R13(%rsp)	/* C preserved */
	movq	%r14,TF_R14(%rsp)	/* C preserved */
	movq	%r15,TF_R15(%rsp)	/* C preserved */
	movl	$TF_HASSEGS,TF_FLAGS(%rsp)
	FAKE_MCOUNT(TF_RIP(%rsp))
	movq	PCPU(CURTHREAD),%rdi
	movq	%rsp,TD_FRAME(%rdi)
	movl	TF_RFLAGS(%rsp),%esi
	andl	$PSL_T,%esi
	call	amd64_syscall
1:	movq	PCPU(CURPCB),%rax
	/* Disable interrupts before testing PCB_FULL_IRET. */
	cli
	testl	$PCB_FULL_IRET,PCB_FLAGS(%rax)
	jnz	4f
	/* Check for and handle AST's on return to userland. */
	movq	PCPU(CURTHREAD),%rax
	testl	$TDF_ASTPENDING | TDF_NEEDRESCHED,TD_FLAGS(%rax)
	jne	3f
	call	handle_ibrs_exit
	/* Restore preserved registers. */
	MEXITCOUNT
	movq	TF_RDI(%rsp),%rdi	/* bonus; preserve arg 1 */
	movq	TF_RSI(%rsp),%rsi	/* bonus: preserve arg 2 */
	movq	TF_RDX(%rsp),%rdx	/* return value 2 */
	movq	TF_RAX(%rsp),%rax	/* return value 1 */
	movq	TF_RFLAGS(%rsp),%r11	/* original %rflags */
	movq	TF_RIP(%rsp),%rcx	/* original %rip */
	movq	TF_RSP(%rsp),%rsp	/* user stack pointer */
	xorl	%r8d,%r8d		/* zero the rest of GPRs */
	xorl	%r10d,%r10d
	cmpq	$~0,PCPU(UCR3)
	je	2f
	movq	PCPU(UCR3),%r9
	movq	%r9,%cr3
2:	xorl	%r9d,%r9d
	swapgs
	sysretq

3:	/* AST scheduled. */
	sti
	movq	%rsp,%rdi
	call	ast
	jmp	1b

4:	/* Requested full context restore, use doreti for that. */
	MEXITCOUNT
	jmp	doreti

/*
 * Here for CYA insurance, in case a "syscall" instruction gets
 * issued from 32 bit compatibility mode. MSR_CSTAR has to point
 * to *something* if EFER_SCE is enabled.
 */
IDTVEC(fast_syscall32)
	sysret

/*
 * DB# handler is very similar to NM#, because 'mov/pop %ss' delay
 * generation of exception until the next instruction is executed,
 * which might be a kernel entry.  So we must execute the handler
 * on IST stack and be ready for non-kernel GSBASE.
 */
IDTVEC(dbg)
	subq	$TF_RIP,%rsp
	movl	$(T_TRCTRAP),TF_TRAPNO(%rsp)
	movq	$0,TF_ADDR(%rsp)
	movq	$0,TF_ERR(%rsp)
	movq	%rdi,TF_RDI(%rsp)
	movq	%rsi,TF_RSI(%rsp)
	movq	%rdx,TF_RDX(%rsp)
	movq	%rcx,TF_RCX(%rsp)
	movq	%r8,TF_R8(%rsp)
	movq	%r9,TF_R9(%rsp)
	movq	%rax,TF_RAX(%rsp)
	movq	%rbx,TF_RBX(%rsp)
	movq	%rbp,TF_RBP(%rsp)
	movq	%r10,TF_R10(%rsp)
	movq	%r11,TF_R11(%rsp)
	movq	%r12,TF_R12(%rsp)
	movq	%r13,TF_R13(%rsp)
	movq	%r14,TF_R14(%rsp)
	movq	%r15,TF_R15(%rsp)
	SAVE_SEGS
	movl	$TF_HASSEGS,TF_FLAGS(%rsp)
	pushfq
	andq	$~(PSL_D | PSL_AC),(%rsp)
	popfq
	testb	$SEL_RPL_MASK,TF_CS(%rsp)
	jnz	dbg_fromuserspace
	/*
	 * We've interrupted the kernel.  Preserve GS.base in %r12,
	 * %cr3 in %r13, and possibly lower half of MSR_IA32_SPEC_CTL in %r14d.
	 */
	movl	$MSR_GSBASE,%ecx
	rdmsr
	movq	%rax,%r12
	shlq	$32,%rdx
	orq	%rdx,%r12
	/* Retrieve and load the canonical value for GS.base. */
	movq	TF_SIZE(%rsp),%rdx
	movl	%edx,%eax
	shrq	$32,%rdx
	wrmsr
	movq	%cr3,%r13
	movq	PCPU(KCR3),%rax
	cmpq	$~0,%rax
	je	1f
	movq	%rax,%cr3
1:	testl	$CPUID_STDEXT3_IBPB,cpu_stdext_feature3(%rip)
	je	2f
	movl	$MSR_IA32_SPEC_CTRL,%ecx
	rdmsr
	movl	%eax,%r14d
	call	handle_ibrs_entry
2:	FAKE_MCOUNT(TF_RIP(%rsp))
	movq	%rsp,%rdi
	call	trap
	MEXITCOUNT
	testl	$CPUID_STDEXT3_IBPB,cpu_stdext_feature3(%rip)
	je	3f
	movl	%r14d,%eax
	xorl	%edx,%edx
	movl	$MSR_IA32_SPEC_CTRL,%ecx
	wrmsr
	/*
	 * Put back the preserved MSR_GSBASE value.
	 */
3:	movl	$MSR_GSBASE,%ecx
	movq	%r12,%rdx
	movl	%edx,%eax
	shrq	$32,%rdx
	wrmsr
	movq	%r13,%cr3
	RESTORE_REGS
	addq	$TF_RIP,%rsp
	jmp	doreti_iret
dbg_fromuserspace:
	/*
	 * Switch to kernel GSBASE and kernel page table, and copy frame
	 * from the IST stack to the normal kernel stack, since trap()
	 * re-enables interrupts, and since we might trap on DB# while
	 * in trap().
	 */
	swapgs
	movq	PCPU(KCR3),%rax
	cmpq	$~0,%rax
	je	1f
	movq	%rax,%cr3
1:	movq	PCPU(RSP0),%rax
	movl	$TF_SIZE,%ecx
	subq	%rcx,%rax
	movq	%rax,%rdi
	movq	%rsp,%rsi
	rep;movsb
	movq	%rax,%rsp
	call	handle_ibrs_entry
	movq	PCPU(CURPCB),%rdi
	orl	$PCB_FULL_IRET,PCB_FLAGS(%rdi)
	testb	$CPUID_STDEXT_FSGSBASE,cpu_stdext_feature(%rip)
	jz	3f
	cmpw	$KUF32SEL,TF_FS(%rsp)
	jne	2f
	rdfsbase %rax
	movq	%rax,PCB_FSBASE(%rdi)
2:	cmpw	$KUG32SEL,TF_GS(%rsp)
	jne	3f
	movl	$MSR_KGSBASE,%ecx
	rdmsr
	shlq	$32,%rdx
	orq	%rdx,%rax
	movq	%rax,PCB_GSBASE(%rdi)
3:	jmp	calltrap

/*
 * NMI handling is special.
 *
 * First, NMIs do not respect the state of the processor's RFLAGS.IF
 * bit.  The NMI handler may be entered at any time, including when
 * the processor is in a critical section with RFLAGS.IF == 0.
 * The processor's GS.base value could be invalid on entry to the
 * handler.
 *
 * Second, the processor treats NMIs specially, blocking further NMIs
 * until an 'iretq' instruction is executed.  We thus need to execute
 * the NMI handler with interrupts disabled, to prevent a nested interrupt
 * from executing an 'iretq' instruction and inadvertently taking the
 * processor out of NMI mode.
 *
 * Third, the NMI handler runs on its own stack (tss_ist2). The canonical
 * GS.base value for the processor is stored just above the bottom of its
 * NMI stack.  For NMIs taken from kernel mode, the current value in
 * the processor's GS.base is saved at entry to C-preserved register %r12,
 * the canonical value for GS.base is then loaded into the processor, and
 * the saved value is restored at exit time.  For NMIs taken from user mode,
 * the cheaper 'SWAPGS' instructions are used for swapping GS.base.
 */

IDTVEC(nmi)
	subq	$TF_RIP,%rsp
	movl	$(T_NMI),TF_TRAPNO(%rsp)
	movq	$0,TF_ADDR(%rsp)
	movq	$0,TF_ERR(%rsp)
	movq	%rdi,TF_RDI(%rsp)
	movq	%rsi,TF_RSI(%rsp)
	movq	%rdx,TF_RDX(%rsp)
	movq	%rcx,TF_RCX(%rsp)
	movq	%r8,TF_R8(%rsp)
	movq	%r9,TF_R9(%rsp)
	movq	%rax,TF_RAX(%rsp)
	movq	%rbx,TF_RBX(%rsp)
	movq	%rbp,TF_RBP(%rsp)
	movq	%r10,TF_R10(%rsp)
	movq	%r11,TF_R11(%rsp)
	movq	%r12,TF_R12(%rsp)
	movq	%r13,TF_R13(%rsp)
	movq	%r14,TF_R14(%rsp)
	movq	%r15,TF_R15(%rsp)
	SAVE_SEGS
	movl	$TF_HASSEGS,TF_FLAGS(%rsp)
	pushfq
	andq	$~(PSL_D | PSL_AC),(%rsp)
	popfq
	xorl	%ebx,%ebx
	testb	$SEL_RPL_MASK,TF_CS(%rsp)
	jnz	nmi_fromuserspace
	/*
	 * We've interrupted the kernel.  Preserve GS.base in %r12,
	 * %cr3 in %r13, and possibly lower half of MSR_IA32_SPEC_CTL in %r14d.
	 */
	movl	$MSR_GSBASE,%ecx
	rdmsr
	movq	%rax,%r12
	shlq	$32,%rdx
	orq	%rdx,%r12
	/* Retrieve and load the canonical value for GS.base. */
	movq	TF_SIZE(%rsp),%rdx
	movl	%edx,%eax
	shrq	$32,%rdx
	wrmsr
	movq	%cr3,%r13
	movq	PCPU(KCR3),%rax
	cmpq	$~0,%rax
	je	1f
	movq	%rax,%cr3
1:	testl	$CPUID_STDEXT3_IBPB,cpu_stdext_feature3(%rip)
	je	nmi_calltrap
	movl	$MSR_IA32_SPEC_CTRL,%ecx
	rdmsr
	movl	%eax,%r14d
	call	handle_ibrs_entry
	jmp	nmi_calltrap
nmi_fromuserspace:
	incl	%ebx
	swapgs
	movq	%cr3,%r13
	movq	PCPU(KCR3),%rax
	cmpq	$~0,%rax
	je	1f
	movq	%rax,%cr3
1:	call	handle_ibrs_entry
	movq	PCPU(CURPCB),%rdi
	testq	%rdi,%rdi
	jz	3f
	orl	$PCB_FULL_IRET,PCB_FLAGS(%rdi)
	testb	$CPUID_STDEXT_FSGSBASE,cpu_stdext_feature(%rip)
	jz	3f
	cmpw	$KUF32SEL,TF_FS(%rsp)
	jne	2f
	rdfsbase %rax
	movq	%rax,PCB_FSBASE(%rdi)
2:	cmpw	$KUG32SEL,TF_GS(%rsp)
	jne	3f
	movl	$MSR_KGSBASE,%ecx
	rdmsr
	shlq	$32,%rdx
	orq	%rdx,%rax
	movq	%rax,PCB_GSBASE(%rdi)
3:
/* Note: this label is also used by ddb and gdb: */
nmi_calltrap:
	FAKE_MCOUNT(TF_RIP(%rsp))
	movq	%rsp,%rdi
	call	trap
	MEXITCOUNT
#ifdef HWPMC_HOOKS
	/*
	 * Capture a userspace callchain if needed.
	 *
	 * - Check if the current trap was from user mode.
	 * - Check if the current thread is valid.
	 * - Check if the thread requires a user call chain to be
	 *   captured.
	 *
	 * We are still in NMI mode at this point.
	 */
	testl	%ebx,%ebx
	jz	nocallchain	/* not from userspace */
	movq	PCPU(CURTHREAD),%rax
	orq	%rax,%rax	/* curthread present? */
	jz	nocallchain
	/*
	 * Move execution to the regular kernel stack, because we
	 * committed to return through doreti.
	 */
	movq	%rsp,%rsi	/* source stack pointer */
	movq	$TF_SIZE,%rcx
	movq	PCPU(RSP0),%rdx
	subq	%rcx,%rdx
	movq	%rdx,%rdi	/* destination stack pointer */
	shrq	$3,%rcx		/* trap frame size in long words */
	pushfq
	andq	$~(PSL_D | PSL_AC),(%rsp)
	popfq
	rep
	movsq			/* copy trapframe */
	movq	%rdx,%rsp	/* we are on the regular kstack */

	testl	$TDP_CALLCHAIN,TD_PFLAGS(%rax) /* flagged for capture? */
	jz	nocallchain
	/*
	 * A user callchain is to be captured, so:
	 * - Take the processor out of "NMI" mode by faking an "iret",
	 *   to allow for nested NMI interrupts.
	 * - Enable interrupts, so that copyin() can work.
	 */
	movl	%ss,%eax
	pushq	%rax		/* tf_ss */
	pushq	%rdx		/* tf_rsp (on kernel stack) */
	pushfq			/* tf_rflags */
	movl	%cs,%eax
	pushq	%rax		/* tf_cs */
	pushq	$outofnmi	/* tf_rip */
	iretq
outofnmi:
	/*
	 * At this point the processor has exited NMI mode and is running
	 * with interrupts turned off on the normal kernel stack.
	 *
	 * If a pending NMI gets recognized at or after this point, it
	 * will cause a kernel callchain to be traced.
	 *
	 * We turn interrupts back on, and call the user callchain capture hook.
	 */
	movq	pmc_hook,%rax
	orq	%rax,%rax
	jz	nocallchain
	movq	PCPU(CURTHREAD),%rdi		/* thread */
	movq	$PMC_FN_USER_CALLCHAIN,%rsi	/* command */
	movq	%rsp,%rdx			/* frame */
	sti
	call	*%rax
	cli
nocallchain:
#endif
	testl	%ebx,%ebx	/* %ebx == 0 => return to userland */
	jnz	doreti_exit
	/*
	 * Restore speculation control MSR, if preserved.
	 */
	testl	$CPUID_STDEXT3_IBPB,cpu_stdext_feature3(%rip)
	je	1f
	movl	%r14d,%eax
	xorl	%edx,%edx
	movl	$MSR_IA32_SPEC_CTRL,%ecx
	wrmsr
	/*
	 * Put back the preserved MSR_GSBASE value.
	 */
1:	movl	$MSR_GSBASE,%ecx
	movq	%r12,%rdx
	movl	%edx,%eax
	shrq	$32,%rdx
	wrmsr
	cmpb	$0, nmi_flush_l1d_sw(%rip)
	je	2f
	call	flush_l1d_sw		/* bhyve L1TF assist */
2:	movq	%r13,%cr3
	RESTORE_REGS
	addq	$TF_RIP,%rsp
	jmp	doreti_iret

/*
 * MC# handling is similar to NMI.
 *
 * As with NMIs, machine check exceptions do not respect RFLAGS.IF and
 * can occur at any time with a GS.base value that does not correspond
 * to the privilege level in CS.
 *
 * Machine checks are not unblocked by iretq, but it is best to run
 * the handler with interrupts disabled since the exception may have
 * interrupted a critical section.
 *
 * The MC# handler runs on its own stack (tss_ist3).  The canonical
 * GS.base value for the processor is stored just above the bottom of
 * its MC# stack.  For exceptions taken from kernel mode, the current
 * value in the processor's GS.base is saved at entry to C-preserved
 * register %r12, the canonical value for GS.base is then loaded into
 * the processor, and the saved value is restored at exit time.  For
 * exceptions taken from user mode, the cheaper 'SWAPGS' instructions
 * are used for swapping GS.base.
 */

IDTVEC(mchk)
	subq	$TF_RIP,%rsp
	movl	$(T_MCHK),TF_TRAPNO(%rsp)
	movq	$0,TF_ADDR(%rsp)
	movq	$0,TF_ERR(%rsp)
	movq	%rdi,TF_RDI(%rsp)
	movq	%rsi,TF_RSI(%rsp)
	movq	%rdx,TF_RDX(%rsp)
	movq	%rcx,TF_RCX(%rsp)
	movq	%r8,TF_R8(%rsp)
	movq	%r9,TF_R9(%rsp)
	movq	%rax,TF_RAX(%rsp)
	movq	%rbx,TF_RBX(%rsp)
	movq	%rbp,TF_RBP(%rsp)
	movq	%r10,TF_R10(%rsp)
	movq	%r11,TF_R11(%rsp)
	movq	%r12,TF_R12(%rsp)
	movq	%r13,TF_R13(%rsp)
	movq	%r14,TF_R14(%rsp)
	movq	%r15,TF_R15(%rsp)
	SAVE_SEGS
	movl	$TF_HASSEGS,TF_FLAGS(%rsp)
	pushfq
	andq	$~(PSL_D | PSL_AC),(%rsp)
	popfq
	xorl	%ebx,%ebx
	testb	$SEL_RPL_MASK,TF_CS(%rsp)
	jnz	mchk_fromuserspace
	/*
	 * We've interrupted the kernel.  Preserve GS.base in %r12,
	 * %cr3 in %r13, and possibly lower half of MSR_IA32_SPEC_CTL in %r14d.
	 */
	movl	$MSR_GSBASE,%ecx
	rdmsr
	movq	%rax,%r12
	shlq	$32,%rdx
	orq	%rdx,%r12
	/* Retrieve and load the canonical value for GS.base. */
	movq	TF_SIZE(%rsp),%rdx
	movl	%edx,%eax
	shrq	$32,%rdx
	wrmsr
	movq	%cr3,%r13
	movq	PCPU(KCR3),%rax
	cmpq	$~0,%rax
	je	1f
	movq	%rax,%cr3
1:	testl	$CPUID_STDEXT3_IBPB,cpu_stdext_feature3(%rip)
	je	mchk_calltrap
	movl	$MSR_IA32_SPEC_CTRL,%ecx
	rdmsr
	movl	%eax,%r14d
	call	handle_ibrs_entry
	jmp	mchk_calltrap
mchk_fromuserspace:
	incl	%ebx
	swapgs
	movq	%cr3,%r13
	movq	PCPU(KCR3),%rax
	cmpq	$~0,%rax
	je	1f
	movq	%rax,%cr3
1:	call	handle_ibrs_entry
/* Note: this label is also used by ddb and gdb: */
mchk_calltrap:
	FAKE_MCOUNT(TF_RIP(%rsp))
	movq	%rsp,%rdi
	call	mca_intr
	MEXITCOUNT
	testl	%ebx,%ebx	/* %ebx == 0 => return to userland */
	jnz	doreti_exit
	/*
	 * Restore speculation control MSR, if preserved.
	 */
	testl	$CPUID_STDEXT3_IBPB,cpu_stdext_feature3(%rip)
	je	1f
	movl	%r14d,%eax
	xorl	%edx,%edx
	movl	$MSR_IA32_SPEC_CTRL,%ecx
	wrmsr
	/*
	 * Put back the preserved MSR_GSBASE value.
	 */
1:	movl	$MSR_GSBASE,%ecx
	movq	%r12,%rdx
	movl	%edx,%eax
	shrq	$32,%rdx
	wrmsr
	movq	%r13,%cr3
	RESTORE_REGS
	addq	$TF_RIP,%rsp
	jmp	doreti_iret

ENTRY(fork_trampoline)
	movq	%r12,%rdi		/* function */
	movq	%rbx,%rsi		/* arg1 */
	movq	%rsp,%rdx		/* trapframe pointer */
	call	fork_exit
	MEXITCOUNT
	jmp	doreti			/* Handle any ASTs */

/*
 * To efficiently implement classification of trap and interrupt handlers
 * for profiling, there must be only trap handlers between the labels btrap
 * and bintr, and only interrupt handlers between the labels bintr and
 * eintr.  This is implemented (partly) by including files that contain
 * some of the handlers.  Before including the files, set up a normal asm
 * environment so that the included files doen't need to know that they are
 * included.
 */

#ifdef COMPAT_FREEBSD32
	.data
	.p2align 4
	.text
	SUPERALIGN_TEXT

#include <amd64/ia32/ia32_exception.S>
#endif

	.data
	.p2align 4
	.text
	SUPERALIGN_TEXT
MCOUNT_LABEL(bintr)

#include <amd64/amd64/apic_vector.S>

#ifdef DEV_ATPIC
	.data
	.p2align 4
	.text
	SUPERALIGN_TEXT

#include <amd64/amd64/atpic_vector.S>
#endif

	.text
MCOUNT_LABEL(eintr)

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
	/*
	 * Check if ASTs can be handled now.
	 */
	testb	$SEL_RPL_MASK,TF_CS(%rsp) /* are we returning to user mode? */
	jz	doreti_exit		/* can't handle ASTs now if not */

doreti_ast:
	/*
	 * Check for ASTs atomically with returning.  Disabling CPU
	 * interrupts provides sufficient locking even in the SMP case,
	 * since we will be informed of any new ASTs by an IPI.
	 */
	cli
	movq	PCPU(CURTHREAD),%rax
	testl	$TDF_ASTPENDING | TDF_NEEDRESCHED,TD_FLAGS(%rax)
	je	doreti_exit
	sti
	movq	%rsp,%rdi	/* pass a pointer to the trapframe */
	call	ast
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
	movq	PCPU(CURPCB),%r8

	/*
	 * Do not reload segment registers for kernel.
	 * Since we do not reload segments registers with sane
	 * values on kernel entry, descriptors referenced by
	 * segments registers might be not valid.  This is fatal
	 * for user mode, but is not a problem for the kernel.
	 */
	testb	$SEL_RPL_MASK,TF_CS(%rsp)
	jz	ld_regs
	testl	$PCB_FULL_IRET,PCB_FLAGS(%r8)
	jz	ld_regs
	andl	$~PCB_FULL_IRET,PCB_FLAGS(%r8)
	testl	$TF_HASSEGS,TF_FLAGS(%rsp)
	je	set_segs

do_segs:
	/* Restore %fs and fsbase */
	movw	TF_FS(%rsp),%ax
	.globl	ld_fs
ld_fs:
	movw	%ax,%fs
	cmpw	$KUF32SEL,%ax
	jne	1f
	movl	$MSR_FSBASE,%ecx
	movl	PCB_FSBASE(%r8),%eax
	movl	PCB_FSBASE+4(%r8),%edx
	.globl	ld_fsbase
ld_fsbase:
	wrmsr
1:
	/* Restore %gs and gsbase */
	movw	TF_GS(%rsp),%si
	pushfq
	cli
	movl	$MSR_GSBASE,%ecx
	/* Save current kernel %gs base into %r12d:%r13d */
	rdmsr
	movl	%eax,%r12d
	movl	%edx,%r13d
	.globl	ld_gs
ld_gs:
	movw	%si,%gs
	/* Save user %gs base into %r14d:%r15d */
	rdmsr
	movl	%eax,%r14d
	movl	%edx,%r15d
	/* Restore kernel %gs base */
	movl	%r12d,%eax
	movl	%r13d,%edx
	wrmsr
	popfq
	/*
	 * Restore user %gs base, either from PCB if used for TLS, or
	 * from the previously saved msr read.
	 */
	movl	$MSR_KGSBASE,%ecx
	cmpw	$KUG32SEL,%si
	jne	1f
	movl	PCB_GSBASE(%r8),%eax
	movl	PCB_GSBASE+4(%r8),%edx
	jmp	ld_gsbase
1:
	movl	%r14d,%eax
	movl	%r15d,%edx
	.globl	ld_gsbase
ld_gsbase:
	wrmsr	/* May trap if non-canonical, but only for TLS. */
	.globl	ld_es
ld_es:
	movw	TF_ES(%rsp),%es
	.globl	ld_ds
ld_ds:
	movw	TF_DS(%rsp),%ds
ld_regs:
	RESTORE_REGS
	testb	$SEL_RPL_MASK,TF_CS(%rsp) /* Did we come from kernel? */
	jz	2f			/* keep running with kernel GS.base */
	cli
	call	handle_ibrs_exit_rs
	cmpq	$~0,PCPU(UCR3)
	je	1f
	pushq	%rdx
	movq	PCPU(PTI_RSP0),%rdx
	subq	$PTI_SIZE,%rdx
	movq	%rax,PTI_RAX(%rdx)
	popq	%rax
	movq	%rax,PTI_RDX(%rdx)
	movq	TF_RIP(%rsp),%rax
	movq	%rax,PTI_RIP(%rdx)
	movq	TF_CS(%rsp),%rax
	movq	%rax,PTI_CS(%rdx)
	movq	TF_RFLAGS(%rsp),%rax
	movq	%rax,PTI_RFLAGS(%rdx)
	movq	TF_RSP(%rsp),%rax
	movq	%rax,PTI_RSP(%rdx)
	movq	TF_SS(%rsp),%rax
	movq	%rax,PTI_SS(%rdx)
	movq	PCPU(UCR3),%rax
	swapgs
	movq	%rdx,%rsp
	movq	%rax,%cr3
	popq	%rdx
	popq	%rax
	addq	$8,%rsp
	jmp	doreti_iret
1:	swapgs
2:	addq	$TF_RIP,%rsp
	.globl	doreti_iret
doreti_iret:
	iretq

set_segs:
	movw	$KUDSEL,%ax
	movw	%ax,TF_DS(%rsp)
	movw	%ax,TF_ES(%rsp)
	movw	$KUF32SEL,TF_FS(%rsp)
	movw	$KUG32SEL,TF_GS(%rsp)
	jmp	do_segs

	/*
	 * doreti_iret_fault.  Alternative return code for
	 * the case where we get a fault in the doreti_exit code
	 * above.  trap() (amd64/amd64/trap.c) catches this specific
	 * case, sends the process a signal and continues in the
	 * corresponding place in the code below.
	 */
	ALIGN_TEXT
	.globl	doreti_iret_fault
doreti_iret_fault:
	subq	$TF_RIP,%rsp		/* space including tf_err, tf_trapno */
	movq	%rax,TF_RAX(%rsp)
	movq	%rdx,TF_RDX(%rsp)
	movq	%rcx,TF_RCX(%rsp)
	call	handle_ibrs_entry
	testb	$SEL_RPL_MASK,TF_CS(%rsp)
	jz	1f
	sti
1:
	SAVE_SEGS
	movl	$TF_HASSEGS,TF_FLAGS(%rsp)
	movq	%rdi,TF_RDI(%rsp)
	movq	%rsi,TF_RSI(%rsp)
	movq	%r8,TF_R8(%rsp)
	movq	%r9,TF_R9(%rsp)
	movq	%rbx,TF_RBX(%rsp)
	movq	%rbp,TF_RBP(%rsp)
	movq	%r10,TF_R10(%rsp)
	movq	%r11,TF_R11(%rsp)
	movq	%r12,TF_R12(%rsp)
	movq	%r13,TF_R13(%rsp)
	movq	%r14,TF_R14(%rsp)
	movq	%r15,TF_R15(%rsp)
	movl	$T_PROTFLT,TF_TRAPNO(%rsp)
	movq	$0,TF_ERR(%rsp)	/* XXX should be the error code */
	movq	$0,TF_ADDR(%rsp)
	FAKE_MCOUNT(TF_RIP(%rsp))
	jmp	calltrap

	ALIGN_TEXT
	.globl	ds_load_fault
ds_load_fault:
	movl	$T_PROTFLT,TF_TRAPNO(%rsp)
	testb	$SEL_RPL_MASK,TF_CS(%rsp)
	jz	1f
	sti
1:
	movq	%rsp,%rdi
	call	trap
	movw	$KUDSEL,TF_DS(%rsp)
	jmp	doreti

	ALIGN_TEXT
	.globl	es_load_fault
es_load_fault:
	movl	$T_PROTFLT,TF_TRAPNO(%rsp)
	testl	$PSL_I,TF_RFLAGS(%rsp)
	jz	1f
	sti
1:
	movq	%rsp,%rdi
	call	trap
	movw	$KUDSEL,TF_ES(%rsp)
	jmp	doreti

	ALIGN_TEXT
	.globl	fs_load_fault
fs_load_fault:
	testl	$PSL_I,TF_RFLAGS(%rsp)
	jz	1f
	sti
1:
	movl	$T_PROTFLT,TF_TRAPNO(%rsp)
	movq	%rsp,%rdi
	call	trap
	movw	$KUF32SEL,TF_FS(%rsp)
	jmp	doreti

	ALIGN_TEXT
	.globl	gs_load_fault
gs_load_fault:
	popfq
	movl	$T_PROTFLT,TF_TRAPNO(%rsp)
	testl	$PSL_I,TF_RFLAGS(%rsp)
	jz	1f
	sti
1:
	movq	%rsp,%rdi
	call	trap
	movw	$KUG32SEL,TF_GS(%rsp)
	jmp	doreti

	ALIGN_TEXT
	.globl	fsbase_load_fault
fsbase_load_fault:
	movl	$T_PROTFLT,TF_TRAPNO(%rsp)
	testl	$PSL_I,TF_RFLAGS(%rsp)
	jz	1f
	sti
1:
	movq	%rsp,%rdi
	call	trap
	movq	PCPU(CURTHREAD),%r8
	movq	TD_PCB(%r8),%r8
	movq	$0,PCB_FSBASE(%r8)
	jmp	doreti

	ALIGN_TEXT
	.globl	gsbase_load_fault
gsbase_load_fault:
	movl	$T_PROTFLT,TF_TRAPNO(%rsp)
	testl	$PSL_I,TF_RFLAGS(%rsp)
	jz	1f
	sti
1:
	movq	%rsp,%rdi
	call	trap
	movq	PCPU(CURTHREAD),%r8
	movq	TD_PCB(%r8),%r8
	movq	$0,PCB_GSBASE(%r8)
	jmp	doreti

#ifdef HWPMC_HOOKS
	ENTRY(end_exceptions)
#endif
