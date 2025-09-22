/*	$OpenBSD: vector.S,v 1.103 2025/07/11 20:04:20 bluhm Exp $	*/
/*	$NetBSD: vector.S,v 1.5 2004/06/28 09:13:11 fvdl Exp $	*/

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
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
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

#include <machine/param.h>
#include <machine/i8259.h>
#include <machine/i82093reg.h>
#include <machine/i82489reg.h>
#include <machine/asm.h>
#include <machine/frameasm.h>
#include <machine/segments.h>
#include <machine/trap.h>
#include <machine/intr.h>
#include <machine/psl.h>
#include <machine/codepatch.h>
#include <machine/specialreg.h>

#include "ioapic.h"
#include "lapic.h"
#include "assym.h"
#include "xen.h"
#include "hyperv.h"
#include "vmm.h"

/*****************************************************************************/

/*
 * Trap and fault vector routines
 *
 * On exit from the kernel to user mode, we always need to check for ASTs.  In
 * addition, we need to do this atomically; otherwise an interrupt may occur
 * which causes an AST, but it won't get processed until the next kernel entry
 * (possibly the next clock tick).  Thus, we disable interrupt before checking,
 * and only enable them again on the final `iret' or before calling the AST
 * handler.
 */

/*****************************************************************************/

#define	TRAP(a)		pushq $(a) ; jmp alltraps
#define	ZTRAP(a)	pushq $0 ; TRAP(a)

IDTVEC(trap00)
	ZTRAP(T_DIVIDE)
IDTVEC(trap01)
	ZTRAP(T_TRCTRAP)

/*
 * NMIs can happen at any time, so there's no simple way to tell
 * which GS.base is in place at the time of the interrupt.  Instead,
 * borrow a couple ideas from FreeBSD and put the CPU's kernel
 * GS.base in the memory right above the stack, storing the current
 * one in a pair of callee-saved registers (%r12/13).  We save the
 * current %cr3 in a callee-saved register too (%r15).
 * Note: we don't unblock interrupts because a nested normal interrupt
 * would also reenable NMIs.
 */
IDTVEC(trap02)
	pushq	$0
	pushq	$T_NMI
calltrap_specstk:			# special stack path
	TRAP_ENTRY_KERN
	INTR_CLEAR_GPRS
	movl	$MSR_GSBASE,%ecx	# save current GS.base...
	rdmsr
	movq	%rax,%r12		# ...in %r12 and %r13
	movq	%rdx,%r13
	movq	FRAMESIZE(%rsp),%rax	# get kernel GS.base
	movq	%rax,%rdx
	shrq	$32,%rdx
	wrmsr				# switch to it
	movq	%cr3,%r15		# save current %cr3 in %r15
	movq	CPUVAR(KERN_CR3),%rax	# switch to kernel page tables
	testq	%rax,%rax
	jz	INTRENTRY_LABEL(calltrap_specstk)
	movq	%rax,%cr3
	jmp	INTRENTRY_LABEL(calltrap_specstk)
	.text
	.globl	INTRENTRY_LABEL(calltrap_specstk)
INTRENTRY_LABEL(calltrap_specstk):
	lfence				# block speculation through jz above
	cld
	SMAP_CLAC
	movq	%rsp,%rdi
	call	kerntrap
	movq	$0,-8(%rsp)
	movl	$MSR_GSBASE,%ecx	# restore GS.base
	movq	%r12,%rax
	movq	%r13,%rdx
	wrmsr
	/* who knows what happened in this trap; use IPBP on the way out */
	CODEPATCH_START
	xorl	%edx,%edx
	movl	$PRED_CMD_IBPB,%eax
	movl	$MSR_PRED_CMD,%ecx
	wrmsr
	CODEPATCH_END(CPTAG_IBPB_NOP)
	call	pku_xonly
	movq	$0,-8(%rsp)
	popq	%rdi
	popq	%rsi
	popq	%rdx
	popq	%rcx
	popq	%r8
	popq	%r9
	popq	%r10
	popq	%r11
	popq	%r12
	popq	%r13
	popq	%r14
	jmp	calltrap_specstk_tramp
KUENTRY(calltrap_specstk_tramp)
	movq	%r15,%cr3		# restore %cr3
	popq	%r15
	addq	$8,%rsp			# ignore tf_err
	popq	%rbx
	popq	%rax
	addq	$8,%rsp			# ignore tf_trapno
	popq	%rbp
	iretq

IDTVEC(trap03)
#if defined(GPROF) || !defined(DDBPROF)
	ZTRAP(T_BPTFLT)
#else /* !defined(GPROF) && defined(DDBPROF) */
	pushq	$0
	pushq	$T_BPTFLT
	testb	$SEL_RPL,24(%rsp)
	je	INTRENTRY_LABEL(trap03)
	jmp	alltraps
	.text
	.global	INTRENTRY_LABEL(trap03)
INTRENTRY_LABEL(trap03):
	FENCE_NO_SAFE_SMAP
	TRAP_ENTRY_KERN
	sti
	cld
	SMAP_CLAC
	leaq	dt_prov_kprobe, %rdi
	movq	%rsp, %rsi
	call	dt_prov_kprobe_hook
	movq	$0,-8(%rsp)
	cmpl    $0, %eax
	je     .Lreal_kern_trap

	cli
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
	/* %rax restored below, after being used to shift the stack */

	cmpl    $2, %eax
	je      .Lemulate_ret

.Lemulate_push_rbp:

	/*
	 * We are returning from a probe trap so we need to fix the
	 * stack layout and emulate the patched instruction.
	 * Reserve enough room to emulate "pushq %rbp".
	 */
	subq	$16, %rsp

	movq	(TF_RAX + 16)(%rsp), %rax
	movq	%rax, TF_RAX(%rsp)

	/* Shift hardware-saved registers. */
	movq	(TF_RIP + 16)(%rsp), %rax
	movq	%rax, TF_RIP(%rsp)
	movq	(TF_CS + 16)(%rsp), %rax
	movq	%rax, TF_CS(%rsp)
	movq	(TF_RFLAGS + 16)(%rsp), %rax
	movq	%rax, TF_RFLAGS(%rsp)
	movq	(TF_RSP + 16)(%rsp), %rax
	movq	%rax, TF_RSP(%rsp)
	movq	(TF_SS + 16)(%rsp), %rax
	movq	%rax, TF_SS(%rsp)

	/* Pull 8 bytes off the stack and store %rbp in the expected location.*/
	movq	TF_RSP(%rsp), %rax
	subq	$8, %rax
	movq	%rax, TF_RSP(%rsp)
	movq	%rbp, (%rax)

	/* Finally restore %rax */
	movq	(TF_RAX + 16)(%rsp),%rax
	jmp .ret_int3

.Lemulate_ret:

	/* Store a new return address in %rip */
	movq    TF_RSP(%rsp), %rax
	movq    (%rax), %rax
	movq    %rax, TF_RIP(%rsp)
	addq    $8, TF_RSP(%rsp)

	/* Finally restore %rax */
	movq	(TF_RAX)(%rsp),%rax

.ret_int3:
	addq	$TF_RIP,%rsp
	iretq
#endif /* !defined(GPROF) && defined(DDBPROF) */

IDTVEC(trap04)
	ZTRAP(T_OFLOW)	# impossible: INTO instruction invalid in amd64
IDTVEC(trap05)
	ZTRAP(T_BOUND)	# impossible: BOUND instruction invalid in amd64
IDTVEC(trap06)
	ZTRAP(T_PRIVINFLT)
IDTVEC(trap07)
	ZTRAP(T_DNA)		# impossible: we don't do lazy FPU
IDTVEC(trap08)
	pushq	$T_DOUBLEFLT
	jmp	calltrap_specstk
IDTVEC(trap09)
	ZTRAP(T_FPOPFLT)	# impossible: not generated on amd64
IDTVEC(trap0a)
	TRAP(T_TSSFLT)
IDTVEC(trap0b)
	TRAP(T_SEGNPFLT)
IDTVEC(trap0c)
	TRAP(T_STKFLT)

/*
 * The #GP (general protection fault) handler has a few weird cases
 * to handle:
 *  - trapping in iretq to userspace and
 *  - trapping in xrstor in the kernel.
 *  - trapping when invalid MSRs are read in rdmsr_safe
 * We detect these by examining the %rip in the iretq_frame.
 * Handling them is done by updating %rip in the iretq_frame to point
 * to a stub handler of some sort and then iretq'ing to it.  For the
 * iretq fault we resume in a stub which acts like we got a fresh #GP.
 * For the xrstor fault we resume to a stub which returns an error to
 * the routine that requested the xrstor.
 */
IDTVEC(trap0d)
	pushq	%rdx
	pushq	%rcx
	movq	24(%rsp),%rdx		/* over %r[cd]x and err to %rip */
	leaq	doreti_iret(%rip),%rcx
	cmpq	%rcx,%rdx
	je	.Lhandle_doreti
	leaq	xrstor_fault(%rip),%rcx
	cmpq	%rcx,%rdx
	je	.Lhandle_xrstor
	leaq	xsetbv_fault(%rip),%rcx
	cmpq	%rcx,%rdx
	je	.Lhandle_xsetbv
	leaq	rdmsr_safe_fault(%rip),%rcx
	cmpq	%rcx,%rdx
	je	.Lhandle_rdmsr_safe
	popq	%rcx
	popq	%rdx
	TRAP(T_PROTFLT)

.Lhandle_rdmsr_safe:
	/* rdmsr faulted; just resume in rdmsr_resume */
	leaq	rdmsr_resume(%rip),%rcx
	jmp	1f

.Lhandle_xrstor:
	/* xrstor faulted; just resume in xrstor_resume */
	leaq	xrstor_resume(%rip),%rcx
	jmp	1f

.Lhandle_xsetbv:
	/* xsetbv faulted; just resume in xsetbv_resume */
	leaq	xsetbv_resume(%rip),%rcx
	jmp	1f

.Lhandle_doreti:
	/* iretq faulted; resume in a stub that acts like we got a #GP */
	leaq	.Lhandle_doreti_resume(%rip),%rcx
1:	lfence		/* block speculation through conditionals above */
	movq	%rcx,24(%rsp)		/* over %r[cd]x and err to %rip */
	popq	%rcx
	popq	%rdx
	addq	$8,%rsp			/* pop the err code */
	jmp	doreti_iret
.Lhandle_doreti_resume:
	ZTRAP(T_PROTFLT)

IDTVEC(trap0e)
	TRAP(T_PAGEFLT)
IDTVEC(intrspurious)
IDTVEC_ALIAS(trap0f, intrspurious)
	iretq
IDTVEC(trap10)
	ZTRAP(T_ARITHTRAP)
IDTVEC(trap11)
	TRAP(T_ALIGNFLT)
IDTVEC(trap12)
	ZTRAP(T_MCA)
IDTVEC(trap13)
	ZTRAP(T_XMM)
IDTVEC(trap14)
	ZTRAP(T_VE)
IDTVEC(trap15)
	TRAP(T_CP)

IDTVEC(trap1d)
	/*
	 * #VC is AMD CPU specific, thus we don't use any Intel Meltdown
	 * workarounds.
	 *
	 * We handle #VC different from other traps, as we do not want
	 * to re-enable interrupts.  #VC might happen during IRQ handling
	 * before a specific hardware interrupt gets masked.  Re-enabling
	 * interrupts in the trap handler might cause nested IRQs of
	 * the same level.  Thus keep interrupts disabled.
	 */
	pushq	$T_VC
	testb	$SEL_RPL,24(%rsp)
	je	vctrap_kern
	swapgs
	FENCE_SWAPGS_MIS_TAKEN
	movq	%rax,CPUVAR(SCRATCH)

	/* #VC from userspace */
	TRAP_ENTRY_USER
	cld
	SMAP_CLAC
	/* shortcut to regular path, but with interrupts disabled */
	jmp	recall_trap

	/* #VC from kernspace */
vctrap_kern:
	FENCE_NO_SAFE_SMAP
	TRAP_ENTRY_KERN
	cld
	SMAP_CLAC
	/* shortcut to regular path, but with interrupts disabled */
	jmp	.Lreal_kern_trap

IDTVEC(trap1f)
IDTVEC_ALIAS(trap16, trap1f)
IDTVEC_ALIAS(trap17, trap1f)
IDTVEC_ALIAS(trap18, trap1f)
IDTVEC_ALIAS(trap19, trap1f)
IDTVEC_ALIAS(trap1a, trap1f)
IDTVEC_ALIAS(trap1b, trap1f)
IDTVEC_ALIAS(trap1c, trap1f)
IDTVEC_ALIAS(trap1e, trap1f)
	/* 22 - 31 reserved for future exp */
	ZTRAP(T_RESERVED)

	.section .rodata
	.globl	Xexceptions
	.type Xexceptions,@object
Xexceptions:
	.quad	Xtrap00, Xtrap01, Xtrap02, Xtrap03
	.quad	Xtrap04, Xtrap05, Xtrap06, Xtrap07
	.quad	Xtrap08, Xtrap09, Xtrap0a, Xtrap0b
	.quad	Xtrap0c, Xtrap0d, Xtrap0e, Xtrap0f
	.quad	Xtrap10, Xtrap11, Xtrap12, Xtrap13
	.quad	Xtrap14, Xtrap15, Xtrap16, Xtrap17
	.quad	Xtrap18, Xtrap19, Xtrap1a, Xtrap1b
	.quad	Xtrap1c, Xtrap1d, Xtrap1e, Xtrap1f
END(Xexceptions)

/*
 * All traps go through here.  Figure out whether we're
 * a) coming from usermode and need the Meltdown mitigation before
 *    jumping to user trap handling followed by AST and
 *    return-to-userspace handling, or
 * b) coming from supervisor mode and can directly jump to kernel
 *    trap handling before returning sans AST or other handling.
 */
KUTEXT_PAGE_START
 	.align	NBPG, 0xcc
	/*
	 * This is the Meltdown alltraps page, which is mapped into
	 * the U-K page tables at the same location as alltraps
	 * below.  For this, the Meltdown case, we must be coming from
	 * userspace so we skip the SEL_RPL test and go directly to
	 * the swapgs+use-scratch+change-cr3 sequence.  Switching to
	 * the kernel page tables (thank you, Intel) will make us
	 * continue at the "TRAP_ENTRY_USER" after alltraps below.
	 * In case the CPU speculates past the mov to cr3,
	 * we put a retpoline-style pause-lfence-jmp-to-pause loop.
	 */
Xalltraps:
	swapgs
	movq	%rax,CPUVAR(SCRATCH)
	movq	CPUVAR(KERN_CR3),%rax
	.byte	0x66, 0x90	/* space for FENCE_SWAPGS_MIS_TAKEN below */
	movq	%rax,%cr3
0:	pause
	lfence
	jmp	0b
KUTEXT_PAGE_END

KTEXT_PAGE_START
	.align	NBPG, 0xcc
GENTRY(alltraps)
	CODEPATCH_START
	testb	$SEL_RPL,24(%rsp)
	je	alltraps_kern
	swapgs
	CODEPATCH_END(CPTAG_MELTDOWN_ALLTRAPS)
	FENCE_SWAPGS_MIS_TAKEN
	movq	%rax,CPUVAR(SCRATCH)
	.space	(0b - Xalltraps) - (. - alltraps), 0x90

	/*
	 * Traps from userspace
	 */
	TRAP_ENTRY_USER
	sti
	cld
	SMAP_CLAC
	.globl	recall_trap
recall_trap:
	movq	%rsp, %rdi
	call	usertrap
	movq	$0,-8(%rsp)
	cli
	jmp	intr_user_exit
END(alltraps)

/*
 * Traps from supervisor mode (kernel)
 * If we're not mitigating Meltdown, then there's a conditional branch
 * above and we may need a fence to mitigate CVE-2019-1125.  If we're
 * doing Meltdown mitigation there's just an unconditional branch and
 * can skip the fence.
 */
	_ALIGN_TRAPS
GENTRY(alltraps_kern)
	FENCE_NO_SAFE_SMAP
GENTRY(alltraps_kern_meltdown)
	TRAP_ENTRY_KERN
	sti
	cld
	SMAP_CLAC
.Lreal_kern_trap:
#ifdef DIAGNOSTIC
	movl	CPUVAR(ILEVEL),%ebx
#endif /* DIAGNOSTIC */
	movq	%rsp, %rdi
	call	kerntrap
	movq	$0,-8(%rsp)
2:	cli
#ifndef DIAGNOSTIC
	INTRFASTEXIT
#else /* DIAGNOSTIC */
	cmpl	CPUVAR(ILEVEL),%ebx
	jne	3f
	INTRFASTEXIT
3:	sti
	leaq	spl_lowered(%rip),%rdi
	movl	CPUVAR(ILEVEL),%esi
	movl	%ebx,%edx
	xorq	%rax,%rax
	call	printf
	movq	$0,-8(%rsp)
#ifdef DDB
	int	$3
#endif /* DDB */
	movl	%ebx,CPUVAR(ILEVEL)
	jmp	2b

	.pushsection .rodata
	.type spl_lowered,@object
spl_lowered:
	.asciz	"WARNING: SPL NOT LOWERED ON TRAP EXIT %x %x\n"
END(spl_lowered)
	.popsection
#endif /* DIAGNOSTIC */
END(alltraps_kern)
END(alltraps_kern_meltdown)
KTEXT_PAGE_END

/* #VC trap entry for early bootstrap */
IDTVEC(vctrap_early)
	pushq	$T_VC
	TRAP_ENTRY_KERN		/* early #VC has to be in kernel mode */
	cld
	movq	%rsp, %rdi
	movq	$0x0, %rsi
	call	vctrap
	movq	$0,-8(%rsp)
	INTRFASTEXIT

/*
 * Macros for interrupt entry, call to handler, and exit.
 *
 * XXX
 * The interrupt frame is set up to look like a trap frame.  This may be a
 * waste.  The only handler which needs a frame is the clock handler, and it
 * only needs a few bits.  Xdoreti() needs a trap frame for handling ASTs, but
 * it could easily convert the frame on demand.
 *
 * The direct costs of setting up a trap frame are two pushq's (error code and
 * trap number), an addl to get rid of these, and pushing and popping the
 * callee-saved registers %ebx, %ebp, and %r1[2-5] twice.
 *
 * If the interrupt frame is made more flexible,  INTR can push %eax first and
 * decide the ipending case with less overhead
 */

KUENTRY(x2apic_eoi)
	pushq   %rax
	pushq   %rcx
	pushq   %rdx
	mov     $MSR_X2APIC_EOI,%ecx
	mov     $0,%eax
	mov     $0,%edx
	wrmsr
	popq    %rdx
	popq    %rcx
	popq    %rax
	ret
	lfence
END(x2apic_eoi)

#if NLAPIC > 0
#ifdef MULTIPROCESSOR
KIDTVEC(recurse_lapic_ipi)
	INTR_RECURSE
	jmp	1f
END(Xrecurse_lapic_ipi)
IDTVEC(intr_lapic_ipi)
	INTRENTRY(intr_lapic_ipi)
	CODEPATCH_START
	movl	$0,local_apic+LAPIC_EOI
	CODEPATCH_END(CPTAG_EOI)
	movl	CPUVAR(ILEVEL),%ebx
	cmpl	$IPL_IPI,%ebx
	jae	2f
END(INTRENTRY_LABEL(intr_lapic_ipi))
KIDTVEC_FALLTHROUGH(resume_lapic_ipi)
	endbr64
1:
	incl	CPUVAR(IDEPTH)
	movl	$IPL_IPI,CPUVAR(ILEVEL)
	sti
	cld
	SMAP_CLAC
	movq	%rbx,IF_PPL(%rsp)
	call	x86_ipi_handler
	movq	$0,-8(%rsp)
	jmp	Xdoreti
2:
	movq	$(1 << LIR_IPI),%rax
	orq	%rax,CPUVAR(IPENDING)
	INTRFASTEXIT
END(Xresume_lapic_ipi)

/*
 * "Fast" IPI handlers.  These are the IPIs which are handled without
 * unblocking interrupts, so no need for 'recurse' or 'resume' entry points
 */
/* invalidate the entire TLB, no PCIDs version */
IDTVEC(ipi_invltlb)
	pushq	%rax

	ioapic_asm_ack()

	movq	%cr3, %rax
	movq	%rax, %cr3

	lock
	decq	tlb_shoot_wait

	popq	%rax
	iretq
END(Xipi_invltlb)

#if NVMM > 0
/* Invalidate VMX EPT */
IDTVEC(ipi_invept)
	pushq	%rax
	pushq	%rdx

	ioapic_asm_ack()

	movq	$ept_shoot_vid, %rax
	movq	ept_shoot_mode, %rdx
	invept	(%rax), %rdx

	lock
	decq	tlb_shoot_wait

	popq	%rdx
	popq	%rax
	iretq
END(Xipi_invept)
#endif /* NVMM > 0 */

/* invalidate a single page, no PCIDs version */
IDTVEC(ipi_invlpg)
	pushq	%rax

	ioapic_asm_ack()

	movq	tlb_shoot_addr1, %rax
	invlpg	(%rax)

	lock
	decq	tlb_shoot_wait

	popq	%rax
	iretq
END(Xipi_invlpg)

/* invalidate a range of pages, no PCIDs version */
IDTVEC(ipi_invlrange)
	pushq	%rax
	pushq	%rdx

	ioapic_asm_ack()

	movq	tlb_shoot_addr1, %rax
	movq	tlb_shoot_addr2, %rdx
1:	invlpg	(%rax)
	addq	$PAGE_SIZE, %rax
	cmpq	%rdx, %rax
	jb	1b

	lock
	decq	tlb_shoot_wait

	popq	%rdx
	popq	%rax
	iretq
END(Xipi_invlrange)

/*
 * Invalidate the userspace PCIDs.
 */
IDTVEC(ipi_invltlb_pcid)
	pushq	%rax

	ioapic_asm_ack()

	/* set the type */
	movl	$INVPCID_PCID,%eax

	/* finish getting space for the INVPCID descriptor */
#if INVPCID_PCID == PCID_PROC
	pushq	%rax
#else
	pushq	$PCID_PROC
#endif

	invpcid	(%rsp),%rax

	/* bump the pcid in the descriptor and invpcid again */
	movl	$PCID_PROC_INTEL,(%rsp)
	invpcid	(%rsp),%rax

	lock
	decq	tlb_shoot_wait

	/* restore the stack */
	popq	%rax
	popq	%rax
	iretq
END(Xipi_invltlb_pcid)

/*
 * Invalidate a VA in two PCIDs.  Kernel VAs are present in PCIDs 0 and 1,
 * while userspace VAs are present in PCIDs 1 and 2.
 */
IDTVEC(ipi_invlpg_pcid)
	pushq	%rax

	ioapic_asm_ack()

	/* space for the INVPCID descriptor */
	subq	$16,%rsp

	/* set the PCID in the descriptor */
	movl	tlb_shoot_first_pcid,%eax
	movq	%rax,(%rsp)

	/* set the address in the descriptor */
	movq	tlb_shoot_addr1,%rax
	movq	%rax,8(%rsp)

	/* set the type to zero, and invpcid */
	xorl	%eax,%eax
	invpcid	(%rsp),%rax

	/* bump the pcid in the descriptor and invpcid again */
	addl	$1,(%rsp)
	invpcid	(%rsp),%rax

	lock
	decq	tlb_shoot_wait

	/* restore the stack */
	addq	$16,%rsp
	popq	%rax
	iretq
END(Xipi_invlpg_pcid)

/*
 * Invalidate a range of VA in two PCIDs.  Kernel VAs are present in
 * PCIDs 0 and 1, while userspace VAs are present in PCIDs 1 and 2.
 */
IDTVEC(ipi_invlrange_pcid)
	pushq	%rax
	pushq	%rdx
	pushq	%rcx

	ioapic_asm_ack()

	/* space for the INVPCID descriptor */
	subq	$16,%rsp

	/* set the PCID in the descriptor */
	movl	tlb_shoot_first_pcid,%eax
	movq	%rax,(%rsp)

	/* set up for the loop: load the limit and set the type to zero */
	movq	tlb_shoot_addr2,%rdx
	xorl	%ecx,%ecx

	/* set the address in the descriptor and loop the invalidate */
	movq	tlb_shoot_addr1,%rax
1:	movq	%rax,8(%rsp)
	invpcid	(%rsp),%rcx
	addl	$1,(%rsp)
	invpcid	(%rsp),%rcx
	subl	$1,(%rsp)
	addq	$PAGE_SIZE,%rax
	cmpq	%rdx,%rax
	jb	1b

	lock
	decq	tlb_shoot_wait

	/* restore the stack */
	addq	$16,%rsp
	popq	%rcx
	popq	%rdx
	popq	%rax
	iretq
END(Xipi_invlrange_pcid)

IDTVEC(ipi_wbinvd)
	ioapic_asm_ack()

	wbinvd

	lock
	decq	wbinvd_wait

	iretq
END(Xipi_wbinvd)

#endif /* MULTIPROCESSOR */

	/*
	 * Interrupt from the local APIC timer.
	 */
KIDTVEC(recurse_lapic_ltimer)
	INTR_RECURSE
	jmp	1f
END(Xrecurse_lapic_ltimer)
IDTVEC(intr_lapic_ltimer)
	INTRENTRY(intr_lapic_ltimer)
	CODEPATCH_START
	movl	$0,local_apic+LAPIC_EOI
	CODEPATCH_END(CPTAG_EOI)
	movl	CPUVAR(ILEVEL),%ebx
	cmpl	$IPL_CLOCK,%ebx
	jae	2f
END(INTRENTRY_LABEL(intr_lapic_ltimer))
KIDTVEC_FALLTHROUGH(resume_lapic_ltimer)
	endbr64
1:
	incl	CPUVAR(IDEPTH)
	movl	$IPL_CLOCK,CPUVAR(ILEVEL)
	sti
	cld
	SMAP_CLAC
	movq	%rbx,IF_PPL(%rsp)
	xorq	%rdi,%rdi
	call	lapic_clockintr
	movq	$0,-8(%rsp)
	jmp	Xdoreti
2:
	movq	$(1 << LIR_TIMER),%rax
	orq	%rax,CPUVAR(IPENDING)
	INTRFASTEXIT
END(Xresume_lapic_ltimer)

#if NXEN > 0
/*
 * Xen event channel upcall interrupt handler.
 * Only used when the hypervisor supports direct vector callbacks.
 */
KIDTVEC(recurse_xen_upcall)
	INTR_RECURSE
	jmp	1f
END(Xrecurse_xen_upcall)
IDTVEC(intr_xen_upcall)
	INTRENTRY(intr_xen_upcall)
	call	xen_intr_ack
	movq	$0,-8(%rsp)
	movl	CPUVAR(ILEVEL),%ebx
	cmpl	$IPL_NET,%ebx
	jae	2f
END(INTRENTRY_LABEL(intr_xen_upcall))
KIDTVEC_FALLTHROUGH(resume_xen_upcall)
	endbr64
1:
	incl	CPUVAR(IDEPTH)
	movl	$IPL_NET,CPUVAR(ILEVEL)
	sti
	cld
	SMAP_CLAC
	movq	%rbx,IF_PPL(%rsp)
	call	xen_intr
	movq	$0,-8(%rsp)
	jmp	Xdoreti
2:
	movq	$(1 << LIR_XEN),%rax
	orq	%rax,CPUVAR(IPENDING)
	INTRFASTEXIT
END(Xresume_xen_upcall)
#endif /* NXEN > 0 */

#if NHYPERV > 0
/*
 * Hyperv event channel upcall interrupt handler.
 * Only used when the hypervisor supports direct vector callbacks.
 */
KIDTVEC(recurse_hyperv_upcall)
	INTR_RECURSE
	jmp	1f
END(Xrecurse_hyperv_upcall)
IDTVEC(intr_hyperv_upcall)
	INTRENTRY(intr_hyperv_upcall)
	movl	CPUVAR(ILEVEL),%ebx
	cmpl	$IPL_NET,%ebx
	jae	2f
END(INTRENTRY_LABEL(intr_hyperv_upcall))
KIDTVEC_FALLTHROUGH(resume_hyperv_upcall)
	endbr64
1:
	incl	CPUVAR(IDEPTH)
	movl	$IPL_NET,CPUVAR(ILEVEL)
	sti
	cld
	SMAP_CLAC
	movq	%rbx,IF_PPL(%rsp)
	call	hv_intr
	movq	$0,-8(%rsp)
	jmp	Xdoreti
2:
	movq	$(1 << LIR_HYPERV),%rax
	orq	%rax,CPUVAR(IPENDING)
	INTRFASTEXIT
END(Xresume_hyperv_upcall)
#endif /* NHYPERV > 0 */
#endif /* NLAPIC > 0 */

#define voidop(num)


/*
 * This macro defines the generic stub code. Its arguments modify it
 * for specific PICs.
 */

#define	INTRSTUB(name, num, early_ack, late_ack, mask, unmask, level_mask) \
KIDTVEC(recurse_##name##num)						;\
	INTR_RECURSE							;\
END(Xrecurse_##name##num)						;\
KIDTVEC_FALLTHROUGH(resume_##name##num)					\
	endbr64								;\
	movq	$IREENT_MAGIC,TF_ERR(%rsp)				;\
	movl	%ebx,%r13d						;\
	movq	CPUVAR(ISOURCES) + (num) * 8, %r14			;\
	movl	IS_MAXLEVEL(%r14),%ebx					;\
	jmp	1f							;\
END(Xresume_##name##num)						;\
IDTVEC(intr_##name##num)						;\
	INTRENTRY(intr_##name##num)					;\
	movq	CPUVAR(ISOURCES) + (num) * 8, %r14			;\
	mask(num)			/* mask it in hardware */	;\
	early_ack(num)			/* and allow other intrs */	;\
	incl	uvmexp+V_INTR		/* statistical info */		;\
	testq	%r14,%r14						;\
	jz	9f			/* stray */			;\
	movl	IS_MAXLEVEL(%r14),%ebx					;\
	movl	CPUVAR(ILEVEL),%r13d					;\
	cmpl	%ebx,%r13d						;\
	jae	10f			/* currently masked; hold it */	;\
1:									\
	movq	%r13,IF_PPL(%rsp)					;\
	movl	%ebx,CPUVAR(ILEVEL)					;\
	sti								;\
	cld								;\
	SMAP_CLAC							;\
	incl	CPUVAR(IDEPTH)						;\
	movq	IS_HANDLERS(%r14),%rbx					;\
6:	/* loop, walking chain of handlers */				\
	movl	IH_LEVEL(%rbx),%r12d					;\
	cmpl	%r13d,%r12d						;\
	jle	7f							;\
	movl	%r12d,CPUVAR(ILEVEL)					;\
	movq	%rbx, %rsi						;\
	movq	%rsp, %rdi						;\
	call	intr_handler		/* call it */			;\
	movq	$0,-8(%rsp)						;\
	orl	%eax,%eax		/* should it be counted? */	;\
	jz	4f			/* no, skip it */		;\
	incq	IH_COUNT(%rbx)		/* count the intrs */		;\
	cmpl	$0,intr_shared_edge					;\
	jne	4f			/* if no shared edges ... */	;\
	orl	%eax,%eax		/* 1 means stop trying */	;\
	jns	5f							;\
4:	movq	IH_NEXT(%rbx),%rbx	/* next handler in chain */	;\
	testq	%rbx,%rbx						;\
	jnz	6b							;\
5:	/* successfully handled */					\
	cli								;\
	unmask(num)			/* unmask it in hardware */	;\
	late_ack(num)							;\
	sti								;\
	jmp	Xdoreti			/* lower spl and do ASTs */	;\
7:	/* current IPL > handler's ih_level */				\
	cli								;\
	movq	$(1 << num),%rax					;\
	orq     %rax,CPUVAR(IPENDING)					;\
	level_mask(num)							;\
	late_ack(num)							;\
	sti								;\
	jmp	Xdoreti			/* lower spl and do ASTs */	;\
10:	/* currently masked */						\
	cli								;\
	movq	$(1 << num),%rax					;\
	orq	%rax,CPUVAR(IPENDING)					;\
	level_mask(num)							;\
	late_ack(num)							;\
	INTRFASTEXIT							;\
9:	/* spurious interrupt */					\
	unmask(num)							;\
	late_ack(num)							;\
	testb	$SEL_RPL,TF_CS(%rsp)					;\
	jnz	intr_user_exit						;\
	INTRFASTEXIT							;\
END(INTRENTRY_LABEL(intr_##name##num))

#define ICUADDR IO_ICU1

INTRSTUB(legacy,0,i8259_asm_ack1,voidop,i8259_asm_mask,i8259_asm_unmask,
    voidop)
INTRSTUB(legacy,1,i8259_asm_ack1,voidop,i8259_asm_mask,i8259_asm_unmask,
    voidop)
INTRSTUB(legacy,2,i8259_asm_ack1,voidop,i8259_asm_mask,i8259_asm_unmask,
    voidop)
INTRSTUB(legacy,3,i8259_asm_ack1,voidop,i8259_asm_mask,i8259_asm_unmask,
    voidop)
INTRSTUB(legacy,4,i8259_asm_ack1,voidop,i8259_asm_mask,i8259_asm_unmask,
    voidop)
INTRSTUB(legacy,5,i8259_asm_ack1,voidop,i8259_asm_mask,i8259_asm_unmask,
    voidop)
INTRSTUB(legacy,6,i8259_asm_ack1,voidop,i8259_asm_mask,i8259_asm_unmask,
    voidop)
INTRSTUB(legacy,7,i8259_asm_ack1,voidop,i8259_asm_mask,i8259_asm_unmask,
    voidop)
#undef ICUADDR
#define ICUADDR IO_ICU2

INTRSTUB(legacy,8,i8259_asm_ack2,voidop,i8259_asm_mask,i8259_asm_unmask,
    voidop)
INTRSTUB(legacy,9,i8259_asm_ack2,voidop,i8259_asm_mask,i8259_asm_unmask,
    voidop)
INTRSTUB(legacy,10,i8259_asm_ack2,voidop,i8259_asm_mask,i8259_asm_unmask,
    voidop)
INTRSTUB(legacy,11,i8259_asm_ack2,voidop,i8259_asm_mask,i8259_asm_unmask,
    voidop)
INTRSTUB(legacy,12,i8259_asm_ack2,voidop,i8259_asm_mask,i8259_asm_unmask,
    voidop)
INTRSTUB(legacy,13,i8259_asm_ack2,voidop,i8259_asm_mask,i8259_asm_unmask,
    voidop)
INTRSTUB(legacy,14,i8259_asm_ack2,voidop,i8259_asm_mask,i8259_asm_unmask,
    voidop)
INTRSTUB(legacy,15,i8259_asm_ack2,voidop,i8259_asm_mask,i8259_asm_unmask,
    voidop)

#if NIOAPIC > 0

INTRSTUB(ioapic_edge,0,voidop,ioapic_asm_ack,voidop,voidop,voidop)
INTRSTUB(ioapic_edge,1,voidop,ioapic_asm_ack,voidop,voidop,voidop)
INTRSTUB(ioapic_edge,2,voidop,ioapic_asm_ack,voidop,voidop,voidop)
INTRSTUB(ioapic_edge,3,voidop,ioapic_asm_ack,voidop,voidop,voidop)
INTRSTUB(ioapic_edge,4,voidop,ioapic_asm_ack,voidop,voidop,voidop)
INTRSTUB(ioapic_edge,5,voidop,ioapic_asm_ack,voidop,voidop,voidop)
INTRSTUB(ioapic_edge,6,voidop,ioapic_asm_ack,voidop,voidop,voidop)
INTRSTUB(ioapic_edge,7,voidop,ioapic_asm_ack,voidop,voidop,voidop)
INTRSTUB(ioapic_edge,8,voidop,ioapic_asm_ack,voidop,voidop,voidop)
INTRSTUB(ioapic_edge,9,voidop,ioapic_asm_ack,voidop,voidop,voidop)
INTRSTUB(ioapic_edge,10,voidop,ioapic_asm_ack,voidop,voidop,voidop)
INTRSTUB(ioapic_edge,11,voidop,ioapic_asm_ack,voidop,voidop,voidop)
INTRSTUB(ioapic_edge,12,voidop,ioapic_asm_ack,voidop,voidop,voidop)
INTRSTUB(ioapic_edge,13,voidop,ioapic_asm_ack,voidop,voidop,voidop)
INTRSTUB(ioapic_edge,14,voidop,ioapic_asm_ack,voidop,voidop,voidop)
INTRSTUB(ioapic_edge,15,voidop,ioapic_asm_ack,voidop,voidop,voidop)
INTRSTUB(ioapic_edge,16,voidop,ioapic_asm_ack,voidop,voidop,voidop)
INTRSTUB(ioapic_edge,17,voidop,ioapic_asm_ack,voidop,voidop,voidop)
INTRSTUB(ioapic_edge,18,voidop,ioapic_asm_ack,voidop,voidop,voidop)
INTRSTUB(ioapic_edge,19,voidop,ioapic_asm_ack,voidop,voidop,voidop)
INTRSTUB(ioapic_edge,20,voidop,ioapic_asm_ack,voidop,voidop,voidop)
INTRSTUB(ioapic_edge,21,voidop,ioapic_asm_ack,voidop,voidop,voidop)
INTRSTUB(ioapic_edge,22,voidop,ioapic_asm_ack,voidop,voidop,voidop)
INTRSTUB(ioapic_edge,23,voidop,ioapic_asm_ack,voidop,voidop,voidop)
INTRSTUB(ioapic_edge,24,voidop,ioapic_asm_ack,voidop,voidop,voidop)
INTRSTUB(ioapic_edge,25,voidop,ioapic_asm_ack,voidop,voidop,voidop)
INTRSTUB(ioapic_edge,26,voidop,ioapic_asm_ack,voidop,voidop,voidop)
INTRSTUB(ioapic_edge,27,voidop,ioapic_asm_ack,voidop,voidop,voidop)
INTRSTUB(ioapic_edge,28,voidop,ioapic_asm_ack,voidop,voidop,voidop)
INTRSTUB(ioapic_edge,29,voidop,ioapic_asm_ack,voidop,voidop,voidop)
INTRSTUB(ioapic_edge,30,voidop,ioapic_asm_ack,voidop,voidop,voidop)
INTRSTUB(ioapic_edge,31,voidop,ioapic_asm_ack,voidop,voidop,voidop)
INTRSTUB(ioapic_edge,32,voidop,ioapic_asm_ack,voidop,voidop,voidop)
INTRSTUB(ioapic_edge,33,voidop,ioapic_asm_ack,voidop,voidop,voidop)
INTRSTUB(ioapic_edge,34,voidop,ioapic_asm_ack,voidop,voidop,voidop)
INTRSTUB(ioapic_edge,35,voidop,ioapic_asm_ack,voidop,voidop,voidop)
INTRSTUB(ioapic_edge,36,voidop,ioapic_asm_ack,voidop,voidop,voidop)
INTRSTUB(ioapic_edge,37,voidop,ioapic_asm_ack,voidop,voidop,voidop)
INTRSTUB(ioapic_edge,38,voidop,ioapic_asm_ack,voidop,voidop,voidop)
INTRSTUB(ioapic_edge,39,voidop,ioapic_asm_ack,voidop,voidop,voidop)
INTRSTUB(ioapic_edge,40,voidop,ioapic_asm_ack,voidop,voidop,voidop)
INTRSTUB(ioapic_edge,41,voidop,ioapic_asm_ack,voidop,voidop,voidop)
INTRSTUB(ioapic_edge,42,voidop,ioapic_asm_ack,voidop,voidop,voidop)
INTRSTUB(ioapic_edge,43,voidop,ioapic_asm_ack,voidop,voidop,voidop)
INTRSTUB(ioapic_edge,44,voidop,ioapic_asm_ack,voidop,voidop,voidop)
INTRSTUB(ioapic_edge,45,voidop,ioapic_asm_ack,voidop,voidop,voidop)
INTRSTUB(ioapic_edge,46,voidop,ioapic_asm_ack,voidop,voidop,voidop)
INTRSTUB(ioapic_edge,47,voidop,ioapic_asm_ack,voidop,voidop,voidop)
INTRSTUB(ioapic_edge,48,voidop,ioapic_asm_ack,voidop,voidop,voidop)
INTRSTUB(ioapic_edge,49,voidop,ioapic_asm_ack,voidop,voidop,voidop)
INTRSTUB(ioapic_edge,50,voidop,ioapic_asm_ack,voidop,voidop,voidop)
INTRSTUB(ioapic_edge,51,voidop,ioapic_asm_ack,voidop,voidop,voidop)
INTRSTUB(ioapic_edge,52,voidop,ioapic_asm_ack,voidop,voidop,voidop)
INTRSTUB(ioapic_edge,53,voidop,ioapic_asm_ack,voidop,voidop,voidop)
INTRSTUB(ioapic_edge,54,voidop,ioapic_asm_ack,voidop,voidop,voidop)
INTRSTUB(ioapic_edge,55,voidop,ioapic_asm_ack,voidop,voidop,voidop)
INTRSTUB(ioapic_edge,56,voidop,ioapic_asm_ack,voidop,voidop,voidop)
INTRSTUB(ioapic_edge,57,voidop,ioapic_asm_ack,voidop,voidop,voidop)
INTRSTUB(ioapic_edge,58,voidop,ioapic_asm_ack,voidop,voidop,voidop)
INTRSTUB(ioapic_edge,59,voidop,ioapic_asm_ack,voidop,voidop,voidop)
INTRSTUB(ioapic_edge,60,voidop,ioapic_asm_ack,voidop,voidop,voidop)
INTRSTUB(ioapic_edge,61,voidop,ioapic_asm_ack,voidop,voidop,voidop)
INTRSTUB(ioapic_edge,62,voidop,ioapic_asm_ack,voidop,voidop,voidop)
INTRSTUB(ioapic_edge,63,voidop,ioapic_asm_ack,voidop,voidop,voidop)

INTRSTUB(ioapic_level,0,voidop,ioapic_asm_ack,voidop,ioapic_unmask,ioapic_mask)
INTRSTUB(ioapic_level,1,voidop,ioapic_asm_ack,voidop,ioapic_unmask,ioapic_mask)
INTRSTUB(ioapic_level,2,voidop,ioapic_asm_ack,voidop,ioapic_unmask,ioapic_mask)
INTRSTUB(ioapic_level,3,voidop,ioapic_asm_ack,voidop,ioapic_unmask,ioapic_mask)
INTRSTUB(ioapic_level,4,voidop,ioapic_asm_ack,voidop,ioapic_unmask,ioapic_mask)
INTRSTUB(ioapic_level,5,voidop,ioapic_asm_ack,voidop,ioapic_unmask,ioapic_mask)
INTRSTUB(ioapic_level,6,voidop,ioapic_asm_ack,voidop,ioapic_unmask,ioapic_mask)
INTRSTUB(ioapic_level,7,voidop,ioapic_asm_ack,voidop,ioapic_unmask,ioapic_mask)
INTRSTUB(ioapic_level,8,voidop,ioapic_asm_ack,voidop,ioapic_unmask,ioapic_mask)
INTRSTUB(ioapic_level,9,voidop,ioapic_asm_ack,voidop,ioapic_unmask,ioapic_mask)
INTRSTUB(ioapic_level,10,voidop,ioapic_asm_ack,voidop,ioapic_unmask,ioapic_mask)
INTRSTUB(ioapic_level,11,voidop,ioapic_asm_ack,voidop,ioapic_unmask,ioapic_mask)
INTRSTUB(ioapic_level,12,voidop,ioapic_asm_ack,voidop,ioapic_unmask,ioapic_mask)
INTRSTUB(ioapic_level,13,voidop,ioapic_asm_ack,voidop,ioapic_unmask,ioapic_mask)
INTRSTUB(ioapic_level,14,voidop,ioapic_asm_ack,voidop,ioapic_unmask,ioapic_mask)
INTRSTUB(ioapic_level,15,voidop,ioapic_asm_ack,voidop,ioapic_unmask,ioapic_mask)
INTRSTUB(ioapic_level,16,voidop,ioapic_asm_ack,voidop,ioapic_unmask,ioapic_mask)
INTRSTUB(ioapic_level,17,voidop,ioapic_asm_ack,voidop,ioapic_unmask,ioapic_mask)
INTRSTUB(ioapic_level,18,voidop,ioapic_asm_ack,voidop,ioapic_unmask,ioapic_mask)
INTRSTUB(ioapic_level,19,voidop,ioapic_asm_ack,voidop,ioapic_unmask,ioapic_mask)
INTRSTUB(ioapic_level,20,voidop,ioapic_asm_ack,voidop,ioapic_unmask,ioapic_mask)
INTRSTUB(ioapic_level,21,voidop,ioapic_asm_ack,voidop,ioapic_unmask,ioapic_mask)
INTRSTUB(ioapic_level,22,voidop,ioapic_asm_ack,voidop,ioapic_unmask,ioapic_mask)
INTRSTUB(ioapic_level,23,voidop,ioapic_asm_ack,voidop,ioapic_unmask,ioapic_mask)
INTRSTUB(ioapic_level,24,voidop,ioapic_asm_ack,voidop,ioapic_unmask,ioapic_mask)
INTRSTUB(ioapic_level,25,voidop,ioapic_asm_ack,voidop,ioapic_unmask,ioapic_mask)
INTRSTUB(ioapic_level,26,voidop,ioapic_asm_ack,voidop,ioapic_unmask,ioapic_mask)
INTRSTUB(ioapic_level,27,voidop,ioapic_asm_ack,voidop,ioapic_unmask,ioapic_mask)
INTRSTUB(ioapic_level,28,voidop,ioapic_asm_ack,voidop,ioapic_unmask,ioapic_mask)
INTRSTUB(ioapic_level,29,voidop,ioapic_asm_ack,voidop,ioapic_unmask,ioapic_mask)
INTRSTUB(ioapic_level,30,voidop,ioapic_asm_ack,voidop,ioapic_unmask,ioapic_mask)
INTRSTUB(ioapic_level,31,voidop,ioapic_asm_ack,voidop,ioapic_unmask,ioapic_mask)
INTRSTUB(ioapic_level,32,voidop,ioapic_asm_ack,voidop,ioapic_unmask,ioapic_mask)
INTRSTUB(ioapic_level,33,voidop,ioapic_asm_ack,voidop,ioapic_unmask,ioapic_mask)
INTRSTUB(ioapic_level,34,voidop,ioapic_asm_ack,voidop,ioapic_unmask,ioapic_mask)
INTRSTUB(ioapic_level,35,voidop,ioapic_asm_ack,voidop,ioapic_unmask,ioapic_mask)
INTRSTUB(ioapic_level,36,voidop,ioapic_asm_ack,voidop,ioapic_unmask,ioapic_mask)
INTRSTUB(ioapic_level,37,voidop,ioapic_asm_ack,voidop,ioapic_unmask,ioapic_mask)
INTRSTUB(ioapic_level,38,voidop,ioapic_asm_ack,voidop,ioapic_unmask,ioapic_mask)
INTRSTUB(ioapic_level,39,voidop,ioapic_asm_ack,voidop,ioapic_unmask,ioapic_mask)
INTRSTUB(ioapic_level,40,voidop,ioapic_asm_ack,voidop,ioapic_unmask,ioapic_mask)
INTRSTUB(ioapic_level,41,voidop,ioapic_asm_ack,voidop,ioapic_unmask,ioapic_mask)
INTRSTUB(ioapic_level,42,voidop,ioapic_asm_ack,voidop,ioapic_unmask,ioapic_mask)
INTRSTUB(ioapic_level,43,voidop,ioapic_asm_ack,voidop,ioapic_unmask,ioapic_mask)
INTRSTUB(ioapic_level,44,voidop,ioapic_asm_ack,voidop,ioapic_unmask,ioapic_mask)
INTRSTUB(ioapic_level,45,voidop,ioapic_asm_ack,voidop,ioapic_unmask,ioapic_mask)
INTRSTUB(ioapic_level,46,voidop,ioapic_asm_ack,voidop,ioapic_unmask,ioapic_mask)
INTRSTUB(ioapic_level,47,voidop,ioapic_asm_ack,voidop,ioapic_unmask,ioapic_mask)
INTRSTUB(ioapic_level,48,voidop,ioapic_asm_ack,voidop,ioapic_unmask,ioapic_mask)
INTRSTUB(ioapic_level,49,voidop,ioapic_asm_ack,voidop,ioapic_unmask,ioapic_mask)
INTRSTUB(ioapic_level,50,voidop,ioapic_asm_ack,voidop,ioapic_unmask,ioapic_mask)
INTRSTUB(ioapic_level,51,voidop,ioapic_asm_ack,voidop,ioapic_unmask,ioapic_mask)
INTRSTUB(ioapic_level,52,voidop,ioapic_asm_ack,voidop,ioapic_unmask,ioapic_mask)
INTRSTUB(ioapic_level,53,voidop,ioapic_asm_ack,voidop,ioapic_unmask,ioapic_mask)
INTRSTUB(ioapic_level,54,voidop,ioapic_asm_ack,voidop,ioapic_unmask,ioapic_mask)
INTRSTUB(ioapic_level,55,voidop,ioapic_asm_ack,voidop,ioapic_unmask,ioapic_mask)
INTRSTUB(ioapic_level,56,voidop,ioapic_asm_ack,voidop,ioapic_unmask,ioapic_mask)
INTRSTUB(ioapic_level,57,voidop,ioapic_asm_ack,voidop,ioapic_unmask,ioapic_mask)
INTRSTUB(ioapic_level,58,voidop,ioapic_asm_ack,voidop,ioapic_unmask,ioapic_mask)
INTRSTUB(ioapic_level,59,voidop,ioapic_asm_ack,voidop,ioapic_unmask,ioapic_mask)
INTRSTUB(ioapic_level,60,voidop,ioapic_asm_ack,voidop,ioapic_unmask,ioapic_mask)
INTRSTUB(ioapic_level,61,voidop,ioapic_asm_ack,voidop,ioapic_unmask,ioapic_mask)
INTRSTUB(ioapic_level,62,voidop,ioapic_asm_ack,voidop,ioapic_unmask,ioapic_mask)
INTRSTUB(ioapic_level,63,voidop,ioapic_asm_ack,voidop,ioapic_unmask,ioapic_mask)

#endif

	.section .rodata

	.globl i8259_stubs
	.type i8259_stubs,@object
i8259_stubs:
	.quad Xintr_legacy0, Xrecurse_legacy0, Xresume_legacy0
	.quad Xintr_legacy1, Xrecurse_legacy1, Xresume_legacy1
	.quad Xintr_legacy2, Xrecurse_legacy2, Xresume_legacy2
	.quad Xintr_legacy3, Xrecurse_legacy3, Xresume_legacy3
	.quad Xintr_legacy4, Xrecurse_legacy4, Xresume_legacy4
	.quad Xintr_legacy5, Xrecurse_legacy5, Xresume_legacy5
	.quad Xintr_legacy6, Xrecurse_legacy6, Xresume_legacy6
	.quad Xintr_legacy7, Xrecurse_legacy7, Xresume_legacy7
	.quad Xintr_legacy8, Xrecurse_legacy8, Xresume_legacy8
	.quad Xintr_legacy9, Xrecurse_legacy9, Xresume_legacy9
	.quad Xintr_legacy10, Xrecurse_legacy10, Xresume_legacy10
	.quad Xintr_legacy11, Xrecurse_legacy11, Xresume_legacy11
	.quad Xintr_legacy12, Xrecurse_legacy12, Xresume_legacy12
	.quad Xintr_legacy13, Xrecurse_legacy13, Xresume_legacy13
	.quad Xintr_legacy14, Xrecurse_legacy14, Xresume_legacy14
	.quad Xintr_legacy15, Xrecurse_legacy15, Xresume_legacy15
END(i8259_stubs)

#if NIOAPIC > 0
	.globl ioapic_edge_stubs
	.type ioapic_edge_stubs,@object
ioapic_edge_stubs:
	.quad Xintr_ioapic_edge0, Xrecurse_ioapic_edge0, Xresume_ioapic_edge0
	.quad Xintr_ioapic_edge1, Xrecurse_ioapic_edge1, Xresume_ioapic_edge1
	.quad Xintr_ioapic_edge2, Xrecurse_ioapic_edge2, Xresume_ioapic_edge2
	.quad Xintr_ioapic_edge3, Xrecurse_ioapic_edge3, Xresume_ioapic_edge3
	.quad Xintr_ioapic_edge4, Xrecurse_ioapic_edge4, Xresume_ioapic_edge4
	.quad Xintr_ioapic_edge5, Xrecurse_ioapic_edge5, Xresume_ioapic_edge5
	.quad Xintr_ioapic_edge6, Xrecurse_ioapic_edge6, Xresume_ioapic_edge6
	.quad Xintr_ioapic_edge7, Xrecurse_ioapic_edge7, Xresume_ioapic_edge7
	.quad Xintr_ioapic_edge8, Xrecurse_ioapic_edge8, Xresume_ioapic_edge8
	.quad Xintr_ioapic_edge9, Xrecurse_ioapic_edge9, Xresume_ioapic_edge9
	.quad Xintr_ioapic_edge10, Xrecurse_ioapic_edge10, Xresume_ioapic_edge10
	.quad Xintr_ioapic_edge11, Xrecurse_ioapic_edge11, Xresume_ioapic_edge11
	.quad Xintr_ioapic_edge12, Xrecurse_ioapic_edge12, Xresume_ioapic_edge12
	.quad Xintr_ioapic_edge13, Xrecurse_ioapic_edge13, Xresume_ioapic_edge13
	.quad Xintr_ioapic_edge14, Xrecurse_ioapic_edge14, Xresume_ioapic_edge14
	.quad Xintr_ioapic_edge15, Xrecurse_ioapic_edge15, Xresume_ioapic_edge15
	.quad Xintr_ioapic_edge16, Xrecurse_ioapic_edge16, Xresume_ioapic_edge16
	.quad Xintr_ioapic_edge17, Xrecurse_ioapic_edge17, Xresume_ioapic_edge17
	.quad Xintr_ioapic_edge18, Xrecurse_ioapic_edge18, Xresume_ioapic_edge18
	.quad Xintr_ioapic_edge19, Xrecurse_ioapic_edge19, Xresume_ioapic_edge19
	.quad Xintr_ioapic_edge20, Xrecurse_ioapic_edge20, Xresume_ioapic_edge20
	.quad Xintr_ioapic_edge21, Xrecurse_ioapic_edge21, Xresume_ioapic_edge21
	.quad Xintr_ioapic_edge22, Xrecurse_ioapic_edge22, Xresume_ioapic_edge22
	.quad Xintr_ioapic_edge23, Xrecurse_ioapic_edge23, Xresume_ioapic_edge23
	.quad Xintr_ioapic_edge24, Xrecurse_ioapic_edge24, Xresume_ioapic_edge24
	.quad Xintr_ioapic_edge25, Xrecurse_ioapic_edge25, Xresume_ioapic_edge25
	.quad Xintr_ioapic_edge26, Xrecurse_ioapic_edge26, Xresume_ioapic_edge26
	.quad Xintr_ioapic_edge27, Xrecurse_ioapic_edge27, Xresume_ioapic_edge27
	.quad Xintr_ioapic_edge28, Xrecurse_ioapic_edge28, Xresume_ioapic_edge28
	.quad Xintr_ioapic_edge29, Xrecurse_ioapic_edge29, Xresume_ioapic_edge29
	.quad Xintr_ioapic_edge30, Xrecurse_ioapic_edge30, Xresume_ioapic_edge30
	.quad Xintr_ioapic_edge31, Xrecurse_ioapic_edge31, Xresume_ioapic_edge31
	.quad Xintr_ioapic_edge32, Xrecurse_ioapic_edge32, Xresume_ioapic_edge32
	.quad Xintr_ioapic_edge33, Xrecurse_ioapic_edge33, Xresume_ioapic_edge33
	.quad Xintr_ioapic_edge34, Xrecurse_ioapic_edge34, Xresume_ioapic_edge34
	.quad Xintr_ioapic_edge35, Xrecurse_ioapic_edge35, Xresume_ioapic_edge35
	.quad Xintr_ioapic_edge36, Xrecurse_ioapic_edge36, Xresume_ioapic_edge36
	.quad Xintr_ioapic_edge37, Xrecurse_ioapic_edge37, Xresume_ioapic_edge37
	.quad Xintr_ioapic_edge38, Xrecurse_ioapic_edge38, Xresume_ioapic_edge38
	.quad Xintr_ioapic_edge39, Xrecurse_ioapic_edge39, Xresume_ioapic_edge39
	.quad Xintr_ioapic_edge40, Xrecurse_ioapic_edge40, Xresume_ioapic_edge40
	.quad Xintr_ioapic_edge41, Xrecurse_ioapic_edge41, Xresume_ioapic_edge41
	.quad Xintr_ioapic_edge42, Xrecurse_ioapic_edge42, Xresume_ioapic_edge42
	.quad Xintr_ioapic_edge43, Xrecurse_ioapic_edge43, Xresume_ioapic_edge43
	.quad Xintr_ioapic_edge44, Xrecurse_ioapic_edge44, Xresume_ioapic_edge44
	.quad Xintr_ioapic_edge45, Xrecurse_ioapic_edge45, Xresume_ioapic_edge45
	.quad Xintr_ioapic_edge46, Xrecurse_ioapic_edge46, Xresume_ioapic_edge46
	.quad Xintr_ioapic_edge47, Xrecurse_ioapic_edge47, Xresume_ioapic_edge47
	.quad Xintr_ioapic_edge48, Xrecurse_ioapic_edge48, Xresume_ioapic_edge48
	.quad Xintr_ioapic_edge49, Xrecurse_ioapic_edge49, Xresume_ioapic_edge49
	.quad Xintr_ioapic_edge50, Xrecurse_ioapic_edge50, Xresume_ioapic_edge50
	.quad Xintr_ioapic_edge51, Xrecurse_ioapic_edge51, Xresume_ioapic_edge51
	.quad Xintr_ioapic_edge52, Xrecurse_ioapic_edge52, Xresume_ioapic_edge52
	.quad Xintr_ioapic_edge53, Xrecurse_ioapic_edge53, Xresume_ioapic_edge53
	.quad Xintr_ioapic_edge54, Xrecurse_ioapic_edge54, Xresume_ioapic_edge54
	.quad Xintr_ioapic_edge55, Xrecurse_ioapic_edge55, Xresume_ioapic_edge55
	.quad Xintr_ioapic_edge56, Xrecurse_ioapic_edge56, Xresume_ioapic_edge56
	.quad Xintr_ioapic_edge57, Xrecurse_ioapic_edge57, Xresume_ioapic_edge57
	.quad Xintr_ioapic_edge58, Xrecurse_ioapic_edge58, Xresume_ioapic_edge58
	.quad Xintr_ioapic_edge59, Xrecurse_ioapic_edge59, Xresume_ioapic_edge59
	.quad Xintr_ioapic_edge60, Xrecurse_ioapic_edge60, Xresume_ioapic_edge60
	.quad Xintr_ioapic_edge61, Xrecurse_ioapic_edge61, Xresume_ioapic_edge61
	.quad Xintr_ioapic_edge62, Xrecurse_ioapic_edge62, Xresume_ioapic_edge62
	.quad Xintr_ioapic_edge63, Xrecurse_ioapic_edge63, Xresume_ioapic_edge63
END(ioapic_edge_stubs)

	.globl ioapic_level_stubs
	.type ioapic_level_stubs,@object
ioapic_level_stubs:
	.quad Xintr_ioapic_level0, Xrecurse_ioapic_level0, Xresume_ioapic_level0
	.quad Xintr_ioapic_level1, Xrecurse_ioapic_level1, Xresume_ioapic_level1
	.quad Xintr_ioapic_level2, Xrecurse_ioapic_level2, Xresume_ioapic_level2
	.quad Xintr_ioapic_level3, Xrecurse_ioapic_level3, Xresume_ioapic_level3
	.quad Xintr_ioapic_level4, Xrecurse_ioapic_level4, Xresume_ioapic_level4
	.quad Xintr_ioapic_level5, Xrecurse_ioapic_level5, Xresume_ioapic_level5
	.quad Xintr_ioapic_level6, Xrecurse_ioapic_level6, Xresume_ioapic_level6
	.quad Xintr_ioapic_level7, Xrecurse_ioapic_level7, Xresume_ioapic_level7
	.quad Xintr_ioapic_level8, Xrecurse_ioapic_level8, Xresume_ioapic_level8
	.quad Xintr_ioapic_level9, Xrecurse_ioapic_level9, Xresume_ioapic_level9
	.quad Xintr_ioapic_level10, Xrecurse_ioapic_level10, Xresume_ioapic_level10
	.quad Xintr_ioapic_level11, Xrecurse_ioapic_level11, Xresume_ioapic_level11
	.quad Xintr_ioapic_level12, Xrecurse_ioapic_level12, Xresume_ioapic_level12
	.quad Xintr_ioapic_level13, Xrecurse_ioapic_level13, Xresume_ioapic_level13
	.quad Xintr_ioapic_level14, Xrecurse_ioapic_level14, Xresume_ioapic_level14
	.quad Xintr_ioapic_level15, Xrecurse_ioapic_level15, Xresume_ioapic_level15
	.quad Xintr_ioapic_level16, Xrecurse_ioapic_level16, Xresume_ioapic_level16
	.quad Xintr_ioapic_level17, Xrecurse_ioapic_level17, Xresume_ioapic_level17
	.quad Xintr_ioapic_level18, Xrecurse_ioapic_level18, Xresume_ioapic_level18
	.quad Xintr_ioapic_level19, Xrecurse_ioapic_level19, Xresume_ioapic_level19
	.quad Xintr_ioapic_level20, Xrecurse_ioapic_level20, Xresume_ioapic_level20
	.quad Xintr_ioapic_level21, Xrecurse_ioapic_level21, Xresume_ioapic_level21
	.quad Xintr_ioapic_level22, Xrecurse_ioapic_level22, Xresume_ioapic_level22
	.quad Xintr_ioapic_level23, Xrecurse_ioapic_level23, Xresume_ioapic_level23
	.quad Xintr_ioapic_level24, Xrecurse_ioapic_level24, Xresume_ioapic_level24
	.quad Xintr_ioapic_level25, Xrecurse_ioapic_level25, Xresume_ioapic_level25
	.quad Xintr_ioapic_level26, Xrecurse_ioapic_level26, Xresume_ioapic_level26
	.quad Xintr_ioapic_level27, Xrecurse_ioapic_level27, Xresume_ioapic_level27
	.quad Xintr_ioapic_level28, Xrecurse_ioapic_level28, Xresume_ioapic_level28
	.quad Xintr_ioapic_level29, Xrecurse_ioapic_level29, Xresume_ioapic_level29
	.quad Xintr_ioapic_level30, Xrecurse_ioapic_level30, Xresume_ioapic_level30
	.quad Xintr_ioapic_level31, Xrecurse_ioapic_level31, Xresume_ioapic_level31
	.quad Xintr_ioapic_level32, Xrecurse_ioapic_level32, Xresume_ioapic_level32
	.quad Xintr_ioapic_level33, Xrecurse_ioapic_level33, Xresume_ioapic_level33
	.quad Xintr_ioapic_level34, Xrecurse_ioapic_level34, Xresume_ioapic_level34
	.quad Xintr_ioapic_level35, Xrecurse_ioapic_level35, Xresume_ioapic_level35
	.quad Xintr_ioapic_level36, Xrecurse_ioapic_level36, Xresume_ioapic_level36
	.quad Xintr_ioapic_level37, Xrecurse_ioapic_level37, Xresume_ioapic_level37
	.quad Xintr_ioapic_level38, Xrecurse_ioapic_level38, Xresume_ioapic_level38
	.quad Xintr_ioapic_level39, Xrecurse_ioapic_level39, Xresume_ioapic_level39
	.quad Xintr_ioapic_level40, Xrecurse_ioapic_level40, Xresume_ioapic_level40
	.quad Xintr_ioapic_level41, Xrecurse_ioapic_level41, Xresume_ioapic_level41
	.quad Xintr_ioapic_level42, Xrecurse_ioapic_level42, Xresume_ioapic_level42
	.quad Xintr_ioapic_level43, Xrecurse_ioapic_level43, Xresume_ioapic_level43
	.quad Xintr_ioapic_level44, Xrecurse_ioapic_level44, Xresume_ioapic_level44
	.quad Xintr_ioapic_level45, Xrecurse_ioapic_level45, Xresume_ioapic_level45
	.quad Xintr_ioapic_level46, Xrecurse_ioapic_level46, Xresume_ioapic_level46
	.quad Xintr_ioapic_level47, Xrecurse_ioapic_level47, Xresume_ioapic_level47
	.quad Xintr_ioapic_level48, Xrecurse_ioapic_level48, Xresume_ioapic_level48
	.quad Xintr_ioapic_level49, Xrecurse_ioapic_level49, Xresume_ioapic_level49
	.quad Xintr_ioapic_level50, Xrecurse_ioapic_level50, Xresume_ioapic_level50
	.quad Xintr_ioapic_level51, Xrecurse_ioapic_level51, Xresume_ioapic_level51
	.quad Xintr_ioapic_level52, Xrecurse_ioapic_level52, Xresume_ioapic_level52
	.quad Xintr_ioapic_level53, Xrecurse_ioapic_level53, Xresume_ioapic_level53
	.quad Xintr_ioapic_level54, Xrecurse_ioapic_level54, Xresume_ioapic_level54
	.quad Xintr_ioapic_level55, Xrecurse_ioapic_level55, Xresume_ioapic_level55
	.quad Xintr_ioapic_level56, Xrecurse_ioapic_level56, Xresume_ioapic_level56
	.quad Xintr_ioapic_level57, Xrecurse_ioapic_level57, Xresume_ioapic_level57
	.quad Xintr_ioapic_level58, Xrecurse_ioapic_level58, Xresume_ioapic_level58
	.quad Xintr_ioapic_level59, Xrecurse_ioapic_level59, Xresume_ioapic_level59
	.quad Xintr_ioapic_level60, Xrecurse_ioapic_level60, Xresume_ioapic_level60
	.quad Xintr_ioapic_level61, Xrecurse_ioapic_level61, Xresume_ioapic_level61
	.quad Xintr_ioapic_level62, Xrecurse_ioapic_level62, Xresume_ioapic_level62
	.quad Xintr_ioapic_level63, Xrecurse_ioapic_level63, Xresume_ioapic_level63
END(ioapic_level_stubs)
#endif

/*
 * Soft interrupt handlers
 */
NENTRY(retpoline_r13)
	CODEPATCH_START
	JMP_RETPOLINE(r13)
	CODEPATCH_END(CPTAG_RETPOLINE_R13)
END(retpoline_r13)

KIDTVEC(softtty)
	endbr64
	movl	$IPL_SOFTTTY, CPUVAR(ILEVEL)
	sti
	incl	CPUVAR(IDEPTH)
	movl	$SOFTINTR_TTY,%edi
	call	dosoftint
	movq	$0,-8(%rsp)
	decl	CPUVAR(IDEPTH)
	CODEPATCH_START
	jmp	retpoline_r13
	CODEPATCH_END(CPTAG_RETPOLINE_R13)
END(Xsofttty)

KIDTVEC(softnet)
	endbr64
	movl	$IPL_SOFTNET, CPUVAR(ILEVEL)
	sti
	incl	CPUVAR(IDEPTH)
	movl	$SOFTINTR_NET,%edi
	call	dosoftint
	movq	$0,-8(%rsp)
	decl	CPUVAR(IDEPTH)
	CODEPATCH_START
	jmp	retpoline_r13
	CODEPATCH_END(CPTAG_RETPOLINE_R13)
END(Xsoftnet)

KIDTVEC(softclock)
	endbr64
	movl	$IPL_SOFTCLOCK, CPUVAR(ILEVEL)
	sti
	incl	CPUVAR(IDEPTH)
	movl	$SOFTINTR_CLOCK,%edi
	call	dosoftint
	movq	$0,-8(%rsp)
	decl	CPUVAR(IDEPTH)
	CODEPATCH_START
	jmp	retpoline_r13
	CODEPATCH_END(CPTAG_RETPOLINE_R13)
END(Xsoftclock)
