/*-
 * Copyright (c) 2003  Peter Wemm <peter@FreeBSD.org>
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
 *
 * $FreeBSD$
 */

/*
 * Quick and dirty trampoline to get into 64 bit (long) mode and running
 * with paging enabled so that we enter the kernel at its linked address.
 */
#define MSR_EFER	0xc0000080
#define EFER_LME	0x00000100
#define CR4_PAE		0x00000020
#define CR4_PSE		0x00000010
#define CR0_PG		0x80000000

/* GRRR. Deal with BTX that links us for a non-zero location */
#define VPBASE	0xa000
#define VTOP(x)	((x) + VPBASE)

	.data

	.p2align 12,0x40

	.globl	PT4
PT4:
	.space	0x1000
	.globl	PT3
PT3:
	.space	0x1000
	.globl	PT2
PT2:
	.space	0x1000

gdtdesc:
	.word	gdtend - gdt
	.long	VTOP(gdt)		# low
	.long	0			# high

gdt:
	.long	0			# null descriptor
	.long	0
	.long	0x00000000		# %cs
	.long	0x00209800
	.long	0x00000000		# %ds
	.long	0x00008000
gdtend:
	
	.text
	.code32

	.globl	amd64_tramp
amd64_tramp:
	/* Be sure that interrupts are disabled */
	cli

	/* Turn on EFER.LME */
	movl	$MSR_EFER, %ecx
	rdmsr
	orl	$EFER_LME, %eax
	wrmsr

	/* Turn on PAE */
	movl	%cr4, %eax
	orl	$CR4_PAE, %eax
	movl	%eax, %cr4

	/* Set %cr3 for PT4 */
	movl	$VTOP(PT4), %eax
	movl	%eax, %cr3

	/* Turn on paging (implicitly sets EFER.LMA) */
	movl	%cr0, %eax
	orl	$CR0_PG, %eax
	movl	%eax, %cr0
	
	/* Now we're in compatibility mode. set %cs for long mode */
	movl	$VTOP(gdtdesc), %eax
	movl	VTOP(entry_hi), %esi
	movl	VTOP(entry_lo), %edi
	lgdt	(%eax)
	ljmp	$0x8, $VTOP(longmode)

	.code64
longmode:
	/* We're still running V=P, jump to entry point */
	movl	%esi, %eax
	salq	$32, %rax
	orq	%rdi, %rax
	pushq	%rax
	ret
