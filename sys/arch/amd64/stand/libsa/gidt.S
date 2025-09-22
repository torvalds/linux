/*	$OpenBSD: gidt.S,v 1.13 2022/12/08 01:25:44 guenther Exp $	*/

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

	.file "gidt.S"

#include <machine/asm.h>
#define _LOCORE
#include <machine/trap.h>
#undef _LOCORE
#include <assym.h>

#include "gidt.h"

#ifdef GIDT_DEBUG
#define	gidt_debug0		; \
	mov	$0xb8000, %eax	; \
	mov	$0x47314730, (%eax)
#define	gidt_debug1		; \
	mov	$(0xb8000 - LINKADDR), %eax	; \
	mov	$0x4f314f30, (%eax)
#define	gidt_debug2		; \
	mov	$0xb8004, %eax	; \
	mov	$0x47334732, (%eax)
#define	gidt_debug3		; \
	mov	$0xb8004, %eax	; \
	mov	$0x4f334f32, (%eax)
#define gidt_debug4		; \
	movl	$0xb8008, %eax	; \
	movl	$0x47344733, (%eax)
#else
#define gidt_debug0 /* gidt_debug0 */
#define gidt_debug1 /* gidt_debug1 */
#define gidt_debug2 /* gidt_debug2 */
#define gidt_debug3 /* gidt_debug3 */
#define gidt_debug4 /* gidt_debug4 */
#endif

#define prot2real						\
	gidt_debug0;						\
								\
	ljmp	$S16TEXT, $1f - LINKADDR;			\
1:								\
	.code16;						\
	movw	$S16DATA, %ax;					\
	movw	%ax, %ds;					\
	movw	%ax, %es;					\
	gidt_debug1;						\
								\
	movl	%cr0, %eax;	/* disable pmmm */		\
	andl 	$~CR0_PE, %eax;					\
	movl	%eax, %cr0;					\
								\
	/* reload real cs:ip */					\
	data32 ljmp	$(LINKADDR >> 4), $1f - LINKADDR;	\
1:								\
	movw	%cs, %ax;	/* setup: %ds, %es, %ss = %cs */ \
	movw	%ax, %ds;					\
	movw	%ax, %es;					\
	xorw	%ax, %ax;					\
	movw	%ax, %ss;					\
								\
	gidt_debug2;						\
								\
	data32 addr32 lidt (Idtr_real - LINKADDR); /* load idtr for real mode */

#define real2prot						\
	gidt_debug3;						\
								\
	movw	$LINKADDR >> 4, %ax;				\
	movw	%ax, %ds;					\
	data32 addr32 lgdt (Gdtr - LINKADDR); 	/* load the gdtr */	\
								\
	movl	%cr0, %eax;	/* enable pmmm */		\
	orl	$CR0_PE, %eax;					\
	movl	%eax, %cr0;					\
								\
	data32 ljmp	$S32TEXT, $1f;   /* reload %cs,flush pipeline */\
1:								\
	.code32;						\
	/* reload 32bit %ds, %ss, %es */			\
	mov	$S32DATA, %eax;					\
	mov	%ax, %ds;					\
	mov	%ax, %ss;					\
	mov	%ax, %es;					\
								\
	gidt_debug4;						\
								\
	/* load idtr for debugger and DOS/BIOS iface */		\
	lidt	Idtr;


	.globl	BIOS_regs

	.text
	.code32
	.globl	pmm_init
	.globl	_rtt

ENTRY(_rtt)
#ifdef SOFTRAID
	call	sr_clear_keys
#endif
#ifdef GIDT_DEBUG
	movl	$0xb8000, %ebx
	movl	$0x4f514f51, (%ebx)
#endif
	movw	$0x1234, %ax
	movw	%ax, 0x472	/* warm boot */

	/* Try to use the KBD to reboot system */
	movb	$0xfe, %al
	outb	%al, $0x64

	movl	$0x5000, %ecx
1:	inb	$0x84, %al
	loop	1b

	movb	$0xfe, %al
	outb	%al, $0x64

#ifdef GIDT_DEBUG
	movl	$0xb8000, %ebx
	movl	$0x07310731, (%ebx)
#endif

	/* Try to cause a triple fault... */
	lidt	Idtr_reset
	xorl	%eax, %eax
	divl	%eax, %eax

	/* Again... */
	int $0x8

	/* Again... */
	movl	$0, %esp	/* segment violation */
	ret

#define IPROC(n)	X##n
#define IEMU(n)		IPROC(emu##n)

create_idt_entry:
	movw	%ax, (%ebx)
	movw	$S32TEXT, 2(%ebx)
	movw	$((0x80|SDT_SYS386TGT) << 8), 4(%ebx)
	shr	$16, %eax
	movw	%ax, 6(%ebx)
	addl	$8, %ebx
	ret

	.align	8, 0x90
pmm_init:

#define idte(e)	\
	movl $IPROC(e), %eax; call create_idt_entry
#define idtb(b)	idte(emu##b)

	/* Build interrupt descriptor table. */
	/* Maskable interrupts (32-255) */
	movl	$idt, %ebx
	movl	$Idtr, %eax
	movw	$(640 - 1), (%eax)
	movl	%ebx, 2(%eax)

	/* Internal (0-31) */
	idte(de); idte(db); idte(nmi); idte(bp); idte(of); idte(br)
	idte(ud); idte(nm); idte(df);  idte(fo); idte(ts); idte(np)
	idte(ss); idte(gp); idte(pf);  idte(xx); idte(mf); idte(ac)
	idte(xx)
	idte(xx); idte(xx); idte(xx);  idte(xx); idte(xx); idte(xx)
	idte(xx); idte(xx); idte(xx);  idte(xx); idte(xx); idte(xx)
	idte(xx) 
	/* BIOS entry points (32-63) */
	idtb(0);  idtb(1);  idtb(2);  idtb(3);  idtb(4);  idtb(5)
	idtb(6);  idtb(7);  idtb(8);  idtb(9);  idtb(10); idtb(11)
	idtb(12); idtb(13); idtb(14); idtb(15); idtb(16); idtb(17)
	idtb(18); idtb(19); idtb(20); idtb(21); idtb(22); idtb(23)
	idtb(24); idtb(25); idtb(26); idtb(27); idtb(28); idtb(29)
	idtb(30); idtb(31); idtb(32); idtb(33); idtb(34); idtb(35)
	idtb(36); idtb(37); idtb(38); idtb(39); idtb(40); idtb(41)
	idtb(42); idtb(43); idtb(44); idtb(45); idtb(46); idtb(47)
	/* DOS entry points (64-80) */

#undef idtb
#undef idte

	/* load idtr for interrupts */
	lidt	Idtr
	ret

	.bss
	.align 8, 0x90
idt:
	/* IDT has 80 entries at 8 bytes each. */
	.space  640

	.globl	Idtr
Idtr:	.word	0  // 640 - 1
	.long	0  // idt
	.word	0

	.text
	.align	8
	.globl	Idtr_real
Idtr_real:
	.word	1023
	.long	0
	.word	0

	.align	8
Idtr_reset:
	.long	0, 0

	.align	8
gdt:
		/* 0x00 : null */
	.space	8
		/* 0x08 : flat code */
	.word	0xFFFF			# lolimit
	.word	0			# lobase
	.byte	0			# midbase
	.byte	SDT_MEMERAC | 0 | 0x80	# RXAC, dpl = 0, present
	.byte	0xf | 0 | 0x40 | 0x80	# hilimit, xx, 32bit, 4k granularity
	.byte	0			# hibase
		/* 0x10 : flat data */
	.word	0xFFFF			# lolimit
	.word	0			# lobase
	.byte	0			# midbase
	.byte	SDT_MEMRWA | 0 | 0x80	# RWA, dpl = 0, present
	.byte	0xf | 0 | 0x40 | 0x80	# hilimit, xx, 32bit, 4k granularity
	.byte	0			# hibase
		/* 0x18 : 16 bit code */
	.word	0xFFFF			# lolimit
	.word	(LINKADDR & 0xffff)	# lobase
	.byte	(LINKADDR >> 16) & 0xff	# midbase
	.byte	SDT_MEMERAC | 0 | 0x80	# RXAC, dpl = 0, present
	.byte	0x0 | 0 | 0 | 0		# hilimit, xx, 16bit, byte granularity
	.byte	(LINKADDR >> 20) & 0xff	# hibase
		/* 0x20 : 16 bit data */
	.word	0xFFFF			# lolimit
	.word	(LINKADDR & 0xffff)	# lobase
	.byte	(LINKADDR >> 16) & 0xff	# midbase
	.byte	SDT_MEMRWA | 0 | 0x80	# RWA, dpl = 0, present
	.byte	0x0 | 0 | 0 | 0		# hilimit, xx, 16bit, byte granularity
	.byte	(LINKADDR >> 20) & 0xff	# hibase

.globl Gdtr
Gdtr:	.word	. - gdt - 1
	.long	gdt
	.word	0

#define IENTRY(name,type) \
IPROC(name): \
	pushl	$type ; \
	jmp	1f
#define IENTRY_ERR(name,err,type) \
IPROC(name): \
	pushl	$err ; \
	pushl	$type ; \
	jmp	1f

IPROC(xx):
	pushl	$1
	pushl	$T_RESERVED
	jmp	1f

IENTRY_ERR(de,0,T_DIVIDE)	/* #DE divide by zero */
IENTRY_ERR(db,0,T_TRCTRAP)	/* #DB debug */
IENTRY_ERR(nmi,0,T_NMI)		/* NMI */
IENTRY_ERR(bp,0,T_BPTFLT)	/* #BP breakpoint */
IENTRY_ERR(of,0,T_OFLOW)	/* #OF overflow */
IENTRY_ERR(br,0,T_BOUND)	/* #BR BOUND range exceeded */
IENTRY_ERR(ud,0,T_PRIVINFLT)	/* #UD invalid opcode */
IENTRY_ERR(nm,0,T_DNA)		/* #NM device not available */
IENTRY(df,T_DOUBLEFLT)		/* #DF double fault */
IENTRY_ERR(fo,0,T_FPOPFLT)	/* #FO coprocessor segment overrun */
IENTRY(ts,T_TSSFLT)		/* #TS invalid TSS */
IENTRY(np,T_SEGNPFLT)		/* #NP segment not present */
IENTRY(ss,T_STKFLT)		/* #SS stack fault */
IENTRY(gp,T_PROTFLT)		/* #GP general protection */
IENTRY(pf,T_PAGEFLT)		/* #PF page fault */
IENTRY_ERR(mf,0,T_ARITHTRAP)	/* #MF floating point error */
IENTRY(ac,T_ALIGNFLT)		/* #AC alignment check */
1:
	cli
	hlt

#define	IEMUENT(n)	IEMU(n): pushl $n; jmp 1f

IEMUENT(0);  IEMUENT(1);  IEMUENT(2);  IEMUENT(3)
IEMUENT(4);  IEMUENT(5);  IEMUENT(6);  IEMUENT(7)
IEMUENT(8);  IEMUENT(9);  IEMUENT(10); IEMUENT(11)
IEMUENT(12); IEMUENT(13); IEMUENT(14); IEMUENT(15)
IEMUENT(16); IEMUENT(17); IEMUENT(18); IEMUENT(19)
IEMUENT(20); IEMUENT(21); IEMUENT(22); IEMUENT(23)
IEMUENT(24); IEMUENT(25); IEMUENT(26); IEMUENT(27)
IEMUENT(28); IEMUENT(29); IEMUENT(30); IEMUENT(31)
1:	jmp	EMUh	/* redirect for short jumps */
IEMUENT(32); IEMUENT(33); IEMUENT(34); IEMUENT(35)
IEMUENT(36); IEMUENT(37); IEMUENT(38); IEMUENT(39)
IEMUENT(40); IEMUENT(41); IEMUENT(42); IEMUENT(43)
IEMUENT(44); IEMUENT(45); IEMUENT(46); IEMUENT(47)
1:	jmp	EMUh

/*
 * entry point for BIOS real-mode interface
 * all the magic for real-prot mode switching is here
 *
 * Note: Once in real mode access to .data or .bss should be avoided since it
 * may not be reachable within the current segment. The following code also
 * assumes that .text is writeable.
 *
 * Call:	%eax, %ecx, %edx, %ebx, %ebp, %esi, %edi, %es, %ds
 * Return:	%eax, %edx, %ecx, %eflags (as returned from BIOS)
 *
 */
	.globl	EMUh
	.align	8, 0x90
EMUh:
	/* save %eax */
	mov	%eax, 5f
	pop	%eax

	pusha
	push	%ds
	push	%es
	push	%fs
	push	%gs

	/* save BIOS int vector */
	mov	%al, intno

	/* Load BIOS registers prior to switching to real mode. */
	movl	BIOS_regs+BIOSR_ES, %eax
	mov	%eax, 7f
	movl	BIOS_regs+BIOSR_DS, %eax
	mov	%eax, 6f

	prot2real

	push	%ds

	# data32 movl $Leax, %eax
	.byte	0x66, 0xb8
7:	.long	0x90909090
	mov	%ax, %es

	# data32 movl $Leax, %eax
	.byte	0x66, 0xb8
6:	.long	0x90909090
	mov	%ax, %ds

	# data32 movl $Leax, %eax
	.byte	0x66, 0xb8
5:	.long	0x90909090

	;sti
	int	$0
intno	= . - 1
	;cli

	pop	%ds

	/* Preserve BX and ES for protected mode. */
	addr32 movl %eax, (2f - LINKADDR)
	movl	%ebx, %eax
	addr32 movl %eax, (4f - LINKADDR)
	movl	%es, %eax
	addr32 movl %eax, (3f - LINKADDR)
	addr32 movl (2f - LINKADDR), %eax

	movb	%ah, %bh
	lahf
	xchgb	%ah, %bh

	/* Preserve AX for protected mode. */
	addr32 movl %eax, (2f - LINKADDR)

	real2prot

	# movl $Leax, %eax
	.byte	0xb8
4:	.long	0x90909090
	movl	%eax, BIOS_regs+BIOSR_BX

	# movl $Leax, %eax
	.byte	0xb8
3:	.long	0x90909090
	movl	%eax, BIOS_regs+BIOSR_ES

	# movl $Leax, %eax
	.byte	0xb8
2:	.long	0x90909090

	/* pass BIOS return values back to caller */
	movl	%eax, 0xb*4(%esp)
	movl	%ecx, 0xa*4(%esp)
	movl	%edx, 0x9*4(%esp)
	movb	%bh , 0xe*4(%esp)

	/* save registers into save area */
	movl	%eax, BIOS_regs+BIOSR_AX
	movl	%ecx, BIOS_regs+BIOSR_CX
	movl	%edx, BIOS_regs+BIOSR_DX
	movl	%ebp, BIOS_regs+BIOSR_BP
	movl	%esi, BIOS_regs+BIOSR_SI
	movl	%edi, BIOS_regs+BIOSR_DI

	/* clear NT flag in eflags */
	pushf
	pop	%eax
	and	$0xffffbfff, %eax
	push	%eax
	popf

	pop	%gs
	pop	%fs
	pop	%es
	pop	%ds
	popa
	iret

/* Call buffer at 07c0:0000 in real mode to simulate a BIOS boot */
ENTRY(bootbuf)
	pop	%eax		/* Don't need return address */
	pop	%esi		/* Buffer */
	pop	%edx		/* Device */
	prot2real		/* Switch */

	/* Set up stack */
	cli
	xor	%ax, %ax
	mov	%ax, %ss
	mov	$0xfffc, %esp
	sti

	/* Jump to buffer */
	ljmp	$0x0, $0x7c00

	.end
