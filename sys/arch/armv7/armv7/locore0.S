/*	$OpenBSD: locore0.S,v 1.8 2022/12/08 01:25:44 guenther Exp $	*/
/*	$NetBSD: lubbock_start.S,v 1.1 2003/06/18 10:51:15 bsh Exp $ */

/*
 * Copyright (c) 2002, 2003  Genetec Corporation.  All rights reserved.
 * Written by Hiroyuki Bessho for Genetec Corporation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of Genetec Corporation may not be used to endorse or
 *    promote products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY GENETEC CORPORATION ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL GENETEC CORPORATION
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "assym.h"

#include <machine/asm.h>
#include <arm/sysreg.h>
#include <arm/armreg.h>
#include <arm/pte.h>

/*
 * Kernel start routine for OMAP
 * this code is executed at the very first after the kernel is loaded
 * by U-Boot.
 *
 * This code makes a number of assumptions.
 * 1) it is running in RAM.
 * 2) it is run at a fairly well known offset from the beginning of
 *    a ram section.
 * 3) the memory region around the kernel will be at least about 32MB
 * 4) memory just below the kernel can be used by the kernel
 * 5) memory at the start of the ram section may not be useable
 *    it may contain bootloader/u-boot
 * 6) the fdt/kernel parameters in arg r0-r2 may point outside the
 *    32MB of ram (it likely should be copied very early)
 *
 */
	.text

	.global	bootstrap_start
bootstrap_start:
	/* Save U-Boot arguments */
	mov	r6, r0
	mov	r7, r1
	mov	r8, r2

	/*
	 *  Kernel is loaded in SDRAM (0xX0200000..), and is expected to run
	 *  in VA 0xc0200000..
	 *
	 * which base memory address the kernel is located is unknown
	 * however the kernel should be located at 0x00300000 offset
	 * from that ram area. copy the PC into a register and strip
	 * the low bits
	 */

	/*
	 * If we are running in HYP mode, try to get to SVC safely.
	 * Ensure IRQ, FIQ and Aborts are disabled in HYP mode before
	 * going to SVC.  If we are there already, just go ahead.
	 */
	bl	drop_to_pl1

	/*
	 * Now that we are in SVC, build up pagetables and start up.
	 */
	mov r9, pc
	and r9, r9, #0xf0000000	// Leave the memory base in r9

	/* create the bootstrap MMU table at offset 0x00200000 */
	orr	r0, r9, #0x00200000
	mov	r3, #0
	mov	r2, #0
2:
	str	r3, [r0, r2]
	add	r2, r2, #4
	cmp	r2, #(L1_TABLE_SIZE)
	bne	2b

	adr	r4, mmu_init_table

	mov	r2, r9, lsr #18
	ldr	r3, [r4, #8]
	bic	r3, r3, #0xf0000000
	orr	r3, r3, r9
	str	r2, [r4, #4]
	str	r3, [r4, #8]
	str	r3, [r4, #0x14] // ram address for 0xc0000000

	/*
	 * the first entry has two fields that need to be updated for
	 * specific ram configuration of this board.
	 */
	b	4f

3:
	str	r3, [r0, r2]
	add	r2, r2, #4
	add	r3, r3, #(L1_S_SIZE)
	adds	r1, r1, #-1
	bhi	3b
4:
	ldmia	r4!, {r1,r2,r3}   /* # of sections, PA|attr, VA */
	cmp	r1, #0
	bne	3b

	/*
	 * At this point:
	 * r0 = TTBR0 table
	 */

	/* Enable the mmu */
	bl	start_mmu

	/* Restore U-Boot arguments */
	mov	r0, r6
	mov	r1, r7
	mov	r2, r8
	mov	r3, r9

	/* Jump to kernel code in TRUE VA */
	adr	r6, Lstart
	ldr	pc, [r6]

Lstart:
	.word	start

#define MMU_INIT(va,pa,n_sec,attr) \
	.word	n_sec					    ; \
	.word	4*((va)>>L1_S_SHIFT)			    ; \
	.word	(pa)|(attr)				    ;

mmu_init_table:
	/* map SDRAM VA==PA, WT cacheable */
	MMU_INIT(0x00000000, 0x00000000, 64,
		 L1_TYPE_S|L1_S_C|L1_S_V7_AP(AP_KRW)|L1_S_V7_AF)
	/* map VA 0xc0000000..0xc3ffffff to PA */
	MMU_INIT(0xc0000000, 0x00000000, 64,
		 L1_TYPE_S|L1_S_C|L1_S_V7_AP(AP_KRW)|L1_S_V7_AF)

	.word 0	/* end of table */

	.text
