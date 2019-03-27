/*-
 * Copyright (c) 2001 Takanori Watanabe <takawata@jp.freebsd.org>
 * Copyright (c) 2001-2012 Mitsuru IWASAKI <iwasaki@jp.freebsd.org>
 * Copyright (c) 2003 Peter Wemm
 * Copyright (c) 2008-2012 Jung-uk Kim <jkim@FreeBSD.org>
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

#include <machine/asmacros.h>
#include <machine/ppireg.h>
#include <machine/specialreg.h>
#include <machine/timerreg.h>

#include "assym.inc"

/*
 * Resume entry point.  The BIOS enters here in real mode after POST with
 * CS set to the page where we stored this code.  It should configure the
 * segment registers with a flat 4 GB address space and EFLAGS.IF = 0.
 * Depending on the previous sleep state, we may need to initialize more
 * of the system (i.e., S3 suspend-to-RAM vs. S4 suspend-to-disk).
 */

	.data				/* So we can modify it */

	ALIGN_TEXT
	.code16
wakeup_start:
	/*
	 * Set up segment registers for real mode and a small stack for
	 * any calls we make.  Set up full 32-bit bootstrap kernel flags
	 * since resumectx() doesn't restore flags.  PSL_KERNEL gives
	 * bootstrap kernel flags (with interrupts disabled), not normal
	 * kernel flags.
	 */
	cli				/* make sure no interrupts */
	mov	%cs, %ax		/* copy %cs to %ds.  Remember these */
	mov	%ax, %ds		/* are offsets rather than selectors */
	mov	%ax, %ss
	movw	$PAGE_SIZE, %sp
	pushl	$PSL_KERNEL
	popfl

	/* To debug resume hangs, beep the speaker if the user requested. */
	testb	$~0, resume_beep - wakeup_start
	jz	1f
	movb	$0, resume_beep - wakeup_start

	/* Set PIC timer2 to beep. */
	movb	$(TIMER_SEL2 | TIMER_SQWAVE | TIMER_16BIT), %al
	outb	%al, $TIMER_MODE

	/* Turn on speaker. */
	inb	$IO_PPI, %al
	orb	$PIT_SPKR, %al
	outb	%al, $IO_PPI

	/* Set frequency. */
	movw	$0x4c0, %ax
	outb	%al, $TIMER_CNTR2
	shrw	$8, %ax
	outb	%al, $TIMER_CNTR2
1:

	/* Re-initialize video BIOS if the reset_video tunable is set. */
	testb	$~0, reset_video - wakeup_start
	jz	1f
	movb	$0, reset_video - wakeup_start
	lcall	$0xc000, $3

	/* When we reach here, int 0x10 should be ready.  Hide cursor. */
	movb	$0x01, %ah
	movb	$0x20, %ch
	int	$0x10

	/* Re-start in case the previous BIOS call clobbers them. */
	jmp	wakeup_start
1:

	/*
	 * Find relocation base and patch the gdt descript and ljmp targets
	 */
	xorl	%ebx, %ebx
	mov	%cs, %bx
	sall	$4, %ebx		/* %ebx is now our relocation base */

	/*
	 * Load the descriptor table pointer.  We'll need it when running
	 * in 16-bit protected mode.
	 */
	lgdtl	bootgdtdesc - wakeup_start

	/* Enable protected mode */
	movl	$CR0_PE, %eax
	mov	%eax, %cr0

	/*
	 * Now execute a far jump to turn on protected mode.  This
	 * causes the segment registers to turn into selectors and causes
	 * %cs to be loaded from the gdt.
	 *
	 * The following instruction is:
	 * ljmpl $bootcode32 - bootgdt, $wakeup_32 - wakeup_start
	 * but gas cannot assemble that.  And besides, we patch the targets
	 * in early startup and its a little clearer what we are patching.
	 */
wakeup_sw32:
	.byte	0x66			/* size override to 32 bits */
	.byte	0xea			/* opcode for far jump */
	.long	wakeup_32 - wakeup_start /* offset in segment */
	.word	bootcode32 - bootgdt	/* index in gdt for 32 bit code */

	/*
	 * At this point, we are running in 32 bit legacy protected mode.
	 */
	ALIGN_TEXT
	.code32
wakeup_32:

	mov	$bootdata32 - bootgdt, %eax
	mov	%ax, %ds

	/* Restore EFER, CR4 and CR3. */
	movl	wakeup_efer - wakeup_start(%ebx), %eax
	movl	wakeup_efer - wakeup_start + 4(%ebx), %edx
	cmpl	$0, %eax
	jne	1f
	cmpl	$0, %edx
	je	2f
1:	movl	$MSR_EFER, %ecx
	wrmsr
2:	movl	wakeup_cr4 - wakeup_start(%ebx), %eax
	cmpl	$0, %eax
	je	3f
	mov	%eax, %cr4
3:	movl	wakeup_cr3 - wakeup_start(%ebx), %eax
	mov	%eax, %cr3

	/* Get PCB and return address. */
	movl	wakeup_ret - wakeup_start(%ebx), %edx
	movl	wakeup_pcb - wakeup_start(%ebx), %ecx

	/* Enable paging. */
	mov	%cr0, %eax
	orl	$CR0_PG, %eax
	mov	%eax, %cr0

	/* Jump to return address. */
	jmp	*%edx

	.data

resume_beep:
	.byte	0
reset_video:
	.byte	0

	ALIGN_DATA
bootgdt:
	.long	0x00000000
	.long	0x00000000

bootcode32:
	.long	0x0000ffff
	.long	0x00cf9b00

bootdata32:
	.long	0x0000ffff
	.long	0x00cf9300
bootgdtend:

bootgdtdesc:
	.word	bootgdtend - bootgdt	/* Length */
	.long	bootgdt - wakeup_start	/* Offset plus %ds << 4 */

	ALIGN_DATA
wakeup_efer:
	.long	0
	.long	0
wakeup_cr4:
	.long	0
wakeup_cr3:
	.long	0
wakeup_pcb:
	.long	0
wakeup_ret:
	.long	0
wakeup_gdt:		/* not used */
	.word	0
	.long	0
dummy:
