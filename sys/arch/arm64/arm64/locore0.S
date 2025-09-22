/* $OpenBSD: locore0.S,v 1.10 2022/12/23 17:31:30 kettenis Exp $ */
/*-
 * Copyright (c) 2012-2014 Andrew Turner
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
 * $FreeBSD: head/sys/arm64/arm64/locore.S 282867 2015-05-13 18:57:03Z zbb $
 */

#include "assym.h"
#include <machine/asm.h>
#include <machine/armreg.h>
#include <machine/param.h>
#include <machine/pte.h>

#define	DEVICE_MEM	0
#define	NORMAL_UNCACHED	2
#define	NORMAL_MEM	3

/*
 * We assume:
 *  MMU      on with an identity map, or off
 *  D-Cache: off
 *  I-Cache: on or off
 *  We are loaded at a 2MiB aligned address
 */

#define	INIT_STACK_SIZE	(PAGE_SIZE * 4)

	.text
	.globl _start
_start:
	mov x21, x0
	mov x22, x1
	mov x23, x2

	/* Drop to EL1 */
	bl	drop_to_el1

	/*
	 * Disable the MMU. We may have entered the kernel with it on and
	 * will need to update the tables later. If this has been set up
	 * with anything other than a VA == PA map then this will fail,
	 * but in this case the code to find where we are running from
	 * would have also failed.
	 */
	dsb	sy
	mrs	x2, sctlr_el1
	bic	x2, x2, SCTLR_M
	msr	sctlr_el1, x2
	isb

	/* Set the context id */
	msr	contextidr_el1, xzr

	/* Get the virt -> phys offset */
	bl	get_virt_delta

	/* Store symbol value. */
	adr	x0, .Lesym
	ldr	x0, [x0]
	sub	x0, x0, x29
	add	x21, x21, x29
	str	x21, [x0]

	/*
	 * At this point:
	 * x29 = PA - VA
	 * x28 = Our physical load address
	 */

	/* Create the page tables */
	bl	create_pagetables

	/*
	 * At this point:
	 * x27 = TTBR0 table
	 * x26 = TTBR1 table
	 */

	/* Enable the mmu */
	bl	start_mmu

	/* Jump to the virtual address space */
	ldr	x15, .Lvirtdone
	br	x15

.Linitstack:
	.xword initstack
.Linitstack_end:
	.xword initstack_end
virtdone:
	/* Set up the stack */
	adr	x25, .Linitstack_end
	ldr	x25, [x25]
	mov	sp, x25
	mov	x8, #TRAPFRAME_SIZEOF
	sub     x8, x8, (STACKALIGNBYTES)
	and     x8, x8, ~(STACKALIGNBYTES)

	// pass base of kernel stack as proc0
	adr	x25, .Linitstack
	ldr	x25, [x25]

	sub	sp, sp, x8

	/* Zero the BSS */
	ldr	x15, .Lbss
	ldr	x14, .Lend
1:
	str	xzr, [x15], #8
	cmp	x15, x14
	b.lo	1b

	/* Backup the module pointer */
	mov	x1, x0

	/* Make the page table base a virtual address */
	sub	x26, x26, x29

	// XXX - shouldn't this be 8 * 5 (struct grew from 4 -> 5)
	sub	sp, sp, #(64 * 4)
	mov	x0, sp

	/* Negate the delta so it is VA -> PA */
	neg	x29, x29

	str	x1,  [x0]	/* modulep */
	str	x26, [x0, 8]	/* kern_l1pt */
	str	x29, [x0, 16]	/* kern_delta */
	str	x25, [x0, 24]	/* kern_stack */
	str	x21, [x0, 32]	/* ? (x0 arg on boot) */
	str	x22, [x0, 40]	/* ? (x1 arg on boot) */
	str	x23, [x0, 48]	/* fdt (x2 arg on boot) */

	/* trace back starts here */
	mov	fp, #0
	/* Branch to C code */
	bl	initarm
	bl	main

	/* We should not get here */
	brk	0


	.align 3
.Lvirtdone:
	.quad	virtdone
.Lbss:
	.quad	__bss_start
.Lend:
	.quad	_end

/*
 * This builds the page tables containing the identity map, and the
 * initial kernel virtual map.
 *
 * It relies on:
 *  We were loaded into contiguous 64MB block of memory
 *  We were loaded to an address that is on a 2MiB boundary
 *  x28 contains the physical address we were loaded from
 *
 * The page table for the identity map starts at L0 and maps the 64MB
 * block that the kernel was loaded into by the bootloader using
 * 2MB (L2) pages.  These are loaded into TTBR0.
 *
 * The initial kernel page table starts at L1 and maps the 64MB block
 * that the kernel was initially loaded into by the bootloader using
 * 2MB (L2) pages.  The first 2MB of this 64MB block is unused and
 * not mapped.  These are loaded into TTBR1.
 *
 * The pages for the page tables are allocated aligned in .data
 * (see locore.S).
 */
.Lpagetable:
	.xword pagetable
.Lpagetable_end:
	.xword pagetable_end

.Lesym:
	.xword esym

create_pagetables:
	/* Save the Link register */
	mov	x5, x30

	/* Clean the page table */
	adr	x6, .Lpagetable
	ldr	x6, [x6]
	sub	x6, x6, x29 // VA -> PA
	mov	x26, x6
	adr	x27, .Lpagetable_end
	ldr	x27, [x27]
	sub	x27, x27, x29 // VA -> PA
1:
	stp	xzr, xzr, [x6], #16
	stp	xzr, xzr, [x6], #16
	stp	xzr, xzr, [x6], #16
	stp	xzr, xzr, [x6], #16
	cmp	x6, x27
	b.lo	1b

	/*
	 * Build the TTBR1 maps.
	 */

	/* Create the kernel space L2 table */
	mov	x6, x26				// pagetable_l2_ttbr1:
	mov	x7, #NORMAL_MEM
	add	x8, x28, x29
	mov	x9, x28
	mov	x10, #31			// entries for 64MB - 2MB
	bl	build_l2_block_pagetable

	/* Move to the l1 table */
	add	x26, x26, #PAGE_SIZE * 2	// pagetable_l1_ttbr1:

	/* Link the l1 -> l2 table */
	mov	x9, x6
	mov	x10, #2
	mov	x6, x26
	bl	link_l1_pagetable

	/*
	 * Build the TTBR0 maps.
	 */
	add	x27, x26, #PAGE_SIZE		// pagetable_l2_ttbr0:

	/* Create the kernel space L2 table */
	mov	x6, x27				// pagetable_l2_ttbr0:
	mov	x7, #NORMAL_MEM
	mov	x8, x28
	mov	x9, x28
	mov	x10, #31			// entries for 64MB - 2MB
	bl	build_l2_block_pagetable

	/* Move to the l1 table */
	add	x27, x27, #PAGE_SIZE * 2	// pagetable_l1_ttbr0:

	/* Link the l1 -> l2 table */
	mov	x9, x6
	mov	x10, #2
	mov	x6, x27
	bl	link_l1_pagetable

	/* Move to the l0 table */
	add	x27, x27, #PAGE_SIZE * 2	// pagetable_l0_ttbr0:

	/* Link the l0 -> l1 table */
	mov	x9, x6
	mov	x10, #2
	mov	x6, x27
	bl	link_l0_pagetable

	/* Restore the Link register */
	mov	x30, x5
	ret

/*
 * Builds an L0 -> L1 table descriptor
 *
 * This is a link for a 512GiB block of memory with up to 1GiB regions mapped
 * within it by link_l1_pagetable.
 *
 *  x6  = L0 table
 *  x8  = Virtual Address
 *  x9  = L1 PA (trashed)
 *  x10 = Entry count
 *  x11, x12 and x13 are trashed
 */
link_l0_pagetable:
	/*
	 * Link an L0 -> L1 table entry.
	 */
	/* Find the table index */
	lsr	x11, x8, #L0_SHIFT
	and	x11, x11, #Ln_ADDR_MASK

	/* Build the L0 block entry */
	mov	x12, #L0_TABLE

	/* Only use the output address bits */
	lsr	x9, x9, #PAGE_SHIFT
1:	orr	x13, x12, x9, lsl #PAGE_SHIFT

	/* Store the entry */
	str	x13, [x6, x11, lsl #3]

	sub	x10, x10, #1
	add	x11, x11, #1
	add	x9, x9, #1
	cbnz	x10, 1b

	ret

/*
 * Builds an L1 -> L2 table descriptor
 *
 * This is a link for a 1GiB block of memory with up to 2MiB regions mapped
 * within it by build_l2_block_pagetable.
 *
 *  x6  = L1 table
 *  x8  = Virtual Address
 *  x9  = L2 PA (trashed)
 *  x10 = Entry Count
 *  x11, x12 and x13 are trashed
 */
link_l1_pagetable:
	/*
	 * Link an L1 -> L2 table entry.
	 */
	/* Find the table index */
	lsr	x11, x8, #L1_SHIFT
	and	x11, x11, #Ln_ADDR_MASK

	/* Build the L1 block entry */
	mov	x12, #L1_TABLE

	/* Only use the output address bits */
	lsr	x9, x9, #PAGE_SHIFT
1:	orr	x13, x12, x9, lsl #PAGE_SHIFT

	/* Store the entry */
	str	x13, [x6, x11, lsl #3]

	sub	x10, x10, #1
	add	x11, x11, #1
	add	x9, x9,	#1
	cbnz	x10, 1b

	ret

/*
 * Builds count 2 MiB page table entry
 *  x6  = L2 table
 *  x7  = Type (0 = Device, 1 = Normal)
 *  x8  = VA start
 *  x9  = PA start (trashed)
 *  x10 = Entry count
 *  x11, x12 and x13 are trashed
 */
build_l2_block_pagetable:
	/*
	 * Build the L2 table entry.
	 */
	/* Find the table index */
	lsr	x11, x8, #L2_SHIFT
	and	x11, x11, #Ln_ADDR_MASK

	/* Build the L2 block entry */
	lsl	x12, x7, #2
	orr	x12, x12, #L2_BLOCK
	orr	x12, x12, #(ATTR_nG | ATTR_AF | ATTR_SH(SH_INNER))
	orr	x12, x12, #ATTR_UXN

	/* Only use the output address bits */
	lsr	x9, x9, #L2_SHIFT

	/* Set the physical address for this virtual address */
1:	orr	x13, x12, x9, lsl #L2_SHIFT

	/* Store the entry */
	str	x13, [x6, x11, lsl #3]

	sub	x10, x10, #1
	add	x11, x11, #1
	add	x9, x9, #1
	cbnz	x10, 1b

	ret
