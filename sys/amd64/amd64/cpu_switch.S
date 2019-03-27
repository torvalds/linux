/*-
 * Copyright (c) 2003 Peter Wemm.
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
 * $FreeBSD$
 */

#include <machine/asmacros.h>
#include <machine/specialreg.h>

#include "assym.inc"
#include "opt_sched.h"

/*****************************************************************************/
/* Scheduling                                                                */
/*****************************************************************************/

	.text

#ifdef SMP
#define LK	lock ;
#else
#define LK
#endif

#if defined(SCHED_ULE) && defined(SMP)
#define	SETLK	xchgq
#else
#define	SETLK	movq
#endif

/*
 * cpu_throw()
 *
 * This is the second half of cpu_switch(). It is used when the current
 * thread is either a dummy or slated to die, and we no longer care
 * about its state.  This is only a slight optimization and is probably
 * not worth it anymore.  Note that we need to clear the pm_active bits so
 * we do need the old proc if it still exists.
 * %rdi = oldtd
 * %rsi = newtd
 */
ENTRY(cpu_throw)
	movq	%rsi,%r12
	movq	%rsi,%rdi
	call	pmap_activate_sw
	jmp	sw1
END(cpu_throw)

/*
 * cpu_switch(old, new, mtx)
 *
 * Save the current thread state, then select the next thread to run
 * and load its state.
 * %rdi = oldtd
 * %rsi = newtd
 * %rdx = mtx
 */
ENTRY(cpu_switch)
	/* Switch to new thread.  First, save context. */
	movq	TD_PCB(%rdi),%r8

	movq	(%rsp),%rax			/* Hardware registers */
	movq	%r15,PCB_R15(%r8)
	movq	%r14,PCB_R14(%r8)
	movq	%r13,PCB_R13(%r8)
	movq	%r12,PCB_R12(%r8)
	movq	%rbp,PCB_RBP(%r8)
	movq	%rsp,PCB_RSP(%r8)
	movq	%rbx,PCB_RBX(%r8)
	movq	%rax,PCB_RIP(%r8)

	testl	$PCB_FULL_IRET,PCB_FLAGS(%r8)
	jnz	2f
	orl	$PCB_FULL_IRET,PCB_FLAGS(%r8)
	testl	$TDP_KTHREAD,TD_PFLAGS(%rdi)
	jnz	2f
	testb	$CPUID_STDEXT_FSGSBASE,cpu_stdext_feature(%rip)
	jz	2f
	movl	%fs,%eax
	cmpl	$KUF32SEL,%eax
	jne	1f
	rdfsbase %rax
	movq	%rax,PCB_FSBASE(%r8)
1:	movl	%gs,%eax
	cmpl	$KUG32SEL,%eax
	jne	2f
	movq	%rdx,%r12
	movl	$MSR_KGSBASE,%ecx		/* Read user gs base */
	rdmsr
	shlq	$32,%rdx
	orq	%rdx,%rax
	movq	%rax,PCB_GSBASE(%r8)
	movq	%r12,%rdx

2:
	testl	$PCB_DBREGS,PCB_FLAGS(%r8)
	jnz	store_dr			/* static predict not taken */
done_store_dr:

	/* have we used fp, and need a save? */
	cmpq	%rdi,PCPU(FPCURTHREAD)
	jne	2f
	movq	PCB_SAVEFPU(%r8),%r8
	clts
	cmpl	$0,use_xsave(%rip)
	jne	1f
	fxsave	(%r8)
	jmp	2f
1:	movq	%rdx,%rcx
	movl	xsave_mask,%eax
	movl	xsave_mask+4,%edx
	.globl	ctx_switch_xsave
ctx_switch_xsave:
	/* This is patched to xsaveopt if supported, see fpuinit_bsp1() */
	xsave	(%r8)
	movq	%rcx,%rdx
2:
	/* Save is done.  Now fire up new thread. Leave old vmspace. */
	movq	%rsi,%r12
	movq	%rdi,%r13
	movq	%rdx,%r15
	movq	%rsi,%rdi
	callq	pmap_activate_sw
	SETLK	%r15,TD_LOCK(%r13)		/* Release the old thread */
sw1:
	movq	TD_PCB(%r12),%r8
#if defined(SCHED_ULE) && defined(SMP)
	/* Wait for the new thread to become unblocked */
	movq	$blocked_lock, %rdx
1:
	movq	TD_LOCK(%r12),%rcx
	cmpq	%rcx, %rdx
	pause
	je	1b
#endif
	/*
	 * At this point, we've switched address spaces and are ready
	 * to load up the rest of the next context.
	 */

	/* Skip loading LDT and user fsbase/gsbase for kthreads */
	testl	$TDP_KTHREAD,TD_PFLAGS(%r12)
	jnz	do_kthread

	/*
	 * Load ldt register
	 */
	movq	TD_PROC(%r12),%rcx
	cmpq	$0, P_MD+MD_LDT(%rcx)
	jne	do_ldt
	xorl	%eax,%eax
ld_ldt:	lldt	%ax

	/* Restore fs base in GDT */
	movl	PCB_FSBASE(%r8),%eax
	movq	PCPU(FS32P),%rdx
	movw	%ax,2(%rdx)
	shrl	$16,%eax
	movb	%al,4(%rdx)
	shrl	$8,%eax
	movb	%al,7(%rdx)

	/* Restore gs base in GDT */
	movl	PCB_GSBASE(%r8),%eax
	movq	PCPU(GS32P),%rdx
	movw	%ax,2(%rdx)
	shrl	$16,%eax
	movb	%al,4(%rdx)
	shrl	$8,%eax
	movb	%al,7(%rdx)

do_kthread:
	/* Do we need to reload tss ? */
	movq	PCPU(TSSP),%rax
	movq	PCB_TSSP(%r8),%rdx
	testq	%rdx,%rdx
	cmovzq	PCPU(COMMONTSSP),%rdx
	cmpq	%rax,%rdx
	jne	do_tss
done_tss:
	movq	%r8,PCPU(RSP0)
	movq	%r8,PCPU(CURPCB)
	movq	PCPU(PTI_RSP0),%rax
	cmpq	$~0,PCPU(UCR3)
	cmove	%r8,%rax
	movq	%rax,TSS_RSP0(%rdx)
	movq	%r12,PCPU(CURTHREAD)		/* into next thread */

	/* Test if debug registers should be restored. */
	testl	$PCB_DBREGS,PCB_FLAGS(%r8)
	jnz	load_dr				/* static predict not taken */
done_load_dr:

	/* Restore context. */
	movq	PCB_R15(%r8),%r15
	movq	PCB_R14(%r8),%r14
	movq	PCB_R13(%r8),%r13
	movq	PCB_R12(%r8),%r12
	movq	PCB_RBP(%r8),%rbp
	movq	PCB_RSP(%r8),%rsp
	movq	PCB_RBX(%r8),%rbx
	movq	PCB_RIP(%r8),%rax
	movq	%rax,(%rsp)
	movq	PCPU(CURTHREAD),%rdi
	call	fpu_activate_sw
	ret

	/*
	 * We order these strangely for several reasons.
	 * 1: I wanted to use static branch prediction hints
	 * 2: Most athlon64/opteron cpus don't have them.  They define
	 *    a forward branch as 'predict not taken'.  Intel cores have
	 *    the 'rep' prefix to invert this.
	 * So, to make it work on both forms of cpu we do the detour.
	 * We use jumps rather than call in order to avoid the stack.
	 */

store_dr:
	movq	%dr7,%rax			/* yes, do the save */
	movq	%dr0,%r15
	movq	%dr1,%r14
	movq	%dr2,%r13
	movq	%dr3,%r12
	movq	%dr6,%r11
	movq	%r15,PCB_DR0(%r8)
	movq	%r14,PCB_DR1(%r8)
	movq	%r13,PCB_DR2(%r8)
	movq	%r12,PCB_DR3(%r8)
	movq	%r11,PCB_DR6(%r8)
	movq	%rax,PCB_DR7(%r8)
	andq	$0x0000fc00, %rax		/* disable all watchpoints */
	movq	%rax,%dr7
	jmp	done_store_dr

load_dr:
	movq	%dr7,%rax
	movq	PCB_DR0(%r8),%r15
	movq	PCB_DR1(%r8),%r14
	movq	PCB_DR2(%r8),%r13
	movq	PCB_DR3(%r8),%r12
	movq	PCB_DR6(%r8),%r11
	movq	PCB_DR7(%r8),%rcx
	movq	%r15,%dr0
	movq	%r14,%dr1
	/* Preserve reserved bits in %dr7 */
	andq	$0x0000fc00,%rax
	andq	$~0x0000fc00,%rcx
	movq	%r13,%dr2
	movq	%r12,%dr3
	orq	%rcx,%rax
	movq	%r11,%dr6
	movq	%rax,%dr7
	jmp	done_load_dr

do_tss:	movq	%rdx,PCPU(TSSP)
	movq	%rdx,%rcx
	movq	PCPU(TSS),%rax
	movw	%cx,2(%rax)
	shrq	$16,%rcx
	movb	%cl,4(%rax)
	shrq	$8,%rcx
	movb	%cl,7(%rax)
	shrq	$8,%rcx
	movl	%ecx,8(%rax)
	movb	$0x89,5(%rax)	/* unset busy */
	movl	$TSSSEL,%eax
	ltr	%ax
	jmp	done_tss

do_ldt:	movq	PCPU(LDT),%rax
	movq	P_MD+MD_LDT_SD(%rcx),%rdx
	movq	%rdx,(%rax)
	movq	P_MD+MD_LDT_SD+8(%rcx),%rdx
	movq	%rdx,8(%rax)
	movl	$LDTSEL,%eax
	jmp	ld_ldt
END(cpu_switch)

/*
 * savectx(pcb)
 * Update pcb, saving current processor state.
 */
ENTRY(savectx)
	/* Save caller's return address. */
	movq	(%rsp),%rax
	movq	%rax,PCB_RIP(%rdi)

	movq	%rbx,PCB_RBX(%rdi)
	movq	%rsp,PCB_RSP(%rdi)
	movq	%rbp,PCB_RBP(%rdi)
	movq	%r12,PCB_R12(%rdi)
	movq	%r13,PCB_R13(%rdi)
	movq	%r14,PCB_R14(%rdi)
	movq	%r15,PCB_R15(%rdi)

	movq	%cr0,%rax
	movq	%rax,PCB_CR0(%rdi)
	movq	%cr2,%rax
	movq	%rax,PCB_CR2(%rdi)
	movq	%cr3,%rax
	movq	%rax,PCB_CR3(%rdi)
	movq	%cr4,%rax
	movq	%rax,PCB_CR4(%rdi)

	movq	%dr0,%rax
	movq	%rax,PCB_DR0(%rdi)
	movq	%dr1,%rax
	movq	%rax,PCB_DR1(%rdi)
	movq	%dr2,%rax
	movq	%rax,PCB_DR2(%rdi)
	movq	%dr3,%rax
	movq	%rax,PCB_DR3(%rdi)
	movq	%dr6,%rax
	movq	%rax,PCB_DR6(%rdi)
	movq	%dr7,%rax
	movq	%rax,PCB_DR7(%rdi)

	movl	$MSR_FSBASE,%ecx
	rdmsr
	movl	%eax,PCB_FSBASE(%rdi)
	movl	%edx,PCB_FSBASE+4(%rdi)
	movl	$MSR_GSBASE,%ecx
	rdmsr
	movl	%eax,PCB_GSBASE(%rdi)
	movl	%edx,PCB_GSBASE+4(%rdi)
	movl	$MSR_KGSBASE,%ecx
	rdmsr
	movl	%eax,PCB_KGSBASE(%rdi)
	movl	%edx,PCB_KGSBASE+4(%rdi)
	movl	$MSR_EFER,%ecx
	rdmsr
	movl	%eax,PCB_EFER(%rdi)
	movl	%edx,PCB_EFER+4(%rdi)
	movl	$MSR_STAR,%ecx
	rdmsr
	movl	%eax,PCB_STAR(%rdi)
	movl	%edx,PCB_STAR+4(%rdi)
	movl	$MSR_LSTAR,%ecx
	rdmsr
	movl	%eax,PCB_LSTAR(%rdi)
	movl	%edx,PCB_LSTAR+4(%rdi)
	movl	$MSR_CSTAR,%ecx
	rdmsr
	movl	%eax,PCB_CSTAR(%rdi)
	movl	%edx,PCB_CSTAR+4(%rdi)
	movl	$MSR_SF_MASK,%ecx
	rdmsr
	movl	%eax,PCB_SFMASK(%rdi)
	movl	%edx,PCB_SFMASK+4(%rdi)

	sgdt	PCB_GDT(%rdi)
	sidt	PCB_IDT(%rdi)
	sldt	PCB_LDT(%rdi)
	str	PCB_TR(%rdi)

	movl	$1,%eax
	ret
END(savectx)

/*
 * resumectx(pcb)
 * Resuming processor state from pcb.
 */     
ENTRY(resumectx)
	/* Switch to KPML4phys. */
	movq	KPML4phys,%rax
	movq	%rax,%cr3

	/* Force kernel segment registers. */
	movl	$KDSEL,%eax
	movw	%ax,%ds
	movw	%ax,%es
	movw	%ax,%ss
	movl	$KUF32SEL,%eax
	movw	%ax,%fs
	movl	$KUG32SEL,%eax
	movw	%ax,%gs

	movl	$MSR_FSBASE,%ecx
	movl	PCB_FSBASE(%rdi),%eax
	movl	4 + PCB_FSBASE(%rdi),%edx
	wrmsr
	movl	$MSR_GSBASE,%ecx
	movl	PCB_GSBASE(%rdi),%eax
	movl	4 + PCB_GSBASE(%rdi),%edx
	wrmsr
	movl	$MSR_KGSBASE,%ecx
	movl	PCB_KGSBASE(%rdi),%eax
	movl	4 + PCB_KGSBASE(%rdi),%edx
	wrmsr

	/* Restore EFER one more time. */
	movl	$MSR_EFER,%ecx
	movl	PCB_EFER(%rdi),%eax
	wrmsr

	/* Restore fast syscall stuff. */
	movl	$MSR_STAR,%ecx
	movl	PCB_STAR(%rdi),%eax
	movl	4 + PCB_STAR(%rdi),%edx
	wrmsr
	movl	$MSR_LSTAR,%ecx
	movl	PCB_LSTAR(%rdi),%eax
	movl	4 + PCB_LSTAR(%rdi),%edx
	wrmsr
	movl	$MSR_CSTAR,%ecx
	movl	PCB_CSTAR(%rdi),%eax
	movl	4 + PCB_CSTAR(%rdi),%edx
	wrmsr
	movl	$MSR_SF_MASK,%ecx
	movl	PCB_SFMASK(%rdi),%eax
	wrmsr

	/* Restore CR0, CR2, CR4 and CR3. */
	movq	PCB_CR0(%rdi),%rax
	movq	%rax,%cr0
	movq	PCB_CR2(%rdi),%rax
	movq	%rax,%cr2
	movq	PCB_CR4(%rdi),%rax
	movq	%rax,%cr4
	movq	PCB_CR3(%rdi),%rax
	movq	%rax,%cr3

	/* Restore descriptor tables. */
	lidt	PCB_IDT(%rdi)
	lldt	PCB_LDT(%rdi)

#define	SDT_SYSTSS	9
#define	SDT_SYSBSY	11

	/* Clear "task busy" bit and reload TR. */
	movq	PCPU(TSS),%rax
	andb	$(~SDT_SYSBSY | SDT_SYSTSS),5(%rax)
	movw	PCB_TR(%rdi),%ax
	ltr	%ax

#undef	SDT_SYSTSS
#undef	SDT_SYSBSY

	/* Restore debug registers. */
	movq	PCB_DR0(%rdi),%rax
	movq	%rax,%dr0
	movq	PCB_DR1(%rdi),%rax
	movq	%rax,%dr1
	movq	PCB_DR2(%rdi),%rax
	movq	%rax,%dr2
	movq	PCB_DR3(%rdi),%rax
	movq	%rax,%dr3
	movq	PCB_DR6(%rdi),%rax
	movq	%rax,%dr6
	movq	PCB_DR7(%rdi),%rax
	movq	%rax,%dr7

	/* Restore other callee saved registers. */
	movq	PCB_R15(%rdi),%r15
	movq	PCB_R14(%rdi),%r14
	movq	PCB_R13(%rdi),%r13
	movq	PCB_R12(%rdi),%r12
	movq	PCB_RBP(%rdi),%rbp
	movq	PCB_RSP(%rdi),%rsp
	movq	PCB_RBX(%rdi),%rbx

	/* Restore return address. */
	movq	PCB_RIP(%rdi),%rax
	movq	%rax,(%rsp)

	xorl	%eax,%eax
	ret
END(resumectx)
