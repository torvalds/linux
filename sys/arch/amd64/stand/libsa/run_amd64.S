/*
 * Copyright (c) 2019 Mike Larkin <mlarkin@openbsd.org>
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

#define CODE_SEGMENT	0x8
#define DATA_SEGMENT	0x10

	.text
	.code32
	.global	launch_amd64_kernel_long
	/*
	 * void launch_amd64_kernel_long(caddr_t base, caddr_t pml4,
	 *     caddr_t rsp, uint64_t entry, int boothowto, int bootdev,
	 *     int bootapiver, uint64_t end, int extmem, int cnvmem,
	 *     int ac, uint64_t av);
	 */
launch_amd64_kernel_long:
asm_start:
	xchg	%bx, %bx

	/*
	 * We are in 32 bit mode
	 *  - compute indirect jump targets
	 *  - compute GDT locations
	 */
	popl	%edi		/* Discard return address */
	popl	%edi		/* %edi = bootloader base address */

	/* Current mode -> 32 bit jump target */
	leal	(prot_mode - asm_start)(%edi), %eax
	movl	%eax, (start32r - asm_start)(%edi)

	/* 32 bit -> 64 bit jump target */
	leal	(long_mode - asm_start)(%edi), %eax
	movl	%eax, (start64r - asm_start)(%edi)

	/* Current mode -> 32 bit GDT */
	leal	(gdt32 - asm_start)(%edi), %eax
	movl	%eax, (gdtrr32 - asm_start)(%edi)

	/* 32 bit -> 64 bit GDT */
	leal	(gdt64 - asm_start)(%edi), %eax
	movl	%eax, (gdtrr64 - asm_start)(%edi)

	cli

	lgdtl	(gdtr32 - asm_start)(%edi)

	mov	%cr0,	%eax
	orl	$(CR0_PE), %eax
	mov	%eax,	%cr0

	ljmpl	*(start32r - asm_start)(%edi)

	.align 4
prot_mode:
	movw	$DATA_SEGMENT, %ax
	movw	%ax, %ds
	movw	%ax, %es
	movw	%ax, %gs
	movw	%ax, %ss
	movw	%ax, %fs

	mov	$(CR4_PAE | CR4_PSE), %eax
	mov	%eax, %cr4

	lgdtl	(gdtr64 - asm_start)(%edi)
	movl	$MSR_EFER, %ecx
	xorl	%edx, %edx
	movl	$(EFER_LME | EFER_NXE | EFER_SCE), %eax
	wrmsr

	movl	(%esp), %eax	/* first arg - PML4 */
	movl	4(%esp), %ebx	/* second arg - kernel stack */
	movl	%eax, %cr3

	jmp	1f
1:	jmp	1f
1:	movl	%cr0, %eax
	orl	$CR0_DEFAULT, %eax
	movl	%eax, %cr0

	jmp	1f
1:	jmp	1f
1:	ljmpl	*(start64r - asm_start)(%edi)

long_mode:
	.code64
	movq	%rsp, %rax
	movq	%rbx, %rsp

	/*
	 * params stack at %rax right now:
	 *
	 * +0	32 bit PML4 ptr
	 * +4	32 bit kernel stack
	 * +8	start va low bytes
	 + +12  start va high bytes
	 * +16	boothowto			(%rdi)
	 * +20	bootdev				(%rsi)
	 * +24	BOOTARG_APIVER			(%rdx)
	 * +28	marks[MARK_END] low bytes	(%rcx)
	 * +32	marks[MARK_END] high bytes	(%rcx)
	 * +36	extmem		(%r8)
	 * +40	cnvmem		(%r9)
	 * +44	ac		(%rsp, will be %rsp + 8 after call below)
	 * +48	av low bytes	(%rsp + 8, will be %rsp + 16 after call below)
	 * +52	av high bytes	(%rsp + 16, will be %rsp + 24 after call below)
	 */

	movl	16(%rax),	%edi
	movl	20(%rax),	%esi
	movl	24(%rax),	%edx
	movq	28(%rax),	%rcx
	movl	36(%rax),	%r8d
	movl	40(%rax),	%r9d
	movq	48(%rax),	%rbx	/* av */
	pushq	%rbx
	movl	44(%rax),	%ebx	/* ac */
	pushq	%rbx
	movq	8(%rax),	%r11

	/*
	 * start(howto, bootdev, BOOTARG_APIVER, marks[MARK_END],
	 *     extmem, cnvmem, ac, av);
	 */
	call	*%r11

	cli
	hlt
	/* NOTREACHED */

	.align	4
start32r:
	.long	0
	.long	CODE_SEGMENT

start64r:
	.long	0
	.long	CODE_SEGMENT

gdt32:
	.long	0, 0
	.byte	0xff, 0xff, 0x00, 0x00, 0x00, 0x9f, 0xcf, 0x00
	.byte	0xff, 0xff, 0x00, 0x00, 0x00, 0x93, 0xcf, 0x00
gdtr32:
	.word	gdtr32 - gdt32
gdtrr32:
	.quad

	.align 8
gdt64:
        .quad   0x0000000000000000
        .quad   0x00af9a000000ffff
        .quad   0x00cf92000000ffff

gdtr64:
        .word   gdtr64 - gdt64
gdtrr64:
        .quad
