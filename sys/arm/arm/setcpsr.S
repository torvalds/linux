/*	$NetBSD: setcpsr.S,v 1.2 2002/08/15 01:37:02 briggs Exp $	*/

/*-
 * Copyright (c) 1994 Mark Brinicombe.
 * Copyright (c) 1994 Brini.
 * All rights reserved.
 *
 * This code is derived from software written for Brini by Mark Brinicombe
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Brini.
 * 4. The name of the company nor the name of the author may be used to
 *    endorse or promote products derived from this software without specific
 *    prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY BRINI ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL BRINI OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * RiscBSD kernel project
 *
 * setcpsr.S
 *
 * Miscellaneous routines to play with the CPSR register
 *
 * Eventually this routine can be inline assembly.
 *
 * Created      : 12/09/94
 *
 * Based of kate/display/setcpsr.s
 *
 */

#include <machine/asm.h>
__FBSDID("$FreeBSD$");

/* Sets and clears bits in the CPSR register
 *
 *  r0 - bic mask
 *  r1 - eor mask
 */

ENTRY_NP(SetCPSR)
        mrs     r3, cpsr		/* Set the CPSR */
	bic	r2, r3, r0
        eor     r2, r2, r1
        msr     cpsr_fsxc, r2

        mov	r0, r3			/* Return the old CPSR */

	RET
END(SetCPSR)


/* Gets the CPSR register
 *
 * Returns the CPSR in r0
 */

ENTRY_NP(GetCPSR)
        mrs     r0, cpsr		/* Get the CPSR */

	RET
END(GetCPSR)

