/*-
 * Copyright (c) 2013-2014 Robert N. M. Watson
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract (FA8750-10-C-0237)
 * ("CTSRD"), as part of the DARPA CRASH research programme.
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

.set mips64
.set noreorder
.set nobopt
.set noat

/*
 * Save arguments from the BERI firmware for use in C-land, and jump into
 * main.  Assume that registers/stack/etc are sufficiently initialised to get
 * going.  Notice that we use only temporaries while relocating, as we want to
 * retain argument registers to pass in to main().
 *
 * Note slightly surprising structure: boot2 is linked for a specific address,
 * but we may start running the code somewhere else (e.g., in DRAM as inserted
 * with JTAG, or in flash).  The starting assembly is therefore PIC, but the
 * main body of the code is not PIC.
 */


		.text
		.global prerelocate_start
		.ent prerelocate_start
prerelocate_start:

		/*
		 * Calculate the actual run-time, pre-relocated value of
		 * 'start', which we will use as the source address for
		 * memcpy().  Note that although a symbol is used here, this
		 * should generate code for a short relative branch, leaving
		 * the previous $pc in $ra.
		 */
		bal	baltarget
		nop
baltarget:
		dsub	$t1, $ra, 8				/* Src. */

		/*
		 * Relocate boot2 to DRAM where we can write back global
		 * variable values; jump to it.  Assume all values are 32-bit
		 * aligned.  Use an inline PIC version of memcpy()
		 * pre-relocation; strong alignment assumptions.
		 */
		dla	$t0, __boot2_base_vaddr__		/* Dst. */
		dla	$t2, __boot_loader_len__		/* Len. */

memcpy_loop:
		beq	$t2, 0, memcopy_done
		nop
		lw	$at, 0($t1)
		sw	$at, 0($t0)
		daddiu	$t0, 4
		daddiu	$t1, 4
		daddi	$t2, -4
		b	memcpy_loop
		nop

memcopy_done:
		/*
		 * We can now jump into the relocated code, running from
		 * cached DRAM rather than uncached flash.  Note that a
		 * relative branch instruction cannot be used.
		 */
		dla	$at, relocated_start
		jr	$at
		nop

relocated_start:
		dla	$at, start
		jr	$at
		nop

		.end prerelocate_start
