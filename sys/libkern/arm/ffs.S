/*	$NetBSD: ffs.S,v 1.3 2003/04/05 23:27:15 bjh21 Exp $	*/
/*-
 * Copyright (c) 2001 Christopher Gilbert
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
 * 3. The name of the company nor the name of the author may be used to
 *    endorse or promote products derived from this software without specific
 *    prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <machine/asm.h>
__FBSDID("$FreeBSD$");

	.syntax	unified

/*
 * ffs - find first set bit, this algorithm isolates the first set
 * bit, then multiplies the number by 0x0450fbaf which leaves the top
 * 6 bits as an index into the table.  This algorithm should be a win
 * over the checking each bit in turn as per the C compiled version.
 *
 * under ARMv5 there's an instruction called CLZ (count leading Zero's) that
 * could be used
 *
 * This is the ffs algorithm devised by d.seal and posted to comp.sys.arm on
 * 16 Feb 1994.
 */

ENTRY(ffs)

	/* Standard trick to isolate bottom bit in r0 or 0 if r0 = 0 on entry */
 	rsb     r1, r0, #0
 	ands    r0, r0, r1
#ifndef _ARM_ARCH_5
	
	/*
	 * now r0 has at most one set bit, call this X
	 * if X = 0, all further instructions are skipped
	 */
	adrne   r2, .L_ffs_table
	orrne   r0, r0, r0, lsl #4  /* r0 = X * 0x11 */ 
	orrne   r0, r0, r0, lsl #6  /* r0 = X * 0x451 */
	rsbne   r0, r0, r0, lsl #16 /* r0 = X * 0x0450fbaf */
              
	/* now lookup in table indexed on top 6 bits of r0 */
	ldrbne  r0, [ r2, r0, lsr #26 ]

	RET
.text;
.type .L_ffs_table, _ASM_TYPE_OBJECT;
.L_ffs_table:
/*               0   1   2   3   4   5   6   7           */
	.byte	 0,  1,  2, 13,  3,  7,  0, 14  /*  0- 7 */
	.byte	 4,  0,  8,  0,  0,  0,  0, 15  /*  8-15 */
	.byte	11,  5,  0,  0,  9,  0,  0, 26  /* 16-23 */
	.byte	 0,  0,  0,  0,  0, 22, 28, 16  /* 24-31 */
	.byte	32, 12,  6,  0,  0,  0,  0,  0	/* 32-39 */
	.byte	10,  0,  0, 25,  0,  0, 21, 27  /* 40-47 */
	.byte	31,  0,  0,  0,  0, 24,  0, 20  /* 48-55 */
	.byte   30,  0, 23, 19, 29, 18, 17,  0  /* 56-63 */
#else
	clzne	r0, r0
	rsbne	r0, r0, #32
	RET
#endif
END(ffs)

