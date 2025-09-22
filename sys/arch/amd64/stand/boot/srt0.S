/*	$OpenBSD: srt0.S,v 1.4 2022/12/08 01:25:44 guenther Exp $	*/

/*
 * Copyright (c) 1997 Michael Shalayeff
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR 
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */
#include <machine/asm.h>
#include <assym.h>

#define BOOTSTACK 0xfffc

	.globl	end
	.globl	edata
	.globl	boot
	.globl	_rtt
	.globl	bios_bootdev
	.globl	pmm_init
	.globl	Gdtr

	.text
	.align	16
	.code16
	.globl	_start
_start:
	popl %eax
	cmpl $BOOTMAGIC, %eax
	je	1f
#ifdef DEBUG
	movl	$0xb80a0, %ebx
	addr32 movl $0x07420742, (%ebx)
#endif
1:
	popl %edx
	cli
	pushl	%cs
	popl	%ds
	addr32 data32 lgdt	(Gdtr - LINKADDR)
	movl	%cr0, %eax
	orl $CR0_PE, %eax
	data32 movl %eax, %cr0
	data32 ljmp $8, $1f
1:
	.code32
	movl	$0x10,%eax
	mov	%ax,%ds
	mov	%ax,%ss
	mov	%ax,%es
	mov	%ax,%fs
	mov	%ax,%gs
	movl	$BOOTSTACK,%esp
	pushl	%edx
	movl	%edx, bios_bootdev

	/* Now do it all */
#ifdef DEBUG
	movl	$0xb80a4, %ebx
	movl	$0x07520752, (%ebx)
#endif
	/* zero .bss */
	xorl	%eax, %eax
	movl	$end, %ecx
	subl	$edata,%ecx
	movl	$edata, %edi
	cld
	rep;	stosb

	call	pmm_init
	call	boot

	jmp	_rtt
