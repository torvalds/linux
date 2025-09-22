/*	$OpenBSD: frameasm.h,v 1.27 2023/07/27 00:30:07 guenther Exp $	*/
/*	$NetBSD: frameasm.h,v 1.1 2003/04/26 18:39:40 fvdl Exp $	*/

#ifndef _AMD64_MACHINE_FRAMEASM_H
#define _AMD64_MACHINE_FRAMEASM_H

/*
 * Macros to define pushing/popping frames for interrupts, traps
 * and system calls. Currently all the same; will diverge later.
 */

/*
 * These are used on interrupt or trap entry or exit.
 */
#define INTR_SAVE_GPRS \
	subq	$120,%rsp		; \
	INTR_SAVE_MOST_GPRS_NO_ADJ	; \
	movq	%rcx,TF_RCX(%rsp)
#define INTR_SAVE_MOST_GPRS_NO_ADJ \
	movq	%r15,TF_R15(%rsp)	; \
	movq	%r14,TF_R14(%rsp)	; \
	movq	%r13,TF_R13(%rsp)	; \
	movq	%r12,TF_R12(%rsp)	; \
	movq	%r11,TF_R11(%rsp)	; \
	movq	%r10,TF_R10(%rsp)	; \
	movq	%r9,TF_R9(%rsp)		; \
	movq	%r8,TF_R8(%rsp)		; \
	movq	%rdi,TF_RDI(%rsp)	; \
	movq	%rsi,TF_RSI(%rsp)	; \
	movq	%rbp,TF_RBP(%rsp)	; \
	leaq	TF_RBP(%rsp),%rbp	; \
	movq	%rbx,TF_RBX(%rsp)	; \
	movq	%rdx,TF_RDX(%rsp)	; \
	movq	%rax,TF_RAX(%rsp)

/*
 * We clear registers when coming from userspace to prevent
 * user-controlled values from being available for use in speculative
 * execution in the kernel.  %rsp and %rbp are the kernel values when
 * this is used, so there are only 14 to clear.  32bit operations clear
 * the register upper-halves automatically.
 */
#define INTR_CLEAR_GPRS \
	xorl	%eax,%eax		; \
	xorl	%ebx,%ebx		; \
	xorl	%ecx,%ecx		; \
	xorl	%edx,%edx		; \
	xorl	%esi,%esi		; \
	xorl	%edi,%edi		; \
	xorl	%r8d,%r8d		; \
	xorl	%r9d,%r9d		; \
	xorl	%r10d,%r10d		; \
	xorl	%r11d,%r11d		; \
	xorl	%r12d,%r12d		; \
	xorl	%r13d,%r13d		; \
	xorl	%r14d,%r14d		; \
	xorl	%r15d,%r15d


/*
 * For real interrupt code paths, where we can come from userspace.
 * We only have an iretq_frame on entry.
 */
#define INTRENTRY_LABEL(label)	X##label##_untramp
#define	INTRENTRY(label) \
	endbr64				; \
	testb	$SEL_RPL,IRETQ_CS(%rsp)	; \
	je	INTRENTRY_LABEL(label)	; \
	swapgs				; \
	FENCE_SWAPGS_MIS_TAKEN 		; \
	movq	%rax,CPUVAR(SCRATCH)	; \
	CODEPATCH_START			; \
	movq	CPUVAR(KERN_CR3),%rax	; \
	movq	%rax,%cr3		; \
	CODEPATCH_END(CPTAG_MELTDOWN_NOP);\
	jmp	98f			; \
END(X##label)				; \
_ENTRY(INTRENTRY_LABEL(label)) /* from kernel */ \
	FENCE_NO_SAFE_SMAP		; \
	subq	$TF_RIP,%rsp		; \
	movq	%rcx,TF_RCX(%rsp)	; \
	jmp	99f			; \
	_ALIGN_TRAPS			; \
98:	/* from userspace */		  \
	movq	CPUVAR(KERN_RSP),%rax	; \
	xchgq	%rax,%rsp		; \
	movq	%rcx,TF_RCX(%rsp)	; \
	RET_STACK_REFILL_WITH_RCX	; \
	/* copy iretq frame to the trap frame */ \
	movq	IRETQ_RIP(%rax),%rcx	; \
	movq	%rcx,TF_RIP(%rsp)	; \
	movq	IRETQ_CS(%rax),%rcx	; \
	movq	%rcx,TF_CS(%rsp)	; \
	movq	IRETQ_RFLAGS(%rax),%rcx	; \
	movq	%rcx,TF_RFLAGS(%rsp)	; \
	movq	IRETQ_RSP(%rax),%rcx	; \
	movq	%rcx,TF_RSP(%rsp)	; \
	movq	IRETQ_SS(%rax),%rcx	; \
	movq	%rcx,TF_SS(%rsp)	; \
	movq	CPUVAR(SCRATCH),%rax	; \
99:	INTR_SAVE_MOST_GPRS_NO_ADJ	; \
	INTR_CLEAR_GPRS			; \
	movq	%rax,TF_ERR(%rsp)

#define INTRFASTEXIT \
	jmp	intr_fast_exit

/*
 * Entry for faking up an interrupt frame after spllower() unblocks
 * a previously received interrupt.  On entry, %r13 has the %rip
 * to return to.  %r10 and %r11 are scratch.
 */
#define	INTR_RECURSE \
	endbr64				; \
	/* fake the iretq_frame */	; \
	movq	%rsp,%r10		; \
	movl	%ss,%r11d		; \
	pushq	%r11			; \
	pushq	%r10			; \
	pushfq				; \
	movl	%cs,%r11d		; \
	pushq	%r11			; \
	pushq	%r13			; \
	/* now do the rest of the intrframe */ \
	subq	$16,%rsp		; \
	INTR_SAVE_GPRS


/* 
 * Entry for traps from kernel, where there's a trapno + err already
 * on the stack.  We have to move the err from its hardware location
 * to the location we want it.
 */
#define	TRAP_ENTRY_KERN \
	subq	$120,%rsp		; \
	movq	%rcx,TF_RCX(%rsp)	; \
	movq	(TF_RIP - 8)(%rsp),%rcx	; \
	movq	%rcx,TF_ERR(%rsp)	; \
	INTR_SAVE_MOST_GPRS_NO_ADJ

/*
 * Entry for traps from userland, where there's a trapno + err on
 * the iretq stack.
 * Assumes that %rax has been saved in CPUVAR(SCRATCH).
 */
#define	TRAP_ENTRY_USER \
	movq	CPUVAR(KERN_RSP),%rax		; \
	xchgq	%rax,%rsp			; \
	movq	%rcx,TF_RCX(%rsp)		; \
	RET_STACK_REFILL_WITH_RCX		; \
	/* copy trapno+err to the trap frame */ \
	movq	0(%rax),%rcx			; \
	movq	%rcx,TF_TRAPNO(%rsp)		; \
	movq	8(%rax),%rcx			; \
	movq	%rcx,TF_ERR(%rsp)		; \
	/* copy iretq frame to the trap frame */ \
	movq	(IRETQ_RIP+16)(%rax),%rcx	; \
	movq	%rcx,TF_RIP(%rsp)		; \
	movq	(IRETQ_CS+16)(%rax),%rcx	; \
	movq	%rcx,TF_CS(%rsp)		; \
	movq	(IRETQ_RFLAGS+16)(%rax),%rcx	; \
	movq	%rcx,TF_RFLAGS(%rsp)		; \
	movq	(IRETQ_RSP+16)(%rax),%rcx	; \
	movq	%rcx,TF_RSP(%rsp)		; \
	movq	(IRETQ_SS+16)(%rax),%rcx	; \
	movq	%rcx,TF_SS(%rsp)		; \
	movq	CPUVAR(SCRATCH),%rax		; \
	INTR_SAVE_MOST_GPRS_NO_ADJ		; \
	INTR_CLEAR_GPRS

/*
 * Entry from syscall instruction, where RIP is in %rcx and RFLAGS is in %r11.
 * We stash the syscall # in tf_err for SPL check.
 * Assumes that %rax has been saved in CPUVAR(SCRATCH).
 */
#define	SYSCALL_ENTRY \
	movq	CPUVAR(KERN_RSP),%rax				; \
	xchgq	%rax,%rsp					; \
	movq	%rcx,TF_RCX(%rsp)				; \
	movq	%rcx,TF_RIP(%rsp)				; \
	RET_STACK_REFILL_WITH_RCX				; \
	movq	$(GSEL(GUDATA_SEL, SEL_UPL)),TF_SS(%rsp)	; \
	movq	%rax,TF_RSP(%rsp)				; \
	movq	CPUVAR(SCRATCH),%rax				; \
	INTR_SAVE_MOST_GPRS_NO_ADJ				; \
	movq	%r11, TF_RFLAGS(%rsp)				; \
	movq	$(GSEL(GUCODE_SEL, SEL_UPL)), TF_CS(%rsp)	; \
	movq	%rax,TF_ERR(%rsp)				; \
	INTR_CLEAR_GPRS

#define CHECK_ASTPENDING(reg)	movq	CPUVAR(CURPROC),reg		; \
				cmpq	$0, reg				; \
				je	99f				; \
				cmpl	$0, P_MD_ASTPENDING(reg)	; \
				99:

#define CLEAR_ASTPENDING(reg)	movl	$0, P_MD_ASTPENDING(reg)

#endif /* _AMD64_MACHINE_FRAMEASM_H */
