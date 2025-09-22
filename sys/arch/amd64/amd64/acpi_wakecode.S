/* $OpenBSD: acpi_wakecode.S,v 1.51 2025/05/05 23:02:39 guenther Exp $ */
/*
 * Copyright (c) 2001 Takanori Watanabe <takawata@jp.freebsd.org>
 * Copyright (c) 2001 Mitsuru IWASAKI <iwasaki@jp.freebsd.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
/*
 * Copyright (c) 2008, 2009 Mike Larkin <mlarkin@openbsd.org>
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

#define _ACPI_WAKECODE

#include "assym.h"
#include <machine/asm.h>
#ifdef HIBERNATE
#include <machine/hibernate_var.h>
#endif /* HIBERNATE */
#include <machine/specialreg.h>
#include <machine/param.h>
#include <machine/segments.h>
#include <dev/acpi/acpivar.h>
#include "lapic.h"

#ifdef __clang__
#define addr32
#endif

#define _ACPI_TRMP_LABEL(a) a = . - acpi_real_mode_resume + \
	ACPI_TRAMPOLINE
#define _ACPI_TRMP_OFFSET(a) a = . - acpi_real_mode_resume
#define _ACPI_TRMP_DATA_LABEL(a) a = . - acpi_tramp_data_start + \
	ACPI_TRAMP_DATA
#define _ACPI_TRMP_DATA_OFFSET(a) a = . - acpi_tramp_data_start
#define _ACPI_RM_CODE_SEG (ACPI_TRAMPOLINE >> 4)
#define _ACPI_RM_DATA_SEG (ACPI_TRAMP_DATA >> 4)

/*
 * On wakeup, we'll start executing at acpi_real_mode_resume.
 * This is based on the wakeup vector previously stored with
 * ACPI before we went to sleep. ACPI's wakeup vector is a
 * physical address - in our case, it's calculated and mapped
 * by the kernel and stuffed into a low page early in the boot
 * process.
 *
 * We wakeup in real mode, at some phys addr based on the ACPI
 * specification (cs = phys>>8, ip = phys & 0xF). For example,
 * if our phys addr is 0x13000, we'd have cs=0x1300,ip=0
 *
 * The wakeup code needs to do the following:
 *     1. Reenable the video display
 *     2. Enter 32 bit protected mode
 *     3. Reenable paging
 *     4. Enter long mode
 *     5. Restore saved CPU registers
 *
 * Initial copy of this code gets placed in .rodata, kernel makes
 * RX copy of it in the ACPI trampoline page.
 */
	.section .rodata
	.code16
	.align 4, 0xcc
	.global acpi_resume_end
	.global acpi_pdirpa
	.global acpi_tramp_data_start
	.global acpi_tramp_data_end
GENTRY(acpi_real_mode_resume)
_ACPI_TRMP_OFFSET(.Lacpi_s3_vector_real)
	nop
	cli
	cld

	/*
	 * Set up segment registers for real mode.
	 * We'll only be in real mode for a moment, and we don't have
	 * ant real dependencies on data or stack, so we'll just use
	 * the code segment for data and stack (eg, a 64k memory space).
	 */
	movw	$(_ACPI_RM_DATA_SEG), %ax
	movw	%ax, %ds
	movw	%ax, %ss
	movw	%cs, %ax
	movw	%ax, %es
	addr32 lidtl	.Lclean_idt

	/*
	 * Set up stack to grow down from offset 0x0FFE.
	 * We will only be doing a few push/pops and no calls in real
	 * mode, so as long as the real mode code in the segment
	 * plus stack doesn't exceed 0x0FFE (4094) bytes, we'll be ok.
	 */
	movw	$0x0FFE,%sp

	/*
	 * Clear flags
	 */
	pushl	$0
	popfl

	/*
	 * Flush instruction prefetch queue
	 */
	jmp	1f
1:	jmp	1f
1:

	/*
	 * We're about to enter protected mode, so we need a GDT for that.
	 * Set up a temporary GDT describing 2 segments, one for code
	 * extending from 0x00000000-0xffffffff and one for data
	 * with the same range. This GDT will only be in use for a short
	 * time, until we restore the saved GDT that we had when we went
	 * to sleep.
	 */
	addr32 lgdtl	.Ltmp_gdt

	/*
	 * Enable protected mode by setting the PE bit in CR0
	 */
	mov	%cr0,%eax
	orl	$(CR0_PE),%eax
	mov	%eax,%cr0

	/*
	 * Force CPU into protected mode by making an intersegment jump (to
	 * ourselves, just a few lines down from here). We rely on the kernel
	 * to fixup the jump target address previously.
	 */
	ljmpl	$0x8, $.Lacpi_protected_mode_trampoline

	.code32
	.align 16, 0xcc
_ACPI_TRMP_LABEL(.Lacpi_protected_mode_trampoline)
	/* acpi_protected_mode_resume: */
	nop

	/*
	 * We're in protected mode now, without paging enabled.
	 *
	 * Set up segment selectors for protected mode.
	 * We've already set up our cs via the intersegment jump earlier,
	 * but we need to set ds,es,fs,gs,ss to all point to the
	 * 4GB flat data segment we defined earlier.
	 */
	movw	$GSEL(GDATA_SEL,SEL_KPL),%ax
	movw	%ax,%ds
	movw	%ax,%es
	movw	%ax,%gs
	movw	%ax,%ss
	movw	%ax,%fs

	/*
	 * Reset ESP based on protected mode. We can do this here
	 * because we haven't put anything on the stack via a
	 * call or push that we haven't cleaned up already.
	 */
	addl	$(ACPI_TRAMP_DATA), %esp

	/* Set CR4 to something sane for entry into long mode */
	mov	$(CR4_PAE|CR4_OSFXSR|CR4_OSXMMEXCPT|CR4_PSE),%eax
	mov	%eax,%cr4

	/*
	 * Set up a temporary long mode GDT describing 2
	 * segments, one for code and one for data.
	 */
	lgdt	.Ltmp_gdt64

	/* Restore saved EFER (LME, NXE, etc) */
	movl	$MSR_EFER, %ecx
	rdmsr
	movl	.Lacpi_saved_efer, %eax
	andl	$(EFER_LME | EFER_NXE | EFER_SCE), %eax
	wrmsr

	/* Reenable paging using temporary cr3 */
	movl	$acpi_pdirpa, %eax
	movl	(%eax), %eax
	movl	%eax, %cr3

	/* Flush the prefetch queue again */
	jmp	1f
1:	jmp	1f
1:

	/* Reenable paging by setting the appropriate bits in CR0 */
	movl	%cr0,%eax
	orl	$CR0_DEFAULT,%eax
	movl	%eax,%cr0

	/* Flush the prefetch queue again */
	jmp	1f
1:	jmp	1f
1:

	/* Enter long mode by making another intersegment jump */
	ljmp	$0x8, $.Lacpi_long_mode_trampoline

	.code64
	.align 16, 0xcc
_ACPI_TRMP_LABEL(.Lacpi_long_mode_trampoline)

	/* Reset stack */
	movq	$(ACPI_TRAMP_DATA + 0x0FF8), %rsp

	/* Load GDT based on our saved copy */
	lgdt	.Lacpi_saved_gdt

	/* Reset segment registers */
	movw	$GSEL(GDATA_SEL, SEL_KPL),%ax
	movw	%ax,%ds
	movw	%ax,%es
	movw	%ax,%ss

	xorw	%ax, %ax
	movw	%ax, %fs
	movw	%ax, %gs

	/* Restore registers - start with the MSRs */
#if NLAPIC > 0
	movl	$MSR_APICBASE, %ecx
	movl	.Lacpi_saved_apicbase, %eax
	movl	.Lacpi_saved_apicbase+4, %edx
	wrmsr
#endif

	movl	$MSR_STAR, %ecx
	movl	.Lacpi_saved_star, %eax
	movl	.Lacpi_saved_star+4, %edx
	wrmsr

	movl	$MSR_LSTAR, %ecx
	movl	.Lacpi_saved_lstar, %eax
	movl	.Lacpi_saved_lstar+4, %edx
	wrmsr

	movl	$MSR_CSTAR, %ecx
	xorl	%eax, %eax
	xorl	%edx, %edx
	wrmsr

	movl	$MSR_SFMASK, %ecx
	movl	.Lacpi_saved_sfmask, %eax
	movl	.Lacpi_saved_sfmask+4, %edx
	wrmsr

	movl	$MSR_FSBASE, %ecx
	movl	.Lacpi_saved_fsbase, %eax
	movl	.Lacpi_saved_fsbase+4, %edx
	wrmsr

	movl	$MSR_GSBASE, %ecx
	movl	.Lacpi_saved_gsbase, %eax
	movl	.Lacpi_saved_gsbase+4, %edx
	wrmsr

	movl	$MSR_KERNELGSBASE, %ecx
	movl	.Lacpi_saved_kgs, %eax
	movl	.Lacpi_saved_kgs+4, %edx
	wrmsr

	/* Restore control registers */
	movq	.Lacpi_saved_cr8, %rax
	movq	%rax, %cr8
	movq	.Lacpi_saved_cr4, %rax
	movq	%rax, %cr4
	movq	.Lacpi_saved_cr3, %rax
	movq	%rax, %cr3

	/* Flush the prefetch queue again */
	jmp	1f
1:	jmp	1f
1:

	movq	.Lacpi_saved_cr2, %rax
	movq	%rax, %cr2
	movq	.Lacpi_saved_cr0, %rax
	movq	%rax, %cr0

	/* Flush the prefetch queue again */
	jmp	1f
1:	jmp	1f
1:

	lldt	.Lacpi_saved_ldt
	lidt	.Lacpi_saved_idt

	/* Restore the saved task register */
	xorq	%rcx, %rcx
	movw	.Lacpi_saved_tr, %cx
	movq	.Lacpi_saved_gdt+2, %rax
	andb	$0xF9, 5(%rax,%rcx)
	ltr	%cx

	/* Reset debug control registers */
	xorl	%eax,%eax
	movq	%rax,%dr6
	movq	%rax,%dr7

	pushq	.Lacpi_saved_fl
	popfq

	movq	.Lacpi_saved_rbx, %rbx
	movq	.Lacpi_saved_rcx, %rcx
	movq	.Lacpi_saved_rdx, %rdx
	movq	.Lacpi_saved_rbp, %rbp
	movq	.Lacpi_saved_rsi, %rsi
	movq	.Lacpi_saved_rdi, %rdi
	movq	.Lacpi_saved_rsp, %rsp

	movq	.Lacpi_saved_r8, %r8
	movq	.Lacpi_saved_r9, %r9
	movq	.Lacpi_saved_r10, %r10
	movq	.Lacpi_saved_r11, %r11
	movq	.Lacpi_saved_r12, %r12
	movq	.Lacpi_saved_r13, %r13
	movq	.Lacpi_saved_r14, %r14
	movq	.Lacpi_saved_r15, %r15

	/* Poke CR3 one more time. Might not be necessary */
	movq	.Lacpi_saved_cr3, %rax
	movq	%rax, %cr3

	xorq	%rax, %rax
	jmp	*.Lacpi_saved_ret

#ifdef HIBERNATE
	/*
	 * hibernate_resume_machdep drops to real mode and
	 * restarts the OS using the saved S3 resume vector
	 */
	.code64
NENTRY(hibernate_resume_machdep)
	/*
	 * On resume time page table, switch temporarily to the suspended
	 * kernel's old page table (needed to access the suspended kernel's
	 * retguard area)
	 */
	movq	.Lacpi_saved_cr3, %rax
	movq	%rax, %cr3

	/*
	 * Now back on suspended kernel's page tables. Need to copy
	 * into rodata, so instead of fixing up the perms here and
	 * resetting them later, temporarily disable CR0.WP to allow
	 * us to write.
	 */
	movq	%cr0, %rax
	andq	$(~CR0_WP), %rax
	movq	%rax, %cr0

	movq	%rdi, %rsi
	movq	$__retguard_start, %rdi
	movq	$__retguard_end, %rcx
	subq	%rdi, %rcx
	shrq	$0x3, %rcx
	rep	movsq

	/* Reenable CR0.WP */
	movq	%cr0, %rax
	orq	$(CR0_WP), %rax
	movq	%rax, %cr0

	cli
	/* Jump to the identity mapped version of ourself */
	mov	$.Lhibernate_resume_vector_2, %rax
	jmp	*%rax
END(hibernate_resume_machdep)

	.section .rodata
_ACPI_TRMP_LABEL(.Lhibernate_resume_vector_2)

	/* Get out of 64 bit CS */
	lgdtq	.Ltmp_gdt6416

	/* Jump out of 64 bit mode, to hibernate_resume_vector_3 below */
	ljmp	*(.Lhibernate_indirect_16)

_ACPI_TRMP_OFFSET(.Lhibernate_resume_vector_3)
	.code16

	/* must clear CR4.PCIDE before clearing CR0.PG */
	movl	%cr4, %eax
	andl	$(~CR4_PCIDE), %eax
	movl	%eax, %cr4

	movl	%cr0, %eax
	/* Disable CR0.PG - no paging */
	andl	$(~CR0_PG), %eax
	/* Disable CR0.PE - real mode */
	andl	$(~CR0_PE), %eax
	movl	%eax, %cr0

	/* Set up real mode segment selectors */
	movw	$(_ACPI_RM_DATA_SEG), %ax
	movw	%ax, %ds
	movw	%ax, %ss
	movw	%ax, %es
	movw	%ax, %fs
	movw	%ax, %gs
	movl	$0x0FFE, %esp
	addr32 lidtl	.Lclean_idt

	/* Jump to the S3 resume vector */
	ljmp	$(_ACPI_RM_CODE_SEG), $.Lacpi_s3_vector_real

NENTRY(hibernate_drop_to_real_mode)
	.code64
	cli
	/* Jump to the identity mapped version of ourself */
	mov	$.Lhibernate_resume_vector_2b, %rax
	jmp	*%rax
END(hibernate_drop_to_real_mode)

	.section .rodata
_ACPI_TRMP_LABEL(.Lhibernate_resume_vector_2b)

	/* Get out of 64 bit CS */
	lgdtq	.Ltmp_gdt6416

	/* Jump out of 64 bit mode, to hibernate_resume_vector_3b below */
	ljmp	*(.Lhibernate_indirect_16b)

_ACPI_TRMP_OFFSET(.Lhibernate_resume_vector_3b)
	.code16

	/* must clear CR4.PCIDE before clearing CR0.PG */
	movl	%cr4, %eax
	andl	$(~CR4_PCIDE), %eax
	movl	%eax, %cr4

	movl	%cr0, %eax
	/* Disable CR0.PG - no paging */
	andl	$(~CR0_PG), %eax
	/* Disable CR0.PE - real mode */
	andl	$(~CR0_PE), %eax
	movl	%eax, %cr0

	/* Set up real mode segment selectors */
	movw	$(_ACPI_RM_DATA_SEG), %ax
	movw	%ax, %ds
	movw	%ax, %es
	movw	%ax, %fs
	movw	%ax, %gs
	movw	%ax, %ss
	movl	$0x0FFE, %esp
	addr32 lidtl	.Lclean_idt

_ACPI_TRMP_OFFSET(.Lhib_hlt_real)
	hlt
	ljmp	$(_ACPI_RM_CODE_SEG), $.Lhib_hlt_real

	.code64
	/* Switch to hibernate resume pagetable */
NENTRY(hibernate_activate_resume_pt_machdep)
	RETGUARD_SETUP(hibernate_activate_resume_pt_machdep, r11)
	/* Enable large pages */
	movq	%cr4, %rax
	orq	$(CR4_PSE), %rax

	/* Disable global pages */
	andq	$(~CR4_PGE), %rax
	movq	%rax, %cr4

	wbinvd
	movq	$HIBERNATE_PML4T, %rax
	movq	%rax,	%cr3
	jmp	1f

1:	RETGUARD_CHECK(hibernate_activate_resume_pt_machdep, r11)
	ret
	lfence
END(hibernate_activate_resume_pt_machdep)

	/*
	 * Switch to the private resume-time hibernate stack
	 */
NENTRY(hibernate_switch_stack_machdep)
	RETGUARD_SETUP(hibernate_switch_stack_machdep, r11)
	movq	(%rsp), %rax
	movq	%rax, HIBERNATE_STACK_PAGE + HIBERNATE_STACK_OFFSET
	movq	$(HIBERNATE_STACK_PAGE + HIBERNATE_STACK_OFFSET), %rax
	movq	%rax, %rsp

	/* On our own stack from here onward */
	RETGUARD_CHECK(hibernate_switch_stack_machdep, r11)
	ret
	lfence
END(hibernate_switch_stack_machdep)

NENTRY(hibernate_flush)
	RETGUARD_SETUP(hibernate_flush, r11)
	invlpg	HIBERNATE_INFLATE_PAGE
	RETGUARD_CHECK(hibernate_flush, r11)
	ret
	lfence
END(hibernate_flush)
#endif /* HIBERNATE */

	/*
	 * End of resume code (code copied to ACPI_TRAMPOLINE)
	 */
	.section .rodata
	.type	acpi_resume_end,@object
acpi_resume_end:
END(acpi_real_mode_resume)

	/*
	 * Initial copy of this data gets placed in .rodata, kernel makes
	 * RW copy of it in the tramp data page.
	 */
	.section .rodata
	.type	acpi_tramp_data_start,@object
acpi_tramp_data_start:
_ACPI_TRMP_DATA_OFFSET(.Ltmp_gdt)
	.word	.Ltmp_gdt_end - .Ltmp_gdtable
	.long	.Ltmp_gdtable

	.align 8, 0xcc
_ACPI_TRMP_DATA_LABEL(.Ltmp_gdtable)
	/*
	 * null
	 */
	.word	0, 0
	.byte	0, 0, 0, 0
	/*
	 * Code
	 * Limit: 0xffffffff
	 * Base: 0x00000000
	 * Descriptor Type: Code
	 * Segment Type: CRA
	 * Present: True
	 * Priv: 0
	 * AVL: False
	 * 64-bit: False
	 * 32-bit: True
	 *
	 */
	.word	0xffff, 0
	.byte	0, 0x9f, 0xcf, 0

	/*
	 * Data
	 * Limit: 0xffffffff
	 * Base: 0x00000000
	 * Descriptor Type:
	 * Segment Type: W
	 * Present: True
	 * Priv: 0
	 * AVL: False
	 * 64-bit: False
	 * 32-bit: True
	 *
	 */
	.word	0xffff, 0
	.byte	0, 0x93, 0xcf, 0
_ACPI_TRMP_DATA_LABEL(.Ltmp_gdt_end)

	.align 8, 0xcc
_ACPI_TRMP_DATA_OFFSET(.Lclean_idt)
	.word	0xffff
	.long	0
	.word	0

	.align 8, 0xcc
_ACPI_TRMP_DATA_LABEL(.Ltmp_gdt64)
	.word	.Ltmp_gdt64_end - .Ltmp_gdtable64
	.long	.Ltmp_gdtable64

	.align 8, 0xcc
_ACPI_TRMP_DATA_LABEL(.Ltmp_gdtable64)
	.quad	0x0000000000000000
	.quad	0x00af9a000000ffff
	.quad	0x00cf92000000ffff
_ACPI_TRMP_DATA_LABEL(.Ltmp_gdt64_end)

	.align 8, 0xcc
_ACPI_TRMP_DATA_LABEL(.Ltmp_gdt6416)
	.word	.Ltmp_gdt6416_end - .Ltmp_gdtable6416
	.quad	.Ltmp_gdtable6416

	.align 8, 0xcc
_ACPI_TRMP_DATA_LABEL(.Ltmp_gdtable6416)
	.quad	0x0000000000000000
	.quad	0x00af9a000000ffff
	.quad	0x00cf92000000ffff
	.word	0x0fff, (ACPI_TRAMPOLINE % 0x10000)
	.byte	(ACPI_TRAMPOLINE >> 16), 0x9a, 0, 0
_ACPI_TRMP_DATA_LABEL(.Ltmp_gdt6416_end)

	.align 8, 0xcc
_ACPI_TRMP_DATA_LABEL(.Lacpi_saved_rbx)
	.quad 0
_ACPI_TRMP_DATA_LABEL(.Lacpi_saved_rcx)
	.quad 0
_ACPI_TRMP_DATA_LABEL(.Lacpi_saved_rdx)
	.quad 0
_ACPI_TRMP_DATA_LABEL(.Lacpi_saved_rbp)
	.quad 0
_ACPI_TRMP_DATA_LABEL(.Lacpi_saved_rsi)
	.quad 0
_ACPI_TRMP_DATA_LABEL(.Lacpi_saved_rdi)
	.quad 0
_ACPI_TRMP_DATA_LABEL(.Lacpi_saved_rsp)
	.quad 0
_ACPI_TRMP_DATA_LABEL(.Lacpi_saved_r8)
	.quad 0
_ACPI_TRMP_DATA_LABEL(.Lacpi_saved_r9)
	.quad 0
_ACPI_TRMP_DATA_LABEL(.Lacpi_saved_r10)
	.quad 0
_ACPI_TRMP_DATA_LABEL(.Lacpi_saved_r11)
	.quad 0
_ACPI_TRMP_DATA_LABEL(.Lacpi_saved_r12)
	.quad 0
_ACPI_TRMP_DATA_LABEL(.Lacpi_saved_r13)
	.quad 0
_ACPI_TRMP_DATA_LABEL(.Lacpi_saved_r14)
	.quad 0
_ACPI_TRMP_DATA_LABEL(.Lacpi_saved_r15)
	.quad 0
_ACPI_TRMP_DATA_LABEL(.Lacpi_saved_fl)
	.quad 0
_ACPI_TRMP_DATA_LABEL(.Lacpi_saved_cr0)
	.quad 0
_ACPI_TRMP_DATA_LABEL(.Lacpi_saved_cr2)
	.quad 0
_ACPI_TRMP_DATA_LABEL(.Lacpi_saved_cr3)
	.quad 0
_ACPI_TRMP_DATA_LABEL(.Lacpi_saved_cr4)
	.quad 0
_ACPI_TRMP_DATA_LABEL(.Lacpi_saved_cr8)
	.quad 0
_ACPI_TRMP_DATA_LABEL(.Lacpi_saved_ret)
	.quad 0

	.align 8, 0xcc
_ACPI_TRMP_DATA_LABEL(.Lacpi_saved_idt)
	.space 10

	.align 8, 0xcc
_ACPI_TRMP_DATA_LABEL(.Lacpi_saved_gdt)
	.space 10

	.align 8, 0xcc
_ACPI_TRMP_DATA_LABEL(.Lacpi_saved_ldt)
	.space 10

_ACPI_TRMP_DATA_LABEL(.Lacpi_saved_tr)
	.short 0

	.align 4, 0xcc
_ACPI_TRMP_DATA_LABEL(.Lacpi_saved_efer)
	.long 0

	.align 8, 0xcc
_ACPI_TRMP_DATA_LABEL(.Lacpi_saved_fsbase)
	.quad 0
_ACPI_TRMP_DATA_LABEL(.Lacpi_saved_gsbase)
	.quad 0
_ACPI_TRMP_DATA_LABEL(.Lacpi_saved_kgs)
	.quad 0
_ACPI_TRMP_DATA_LABEL(.Lacpi_saved_star)
	.quad 0
_ACPI_TRMP_DATA_LABEL(.Lacpi_saved_lstar)
	.quad 0
_ACPI_TRMP_DATA_LABEL(.Lacpi_saved_sfmask)
	.quad 0
#if NLAPIC > 0
_ACPI_TRMP_DATA_LABEL(.Lacpi_saved_apicbase)
	.quad 0
#endif

	.align 4, 0xcc
	.type	acpi_pdirpa,@object
_ACPI_TRMP_DATA_LABEL(acpi_pdirpa)
	.long 0
	.size	acpi_pdirpa, 4
#ifdef HIBERNATE
_ACPI_TRMP_DATA_LABEL(.Lhibernate_indirect_16)
	.long	.Lhibernate_resume_vector_3
	.word	0x18
_ACPI_TRMP_DATA_LABEL(.Lhibernate_indirect_16b)
	.long	.Lhibernate_resume_vector_3b
	.word	0x18
#endif /* HIBERNATE */

	.type	acpi_tramp_data_end,@object
acpi_tramp_data_end:
END(acpi_tramp_data_start)

	/*
	 * acpi_savecpu saves the processor's registers and flags
	 * for use during the ACPI suspend/resume process.
	 */

	.code64
NENTRY(acpi_savecpu)
	movq	(%rsp), %rax
	movq	%rax, .Lacpi_saved_ret

	movq	%rbx, .Lacpi_saved_rbx
	movq	%rcx, .Lacpi_saved_rcx
	movq	%rdx, .Lacpi_saved_rdx
	movq	%rbp, .Lacpi_saved_rbp
	movq	%rsi, .Lacpi_saved_rsi
	movq	%rdi, .Lacpi_saved_rdi
	movq	%rsp, .Lacpi_saved_rsp
	/*
	 * acpi_protected_mode_resume performs restores inline, so undo own
	 * ret
	 */
	addq	$0x8, .Lacpi_saved_rsp

	movq	%r8, .Lacpi_saved_r8
	movq	%r9, .Lacpi_saved_r9
	movq	%r10, .Lacpi_saved_r10
	movq	%r11, .Lacpi_saved_r11
	movq	%r12, .Lacpi_saved_r12
	movq	%r13, .Lacpi_saved_r13
	movq	%r14, .Lacpi_saved_r14
	movq	%r15, .Lacpi_saved_r15

	/* Scratch reg saved - set up retguard */
	RETGUARD_SETUP(acpi_savecpu, r11)

	pushfq
	popq	.Lacpi_saved_fl

	movq	%cr0, %rax
	movq	%rax, .Lacpi_saved_cr0
	movq	%cr2, %rax
	movq	%rax, .Lacpi_saved_cr2
	movq	%cr3, %rax
	movq	%rax, .Lacpi_saved_cr3
	movq	%cr4, %rax
	movq	%rax, .Lacpi_saved_cr4
	movq	%cr8, %rax
	movq	%rax, .Lacpi_saved_cr8

	pushq	%rcx
	pushq	%rdx
#if NLAPIC > 0
	movl	$MSR_APICBASE, %ecx
	rdmsr
	movl	%eax, .Lacpi_saved_apicbase
	movl	%edx, .Lacpi_saved_apicbase+4
#endif

	movl	$MSR_STAR, %ecx
	rdmsr
	movl	%eax, .Lacpi_saved_star
	movl	%edx, .Lacpi_saved_star+4

	movl	$MSR_LSTAR, %ecx
	rdmsr
	movl	%eax, .Lacpi_saved_lstar
	movl	%edx, .Lacpi_saved_lstar+4

	movl	$MSR_SFMASK, %ecx
	rdmsr
	movl	%eax, .Lacpi_saved_sfmask
	movl	%edx, .Lacpi_saved_sfmask+4

	movl	$MSR_FSBASE, %ecx
	rdmsr
	movl	%eax, .Lacpi_saved_fsbase
	movl	%edx, .Lacpi_saved_fsbase+4

	movl	$MSR_GSBASE, %ecx
	rdmsr
	movl	%eax, .Lacpi_saved_gsbase
	movl	%edx, .Lacpi_saved_gsbase+4

	movl	$MSR_KERNELGSBASE, %ecx
	rdmsr
	movl	%eax, .Lacpi_saved_kgs
	movl	%edx, .Lacpi_saved_kgs+4

	movl	$MSR_EFER, %ecx
	rdmsr
	movl	%eax, .Lacpi_saved_efer
	popq	%rdx
	popq	%rcx

	sgdt	.Lacpi_saved_gdt
	sidt	.Lacpi_saved_idt
	sldt	.Lacpi_saved_ldt
	str	.Lacpi_saved_tr

	movl	$1, %eax
	RETGUARD_CHECK(acpi_savecpu, r11)
	ret
	lfence
END(acpi_savecpu)
