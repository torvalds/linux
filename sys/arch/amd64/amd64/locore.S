/*	$OpenBSD: locore.S,v 1.151 2025/08/02 07:33:28 sf Exp $	*/
/*	$NetBSD: locore.S,v 1.13 2004/03/25 18:33:17 drochner Exp $	*/

/*
 * Copyright-o-rama!
 */

/*
 * Copyright (c) 2001 Wasabi Systems, Inc.
 * All rights reserved.
 *
 * Written by Frank van der Linden for Wasabi Systems, Inc.
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
 *      This product includes software developed for the NetBSD Project by
 *      Wasabi Systems, Inc.
 * 4. The name of Wasabi Systems, Inc. may not be used to endorse
 *    or promote products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY WASABI SYSTEMS, INC. ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL WASABI SYSTEMS, INC
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */


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
 * William Jolitz.
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
 *	@(#)locore.s	7.3 (Berkeley) 5/13/91
 */

#include "assym.h"
#include "efi.h"
#include "lapic.h"
#include "ksyms.h"
#include "xen.h"
#include "hyperv.h"

#include <sys/syscall.h>

#include <machine/param.h>
#include <machine/codepatch.h>
#include <machine/psl.h>
#include <machine/segments.h>
#include <machine/specialreg.h>
#include <machine/trap.h>			/* T_PROTFLT */
#include <machine/frameasm.h>

#if NLAPIC > 0
#include <machine/i82489reg.h>
#endif

/*
 * override user-land alignment before including asm.h
 */
#define	ALIGN_DATA	.align	8,0xcc

#include <machine/asm.h>

#define SET_CURPROC(proc,cpu)			\
	movq	CPUVAR(SELF),cpu	;	\
	movq	proc,CPUVAR(CURPROC)      ;	\
	movq	cpu,P_CPU(proc)

#define GET_CURPCB(reg)			movq	CPUVAR(CURPCB),reg      
#define SET_CURPCB(reg)			movq	reg,CPUVAR(CURPCB)


/*
 * Initialization
 */
	.data

#if NLAPIC > 0 
	.align	NBPG, 0xcc
	.globl	local_apic
local_apic:
	.space	NBPG
#endif

/*****************************************************************************/

/*
 * Signal trampoline; copied to a page mapped into userspace.
 * gdb's backtrace logic matches against the instructions in this.
 */
	.section .rodata
	.globl	sigcode
sigcode:
	endbr64
	call	1f
	movq	%rsp,%rdi
	pushq	%rdi			/* fake return address */
	movq	$SYS_sigreturn,%rax
	.globl sigcodecall
sigcodecall:
	syscall
	.globl	sigcoderet
sigcoderet:
	int3
1:	CODEPATCH_START
	JMP_RETPOLINE(rax)
	CODEPATCH_END(CPTAG_RETPOLINE_RAX)
	.globl	esigcode
esigcode:
	.globl	sigfill
sigfill:
	int3
esigfill:
	.globl	sigfillsiz
sigfillsiz:
	.long	esigfill - sigfill

	.text
/*
 * void lgdt(struct region_descriptor *rdp);
 * Change the global descriptor table.
 */
NENTRY(lgdt)
	RETGUARD_SETUP(lgdt, r11)
	/* Reload the descriptor table. */
	movq	%rdi,%rax
	lgdt	(%rax)
	/* Flush the prefetch q. */
	jmp	1f
	nop
1:	/* Reload "stale" selectors. */
	movl	$GSEL(GDATA_SEL, SEL_KPL),%eax
	movl	%eax,%ds
	movl	%eax,%es
	movl	%eax,%ss
	/* Reload code selector by doing intersegment return. */
	popq	%rax
	pushq	$GSEL(GCODE_SEL, SEL_KPL)
	pushq	%rax
	RETGUARD_CHECK(lgdt, r11)
	lretq
END(lgdt)

#if defined(DDB) || NEFI > 0
ENTRY(setjmp)
	RETGUARD_SETUP(setjmp, r11)
	/*
	 * Only save registers that must be preserved across function
	 * calls according to the ABI (%rbx, %rsp, %rbp, %r12-%r15)
	 * and %rip.
	 */
	movq	%rdi,%rax
	movq	%rbx,(%rax)
	movq	%rsp,8(%rax)
	movq	%rbp,16(%rax)
	movq	%r12,24(%rax)
	movq	%r13,32(%rax)
	movq	%r14,40(%rax)
	movq	%r15,48(%rax)
	movq	(%rsp),%rdx
	movq	%rdx,56(%rax)
	xorl	%eax,%eax
	RETGUARD_CHECK(setjmp, r11)
	ret
	lfence
END(setjmp)

ENTRY(longjmp)
	movq	%rdi,%rax
	movq	8(%rax),%rsp
	movq	56(%rax),%rdx
	movq	%rdx,(%rsp)
	RETGUARD_SETUP(longjmp, r11)
	movq	(%rax),%rbx
	movq	16(%rax),%rbp
	movq	24(%rax),%r12
	movq	32(%rax),%r13
	movq	40(%rax),%r14
	movq	48(%rax),%r15
	xorl	%eax,%eax
	incl	%eax
	RETGUARD_CHECK(longjmp, r11)
	ret
	lfence
END(longjmp)
#endif /* DDB || NEFI > 0 */

/*****************************************************************************/

/*
 * int cpu_switchto(struct proc *old, struct proc *new)
 * Switch from "old" proc to "new".
 */
ENTRY(cpu_switchto)
	pushq	%rbx
	pushq	%rbp
	pushq	%r12
	pushq	%r13
	pushq	%r14
	pushq	%r15

	movq	%rdi, %r13
	movq	%rsi, %r12

	/* Record new proc. */
	movb	$SONPROC,P_STAT(%r12)	# p->p_stat = SONPROC
	SET_CURPROC(%r12,%rcx)

	movl	CPUVAR(CPUID),%r9d

	/* for the FPU/"extended CPU state" handling below */
	movq	xsave_mask(%rip),%rdx
	movl	%edx,%eax
	shrq	$32,%rdx

	/* If old proc exited, don't bother. */
	xorl	%ecx,%ecx
	testq	%r13,%r13
	jz	switch_exited

	/*
	 * Save old context.
	 *
	 * Registers:
	 *   %rax - scratch
	 *   %r13 - old proc, then old pcb
	 *   %rcx - old pmap if not P_SYSTEM
	 *   %r12 - new proc
	 *   %r9d - cpuid
	 */

	/* remember the pmap if not P_SYSTEM */
	testl	$P_SYSTEM,P_FLAG(%r13)
	movq	P_ADDR(%r13),%r13
	jnz	0f
	movq	PCB_PMAP(%r13),%rcx
0:

	/* Save stack pointers. */
	movq	%rsp,PCB_RSP(%r13)
	movq	%rbp,PCB_RBP(%r13)

	/*
	 * If the old proc ran in userspace then save the
	 * floating-point/"extended state" registers
	 */
	testl	$CPUPF_USERXSTATE,CPUVAR(PFLAGS)
	jz	.Lxstate_reset

	movq	%r13, %rdi
#if PCB_SAVEFPU != 0
	addq	$PCB_SAVEFPU,%rdi
#endif
	CODEPATCH_START
	fxsave64	(%rdi)
	CODEPATCH_END(CPTAG_XSAVE)

switch_exited:
	/* now clear the xstate */
	movq	proc0paddr(%rip),%rdi
#if PCB_SAVEFPU != 0
	addq	$PCB_SAVEFPU,%rdi
#endif
	CODEPATCH_START
	fxrstor64	(%rdi)
	CODEPATCH_END(CPTAG_XRSTORS)
	andl	$~CPUPF_USERXSTATE,CPUVAR(PFLAGS)

.Lxstate_reset:
	/*
	 * If the segment registers haven't been reset since the old proc
	 * ran in userspace then reset them now
	 */
	testl	$CPUPF_USERSEGS,CPUVAR(PFLAGS)
	jz	restore_saved
	andl	$~CPUPF_USERSEGS,CPUVAR(PFLAGS)

	/* set %ds, %es, %fs, and %gs to expected value to prevent info leak */
	movw	$(GSEL(GUDATA_SEL, SEL_UPL)),%ax
	movw	%ax,%ds
	movw	%ax,%es
	movw	%ax,%fs
	cli			/* block interrupts when on user GS.base */
	swapgs			/* switch from kernel to user GS.base */
	movw	%ax,%gs		/* set %gs to UDATA and GS.base to 0 */
	swapgs			/* back to kernel GS.base */

restore_saved:
	/*
	 * Restore saved context.
	 *
	 * Registers:
	 *   %rax, %rdx - scratch
	 *   %rcx - old pmap if not P_SYSTEM
	 *   %r12 - new process
	 *   %r13 - new pcb
	 *   %rbx - new pmap if not P_SYSTEM
	 */

	movq	P_ADDR(%r12),%r13

	/* remember the pmap if not P_SYSTEM */
	xorl	%ebx,%ebx
	testl	$P_SYSTEM,P_FLAG(%r12)
	jnz	1f
	movq	PCB_PMAP(%r13),%rbx
1:

	/* No interrupts while loading new state. */
	cli

	/* Restore stack pointers. */
	movq	PCB_RSP(%r13),%rsp
	movq	PCB_RBP(%r13),%rbp

	/* Stack pivot done, setup RETGUARD */
	RETGUARD_SETUP_OFF(cpu_switchto, r11, 6*8)

	/* don't switch cr3 to the same thing it already was */
	movq	PCB_CR3(%r13),%rax
	movq	%cr3,%rdi
	xorq	%rax,%rdi
	btrq	$63,%rdi	/* ignore CR3_REUSE_PCID */
	testq	%rdi,%rdi
	jz	.Lsame_cr3

#ifdef DIAGNOSTIC
	/* verify ci_proc_pmap had been updated properly */
	cmpq	%rcx,CPUVAR(PROC_PMAP)
	jnz	.Lbogus_proc_pmap
#endif
	/* record which pmap this CPU should get IPIs for */
	movq	%rbx,CPUVAR(PROC_PMAP)

.Lset_cr3:
	movq	%rax,%cr3			/* %rax used below too */

.Lsame_cr3:
	/*
	 * If we switched from a userland thread with a shallow call stack
	 * (e.g interrupt->ast->mi_ast->prempt->mi_switch->cpu_switchto)
	 * then the RSB may have attacker controlled entries when we switch
	 * to a deeper call stack in the new thread.  Refill the RSB with
	 * entries safe to speculate into/through.
	 */
	RET_STACK_REFILL_WITH_RCX

	/* Don't bother with the rest if switching to a system process. */
	testq	%rbx,%rbx
	jz	switch_restored

	/* record the bits needed for future U-->K transition */
	movq	PCB_KSTACK(%r13),%rdx
	subq	$FRAMESIZE,%rdx
	movq	%rdx,CPUVAR(KERN_RSP)

	CODEPATCH_START
	/*
	 * Meltdown: iff we're doing separate U+K and U-K page tables,
	 * then record them in cpu_info for easy access in syscall and
	 * interrupt trampolines.
	 */
	movq	PM_PDIRPA_INTEL(%rbx),%rdx
	orq	cr3_reuse_pcid,%rax
	orq	cr3_pcid_proc_intel,%rdx
	movq	%rax,CPUVAR(KERN_CR3)
	movq	%rdx,CPUVAR(USER_CR3)
	CODEPATCH_END(CPTAG_MELTDOWN_NOP)

switch_restored:
	SET_CURPCB(%r13)

	/* Interrupts are okay again. */
	sti
	popq	%r15
	popq	%r14
	popq	%r13
	popq	%r12
	popq	%rbp
	popq	%rbx
	RETGUARD_CHECK(cpu_switchto, r11)
	ret
	lfence

#ifdef DIAGNOSTIC
.Lbogus_proc_pmap:
	leaq	bogus_proc_pmap,%rdi
	call	panic
	int3	/* NOTREACHED */
	.pushsection .rodata
bogus_proc_pmap:
	.asciz	"curcpu->ci_proc_pmap didn't point to previous pmap"
	.popsection
#endif /* DIAGNOSTIC */
END(cpu_switchto)

NENTRY(retpoline_rax)
	CODEPATCH_START
	JMP_RETPOLINE(rax)
	CODEPATCH_END(CPTAG_RETPOLINE_RAX)
END(retpoline_rax)

NENTRY(__x86_indirect_thunk_r11)
	CODEPATCH_START
	JMP_RETPOLINE(r11)
	CODEPATCH_END(CPTAG_RETPOLINE_R11)
END(__x86_indirect_thunk_r11)

ENTRY(cpu_idle_cycle_hlt)
	RETGUARD_SETUP(cpu_idle_cycle_hlt, r11)
	sti
	hlt
	RETGUARD_CHECK(cpu_idle_cycle_hlt, r11)
	ret
	lfence
END(cpu_idle_cycle_hlt)

/*
 * savectx(struct pcb *pcb);
 * Update pcb, saving current processor state.
 */
ENTRY(savectx)
	RETGUARD_SETUP(savectx, r11)
	/* Save stack pointers. */
	movq	%rsp,PCB_RSP(%rdi)
	movq	%rbp,PCB_RBP(%rdi)
	RETGUARD_CHECK(savectx, r11)
	ret
	lfence
END(savectx)

/*
 * syscall insn entry.
 * Enter here with interrupts blocked; %rcx contains the caller's
 * %rip and the original rflags has been copied to %r11.  %cs and
 * %ss have been updated to the kernel segments, but %rsp is still
 * the user-space value.
 * First order of business is to swap to the kernel GS.base so that
 * we can access our struct cpu_info.  After possibly mucking with
 * pagetables, we switch to our kernel stack.  Once that's in place
 * we can save the rest of the syscall frame and unblock interrupts.
 */
KUTEXT_PAGE_START
 	.align	NBPG, 0xcc
XUsyscall_meltdown:
	/*
	 * This is the real Xsyscall_meltdown page, which is mapped into
	 * the U-K page tables at the same location as Xsyscall_meltdown
	 * below.  For this, the Meltdown case, we use the scratch space
	 * in cpu_info so we can switch to the kernel page tables
	 * (thank you, Intel), at which point we'll continue at the
	 * "SYSCALL_ENTRY" after Xsyscall below.
	 * In case the CPU speculates past the mov to cr3, we put a
	 * retpoline-style pause-lfence-jmp-to-pause loop.
	 */
	endbr64
	swapgs
	movq	%rax,CPUVAR(SCRATCH)
	movq	CPUVAR(KERN_CR3),%rax
	movq	%rax,%cr3
0:	pause
	lfence
	jmp	0b
KUTEXT_PAGE_END

KTEXT_PAGE_START
	.align	NBPG, 0xcc
GENTRY(Xsyscall_meltdown)
	/* pad to match real Xsyscall_meltdown positioning above */
	movq	CPUVAR(KERN_CR3),%rax
	movq	%rax,%cr3
GENTRY(Xsyscall)
	endbr64
	swapgs
	movq	%rax,CPUVAR(SCRATCH)
	SYSCALL_ENTRY			/* create trapframe */
	sti

	movq	CPUVAR(CURPROC),%r14
	movq	%rsp,P_MD_REGS(%r14)	# save pointer to frame
	andl	$~MDP_IRET,P_MD_FLAGS(%r14)
	movq	%rsp,%rdi
	call	syscall

.Lsyscall_check_asts:
	/* Check for ASTs on exit to user mode. */
	cli
	CHECK_ASTPENDING(%r11)
	je	2f
	CLEAR_ASTPENDING(%r11)
	sti
	movq	%rsp,%rdi
	call	ast
	jmp	.Lsyscall_check_asts

2:
#ifdef DIAGNOSTIC
	cmpl	$IPL_NONE,CPUVAR(ILEVEL)
	jne	.Lsyscall_spl_not_lowered
#endif /* DIAGNOSTIC */

	/* Could registers have been changed that require an iretq? */
	testl	$MDP_IRET, P_MD_FLAGS(%r14)
	jne	intr_user_exit_post_ast

	/* Restore FPU/"extended CPU state" if it's not already in the CPU */
	testl	$CPUPF_USERXSTATE,CPUVAR(PFLAGS)
	jz	.Lsyscall_restore_xstate

	/* Restore FS.base if it's not already in the CPU */
	testl	$CPUPF_USERSEGS,CPUVAR(PFLAGS)
	jz	.Lsyscall_restore_fsbase

.Lsyscall_restore_registers:
	/*
	 * If the pmap we're now on isn't the same as the one we
	 * were on last time we were in userspace, then use IBPB
	 * to prevent cross-process branch-target injection.
	 */
	CODEPATCH_START
	movq	CPUVAR(PROC_PMAP),%rbx
	cmpq	CPUVAR(USER_PMAP),%rbx
	je	1f
	xorl	%edx,%edx
	movl	$PRED_CMD_IBPB,%eax
	movl	$MSR_PRED_CMD,%ecx
	wrmsr
	movq	%rbx,CPUVAR(USER_PMAP)
1:
	CODEPATCH_END(CPTAG_IBPB_NOP)
	call	pku_xonly
	RET_STACK_REFILL_WITH_RCX

	movq	TF_R8(%rsp),%r8
	movq	TF_R9(%rsp),%r9
	movq	TF_R10(%rsp),%r10
	movq	TF_R12(%rsp),%r12
	movq	TF_R13(%rsp),%r13
	movq	TF_R14(%rsp),%r14
	movq	TF_R15(%rsp),%r15
	movq	TF_RBX(%rsp),%rbx
	movq	TF_RDX(%rsp),%rdx

	CODEPATCH_START
	xorl	%edi,%edi
	xorl	%esi,%esi
	xorl	%r11d,%r11d
	xorl	%eax,%eax
	xorl	%ecx,%ecx
	movw	%ds,TF_R8(%rsp)
	verw	TF_R8(%rsp)
	CODEPATCH_END(CPTAG_MDS)

	movq	TF_RDI(%rsp),%rdi
	movq	TF_RSI(%rsp),%rsi
	movq	TF_RBP(%rsp),%rbp

	/*
	 * We need to finish reading from the trapframe, then switch
	 * to the user page tables, swapgs, and return.  We need
	 * to get the final value for the register that was used
	 * for the mov to %cr3 from somewhere accessible on the
	 * user page tables, so save it in CPUVAR(SCRATCH) across
	 * the switch.
	 */
	movq	TF_RAX(%rsp),%rax
	movq	TF_RIP(%rsp),%rcx
	movq	TF_RFLAGS(%rsp),%r11
	movq	TF_RSP(%rsp),%rsp
	CODEPATCH_START
	movq	%rax,CPUVAR(SCRATCH)
	movq	CPUVAR(USER_CR3),%rax
	PCID_SET_REUSE_NOP
	movq	%rax,%cr3
Xsyscall_trampback:
0:	pause
	lfence
	jmp	0b
	CODEPATCH_END(CPTAG_MELTDOWN_NOP)
	swapgs
	sysretq
END(Xsyscall)
END(Xsyscall_meltdown)
KTEXT_PAGE_END

KUTEXT_PAGE_START
	.space	(Xsyscall_trampback - Xsyscall_meltdown) - \
		(. - XUsyscall_meltdown), 0xcc
	movq	%rax,%cr3
	movq	CPUVAR(SCRATCH),%rax
	swapgs
	sysretq
KUTEXT_PAGE_END

	.text
	_ALIGN_TRAPS
	/* in this case, need FS.base but not xstate, rarely happens */
.Lsyscall_restore_fsbase:	/* CPU doesn't have curproc's FS.base */
	orl	$CPUPF_USERSEGS,CPUVAR(PFLAGS)
	movq	CPUVAR(CURPCB),%rdi
	jmp	.Lsyscall_restore_fsbase_real

	_ALIGN_TRAPS
.Lsyscall_restore_xstate:	/* CPU doesn't have curproc's xstate */
	orl	$(CPUPF_USERXSTATE|CPUPF_USERSEGS),CPUVAR(PFLAGS)
	movq	CPUVAR(CURPCB),%rdi
	movq	xsave_mask(%rip),%rdx
	movl	%edx,%eax
	shrq	$32,%rdx
#if PCB_SAVEFPU != 0
	addq	$PCB_SAVEFPU,%rdi
#endif
	/* untouched state so can't fault */
	CODEPATCH_START
	fxrstor64	(%rdi)
	CODEPATCH_END(CPTAG_XRSTORS)
#if PCB_SAVEFPU != 0
	subq	$PCB_SAVEFPU,%rdi
#endif
.Lsyscall_restore_fsbase_real:
	movq	PCB_FSBASE(%rdi),%rdx
	movl	%edx,%eax
	shrq	$32,%rdx
	movl	$MSR_FSBASE,%ecx
	wrmsr
	jmp	.Lsyscall_restore_registers

#ifdef DIAGNOSTIC
.Lsyscall_spl_not_lowered:
	leaq	spl_lowered(%rip), %rdi
	movl	TF_ERR(%rsp),%esi	/* syscall # stashed above */
	movl	TF_RDI(%rsp),%edx
	movl	%ebx,%ecx
	movl	CPUVAR(ILEVEL),%r8d
	xorq	%rax,%rax
	call	printf
#ifdef DDB
	int	$3
#endif /* DDB */
	movl	$IPL_NONE,CPUVAR(ILEVEL)
	jmp	.Lsyscall_check_asts

	.section .rodata
spl_lowered:
	.asciz	"WARNING: SPL NOT LOWERED ON SYSCALL %d %d EXIT %x %x\n"
	.text
#endif

NENTRY(proc_trampoline)
	call	proc_trampoline_mi
	movq	%r13,%rdi
	movq	%r12,%rax
	call	retpoline_rax
	movq	CPUVAR(CURPROC),%r14
	jmp	.Lsyscall_check_asts
END(proc_trampoline)


/*
 * Returning to userspace via iretq.  We do things in this order:
 *  - check for ASTs
 *  - restore FPU/"extended CPU state" if it's not already in the CPU
 *  - DIAGNOSTIC: no more C calls after this, so check the SPL
 *  - restore FS.base if it's not already in the CPU
 *  - restore most registers
 *  - update the iret frame from the trapframe
 *  - finish reading from the trapframe
 *  - switch to the trampoline stack	\
 *  - jump to the .kutext segment	|-- Meltdown workaround
 *  - switch to the user page tables	/
 *  - swapgs
 *  - iretq
 */
KTEXT_PAGE_START
        _ALIGN_TRAPS
GENTRY(intr_user_exit)
#ifdef DIAGNOSTIC
	pushfq
	popq	%rdx
	testq	$PSL_I,%rdx
	jnz	.Lintr_user_exit_not_blocked
#endif /* DIAGNOSTIC */

	/* Check for ASTs */
	CHECK_ASTPENDING(%r11)
	je	intr_user_exit_post_ast
	CLEAR_ASTPENDING(%r11)
	sti
	movq	%rsp,%rdi
	call	ast
	cli
	jmp	intr_user_exit

intr_user_exit_post_ast:
	/* Restore FPU/"extended CPU state" if it's not already in the CPU */
	testl	$CPUPF_USERXSTATE,CPUVAR(PFLAGS)
	jz	.Lintr_restore_xstate

	/* Restore FS.base if it's not already in the CPU */
	testl	$CPUPF_USERSEGS,CPUVAR(PFLAGS)
	jz	.Lintr_restore_fsbase

.Lintr_restore_registers:
#ifdef DIAGNOSTIC
	/* no more C calls after this, so check the SPL */
	cmpl	$0,CPUVAR(ILEVEL)
	jne	.Luser_spl_not_lowered
#endif /* DIAGNOSTIC */

	/*
	 * If the pmap we're now on isn't the same as the one we
	 * were on last time we were in userspace, then use IBPB
	 * to prevent cross-process branch-target injection.
	 */
	CODEPATCH_START
	movq	CPUVAR(PROC_PMAP),%rbx
	cmpq	CPUVAR(USER_PMAP),%rbx
	je	1f
	xorl	%edx,%edx
	movl	$PRED_CMD_IBPB,%eax
	movl	$MSR_PRED_CMD,%ecx
	wrmsr
	movq	%rbx,CPUVAR(USER_PMAP)
1:
	CODEPATCH_END(CPTAG_IBPB_NOP)
	call	pku_xonly
	RET_STACK_REFILL_WITH_RCX

	movq	TF_R8(%rsp),%r8
	movq	TF_R9(%rsp),%r9
	movq	TF_R10(%rsp),%r10
	movq	TF_R12(%rsp),%r12
	movq	TF_R13(%rsp),%r13
	movq	TF_R14(%rsp),%r14
	movq	TF_R15(%rsp),%r15
	movq	TF_RBX(%rsp),%rbx

	CODEPATCH_START
	xorl	%edi,%edi
	xorl	%esi,%esi
	xorl	%r11d,%r11d
	xorl	%eax,%eax
	xorl	%edx,%edx
	xorl	%ecx,%ecx
	movw	%ds,TF_R8(%rsp)
	verw	TF_R8(%rsp)
	CODEPATCH_END(CPTAG_MDS)

	movq	TF_RDI(%rsp),%rdi
	movq	TF_RSI(%rsp),%rsi
	movq	TF_RBP(%rsp),%rbp

	/*
	 * To get the final value for the register that was used
	 * for the mov to %cr3, we need access to somewhere accessible
	 * on the user page tables, so we save it in CPUVAR(SCRATCH)
	 * across the switch.
	 */
	/* update iret frame */
	movq	CPUVAR(INTR_RSP),%rdx
	movq	$(GSEL(GUCODE_SEL,SEL_UPL)),IRETQ_CS(%rdx)
	movq	TF_RIP(%rsp),%rax
	movq	%rax,IRETQ_RIP(%rdx)
	movq	TF_RFLAGS(%rsp),%rax
	movq	%rax,IRETQ_RFLAGS(%rdx)
	movq	TF_RSP(%rsp),%rax
	movq	%rax,IRETQ_RSP(%rdx)
	movq	$(GSEL(GUDATA_SEL,SEL_UPL)),IRETQ_SS(%rdx)
	/* finish with the trap frame */
	movq	TF_RAX(%rsp),%rax
	movq	TF_RCX(%rsp),%rcx
	movq	TF_R11(%rsp),%r11
	/* switch to the trampoline stack */
	xchgq	%rdx,%rsp
	movq	TF_RDX(%rdx),%rdx
	CODEPATCH_START
	movq	%rax,CPUVAR(SCRATCH)
	movq	CPUVAR(USER_CR3),%rax
	PCID_SET_REUSE_NOP
	movq	%rax,%cr3
Xiretq_trampback:
KTEXT_PAGE_END
/* the movq %cr3 switches to this "KUTEXT" page */
KUTEXT_PAGE_START
	.space	(Xiretq_trampback - Xsyscall_meltdown) - \
		(. - XUsyscall_meltdown), 0xcc
	movq	CPUVAR(SCRATCH),%rax
.Liretq_swapgs:
	swapgs
doreti_iret_meltdown:
	iretq
KUTEXT_PAGE_END
/*
 * Back to the "KTEXT" page to fill in the speculation trap and the
 * swapgs+iretq used for non-Meltdown kernels.  This switching back
 * and forth between segments is so that we can do the .space
 * calculation below to guarantee the iretq's above and below line
 * up, so the 'doreti_iret' label lines up with the iretq whether
 * the CPU is affected by Meltdown or not.
 */
KTEXT_PAGE_START
0:	pause
	lfence
	jmp	0b
.LKiretq_swapgs:
	/*
	 * The desired directive here would be:
	 *	.space	(.Liretq_swapgs - XUsyscall_meltdown) - \
	 *		(.LKiretq_swapgs - Xsyscall_meltdown), 0xcc
	 *
	 * While llvm-18 and earlier handled that, llvm/clang-19 miscalculates
	 * it and thinks that's a negative number; it's not: the correct value
	 * at this time is "2".  Indeed, it calculates the value correctly if
	 * you append this bit to the end of locore.S:
	 *	.section .rodata
	 *	right_value:
	 *	.quad	(.Liretq_swapgs - XUsyscall_meltdown) - \
	 *		(.LKiretq_swapgs - Xsyscall_meltdown)
	 * You can build locore.o manually, use objdump on it, and see what
	 * value was stored at 'right_value'!  So compiler updates go.
	 * Fortunately(?), this has broken before so the kernel Makefile
	 * checks the compiled locore.o to verify that the iretq instructions
	 * line up (that being the most critical part of what matters).  If
	 * the intr_user_exit sequence changes such that the correct value is
	 * _not_ "2" and builds fail, then append that chunk above and get the
	 * correct new value to use here.  :(
	 */
	.space	2, 0xcc
	CODEPATCH_END(CPTAG_MELTDOWN_NOP)
	swapgs

	.globl	doreti_iret
doreti_iret:
	iretq
KTEXT_PAGE_END

	.text
	_ALIGN_TRAPS
.Lintr_restore_xstate:		/* CPU doesn't have curproc's xstate */
	orl	$CPUPF_USERXSTATE,CPUVAR(PFLAGS)
	movq	CPUVAR(CURPCB),%rdi
#if PCB_SAVEFPU != 0
	addq	$PCB_SAVEFPU,%rdi
#endif
	movq	xsave_mask(%rip),%rdx
	movl	%edx,%eax
	shrq	$32, %rdx
	CODEPATCH_START
	fxrstor64	(%rdi)
	CODEPATCH_END(CPTAG_XRSTORS)
	//testl	%eax,%eax
	//jnz	.Lintr_xrstor_faulted
.Lintr_restore_fsbase:		/* CPU doesn't have curproc's FS.base */
	orl	$CPUPF_USERSEGS,CPUVAR(PFLAGS)
	movq	CPUVAR(CURPCB),%rdx
	movq	PCB_FSBASE(%rdx),%rdx
	movl	%edx,%eax
	shrq	$32,%rdx
	movl	$MSR_FSBASE,%ecx
	wrmsr
	jmp	.Lintr_restore_registers

.Lintr_xrstor_faulted:
	/*
	 * xrstor faulted; we need to reset the FPU state and call trap()
	 * to post a signal, which requires interrupts be enabled.
	 */
	sti
	movq	proc0paddr(%rip),%rdi
#if PCB_SAVEFPU != 0
	addq	$PCB_SAVEFPU,%rdi
#endif
	CODEPATCH_START
	fxrstor64	(%rdi)
	CODEPATCH_END(CPTAG_XRSTORS)
	movq	$T_PROTFLT,TF_TRAPNO(%rsp)
	jmp	recall_trap

#ifdef DIAGNOSTIC
.Lintr_user_exit_not_blocked:
	movl	warn_once(%rip),%edi
	testl	%edi,%edi
	jnz	1f
	incl	%edi
	movl	%edi,warn_once(%rip)
	leaq	.Lnot_blocked(%rip),%rdi
	call	printf
#ifdef DDB
	int	$3
#endif /* DDB */
1:	cli
	jmp	intr_user_exit

.Luser_spl_not_lowered:
	sti
	leaq	intr_spl_lowered(%rip),%rdi
	movl	CPUVAR(ILEVEL),%esi
	xorl	%edx,%edx		/* always SPL zero for userspace */
	xorl	%eax,%eax
	call	printf
#ifdef DDB
	int	$3
#endif /* DDB */
	movl	$0,CPUVAR(ILEVEL)
	cli
	jmp	intr_user_exit

	.section .rodata
intr_spl_lowered:
	.asciz	"WARNING: SPL NOT LOWERED ON TRAP EXIT %x %x\n"
	.text
#endif /* DIAGNOSTIC */
END(Xintr_user_exit)


/*
 * Return to supervisor mode from trap or interrupt
 */
NENTRY(intr_fast_exit)
#ifdef DIAGNOSTIC
	pushfq
	popq	%rdx
	testq	$PSL_I,%rdx
	jnz	.Lintr_exit_not_blocked
#endif /* DIAGNOSTIC */
	movq	TF_RDI(%rsp),%rdi
	movq	TF_RSI(%rsp),%rsi
	movq	TF_R8(%rsp),%r8
	movq	TF_R9(%rsp),%r9
	movq	TF_R10(%rsp),%r10
	movq	TF_R12(%rsp),%r12
	movq	TF_R13(%rsp),%r13
	movq	TF_R14(%rsp),%r14
	movq	TF_R15(%rsp),%r15
	movq	TF_RBP(%rsp),%rbp
	movq	TF_RBX(%rsp),%rbx
	movq	TF_RDX(%rsp),%rdx
	movq	TF_RCX(%rsp),%rcx
	movq	TF_R11(%rsp),%r11
	movq	TF_RAX(%rsp),%rax
	addq	$TF_RIP,%rsp
	iretq

#ifdef DIAGNOSTIC
.Lintr_exit_not_blocked:
	movl	warn_once(%rip),%edi
	testl	%edi,%edi
	jnz	1f
	incl	%edi
	movl	%edi,warn_once(%rip)
	leaq	.Lnot_blocked(%rip),%rdi
	call	printf
#ifdef DDB
	int	$3
#endif /* DDB */
1:	cli
	jmp	intr_fast_exit

	.data
.global warn_once
warn_once:
	.long	0
	.section .rodata
.Lnot_blocked:
	.asciz	"WARNING: INTERRUPTS NOT BLOCKED ON INTERRUPT RETURN: 0x%x 0x%x\n"
	.text
#endif
END(intr_fast_exit)

/*
 * FPU/"extended CPU state" handling
 *	void xrstor_kern(sfp, mask)
 *		using first of xrstors/xrstor/fxrstor, load given state
 *		which is assumed to be trusted: i.e., unaltered from
 *		xsaves/xsaveopt/xsave/fxsave by kernel
 * 	int xrstor_user(sfp, mask)
 *		using first of xrstor/fxrstor, load given state which might
 *		not be trustable: #GP faults will be caught; returns 0/1 if
 *		okay/it trapped.
 *	void fpusave(sfp) 
 *		save current state, but retain it in the FPU
 *	void fpusavereset(sfp)
 *		save current state and reset FPU to initial/kernel state
 *	int xsetbv_user(reg, mask)
 *		load specified %xcr# register, returns 0/1 if okay/it trapped
 */

ENTRY(xrstor_kern)
	RETGUARD_SETUP(xrstor_kern, r11)
	movq	%rsi, %rdx
	movl	%esi, %eax
	shrq	$32, %rdx
	CODEPATCH_START
	fxrstor64	(%rdi)
	CODEPATCH_END(CPTAG_XRSTORS)
	RETGUARD_CHECK(xrstor_kern, r11)
	ret
	lfence
END(xrstor_kern)

ENTRY(xrstor_user)
	RETGUARD_SETUP(xrstor_user, r11)
	movq	%rsi, %rdx
	movl	%esi, %eax
	shrq	$32, %rdx
	.globl	xrstor_fault
xrstor_fault:
	CODEPATCH_START
	fxrstor64	(%rdi)
	CODEPATCH_END(CPTAG_XRSTOR)
	xorl	%eax, %eax
	RETGUARD_CHECK(xrstor_user, r11)
	ret
	lfence
NENTRY(xrstor_resume)
	movl	$1, %eax
	RETGUARD_CHECK(xrstor_user, r11)
	ret
	lfence
END(xrstor_user)

ENTRY(fpusave)
	RETGUARD_SETUP(fpusave, r11)
	movq	xsave_mask(%rip),%rdx
	movl	%edx,%eax
	shrq	$32,%rdx
	CODEPATCH_START
	fxsave64	(%rdi)
	CODEPATCH_END(CPTAG_XSAVE)
	RETGUARD_CHECK(fpusave, r11)
	ret
	lfence
END(fpusave)

ENTRY(fpusavereset)
	RETGUARD_SETUP(fpusavereset, r11)
	movq	xsave_mask(%rip),%rdx
	movl	%edx,%eax
	shrq	$32,%rdx
	CODEPATCH_START
	fxsave64	(%rdi)
	CODEPATCH_END(CPTAG_XSAVE)
	movq	proc0paddr(%rip),%rdi
#if PCB_SAVEFPU != 0
	addq	$PCB_SAVEFPU,%rdi
#endif
	CODEPATCH_START
	fxrstor64	(%rdi)
	CODEPATCH_END(CPTAG_XRSTORS)
	RETGUARD_CHECK(fpusavereset, r11)
	ret
	lfence
END(fpusavereset)

ENTRY(xsetbv_user)
	RETGUARD_SETUP(xsetbv_user, r11)
	movl	%edi, %ecx
	movq	%rsi, %rdx
	movl	%esi, %eax
	shrq	$32, %rdx
	.globl	xsetbv_fault
xsetbv_fault:
	xsetbv
	xorl	%eax, %eax
	RETGUARD_CHECK(xsetbv_user, r11)
	ret
	lfence
NENTRY(xsetbv_resume)
	movl	$1, %eax
	RETGUARD_CHECK(xsetbv_user, r11)
	ret
	lfence
END(xsetbv_user)

CODEPATCH_CODE(_xrstor,		xrstor64 (%rdi))
CODEPATCH_CODE(_xrstors,	xrstors64 (%rdi))
CODEPATCH_CODE(_xsave,		xsave64 (%rdi))
CODEPATCH_CODE(_xsaves,		xsaves64 (%rdi))
CODEPATCH_CODE(_xsaveopt,	xsaveopt64 (%rdi))
CODEPATCH_CODE(_pcid_set_reuse,
		orl	$(CR3_REUSE_PCID >> 32),CPUVAR(USER_CR3 + 4))
CODEPATCH_CODE_LEN(_jmprax,	jmp *%rax; int3)
CODEPATCH_CODE_LEN(_jmpr11,	jmp *%r11; int3)
CODEPATCH_CODE_LEN(_jmpr13,	jmp *%r13; int3)

ENTRY(pagezero)
	RETGUARD_SETUP(pagezero, r11)
	movq    $-PAGE_SIZE,%rdx
	subq    %rdx,%rdi
	xorq    %rax,%rax
1:
	movnti  %rax,(%rdi,%rdx)
	movnti  %rax,8(%rdi,%rdx)
	movnti  %rax,16(%rdi,%rdx)
	movnti  %rax,24(%rdi,%rdx)
	addq    $32,%rdx
	jne     1b
	sfence
	RETGUARD_CHECK(pagezero, r11)
	ret
	lfence
END(pagezero)

/* void pku_xonly(void) */
ENTRY(pku_xonly)
	movq	pg_xo,%rax	/* have PKU support? */
	cmpq	$0,%rax
	je	1f
	movl	$0,%ecx		/* force PKRU for xonly restriction */
	movl	$0,%edx
	movl	$PGK_VALUE,%eax	/* key0 normal, key1 is exec without read */
	wrpkru
1:	ret
	lfence
END(pku_xonly)

/* int rdmsr_safe(u_int msr, uint64_t *data) */
ENTRY(rdmsr_safe)
	RETGUARD_SETUP(rdmsr_safe, r10)

	movl	%edi,	%ecx	/* u_int msr */
	.globl	rdmsr_safe_fault
rdmsr_safe_fault:
	rdmsr
	salq	$32, %rdx
	movl	%eax, %eax
	orq	%rdx, %rax
	movq	%rax, (%rsi)	/* *data */
	xorq	%rax, %rax

	RETGUARD_CHECK(rdmsr_safe, r10)
	ret
	lfence

NENTRY(rdmsr_resume)
	movl	$0x1, %eax
	RETGUARD_CHECK(rdmsr_safe, r10)
	ret
	lfence
END(rdmsr_safe)

#if NHYPERV > 0
/* uint64_t hv_hypercall_trampoline(uint64_t control, paddr_t input, paddr_t output) */
NENTRY(hv_hypercall_trampoline)
	endbr64
	mov	%rdx, %r8
	mov	%rsi, %rdx
	mov	%rdi, %rcx
	jmp	hv_hypercall_page
END(hv_hypercall_trampoline)
	/* Hypercall page needs to be page aligned */
	.text
	.align	NBPG, 0xcc
	.globl	hv_hypercall_page
hv_hypercall_page:
	.skip	0x1000, 0xcc
#endif /* NHYPERV > 0 */

#if NXEN > 0
	/* Hypercall page needs to be page aligned */
	.text
	.align	NBPG, 0xcc
	.globl	xen_hypercall_page
xen_hypercall_page:
	.skip	0x1000, 0xcc
#endif /* NXEN > 0 */
