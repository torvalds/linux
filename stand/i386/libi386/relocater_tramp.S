/*-
 * Copyright 2015 Toomas Soome <tsoome@me.com>
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
 * relocate is needed to support loading code which has to be located
 * below 1MB, as both BTX and loader are using low memory area.
 *
 * relocate and start loaded code. Since loaded code may need to be
 * placed to already occupied memory area, this code is moved to safe
 * memory area and then btx __exec will be called with physical pointer
 * to this area. __exec will set pointer to %eax and use call *%eax,
 * so on entry, we have new "base" address in %eax.
 *
 * Relocate will first set up and load new safe GDT to shut down BTX,
 * then loaded code will be relocated to final memory location,
 * then machine will be switched from 32bit protected mode to 16bit
 * protected mode following by switch to real mode with A20 enabled or
 * disabled. Finally the loaded code will be started and it will take
 * over the whole system.
 *
 * For now, the known "safe" memory area for relocate is 0x600,
 * the actual "free" memory is supposed to start from 0x500, leaving
 * first 0x100 bytes in reserve. As relocate code+data is very small,
 * it will leave enough space to set up boot blocks to 0:7c00 or load
 * linux kernel below 1MB space.
 */
/*
 * segment selectors
 */
		.set SEL_SCODE,0x8
		.set SEL_SDATA,0x10
		.set SEL_RCODE,0x18
		.set SEL_RDATA,0x20

		.p2align	4
		.globl relocater
relocater:
		cli
		/*
		 * set up GDT from new location
		 */
		movl	%eax, %esi		/* our base address */
		add	$(relocater.1-relocater), %eax
		jmp	*%eax
relocater.1:
		/* set up jump */
		lea	(relocater.2-relocater)(%esi), %eax
		movl	%eax, (jump_vector-relocater) (%esi)

		/* set up gdt */
		lea	(gdt-relocater) (%esi), %eax
		movl	%eax, (gdtaddr-relocater) (%esi)

		/* load gdt */
		lgdt	(gdtdesc - relocater) (%esi)
		lidt	(idt-relocater) (%esi)

		/* update cs */
		ljmp *(jump_vector-relocater) (%esi)

		.code32
relocater.2:
		xorl	%eax, %eax
		movb	$SEL_SDATA, %al
		movw	%ax, %ss
		movw	%ax, %ds
		movw	%ax, %es
		movw	%ax, %fs
		movw	%ax, %gs
		movl	%cr0, %eax		/* disable paging */
		andl	$~0x80000000,%eax
		movl	%eax, %cr0
		xorl	%ecx, %ecx		/* flush TLB */
		movl	%ecx, %cr3
		cld
/*
 * relocate data loop. load source, dest and size from
 * relocater_data[i], 0 value will stop the loop.
 * registers used for move: %esi, %edi, %ecx.
 * %ebx to keep base
 * %edx for relocater_data offset
 */
		movl	%esi, %ebx		/* base address */
		xorl	%edx, %edx
loop.1:
		movl	(relocater_data-relocater)(%ebx, %edx, 4), %eax
		testl	%eax, %eax
		jz	loop.2
		movl	(relocater_data-relocater)(%ebx, %edx, 4), %esi
		inc	%edx
		movl	(relocater_data-relocater)(%ebx, %edx, 4), %edi
		inc	%edx
		movl	(relocater_data-relocater)(%ebx, %edx, 4), %ecx
		inc	%edx
		rep
		movsb
		jmp	loop.1
loop.2:
		movl	%ebx, %esi		/* restore esi */
		/*
		 * data is relocated, switch to 16bit mode
		 */
		lea	(relocater.3-relocater)(%esi), %eax
		movl	%eax, (jump_vector-relocater) (%esi)
		movl	$SEL_RCODE, %eax
		movl	%eax, (jump_vector-relocater+4) (%esi)

		ljmp *(jump_vector-relocater) (%esi)
relocater.3:
		.code16

		movw	$SEL_RDATA, %ax
		movw	%ax, %ds
		movw	%ax, %es
		movw	%ax, %fs
		movw	%ax, %gs
		movw	%ax, %ss
		lidt	(idt-relocater) (%esi)
		lea	(relocater.4-relocater)(%esi), %eax
		movl	%eax, (jump_vector-relocater) (%esi)
		xorl	%eax, %eax
		movl	%eax, (jump_vector-relocater+4) (%esi)
		/* clear PE */
		movl	%cr0, %eax
		dec	%al
		movl	%eax, %cr0
		ljmp *(jump_vector-relocater) (%esi)
relocater.4:
		xorw	%ax, %ax
		movw	%ax, %ds
		movw	%ax, %es
		movw	%ax, %fs
		movw	%ax, %gs
		movw	%ax, %ss
		/*
		 * set real mode irq offsets
		 */
		movw	$0x7008,%bx
		in $0x21,%al			# Save master
		push %ax			#  IMR
		in $0xa1,%al			# Save slave
		push %ax			#  IMR
		movb $0x11,%al			# ICW1 to
		outb %al,$0x20			#  master,
		outb %al,$0xa0			#  slave
		movb %bl,%al			# ICW2 to
		outb %al,$0x21			#  master
		movb %bh,%al			# ICW2 to
		outb %al,$0xa1			#  slave
		movb $0x4,%al			# ICW3 to
		outb %al,$0x21			#  master
		movb $0x2,%al			# ICW3 to
		outb %al,$0xa1			#  slave
		movb $0x1,%al			# ICW4 to
		outb %al,$0x21			#  master,
		outb %al,$0xa1			#  slave
		pop %ax				# Restore slave
		outb %al,$0xa1			#  IMR
		pop %ax				# Restore master
		outb %al,$0x21			#  IMR
						# done
		/*
		 * Should A20 be left enabled?
		 */
		/* movw imm16, %ax */
		.byte	0xb8
		.globl	relocator_a20_enabled
relocator_a20_enabled:
		.word	0
		test	%ax, %ax
		jnz	a20_done

		movw	$0xa00, %ax
		movw	%ax, %sp
		movw	%ax, %bp

		/* Disable A20 */
		movw	$0x2400, %ax
		int	$0x15
#		jnc	a20_done

		call	a20_check_state
		testb	%al, %al
		jz	a20_done

		inb	$0x92
		andb	$(~0x03), %al
		outb	$0x92
		jmp	a20_done

a20_check_state:
		movw	$100, %cx
1:
		xorw	%ax, %ax
		movw	%ax, %ds
		decw	%ax
		movw	%ax, %es
		xorw	%ax, %ax
		movw	$0x8000, %ax
		movw	%ax, %si
		addw	$0x10, %ax
		movw	%ax, %di
		movb	%ds:(%si), %dl
		movb	%es:(%di), %al
		movb	%al, %dh
		decb	%dh
		movb	%dh, %ds:(%si)
		outb	%al, $0x80
		outb	%al, $0x80
		movb	%es:(%di), %dh
		subb	%dh, %al
		xorb	$1, %al
		movb	%dl, %ds:(%si)
		testb	%al, %al
		jz	a20_done
		loop	1b
		ret
a20_done:
		/*
		 * set up registers
		 */
		/* movw imm16, %ax. */
		.byte	0xb8
		.globl	relocator_ds
relocator_ds:	.word	0
		movw	%ax, %ds

		/* movw imm16, %ax. */
		.byte	0xb8
		.globl	relocator_es
relocator_es:	.word	0
		movw	%ax, %es

		/* movw imm16, %ax. */
		.byte	0xb8
		.globl	relocator_fs
relocator_fs:	.word	0
		movw	%ax, %fs

		/* movw imm16, %ax. */
		.byte	0xb8
		.globl	relocator_gs
relocator_gs:	.word	0
		movw	%ax, %gs

		/* movw imm16, %ax. */
		.byte	0xb8
		.globl	relocator_ss
relocator_ss:	.word	0
		movw	%ax, %ss

		/* movw imm16, %ax. */
		.byte	0xb8
		.globl	relocator_sp
relocator_sp:	.word	0
		movzwl	%ax, %esp

		/* movw imm32, %eax. */
		.byte	0x66, 0xb8
		.globl	relocator_esi
relocator_esi:	.long	0
		movl	%eax, %esi

		/* movw imm32, %edx. */
		.byte	0x66, 0xba
		.globl	relocator_edx
relocator_edx:	.long	0

		/* movw imm32, %ebx. */
		.byte	0x66, 0xbb
		.globl	relocator_ebx
relocator_ebx:	.long	0

		/* movw imm32, %eax. */
		.byte	0x66, 0xb8
		.globl	relocator_eax
relocator_eax:	.long	0

		/* movw imm32, %ebp. */
		.byte	0x66, 0xbd
		.globl	relocator_ebp
relocator_ebp:	.long	0

		sti
		.byte 0xea			 /* ljmp */
		.globl relocator_ip
relocator_ip:
		.word 0
		.globl relocator_cs
relocator_cs:
		.word 0

/* GDT to reset BTX */
		.code32
		.p2align	4
jump_vector:	.long	0
		.long	SEL_SCODE

gdt:		.word 0x0, 0x0			/* null entry */
		.byte 0x0, 0x0, 0x0, 0x0
		.word 0xffff, 0x0		/* SEL_SCODE */
		.byte 0x0, 0x9a, 0xcf, 0x0
		.word 0xffff, 0x0		/* SEL_SDATA */
		.byte 0x0, 0x92, 0xcf, 0x0
		.word 0xffff, 0x0		/* SEL_RCODE */
		.byte 0x0, 0x9a, 0x0f, 0x0
		.word 0xffff, 0x0		/* SEL_RDATA */
		.byte 0x0, 0x92, 0x0f, 0x0
gdt.1:

gdtdesc:	.word gdt.1 - gdt - 1		/* limit */
gdtaddr:	.long 0				/* base */

idt:		.word 0x3ff
		.long 0

		.globl relocater_data
relocater_data:
		.long 0			/* src */
		.long 0			/* dest */
		.long 0			/* size */
		.long 0			/* src */
		.long 0			/* dest */
		.long 0			/* size */
		.long 0			/* src */
		.long 0			/* dest */
		.long 0			/* size */
		.long 0

		.globl relocater_size
relocater_size:
		.long relocater_size-relocater
