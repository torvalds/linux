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
	.code16
	.globl	_start
_start:
#ifdef DEBUG
	movl	$0xb80a0, %ebx
	addr32 movl $0x07420742, (%ebx)
#endif

/* Clobbers %ax, maybe more */
#define	putc(c)		movb	$c, %al;	call	Lchr

	/*
	 * We are loaded by the PXE loader at location 0x7C00.  Like
	 * the standard /boot, we are linked to run at 0x40120 (load
	 * address 0x40000), so we relocate to there.
	 *
	 * From 0x7C00 to 0x40000 is 0x38400 (230400) bytes, so we don't
	 * have to worry about an overlapping copy until pxeboot is
	 * over 225 KB.
	 *
	 * PXE loads us with a stack that grows down from 0x80000 (512 KB).
	 * While it is unlikely that this will clash with our code that
	 * we're copying up, we create a temporary stack just below us
	 * before the relocate.  We also set the entire %esp register, to
	 * be on the safe side.
	 */
#define PXEBOOTADDR	0x7c00		/* Address where we are loaded by PXE */
	xorw	%ax, %ax
	movw	%ax, %ss		/* CPU disables interrupts till... */
	movl	$PXEBOOTADDR-4, %esp	/* after this instruction */

	pushl	%edx			/* Preserve the drive number. */

	movw	$(PXEBOOTADDR >> 4), %ax /* Reloc from %ds = 0x7c0. */
	movw	$(LINKADDR >> 4), %bx	 /* Reloc to %es = 0x4012. */

	movl	$end, %edx
	subl	$_start, %edx		/* How big are we? */

	/*
	 * Relocate in blocks that are a maximum of 32KB in size, incrementing
	 * the segment registers after each block. The 'rep; movsb' instruction
	 * uses %cx, which limits us to a maximum block size of 0xfff0, even
	 * though we can address the full 64KB within a single segment.
	 */
#define RELOC_BLOCK_SIZE 0x8000
reloc_loop:
	movl	%edx, %ecx
	jcxz	reloc_done
	cmpl	$RELOC_BLOCK_SIZE, %ecx
	jbe	reloc_notrunc
	movl	$RELOC_BLOCK_SIZE, %ecx
reloc_notrunc:
	subl	%ecx, %edx

	movw	%ax, %ds		/* Where we're coming from */
	xorw	%si, %si

	movw	%bx, %es		/* Where we're going to */
	xorw	%di, %di

	cld
	rep; movsb			/* Copy into place */

	addw	$(RELOC_BLOCK_SIZE >> 4), %ax
	addw	$(RELOC_BLOCK_SIZE >> 4), %bx

	jmp	reloc_loop

reloc_done:
	popl	%edx
	jmpl	$(LINKADDR >> 4), $(relocated-_start)	/* Now relocate */

relocated:
	/*
	 * In 16-bit mode, we have segment registers == 0x4012, and
	 * offsets work from here, with offset(_start) == 0.
	 *
	 * In 32-bit mode, we have a flat memory model, where
	 * offset(_start) == 0x40120.  This is how we're linked.
	 *
	 * Now transition to protected mode.
	 *
	 * First, initialise the global descriptor table.
	 */
	cli
	push	%cs
	pop	%ds
	addr32 data32 lgdt (Gdtr - LINKADDR)

	movl	%cr0, %eax
	orl	$CR0_PE, %eax
	data32 movl %eax, %cr0
	data32 ljmp $8, $1f		/* Seg sel 0x08 is flat 32-bit code */
1:
	.code32
	movl	$0x10, %eax		/* Seg sel 0x10 is flat 32-bit data */
	mov	%ax, %ds
	mov	%ax, %es
	mov	%ax, %fs
	mov	%ax, %gs
	mov	%ax, %ss
	movl	$BOOTSTACK, %esp
#ifdef DEBUG
	movl	$0xb8000, %ebx
	movl	$0x07420742, (%ebx)
#endif

	xorl	%edx, %edx
	movl	%edx, bios_bootdev
	pushl	%edx			/* boot() takes this as a parameter */

#ifdef DEBUG
	movl	$0xb80a4, %ebx
	movl	$0x07520752, (%ebx)
#endif

	/* Zero .bss */
	xorl	%eax, %eax
	movl	$end, %ecx
	subl	$edata, %ecx
	movl	$edata, %edi
	cld
	rep;	stosb

	/* Set up an interrupt descriptor table for protected mode. */
	call	pmm_init

	/* Set our program name ("PXEBOOT", not "BOOT"). */
	movl	$pxe_progname, %eax
	movl	%eax, progname

	/*
	 * Now call "main()".
	 *
	 * We run in flat 32-bit protected mode, with no address mapping.
	 */
#ifdef DEBUG
	movl	$0xb8004, %ebx
	movl	$0x07410741, (%ebx)
#endif
	call	boot

	/* boot() should not return.  If it does, reset computer. */
	jmp	_rtt

ENTRY(debugchar)
	pushl	%ebx
	movl	8(%esp), %ebx
	addl	%ebx, %ebx
	addl	$0xb8000, %ebx

	xorl	%eax, %eax
	movb	12(%esp), %al
	
	andl	$0xfffffffe, %ebx
	movb	%al, (%ebx)
	popl	%ebx
	ret

	.code16

/*
 * Write out value in %ax in hex
 */
hex_word:
	pushw	%ax
	mov	%ah, %al
	call	hex_byte
	popw	%ax
	/* fall thru */
/*
 * Write out value in %al in hex
 */
hex_byte:
	pushw	%ax
	shrb	$4, %al
	call	hex_nibble
	popw	%ax
	/* fall thru */

/* Write out nibble in %al */
hex_nibble:
	and	$0x0F, %al
	add	$'0', %al
	cmpb	$'9', %al
	jbe	Lchr
	addb	$'A'-'9'-1, %al
	/* fall thru to Lchr */
/*
 *	Lchr: write the character in %al to console
 */
Lchr:
	pushw	%bx
	movb	$0x0e, %ah
	xorw	%bx, %bx
	incw	%bx		/* movw $0x01, %bx */
	int	$0x10
	popw	%bx
	ret

pxe_progname:
	.asciz	"PXEBOOT"

	.end
