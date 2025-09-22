/*	$OpenBSD: run_i386.S,v 1.3 2022/12/08 01:25:44 guenther Exp $	*/

/*
 * Copyright (c) 2015 YASUOKA Masahiko <yasuoka@yasuoka.net>
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

#include <machine/asm.h>
#include <machine/specialreg.h>

#define	CODE_SEGMENT	0x10
#define	DATA_SEGMENT	0x18

	.globl run_i386_size
run_i386_size:
	.long run_i386_end - run_i386_start

	.align	4
	.text
	.globl	run_i386_start
run_i386_start:
start:
	/*
	 * run_i386(_start) is to call the loaded kernel's start() with
	 * 32bit segment mode from x64 mode.
	 * %rdi == loaded start address, %rsi == kernel start address
	 */

	/* re-arrange the parameters for the x86 calling convention */
	mov	%edx, (run_i386_end - start - 0x20)(%rdi)
	mov	%ecx, (run_i386_end - start - 0x1c)(%rdi)
	mov	%r8d, (run_i386_end - start - 0x18)(%rdi)
	mov	%r9d, (run_i386_end - start - 0x14)(%rdi)
	mov	0x8(%rsp), %edx
	mov	%edx, (run_i386_end - start - 0x10)(%rdi)
	mov	0x10(%rsp), %edx
	mov	%edx, (run_i386_end - start - 0xc)(%rdi)
	mov	0x18(%rsp), %edx
	mov	%edx, (run_i386_end - start - 0x8)(%rdi)
	mov	0x20(%rsp), %edx
	mov	%edx, (run_i386_end - start - 0x4)(%rdi)

	/* Prepare jump address */
	lea	(start32a - start)(%rdi), %rax
	movl	%eax, (start32r - start)(%rdi)

	cli

	/* Setup GDT */
	lea	(gdt - start)(%rdi), %rax
	mov	%rax, (gdtrr - start)(%rdi)
	lgdt	(gdtr - start)(%rdi)

	/* Jump to set %cs */
	ljmp	*(start32r - start)(%rdi)

	.align	4
start32a:
	.code32
	movl	$DATA_SEGMENT, %eax
	movl	%eax, %ds
	movl	%eax, %es
	movl	%eax, %fs
	movl	%eax, %gs
	movl	%eax, %ss

	lea	(run_i386_end - start - 0x20)(%edi), %eax
	mov	%eax, %esp

	/* Disable Paging in CR0 */
	movl	%cr0, %eax
	andl	$(~CR0_PG), %eax
	movl	%eax, %cr0

	/* Disable PAE in CR4 */
	movl	%cr4, %eax
	andl	$(~CR4_PAE), %eax
	movl	%eax, %cr4

	jmp	start32b
start32b:
	.code32

	call	*%esi

	.align	4
start32r:
	.long	0
	.long	CODE_SEGMENT
	.align	4
gdt:
	.long	0, 0
	.long	0, 0
	.byte	0xff, 0xff, 0x00, 0x00, 0x00, 0x9f, 0xcf, 0x00
	.byte	0xff, 0xff, 0x00, 0x00, 0x00, 0x93, 0xcf, 0x00
gdtr:
	.word	gdtr - gdt
gdtrr:
	.quad
start32end:
	/* Space for the stack */
	.align	4
	.space	8192
run_i386_end:
