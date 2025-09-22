/*	$OpenBSD: mptramp.S,v 1.23 2024/07/12 13:10:58 deraadt Exp $	*/
/*	$NetBSD: mptramp.S,v 1.1 2003/04/26 18:39:30 fvdl Exp $	*/

/*-
 * Copyright (c) 2000 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by RedBack Networks Inc.
 *
 * Author: Bill Sommerfeld
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

/*
 * Copyright (c) 1999 Stefan Grefen
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
 *      This product includes software developed by the NetBSD
 *      Foundation, Inc. and its contributors.
 * 4. Neither the name of The NetBSD Foundation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR AND CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
/*
 * MP startup ...
 * the stuff from cpu_spinup_trampoline to mp_startup
 * is copied into the first 640 KB
 *
 * We startup the processors now when the kthreads become ready.
 * The steps are:
 *        1)   Get the processors running kernel-code from a special
 *                  page-table and stack page, do chip identification.
 *        2)   halt the processors waiting for them to be enabled
 *              by a idle-thread
 */

#include "assym.h"
#include <machine/param.h>
#include <machine/asm.h>
#include <machine/specialreg.h>
#include <machine/segments.h>
#include <machine/mpbiosvar.h>
#include <machine/i82489reg.h>

#ifdef __clang__
#define addr32
#endif

#define _TRMP_LABEL(a)	a = . - cpu_spinup_trampoline + MP_TRAMPOLINE
#define _TRMP_OFFSET(a)	a = . - cpu_spinup_trampoline

#define _TRMP_DATA_LABEL(a)	a = . - mp_tramp_data_start + MP_TRAMP_DATA
#define _TRMP_DATA_OFFSET(a)	a = . - mp_tramp_data_start

	.global cpu_spinup_trampoline_end
	.global mp_tramp_data_start
	.global mp_tramp_data_end

	/*
	 * Initial copy of this code gets placed in .rodata, kernel
	 * makes RX copy of it in the MP trampoline page.
	 */
	.section .rodata
	.align 4, 0xcc
	.code16
GENTRY(cpu_spinup_trampoline)
	cli
	movw	$(MP_TRAMP_DATA >> 4), %ax
	movw	%ax, %ds
	movw	%cs, %ax
	movw	%ax, %es
	movw	%ax, %ss
	addr32 lgdtl (.Lmptramp_gdt32_desc)	# load flat descriptor table
	movl	%cr0, %eax	# get cr0
	orl	$CR0_PE, %eax	# enable protected mode
	movl	%eax, %cr0	# doit
	ljmpl	$0x8, $.Lmp_startup

_TRMP_LABEL(.Lmp_startup)
	.code32

	movl	$0x10, %eax	# data segment
	movw	%ax, %ds
	movw	%ax, %ss
	movw	%ax, %es
	movw	%ax, %fs
	movw	%ax, %gs

	movl	$(MP_TRAMP_DATA + NBPG - 16),%esp	# bootstrap stack end,
							# with scratch space..

	/* First, reset the PSL. */
	pushl	$PSL_MBO
	popfl

	movl	%cr4,%eax
	orl	$(CR4_DEFAULT),%eax
	movl	%eax,%cr4

	movl	$MSR_EFER,%ecx
	rdmsr
	movl	%edx, %edi		# %edx is needed by wrmsr below

	# Check if we need to enable NXE
	movl	$0x80000001, %eax
	cpuid
	andl	$CPUID_NXE, %edx
	xorl	%eax,%eax
	testl	%edx, %edx
	jz	1f
	orl	$EFER_NXE, %eax
1:
	orl	$(EFER_LME|EFER_SCE), %eax

	movl	%edi, %edx		# Restore saved %edx
	movl	$MSR_EFER,%ecx
	wrmsr

	movl	$mp_pdirpa, %ecx
	movl	(%ecx), %ecx
	movl	%ecx,%cr3		# load ptd addr into mmu

	movl	$GSEL(GDATA_SEL, SEL_KPL),%eax	#switch to new segment
	movl	%eax,%ds
	movl	%eax,%es
	movl	%eax,%ss

	movl	$.Lmptramp_gdt64_desc,%eax
	lgdt	(%eax)

	xorw	%ax, %ax
	movw	%ax, %fs

	movl	$.Lmptramp_jmp64,%eax

	movl	%cr0,%ecx		# get control word
	orl	$CR0_DEFAULT,%ecx
	movl	%ecx, %cr0

	ljmp	*(%eax)

_TRMP_LABEL(.Lmptramp_longmode)
	.code64
	movabsq	$cpu_spinup_finish,%rax
	jmp	*%rax

	.type	cpu_spinup_trampoline_end,@object
cpu_spinup_trampoline_end:	#end of code copied to MP_TRAMPOLINE
END(cpu_spinup_trampoline)

	.text
GENTRY(cpu_spinup_finish)
	movl	x2apic_enabled,%eax
	testl	%eax,%eax
	jz	1f

	mov	$MSR_APICBASE,%ecx
	mov	$0,%edx
	rdmsr
	orl	$APICBASE_ENABLE_X2APIC,%eax
	wrmsr
	mov	$MSR_X2APIC_ID,%ecx
	rdmsr
	andl	$X2APIC_ID_MASK,%eax
	jmp	2f
1:
	movl	local_apic+LAPIC_ID,%eax
	shrl	$LAPIC_ID_SHIFT,%eax
2:
	xorq	%rcx,%rcx
3:
	movq	cpu_info(,%rcx,8),%rdi
	incq	%rcx
	movl	CPU_INFO_APICID(%rdi),%edx
	cmpl	%eax,%edx
	jne	3b

	movq	CPU_INFO_IDLE_PCB(%rdi),%rsi

	movq	PCB_RSP(%rsi),%rsp
	movq	PCB_RBP(%rsi),%rbp

	movq	CPU_INFO_GDT(%rdi),%rax
	movw	$(GDT_SIZE-1),-10(%rsp)
	movq	%rax,-8(%rsp)
	lgdt	-10(%rsp)

	/* Switch address space. */
	movq	PCB_CR3(%rsi),%rax
	movq	%rax,%cr3
	movl	$CR0_DEFAULT,%eax
	movq	%rax,%cr0
	call	cpu_hatch
	movq	$0,-8(%rsp)
END(cpu_spinup_finish)
	/* NOTREACHED */

	.section .rodata
	.type	mp_tramp_data_start,@object
mp_tramp_data_start:
_TRMP_DATA_LABEL(.Lmptramp_jmp64)
	.long	.Lmptramp_longmode
	.word	GSEL(GCODE_SEL, SEL_KPL)

	.global mp_pdirpa
	.type	mp_pdirpa,@object
_TRMP_DATA_LABEL(mp_pdirpa)
	.long 0
	.size	mp_pdirpa,4


_TRMP_DATA_LABEL(.Lmptramp_gdt32)
	.quad 0x0000000000000000
	.quad 0x00cf9f000000ffff
	.quad 0x00cf93000000ffff
_TRMP_DATA_OFFSET(.Lmptramp_gdt32_desc)
	.word 0x17
	.long .Lmptramp_gdt32

_TRMP_DATA_LABEL(.Lmptramp_gdt64)
	.quad 0x0000000000000000
	.quad 0x00af9a000000ffff
	.quad 0x00cf92000000ffff
_TRMP_DATA_LABEL(.Lmptramp_gdt64_desc)
	.word 0x17
	.long .Lmptramp_gdt64

	.type	mp_tramp_data_end,@object
mp_tramp_data_end:
END(mp_tramp_data_start)
