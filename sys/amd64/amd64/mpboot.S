/*-
 * Copyright (c) 2003 Peter Wemm
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

#include <machine/asmacros.h>		/* miscellaneous asm macros */
#include <machine/specialreg.h>

#include "assym.inc"

	.data				/* So we can modify it */

	.p2align 4,0
	.globl	mptramp_start
mptramp_start:
	.code16
	/*
	 * The AP enters here in response to the startup IPI.
	 * We are in real mode. %cs is the only segment register set.
	 */
	cli				/* make sure no interrupts */
	mov	%cs, %ax		/* copy %cs to %ds.  Remember these */
	mov	%ax, %ds		/* are offsets rather than selectors */
	mov	%ax, %ss

	/*
	 * Find relocation base and patch the gdt descript and ljmp targets
	 */
	xorl	%ebx,%ebx
	mov	%cs, %bx
	sall	$4, %ebx		/* %ebx is now our relocation base */
	orl	%ebx, lgdt_desc-mptramp_start+2
	orl	%ebx, jmp_32-mptramp_start+2
	orl	%ebx, jmp_64-mptramp_start+1

	/*
	 * Load the descriptor table pointer.  We'll need it when running
	 * in 16 bit protected mode.
	 */
	lgdt	lgdt_desc-mptramp_start

	/* Enable protected mode */
	movl	$CR0_PE, %eax
	mov	%eax, %cr0 

	/*
	 * Now execute a far jump to turn on protected mode.  This
	 * causes the segment registers to turn into selectors and causes
	 * %cs to be loaded from the gdt.
	 *
	 * The following instruction is:
	 * ljmpl $bootcode-gdt, $protmode-mptramp_start
	 * but gas cannot assemble that.  And besides, we patch the targets
	 * in early startup and its a little clearer what we are patching.
	 */
jmp_32:
	.byte	0x66			/* size override to 32 bits */
	.byte	0xea			/* opcode for far jump */
	.long	protmode-mptramp_start	/* offset in segment */
	.word	bootcode-gdt		/* index in gdt for 32 bit code */

	/*
	 * At this point, we are running in 32 bit legacy protected mode.
	 */
	.code32
protmode:
	mov	$bootdata-gdt, %eax
	mov	%ax, %ds

	/* Turn on the PAE bit for when paging is enabled */
	mov	%cr4, %eax
	orl	$CR4_PAE, %eax
	mov	%eax, %cr4

	/*
	 * Enable EFER.LME so that we get long mode when all the prereqs are
	 * in place.  In this case, it turns on when CR0_PG is finally enabled.
	 * Pick up a few other EFER bits that we'll use need we're here.
	 */
	movl	$MSR_EFER, %ecx
	rdmsr
	orl	$EFER_LME | EFER_SCE, %eax
	wrmsr

	/*
	 * Point to the embedded page tables for startup.  Note that this
	 * only gets accessed after we're actually in 64 bit mode, however
	 * we can only set the bottom 32 bits of %cr3 in this state.  This
	 * means we are required to use a temporary page table that is below
	 * the 4GB limit.  %ebx is still our relocation base.  We could just
	 * subtract 3 * PAGE_SIZE, but that would be too easy.
	 */
	leal	mptramp_pagetables-mptramp_start(%ebx),%eax
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
	 * ljmp $kernelcode-gdt, $tramp_64-mptramp_start
	 * but gas cannot assemble that.  And besides, we patch the targets
	 * in early startup and its a little clearer what we are patching.
	 */
jmp_64:
	.byte	0xea			/* opcode for far jump */
	.long	tramp_64-mptramp_start	/* offset in segment */
	.word	kernelcode-gdt		/* index in gdt for 64 bit code */

	/*
	 * Yeehar!  We're running in 64 bit mode!  We can mostly ignore our
	 * segment registers, and get on with it.
	 * Note that we are running at the correct virtual address, but with
	 * a 1:1 1GB mirrored mapping over entire address space.  We had better
	 * switch to a real %cr3 promptly so that we can get to the direct map
	 * space. Remember that jmp is relative and that we've been relocated,
	 * so use an indirect jump.
	 */
	.code64
tramp_64:
	movabsq	$entry_64,%rax		/* 64 bit immediate load */
	jmp	*%rax

	.p2align 4,0
gdt:
	/*
	 * All segment descriptor tables start with a null descriptor
	 */
	.long	0x00000000
	.long	0x00000000

	/*
	 * This is the 64 bit long mode code descriptor.  There is no
	 * 64 bit data descriptor.
	 */
kernelcode:
	.long	0x00000000
	.long	0x00209800

	/*
	 * This is the descriptor for the 32 bit boot code.
	 * %cs:  +A, +R, -C, DPL=0, +P, +D, +G
	 * Accessed, Readable, Present, 32 bit, 4G granularity
	 */
bootcode:
	.long	0x0000ffff
	.long	0x00cf9b00

	/*
	 * This is the descriptor for the 32 bit boot data.
	 * We load it into %ds and %ss.  The bits for each selector
	 * are interpreted slightly differently.
	 * %ds:  +A, +W, -E, DPL=0, +P, +D, +G
	 * %ss:  +A, +W, -E, DPL=0, +P, +B, +G
	 * Accessed, Writeable, Expand up, Present, 32 bit, 4GB 
	 * For %ds, +D means 'default operand size is 32 bit'.
	 * For %ss, +B means the stack register is %esp rather than %sp.
	 */
bootdata:
	.long	0x0000ffff
	.long	0x00cf9300

gdtend:

	/*
	 * The address of our page table pages that the boot code
	 * uses to trampoline up to kernel address space.
	 */
	.globl	mptramp_pagetables
mptramp_pagetables:
	.long	0

	/*
	 * The pseudo descriptor for lgdt to use.
	 */
lgdt_desc:	
	.word	gdtend-gdt		/* Length */
	.long	gdt-mptramp_start	/* Offset plus %ds << 4 */

mptramp_end:
	/*
	 * The size of the trampoline code that needs to be relocated
	 * below the 1MiB boundary.
	 */
	.globl	bootMP_size
bootMP_size:
	.long	mptramp_end - mptramp_start

	/*
	 * From here on down is executed in the kernel .text section.
	 */
	.text
	.code64
	.p2align 4,0
entry_64:
	/*
	 * If the BSP reported NXE support, enable EFER.NXE for all APs
	 * prior to loading %cr3. This avoids page faults if the AP
	 * encounters memory marked with the NX bit prior to detecting and
	 * enabling NXE support.
	 */
	movq	pg_nx, %rbx
	testq	%rbx, %rbx
	je	1f
	movl	$MSR_EFER, %ecx
	rdmsr
	orl	$EFER_NXE, %eax
	wrmsr

1:
	/*
	 * Load a real %cr3 that has all the direct map stuff and switches
	 * off the 1GB replicated mirror.  Load a stack pointer and jump
	 * into AP startup code in C.
	 */
	movq	KPML4phys, %rax
	movq	%rax, %cr3
	movq	bootSTK, %rsp
	jmp	init_secondary
