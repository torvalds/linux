/*-
 * Copyright (c) 2001 Takanori Watanabe <takawata@jp.freebsd.org>
 * Copyright (c) 2001 Mitsuru IWASAKI <iwasaki@jp.freebsd.org>
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
 * Resume entry point for real mode.
 *
 * If XFirmwareWakingVector is zero and FirmwareWakingVector is non-zero
 * in FACS, the BIOS enters here in real mode after POST with CS set to
 * (FirmwareWakingVector >> 4) and IP set to (FirmwareWakingVector & 0xf).
 * Depending on the previous sleep state, we may need to initialize more
 * of the system (i.e., S3 suspend-to-RAM vs. S4 suspend-to-disk).
 *
 * Note: If XFirmwareWakingVector is non-zero, it should disable address
 * translation/paging and interrupts, load all segment registers with
 * a flat 4 GB address space, and set EFLAGS.IF to zero.  Currently
 * this mode is not supported by this code.
 */

	.data				/* So we can modify it */

	ALIGN_TEXT
	.code16
wakeup_start:
	/*
	 * Set up segment registers for real mode, a small stack for
	 * any calls we make, and clear any flags.
	 */
	cli				/* make sure no interrupts */
	mov	%cs, %ax		/* copy %cs to %ds.  Remember these */
	mov	%ax, %ds		/* are offsets rather than selectors */
	mov	%ax, %ss
	movw	$PAGE_SIZE, %sp
	xorw	%ax, %ax
	pushw	%ax
	popfw

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

	/* Turn on the PAE bit for when paging is enabled */
	mov	%cr4, %eax
	orl	$CR4_PAE, %eax
	mov	%eax, %cr4

	/*
	 * Enable EFER.LME so that we get long mode when all the prereqs are
	 * in place.  In this case, it turns on when CR0_PG is finally enabled.
	 * Also it picks up a few other EFER bits that we'll use need we're
	 * here, like SYSCALL and NX enable.
	 */
	movl	$MSR_EFER, %ecx
	movl	wakeup_efer - wakeup_start(%ebx), %eax
	movl	wakeup_efer + 4 - wakeup_start(%ebx), %edx
	wrmsr

	/*
	 * Point to the embedded page tables for startup.  Note that this
	 * only gets accessed after we're actually in 64 bit mode, however
	 * we can only set the bottom 32 bits of %cr3 in this state.  This
	 * means we are required to use a temporary page table that is below
	 * the 4GB limit.  %ebx is still our relocation base.  We could just
	 * subtract 3 * PAGE_SIZE, but that would be too easy.
	 */
	leal	wakeup_pagetables - wakeup_start(%ebx), %eax
	movl	(%eax), %eax
	mov	%eax, %cr3

	/*
	 * Finally, switch to long bit mode by enabling paging.  We have
	 * to be very careful here because all the segmentation disappears
	 * out from underneath us.  The spec says we can depend on the
	 * subsequent pipelined branch to execute, but *only if* everything
	 * is still identity mapped.  If any mappings change, the pipeline
	 * will flush.
	 */
	mov	%cr0, %eax
	orl	$CR0_PG, %eax
	mov	%eax, %cr0

	/*
	 * At this point paging is enabled, and we are in "compatibility" mode.
	 * We do another far jump to reload %cs with the 64 bit selector.
	 * %cr3 points to a 4-level page table page.
	 * We cannot yet jump all the way to the kernel because we can only
	 * specify a 32 bit linear address.  So, yet another trampoline.
	 *
	 * The following instruction is:
	 * ljmp $bootcode64 - bootgdt, $wakeup_64 - wakeup_start
	 * but gas cannot assemble that.  And besides, we patch the targets
	 * in early startup and its a little clearer what we are patching.
	 */
wakeup_sw64:
	.byte	0xea			/* opcode for far jump */
	.long	wakeup_64 - wakeup_start /* offset in segment */
	.word	bootcode64 - bootgdt	/* index in gdt for 64 bit code */

	/*
	 * Yeehar!  We're running in 64-bit mode!  We can mostly ignore our
	 * segment registers, and get on with it.
	 * Note that we are running at the correct virtual address, but with
	 * a 1:1 1GB mirrored mapping over entire address space.  We had better
	 * switch to a real %cr3 promptly so that we can get to the direct map
	 * space. Remember that jmp is relative and that we've been relocated,
	 * so use an indirect jump.
	 */
	ALIGN_TEXT
	.code64
wakeup_64:
	mov	$bootdata64 - bootgdt, %eax
	mov	%ax, %ds

	/* Restore arguments. */
	movq	wakeup_pcb - wakeup_start(%rbx), %rdi
	movq	wakeup_ret - wakeup_start(%rbx), %rax

	/* Restore GDT. */
	lgdt	wakeup_gdt - wakeup_start(%rbx)

	/* Jump to return address. */
	jmp	*%rax

	.data

resume_beep:
	.byte	0
reset_video:
	.byte	0

	ALIGN_DATA
bootgdt:
	.long	0x00000000
	.long	0x00000000
	.long	0x00000000
	.long	0x00000000
	.long	0x00000000
	.long	0x00000000
	.long	0x00000000
	.long	0x00000000

bootcode64:
	.long	0x0000ffff
	.long	0x00af9b00

bootdata64:
	.long	0x0000ffff
	.long	0x00af9300

bootcode32:
	.long	0x0000ffff
	.long	0x00cf9b00

bootdata32:
	.long	0x0000ffff
	.long	0x00cf9300
bootgdtend:

wakeup_pagetables:
	.long	0

bootgdtdesc:
	.word	bootgdtend - bootgdt	/* Length */
	.long	bootgdt - wakeup_start	/* Offset plus %ds << 4 */

	ALIGN_DATA
wakeup_pcb:
	.quad	0
wakeup_ret:
	.quad	0
wakeup_efer:
	.quad	0
wakeup_gdt:
	.word	0
	.quad	0
dummy:
