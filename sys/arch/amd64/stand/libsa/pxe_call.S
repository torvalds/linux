/*	$OpenBSD: pxe_call.S,v 1.5 2022/12/08 01:25:44 guenther Exp $ */
/*	$NetBSD: pxe_call.S,v 1.2 2002/03/27 17:24:22 kanaoka Exp $	*/

/*
 * Copyright 2001 Wasabi Systems, Inc.
 * All rights reserved.
 *
 * Written by Jason R. Thorpe for Wasabi Systems, Inc.
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
 *	This product includes software developed for the NetBSD Project by
 *	Wasabi Systems, Inc.
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

/*
 * Low level PXE BIOS call glue.
 */

#include <machine/asm.h>
#include <assym.h>

#include "gidt.h"

ENTRY(pxecall_bangpxe)
	.code32
	pushl	%ebp
	movl	%esp, %ebp
	pushl	%ebx
	pushl	%ecx
	pushl	%edx

	/* For simplicity, just move all 32 bits. */
	movl	8(%ebp), %ebx

	pushw	pxe_command_buf_seg
	pushw	pxe_command_buf_off
	pushw	%bx

	call	prot_to_real			/* Enter real mode */
	.code16

	sti
	/* The encoding is: 0x9a offlo offhi seglo seghi */
	lcall	$0, $0xffff
	.globl	bangpxe_off
bangpxe_off = . - 4
	.globl	bangpxe_seg
bangpxe_seg = . - 2

	cli
	call	real_to_prot			/* Leave real mode */
	.code32

	add	$6, %esp

	popl	%edx
	popl	%ecx
	popl	%ebx
	popl	%ebp
	ret

ENTRY(pxecall_pxenv)
	.code32
	pushl	%ebp
	movl	%esp, %ebp
	pushl	%ebx
	pushl	%ecx
	pushl	%edx
	pushl	%edi

	/*
         * Using the PXENV+ calling convention, the (16 bit) function
         * number is passed in %bx, with the address of the command
         * buffer in %es:%di.
	 */
	movl	8(%ebp), %ebx	/* For simplicity, just move all 32 bits. */

	/*
	 * prot_to_real() will set %es to BOOTSEG, so we just need to set
	 * %(e)di up here.  Remember to relocate it!
	 */
	movl	$pxe_command_buf, %edi
	subl	$LINKADDR, %edi

	call	prot_to_real			/* Enter real mode */
	.code16

	/* The encoding is: 0x9a offlo offhi seglo seghi */
	lcall	$0, $0xffff
	.globl	pxenv_off
pxenv_off = . - 4
	.globl	pxenv_seg
pxenv_seg = . - 2

	call	real_to_prot			/* Leave real mode */
	.code32

	popl	%edi
	popl	%edx
	popl	%ecx
	popl	%ebx
	popl	%ebp
	ret

/*
 * prot_to_real()
 *
 * Switch the processor back into real mode.
 */
	.globl	prot_to_real
prot_to_real:
	.code32
	ljmp	$S16TEXT, $p2r16 - LINKADDR
p2r16:
	.code16

	movw	$S16DATA, %ax
	movw	%ax, %ds
	movw	%ax, %es

	movl	%cr0, %eax		/* Disable protected mode */
	andl 	$~CR0_PE, %eax
	movl	%eax, %cr0

	/* reload real cs:ip */
	data32 ljmp	$(LINKADDR >> 4), $p2r16real - LINKADDR
p2r16real:
	xorw	%ax, %ax		/* Reset segment registers: */
	movw	%ax, %ss		/* %ss: for our stack */

	movw	$LINKADDR >> 4, %ax	/* We're linked to LINKADDR/16:0000 */
	movw	%ax, %ds		/* %ds: so we can get at Idtr_real */

	.extern	Idtr_real
	data32 addr32 lidt (Idtr_real - LINKADDR);	/* Set up IDT for real mode */

	movw	%cs, %ax
	movw	%ax, %ds
	movw	%ax, %es		/* Set %ds = %es = %cs */

	/*
	 * We were called from 32-bit mode, so there's a 32-bit
	 * return address on the stack.  No segment.  This is within
	 * the flat memory model, so we need to adjust it back so
	 * that it's relative to our 16-bit %cs.
	 */
	popl	%eax
	subl	$LINKADDR, %eax
	pushw	%ax
	ret

/*
 * real_to_prot()
 *
 * Switch the processor back into protected mode.
 */
	.globl	real_to_prot
real_to_prot:
	.code16

	movw	$LINKADDR >> 4, %ax	/* We're linked to LINKADDR/16:0000 */
	movw	%ax, %ds
	data32 addr32 lgdt (Gdtr - LINKADDR)	/* Reload the GDT */

	movl	%cr0, %eax		/* Enable protected mode */
	orl	$CR0_PE, %eax
	movl	%eax, %cr0

	data32 ljmp	$S32TEXT, $r2p32 /* Reload %cs, flush pipeline */
r2p32:
	.code32
	/* Reload 32-bit %ds, %ss, %es */
	movl	$S32DATA, %eax
	mov	%ax, %ds
	mov	%ax, %ss
	mov	%ax, %es

	/* Load IDT for debugger and DOS/BIOS interface */
	.extern	Idtr
	lidt	Idtr

	xorl	%eax, %eax
	popw	%ax			/* 16-bit return addr on stack */
	addl	$LINKADDR, %eax
	pushl	%eax			/* Now have correct 32-bit ret addr */
	ret

	.end
