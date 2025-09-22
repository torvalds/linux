/*	$OpenBSD: vmm_support.S,v 1.29 2025/05/20 08:35:37 bluhm Exp $	*/
/*
 * Copyright (c) 2014 Mike Larkin <mlarkin@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "assym.h"
#include <machine/param.h>
#include <machine/asm.h>
#include <machine/codepatch.h>
#include <machine/psl.h>
#include <machine/specialreg.h>

/*
 * XXX duplicated in vmmvar.h due to song-and-dance with sys/rwlock.h inclusion
 * here
 */
#define VMX_FAIL_LAUNCH_UNKNOWN 1
#define VMX_FAIL_LAUNCH_INVALID_VMCS 2
#define VMX_FAIL_LAUNCH_VALID_VMCS 3

	.global vmxon
	.global vmxoff
	.global vmclear
	.global vmptrld
	.global vmptrst
	.global vmwrite
	.global vmread
	.global invvpid
	.global invept
	.global vmx_enter_guest
	.global vmm_dispatch_intr
	.global svm_enter_guest
	.global svm_seves_enter_guest

	.text
	.code64
	.align 16,0xcc
vmm_dispatch_intr:
	movq	%rsp, %r11	/* r11 = temporary register */
	andq    $0xFFFFFFFFFFFFFFF0, %rsp
	movw	%ss, %ax
	pushq   %rax
	pushq	%r11
	pushfq
	movw	%cs, %ax
	pushq   %rax
	cli
	callq	*%rdi
	movq	$0,-8(%rsp)
	ret
	lfence

ENTRY(vmxon)
	RETGUARD_SETUP(vmxon, r11)
	xorq	%rax, %rax
	vmxon	(%rdi)
	setna	%al
	RETGUARD_CHECK(vmxon, r11)
	ret
	lfence
END(vmxon)

ENTRY(vmxoff)
	RETGUARD_SETUP(vmxoff, r11)
	xorq	%rax, %rax
	vmxoff
	setna	%al
	RETGUARD_CHECK(vmxoff, r11)
	ret
	lfence
END(vmxoff)

ENTRY(vmclear)
	RETGUARD_SETUP(vmclear, r11)
	xorq	%rax, %rax
	vmclear	(%rdi)
	setna	%al
	RETGUARD_CHECK(vmclear, r11)
	ret
	lfence
END(vmclear)

ENTRY(vmptrld)
	RETGUARD_SETUP(vmptrld, r11)
	xorq	%rax, %rax
	vmptrld	(%rdi)
	setna	%al
	RETGUARD_CHECK(vmptrld, r11)
	ret
	lfence
END(vmptrld)

ENTRY(vmptrst)
	RETGUARD_SETUP(vmptrst, r11)
	xorq	%rax, %rax
	vmptrst	(%rdi)
	setna	%al
	RETGUARD_CHECK(vmptrst, r11)
	ret
	lfence

ENTRY(vmwrite)
	RETGUARD_SETUP(vmwrite, r11)
	xorq	%rax, %rax
	vmwrite	%rsi, %rdi
	setna	%al
	RETGUARD_CHECK(vmwrite, r11)
	ret
	lfence
END(vmwrite)

ENTRY(vmread)
	RETGUARD_SETUP(vmread, r11)
	xorq	%rax, %rax
	vmread	%rdi, (%rsi)
	setna	%al
	RETGUARD_CHECK(vmread, r11)
	ret
	lfence
END(vmread)

/*
 * Intel SDM Vol 3C, 31.2 defines different "vmfail" types, but there's no
 * need to distinguish between CF=1 and ZF=1 for invvpid or invept.
 */
ENTRY(invvpid)
	RETGUARD_SETUP(invvpid, r11)
	invvpid (%rsi), %rdi
        jbe     invvpid_fail
        xorq    %rax, %rax
        jmp     invvpid_ret
invvpid_fail:
        movq    $1, %rax
invvpid_ret:
	RETGUARD_CHECK(invvpid, r11)
	ret
	lfence
END(invvpid)

ENTRY(invept)
	RETGUARD_SETUP(invept, r11)
	invept  (%rsi), %rdi
        jbe     invept_fail
        xorq    %rax, %rax
        jmp     invept_ret
invept_fail:
        movq    $1, %rax
invept_ret:
	RETGUARD_CHECK(invept, r11)
	ret
	lfence
END(invept)

ENTRY(vmx_enter_guest)
	RETGUARD_SETUP(vmx_enter_guest, r11)
	movq	%rdx, %r8	/* resume flag */
	movq	%rcx, %r9	/* L1DF MSR support */
	testq	%r8, %r8
	jnz skip_init

	/*
	 * XXX make vmx_exit_handler a global and put this in the per-vcpu
	 * init code
	 */
	movq	$VMCS_HOST_IA32_RIP, %rdi
	movq	$vmx_exit_handler_asm, %rax
	vmwrite %rax, %rdi	/* Host RIP */

skip_init:
	RETGUARD_PUSH(r11)

	/* Preserve callee-preserved registers as per AMD64 ABI */
	pushq	%r15
	pushq	%r14
	pushq	%r13
	pushq	%r12
	pushq	%rbp
	pushq	%rbx
	pushq	%rsi		/* Guest Regs Pointer */

	/*
	 * XXX this MDS mitigation and the L1TF mitigation are believed
	 * XXX to overlap in some cases, but Intel hasn't provided the
	 * XXX information yet to make the correct choices.
	 */
	CODEPATCH_START
	xorl	%eax,%eax
	xorl	%ebx,%ebx
	xorl	%ecx,%ecx
	xorl	%edx,%edx
	xorl	%esi,%esi
	xorl	%edi,%edi
	xorl	%ebp,%ebp
	/*
	 * r8 is a boolean flagging launch or resume
	 * r9 is 0-2 about the CPU
	 */
	xorl	%r10d,%r10d
	xorl	%r11d,%r11d
	xorl	%r12d,%r12d
	xorl	%r13d,%r13d
	xorl	%r14d,%r14d
	xorl	%r15d,%r15d
	subq	$8, %rsp
	movw	%ds, (%rsp)
	verw	(%rsp)
	addq	$8, %rsp
	CODEPATCH_END(CPTAG_MDS_VMM)
	movq	(%rsp),%rsi	/* reload now that it's mucked with */

	movq	$VMCS_HOST_IA32_RSP, %rdi
	movq	%rsp, %rax
	vmwrite	%rax, %rdi	/* Host RSP */

	/*
	 * Intel L1TF vulnerability fix
	 *
	 * Certain Intel CPUs are broken and allow guest VMs to bypass
	 * EPT entirely as their address harvesting logic treats guest
	 * PTEs as host physical addresses. Flush L1 Dcache to prevent
	 * information leakage by command MSR or manually reading a
	 * bunch of junk in order to fill sizeof(L1 Dcache)*2.
	 *
	 * %r9 (inherited from parameter 4 in %rcx earlier)
	 * determines the flushing requirements
	 *  0 - use manual "junk read" flush
	 *  1 - use MSR command
	 *  2 (VMX_SKIP_L1D_FLUSH) - no flush required on this CPU
	 */
	cmpq	$VMX_SKIP_L1D_FLUSH, %r9
	je	done_flush

	testq	%r9, %r9
	jz	no_l1df_msr

	/* CPU has command MSR */
	movq	$MSR_FLUSH_CMD, %rcx
	xorq	%rdx, %rdx
	movq	$FLUSH_CMD_L1D_FLUSH, %rax
	wrmsr
	jmp	done_flush

no_l1df_msr:
	xorq	%r9, %r9
l1df_tlb_loop:
	/* XXX get the right L1 size from cpuid */
	cmpq	$VMX_L1D_FLUSH_SIZE, %r9
	je	l1df_tlb_done
	movb	l1tf_flush_region(%r9), %al
	addq	$PAGE_SIZE, %r9
	jmp	l1df_tlb_loop

l1df_tlb_done:
	/*
	 * Serialize: ensure previous TLB loads don't pull PTDs
	 * or other PA-containing data into the L1D.
	 */
	xorq	%rax, %rax
	cpuid

	xorq	%r9, %r9
l1df_load_cache:
	movb	l1tf_flush_region(%r9), %al
	/* XXX get the right cacheline size from cpuid */
	addq	$0x40, %r9
	cmpq	$VMX_L1D_FLUSH_SIZE, %r9
	jne	l1df_load_cache
	lfence

done_flush:
	testq	%r8, %r8
	jnz	do_resume

	/* Restore guest registers */
	movq	0xa0(%rsi), %rax
	movq	%rax, %dr0
	movq	0xa8(%rsi), %rax
	movq	%rax, %dr1
	movq	0xb0(%rsi), %rax
	movq	%rax, %dr2
	movq	0xb8(%rsi), %rax
	movq	%rax, %dr3
	movq	0xc0(%rsi), %rax
	movq	%rax, %dr6
	movq	0x78(%rsi), %rax
	movq	%rax, %cr2
	movq	0x70(%rsi), %r15
	movq	0x68(%rsi), %r14
	movq	0x60(%rsi), %r13
	movq	0x58(%rsi), %r12
	movq	0x50(%rsi), %r11
	movq	0x48(%rsi), %r10
	movq	0x40(%rsi), %r9
	movq	%rsi, %r8
	/* XXX get the right cacheline size from cpuid */
	addq	$0x40, %r8
	clflush	(%r8)
	movq	0x38(%rsi), %r8
	movq	0x30(%rsi), %rbp
	movq	0x28(%rsi), %rdi
	movq	0x20(%rsi), %rdx
	movq	0x18(%rsi), %rcx
	movq	0x10(%rsi), %rbx
	movq	0x08(%rsi), %rax
	clflush	(%rsi)
	movq	0x00(%rsi), %rsi

	vmlaunch
	jmp	fail_launch_or_resume
do_resume:
	/* Restore guest registers */
	movq	0xa0(%rsi), %rax
	movq	%rax, %dr0
	movq	0xa8(%rsi), %rax
	movq	%rax, %dr1
	movq	0xb0(%rsi), %rax
	movq	%rax, %dr2
	movq	0xb8(%rsi), %rax
	movq	%rax, %dr3
	movq	0xc0(%rsi), %rax
	movq	%rax, %dr6
	movq	0x78(%rsi), %rax
	movq	%rax, %cr2
	movq	0x70(%rsi), %r15
	movq	0x68(%rsi), %r14
	movq	0x60(%rsi), %r13
	movq	0x58(%rsi), %r12
	movq	0x50(%rsi), %r11
	movq	0x48(%rsi), %r10
	movq	0x40(%rsi), %r9
	movq	%rsi, %r8
	/* XXX get the right cacheline size from cpuid */
	addq	$0x40, %r8
	clflush	(%r8)
	movq	0x38(%rsi), %r8
	movq	0x30(%rsi), %rbp
	movq	0x28(%rsi), %rdi
	movq	0x20(%rsi), %rdx
	movq	0x18(%rsi), %rcx
	movq	0x10(%rsi), %rbx
	movq	0x08(%rsi), %rax
	clflush	(%rsi)
	movq	0x00(%rsi), %rsi

	vmresume
fail_launch_or_resume:
	RET_STACK_REFILL_WITH_RCX

	/* Failed launch/resume (fell through) */
	jc fail_launch_invalid_vmcs	/* Invalid VMCS */
	jz fail_launch_valid_vmcs	/* Valid VMCS, failed launch/resume */

	/* Unknown failure mode (not documented as per Intel SDM) */
fail_launch_unknown:
	movq	$VMX_FAIL_LAUNCH_UNKNOWN, %rdi
	popq	%rsi
	jmp	restore_host

fail_launch_invalid_vmcs:
	movq	$VMX_FAIL_LAUNCH_INVALID_VMCS, %rdi
	popq	%rsi
	jmp	restore_host

fail_launch_valid_vmcs:
	movq	$VMCS_INSTRUCTION_ERROR, %rdi
	popq	%rsi
	vmread	%rdi, %rax
	/* XXX check failure of vmread */
	movl	%eax, 0x80(%rsi)
	movq	$VMX_FAIL_LAUNCH_VALID_VMCS, %rdi
	jmp	restore_host

vmx_exit_handler_asm:
	/* Preserve guest registers not saved in VMCS */
	pushq	%rsi
	pushq	%rdi
	movq	0x10(%rsp), %rdi
	movq	0x8(%rsp), %rsi
	movq	%rsi, (%rdi)
	popq	%rdi
	popq	%rsi	/* discard */

	popq	%rsi
	movq	%rax, 0x8(%rsi)
	movq	%rbx, 0x10(%rsi)
	movq	%rcx, 0x18(%rsi)
	movq	%rdx, 0x20(%rsi)
	movq	%rdi, 0x28(%rsi)
	movq	%rbp, 0x30(%rsi)
	movq	%r8, 0x38(%rsi)
	movq	%r9, 0x40(%rsi)
	movq	%r10, 0x48(%rsi)
	movq	%r11, 0x50(%rsi)
	movq	%r12, 0x58(%rsi)
	movq	%r13, 0x60(%rsi)
	movq	%r14, 0x68(%rsi)
	movq	%r15, 0x70(%rsi)
	movq	%cr2, %rax
	movq	%rax, 0x78(%rsi)
	movq	%dr0, %rax
	movq	%rax, 0xa0(%rsi)
	movq	%dr1, %rax
	movq	%rax, 0xa8(%rsi)
	movq	%dr2, %rax
	movq	%rax, 0xb0(%rsi)
	movq	%dr3, %rax
	movq	%rax, 0xb8(%rsi)
	movq	%dr6, %rax
	movq	%rax, 0xc0(%rsi)

	/* %rdi = 0 means we took an exit */
	xorq	%rdi, %rdi

	RET_STACK_REFILL_WITH_RCX

restore_host:
	popq	%rbx
	popq	%rbp
	popq	%r12
	popq	%r13
	popq	%r14
	popq	%r15

	RETGUARD_POP(r11)

	movq	%rdi, %rax
	RETGUARD_CHECK(vmx_enter_guest, r11)
	ret
	lfence
END(vmx_enter_guest)

ENTRY(svm_enter_guest)
	RETGUARD_SETUP(svm_enter_guest, r11)
	clgi
	movq	%rdi, %r8
	pushfq

	pushq	%rdx	/* gdt pointer */

	/*
	 * Save (possibly) lazy-switched selectors
	 */
	strw	%ax
	pushw	%ax
	movw	%es, %ax
	pushw	%ax
	movw	%ds, %ax
	pushw	%ax
	movw	%ss, %ax
	pushw	%ax

	movq	$MSR_FSBASE, %rcx
	rdmsr
	pushq	%rax
	pushq	%rdx
	pushw	%fs
	movq	$MSR_GSBASE, %rcx
	rdmsr
	pushq	%rax
	pushq	%rdx
	pushw	%gs
	movq	$MSR_KERNELGSBASE, %rcx
	rdmsr
	pushq	%rax
	pushq	%rdx

	/*
	 * Save various MSRs
	 */
	movq	$MSR_STAR, %rcx
	rdmsr
	pushq	%rax
	pushq	%rdx

	movq	$MSR_LSTAR, %rcx
	rdmsr
	pushq	%rax
	pushq	%rdx

	movq	$MSR_SFMASK, %rcx
	rdmsr
	pushq	%rax
	pushq	%rdx

	RETGUARD_PUSH(r11)

	/* Preserve callee-preserved registers as per AMD64 ABI */
	pushq	%r15
	pushq	%r14
	pushq	%r13
	pushq	%r12
	pushq	%rbp
	pushq	%rbx
	pushq	%rsi		/* Guest Regs Pointer */

	/* Restore guest registers */
	movq	%r8, %rax	/* rax = vmcb pa */
	movq	0xa0(%rsi), %r8
	movq	%r8, %dr0
	movq	0xa8(%rsi), %r8
	movq	%r8, %dr1
	movq	0xb0(%rsi), %r8
	movq	%r8, %dr2
	movq	0xb8(%rsi), %r8
	movq	%r8, %dr3
	/* %dr6 is saved in the VMCB */
	movq	0x78(%rsi), %r8
	movq	%r8, %cr2
	movq	0x70(%rsi), %r15
	movq	0x68(%rsi), %r14
	movq	0x60(%rsi), %r13
	movq	0x58(%rsi), %r12
	movq	0x50(%rsi), %r11
	movq	0x48(%rsi), %r10
	movq	0x40(%rsi), %r9
	movq	0x38(%rsi), %r8
	movq	0x30(%rsi), %rbp
	movq	0x28(%rsi), %rdi
	movq	0x20(%rsi), %rdx
	movq	0x18(%rsi), %rcx
	movq	0x10(%rsi), %rbx
	/* %rax at 0x08(%rsi) is not needed in SVM */
	movq	0x00(%rsi), %rsi

	vmload	%rax
	vmrun	%rax
	vmsave	%rax

	/* Preserve guest registers not saved in VMCB */
	pushq	%rsi
	pushq	%rdi
	movq	0x10(%rsp), %rdi
	movq	0x8(%rsp), %rsi
	movq	%rsi, (%rdi)
	popq	%rdi
	popq	%rsi	/* discard */

	popq	%rsi
	/* %rax at 0x08(%rsi) is not needed in SVM */
	movq	%rbx, 0x10(%rsi)
	movq	%rcx, 0x18(%rsi)
	movq	%rdx, 0x20(%rsi)
	movq	%rdi, 0x28(%rsi)
	movq	%rbp, 0x30(%rsi)
	movq	%r8, 0x38(%rsi)
	movq	%r9, 0x40(%rsi)
	movq	%r10, 0x48(%rsi)
	movq	%r11, 0x50(%rsi)
	movq	%r12, 0x58(%rsi)
	movq	%r13, 0x60(%rsi)
	movq	%r14, 0x68(%rsi)
	movq	%r15, 0x70(%rsi)
	movq	%cr2, %rax
	movq	%rax, 0x78(%rsi)
	movq	%dr0, %rax
	movq	%rax, 0xa0(%rsi)
	movq	%dr1, %rax
	movq	%rax, 0xa8(%rsi)
	movq	%dr2, %rax
	movq	%rax, 0xb0(%rsi)
	movq	%dr3, %rax
	movq	%rax, 0xb8(%rsi)
	/* %dr6 is saved in the VMCB */

	/* %rdi = 0 means we took an exit */
	xorq	%rdi, %rdi

restore_host_svm:
	popq	%rbx
	popq	%rbp
	popq	%r12
	popq	%r13
	popq	%r14
	popq	%r15

	RETGUARD_POP(r11)

	/*
	 * Restore saved MSRs
	 */
	popq	%rdx
	popq	%rax
	movq	$MSR_SFMASK, %rcx
	wrmsr

	/* make sure guest doesn't bleed into host */
	xorl	%edx, %edx
	xorl	%eax, %eax
	movq	$MSR_CSTAR, %rcx
	wrmsr

	popq	%rdx
	popq	%rax
	movq	$MSR_LSTAR, %rcx
	wrmsr

	popq	%rdx
	popq	%rax
	movq	$MSR_STAR, %rcx
	wrmsr

	/*
	 * popw %gs will reset gsbase to 0, so preserve it
	 * first. This is to accommodate possibly lazy-switched
	 * selectors from above
	 */
	cli
	popq	%rdx
	popq	%rax
	movq	$MSR_KERNELGSBASE, %rcx
	wrmsr

	popw	%gs
	popq	%rdx
	popq	%rax
	movq	$MSR_GSBASE, %rcx
	wrmsr

	popw	%fs
	popq	%rdx
	popq	%rax
	movq	$MSR_FSBASE, %rcx
	wrmsr

	popw	%ax
	movw	%ax, %ss
	popw	%ax
	movw	%ax, %ds
	popw	%ax
	movw	%ax, %es

	xorq	%rax, %rax
	lldtw	%ax		/* Host LDT is always 0 */

	popw	%ax		/* ax = saved TR */

	popq	%rdx
	addq	$0x2, %rdx
	movq	(%rdx), %rdx

	/* rdx = GDTR base addr */
	andb	$0xF9, 5(%rdx, %rax)

	ltrw	%ax

	popfq

	movq	%rdi, %rax

	RETGUARD_CHECK(svm_enter_guest, r11)
	ret
	lfence
END(svm_enter_guest)

/*
 * When using SEV-ES we have to save some of the host registers to
 * the host state save area (HSA).  According to the AMD Programmer's
 * Manual Volume 2 Appendix B the HSA has the same layout as the guest
 * save area (VMSA) except that it starts at offset 0x400 in the HSA
 * page.
 */
ENTRY(svm_seves_enter_guest)
	RETGUARD_SETUP(svm_seves_enter_guest, r11)
	clgi
	movq	%rdi, %r8
	pushfq

	pushq	%rdx	/* gdt pointer */

	/*
	 * Save (possibly) lazy-switched selectors
	 */
	strw	%ax
	pushw	%ax
	movw	%es, %ax
	pushw	%ax
	movw	%ds, %ax
	pushw	%ax
	movw	%ss, %ax
	pushw	%ax

	movq	$MSR_FSBASE, %rcx
	rdmsr
	pushq	%rax
	pushq	%rdx
	pushw	%fs
	movq	$MSR_GSBASE, %rcx
	rdmsr
	pushq	%rax
	pushq	%rdx
	pushw	%gs
	movq	$MSR_KERNELGSBASE, %rcx
	rdmsr
	pushq	%rax
	pushq	%rdx

	/*
	 * Save various MSRs
	 */
	movq	$MSR_STAR, %rcx
	rdmsr
	pushq	%rax
	pushq	%rdx

	movq	$MSR_LSTAR, %rcx
	rdmsr
	pushq	%rax
	pushq	%rdx

	movq	$MSR_SFMASK, %rcx
	rdmsr
	pushq	%rax
	pushq	%rdx

	RETGUARD_PUSH(r11)

	/*
	 * Preserve callee-preserved registers as per AMD64 ABI in
	 * HSA. Although all registers will be restored from HSA
	 * on vmexit, these will not be saved on vmrun.
	 */
	movq	%r15, 0x378(%rsi)
	movq	%r14, 0x370(%rsi)
	movq	%r13, 0x368(%rsi)
	movq	%r12, 0x360(%rsi)
	movq	%rbp, 0x328(%rsi)
	movq	%rbx, 0x318(%rsi)

	movq	%r8, %rax	/* rax = vmcb pa */

	vmrun	%rax

	/* %rdi = 0 means we took an exit */
	xorq	%rdi, %rdi

	RETGUARD_POP(r11)

	/*
	 * Restore saved MSRs
	 */
	popq	%rdx
	popq	%rax
	movq	$MSR_SFMASK, %rcx
	wrmsr

	/* make sure guest doesn't bleed into host */
	xorl	%edx, %edx
	xorl	%eax, %eax
	movq	$MSR_CSTAR, %rcx
	wrmsr

	popq	%rdx
	popq	%rax
	movq	$MSR_LSTAR, %rcx
	wrmsr

	popq	%rdx
	popq	%rax
	movq	$MSR_STAR, %rcx
	wrmsr

	/*
	 * popw %gs will reset gsbase to 0, so preserve it
	 * first. This is to accommodate possibly lazy-switched
	 * selectors from above
	 */
	cli
	popq	%rdx
	popq	%rax
	movq	$MSR_KERNELGSBASE, %rcx
	wrmsr

	popw	%gs
	popq	%rdx
	popq	%rax
	movq	$MSR_GSBASE, %rcx
	wrmsr

	popw	%fs
	popq	%rdx
	popq	%rax
	movq	$MSR_FSBASE, %rcx
	wrmsr

	popw	%ax
	movw	%ax, %ss
	popw	%ax
	movw	%ax, %ds
	popw	%ax
	movw	%ax, %es

	xorq	%rax, %rax
	lldtw	%ax		/* Host LDT is always 0 */

	popw	%ax		/* ax = saved TR */

	popq	%rdx
	addq	$0x2, %rdx
	movq	(%rdx), %rdx

	/* rdx = GDTR base addr */
	andb	$0xF9, 5(%rdx, %rax)

	ltrw	%ax

	popfq

	movq	%rdi, %rax

	RETGUARD_CHECK(svm_seves_enter_guest, r11)
	ret
	lfence
END(svm_seves_enter_guest)
