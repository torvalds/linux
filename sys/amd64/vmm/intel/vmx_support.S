/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011 NetApp, Inc.
 * Copyright (c) 2013 Neel Natu <neel@freebsd.org>
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY NETAPP, INC ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL NETAPP, INC OR CONTRIBUTORS BE LIABLE
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

#include "vmx_assym.h"

#ifdef SMP
#define	LK	lock ;
#else
#define	LK
#endif

/* Be friendly to DTrace FBT's prologue/epilogue pattern matching */
#define VENTER  push %rbp ; mov %rsp,%rbp
#define VLEAVE  pop %rbp

/*
 * Save the guest context.
 */
#define	VMX_GUEST_SAVE							\
	movq	%rdi,VMXCTX_GUEST_RDI(%rsp);				\
	movq	%rsi,VMXCTX_GUEST_RSI(%rsp);				\
	movq	%rdx,VMXCTX_GUEST_RDX(%rsp);				\
	movq	%rcx,VMXCTX_GUEST_RCX(%rsp);				\
	movq	%r8,VMXCTX_GUEST_R8(%rsp);				\
	movq	%r9,VMXCTX_GUEST_R9(%rsp);				\
	movq	%rax,VMXCTX_GUEST_RAX(%rsp);				\
	movq	%rbx,VMXCTX_GUEST_RBX(%rsp);				\
	movq	%rbp,VMXCTX_GUEST_RBP(%rsp);				\
	movq	%r10,VMXCTX_GUEST_R10(%rsp);				\
	movq	%r11,VMXCTX_GUEST_R11(%rsp);				\
	movq	%r12,VMXCTX_GUEST_R12(%rsp);				\
	movq	%r13,VMXCTX_GUEST_R13(%rsp);				\
	movq	%r14,VMXCTX_GUEST_R14(%rsp);				\
	movq	%r15,VMXCTX_GUEST_R15(%rsp);				\
	movq	%cr2,%rdi;						\
	movq	%rdi,VMXCTX_GUEST_CR2(%rsp);				\
	movq	%rsp,%rdi;

/*
 * Assumes that %rdi holds a pointer to the 'vmxctx'.
 *
 * On "return" all registers are updated to reflect guest state. The two
 * exceptions are %rip and %rsp. These registers are atomically switched
 * by hardware from the guest area of the vmcs.
 *
 * We modify %rsp to point to the 'vmxctx' so we can use it to restore
 * host context in case of an error with 'vmlaunch' or 'vmresume'.
 */
#define	VMX_GUEST_RESTORE						\
	movq	%rdi,%rsp;						\
	movq	VMXCTX_GUEST_CR2(%rdi),%rsi;				\
	movq	%rsi,%cr2;						\
	movq	VMXCTX_GUEST_RSI(%rdi),%rsi;				\
	movq	VMXCTX_GUEST_RDX(%rdi),%rdx;				\
	movq	VMXCTX_GUEST_RCX(%rdi),%rcx;				\
	movq	VMXCTX_GUEST_R8(%rdi),%r8;				\
	movq	VMXCTX_GUEST_R9(%rdi),%r9;				\
	movq	VMXCTX_GUEST_RAX(%rdi),%rax;				\
	movq	VMXCTX_GUEST_RBX(%rdi),%rbx;				\
	movq	VMXCTX_GUEST_RBP(%rdi),%rbp;				\
	movq	VMXCTX_GUEST_R10(%rdi),%r10;				\
	movq	VMXCTX_GUEST_R11(%rdi),%r11;				\
	movq	VMXCTX_GUEST_R12(%rdi),%r12;				\
	movq	VMXCTX_GUEST_R13(%rdi),%r13;				\
	movq	VMXCTX_GUEST_R14(%rdi),%r14;				\
	movq	VMXCTX_GUEST_R15(%rdi),%r15;				\
	movq	VMXCTX_GUEST_RDI(%rdi),%rdi; /* restore rdi the last */

/*
 * Clobber the remaining registers with guest contents so they can't
 * be misused.
 */
#define	VMX_GUEST_CLOBBER						\
	xor	%rax, %rax;						\
	xor	%rcx, %rcx;						\
	xor	%rdx, %rdx;						\
	xor	%rsi, %rsi;						\
	xor	%r8, %r8;						\
	xor	%r9, %r9;						\
	xor	%r10, %r10;						\
	xor	%r11, %r11;

/*
 * Save and restore the host context.
 *
 * Assumes that %rdi holds a pointer to the 'vmxctx'.
 */
#define	VMX_HOST_SAVE							\
	movq    %r15, VMXCTX_HOST_R15(%rdi);				\
	movq    %r14, VMXCTX_HOST_R14(%rdi);				\
	movq    %r13, VMXCTX_HOST_R13(%rdi);				\
	movq    %r12, VMXCTX_HOST_R12(%rdi);				\
	movq    %rbp, VMXCTX_HOST_RBP(%rdi);				\
	movq    %rsp, VMXCTX_HOST_RSP(%rdi);				\
	movq    %rbx, VMXCTX_HOST_RBX(%rdi);				\

#define	VMX_HOST_RESTORE						\
	movq	VMXCTX_HOST_R15(%rdi), %r15;				\
	movq	VMXCTX_HOST_R14(%rdi), %r14;				\
	movq	VMXCTX_HOST_R13(%rdi), %r13;				\
	movq	VMXCTX_HOST_R12(%rdi), %r12;				\
	movq	VMXCTX_HOST_RBP(%rdi), %rbp;				\
	movq	VMXCTX_HOST_RSP(%rdi), %rsp;				\
	movq	VMXCTX_HOST_RBX(%rdi), %rbx;				\

/*
 * vmx_enter_guest(struct vmxctx *vmxctx, int launched)
 * %rdi: pointer to the 'vmxctx'
 * %rsi: pointer to the 'vmx'
 * %edx: launch state of the VMCS
 * Interrupts must be disabled on entry.
 */
ENTRY(vmx_enter_guest)
	VENTER
	/*
	 * Save host state before doing anything else.
	 */
	VMX_HOST_SAVE

	/*
	 * Activate guest pmap on this cpu.
	 */
	movq	VMXCTX_PMAP(%rdi), %r11
	movl	PCPU(CPUID), %eax
	LK btsl	%eax, PM_ACTIVE(%r11)

	/*
	 * If 'vmx->eptgen[curcpu]' is not identical to 'pmap->pm_eptgen'
	 * then we must invalidate all mappings associated with this EPTP.
	 */
	movq	PM_EPTGEN(%r11), %r10
	cmpq	%r10, VMX_EPTGEN(%rsi, %rax, 8)
	je	guest_restore

	/* Refresh 'vmx->eptgen[curcpu]' */
	movq	%r10, VMX_EPTGEN(%rsi, %rax, 8)

	/* Setup the invept descriptor on the host stack */
	mov	%rsp, %r11
	movq	VMX_EPTP(%rsi), %rax
	movq	%rax, -16(%r11)
	movq	$0x0, -8(%r11)
	mov	$0x1, %eax		/* Single context invalidate */
	invept	-16(%r11), %rax
	jbe	invept_error		/* Check invept instruction error */

guest_restore:
	movl	%edx, %r8d
	cmpb	$0, guest_l1d_flush_sw(%rip)
	je	after_l1d
	call	flush_l1d_sw
after_l1d:
	cmpl	$0, %r8d
	je	do_launch
	VMX_GUEST_RESTORE
	vmresume
	/*
	 * In the common case 'vmresume' returns back to the host through
	 * 'vmx_exit_guest' with %rsp pointing to 'vmxctx'.
	 *
	 * If there is an error we return VMX_VMRESUME_ERROR to the caller.
	 */
	movq	%rsp, %rdi		/* point %rdi back to 'vmxctx' */
	movl	$VMX_VMRESUME_ERROR, %eax
	jmp	decode_inst_error

do_launch:
	VMX_GUEST_RESTORE
	vmlaunch
	/*
	 * In the common case 'vmlaunch' returns back to the host through
	 * 'vmx_exit_guest' with %rsp pointing to 'vmxctx'.
	 *
	 * If there is an error we return VMX_VMLAUNCH_ERROR to the caller.
	 */
	movq	%rsp, %rdi		/* point %rdi back to 'vmxctx' */
	movl	$VMX_VMLAUNCH_ERROR, %eax
	jmp	decode_inst_error

invept_error:
	movl	$VMX_INVEPT_ERROR, %eax
	jmp	decode_inst_error

decode_inst_error:
	movl	$VM_FAIL_VALID, %r11d
	jz	inst_error
	movl	$VM_FAIL_INVALID, %r11d
inst_error:
	movl	%r11d, VMXCTX_INST_FAIL_STATUS(%rdi)

	/*
	 * The return value is already populated in %eax so we cannot use
	 * it as a scratch register beyond this point.
	 */

	/*
	 * Deactivate guest pmap from this cpu.
	 */
	movq	VMXCTX_PMAP(%rdi), %r11
	movl	PCPU(CPUID), %r10d
	LK btrl	%r10d, PM_ACTIVE(%r11)

	VMX_HOST_RESTORE
	VLEAVE
	ret

/*
 * Non-error VM-exit from the guest. Make this a label so it can
 * be used by C code when setting up the VMCS.
 * The VMCS-restored %rsp points to the struct vmxctx
 */
	ALIGN_TEXT
	.globl	vmx_exit_guest_flush_rsb
vmx_exit_guest_flush_rsb:
	/*
	 * Save guest state that is not automatically saved in the vmcs.
	 */
	VMX_GUEST_SAVE

	/*
	 * Deactivate guest pmap from this cpu.
	 */
	movq	VMXCTX_PMAP(%rdi), %r11
	movl	PCPU(CPUID), %r10d
	LK btrl	%r10d, PM_ACTIVE(%r11)

	VMX_HOST_RESTORE

	VMX_GUEST_CLOBBER

	/*
	 * To prevent malicious branch target predictions from
	 * affecting the host, overwrite all entries in the RSB upon
	 * exiting a guest.
	 */
	mov	$16, %ecx	/* 16 iterations, two calls per loop */
	mov	%rsp, %rax
0:	call	2f		/* create an RSB entry. */
1:	pause
	call	1b		/* capture rogue speculation. */
2:	call	2f		/* create an RSB entry. */
1:	pause
	call	1b		/* capture rogue speculation. */
2:	sub	$1, %ecx
	jnz	0b
	mov	%rax, %rsp

	/*
	 * This will return to the caller of 'vmx_enter_guest()' with a return
	 * value of VMX_GUEST_VMEXIT.
	 */
	movl	$VMX_GUEST_VMEXIT, %eax
	VLEAVE
	ret

	.globl	vmx_exit_guest
vmx_exit_guest:
	/*
	 * Save guest state that is not automatically saved in the vmcs.
	 */
	VMX_GUEST_SAVE

	/*
	 * Deactivate guest pmap from this cpu.
	 */
	movq	VMXCTX_PMAP(%rdi), %r11
	movl	PCPU(CPUID), %r10d
	LK btrl	%r10d, PM_ACTIVE(%r11)

	VMX_HOST_RESTORE

	VMX_GUEST_CLOBBER

	/*
	 * This will return to the caller of 'vmx_enter_guest()' with a return
	 * value of VMX_GUEST_VMEXIT.
	 */
	movl	$VMX_GUEST_VMEXIT, %eax
	VLEAVE
	ret
END(vmx_enter_guest)

/*
 * %rdi = interrupt handler entry point
 *
 * Calling sequence described in the "Instruction Set Reference" for the "INT"
 * instruction in Intel SDM, Vol 2.
 */
ENTRY(vmx_call_isr)
	VENTER
	mov	%rsp, %r11			/* save %rsp */
	and	$~0xf, %rsp			/* align on 16-byte boundary */
	pushq	$KERNEL_SS			/* %ss */
	pushq	%r11				/* %rsp */
	pushfq					/* %rflags */
	pushq	$KERNEL_CS			/* %cs */
	cli					/* disable interrupts */
	callq	*%rdi				/* push %rip and call isr */
	VLEAVE
	ret
END(vmx_call_isr)
