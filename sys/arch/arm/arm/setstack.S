/*	$OpenBSD: setstack.S,v 1.5 2018/08/06 18:39:13 kettenis Exp $	*/
/*	$NetBSD: setstack.S,v 1.2 2002/08/15 01:37:02 briggs Exp $	*/

/*
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
 * setstack.S
 *
 * Miscellaneous routine to play with the stack pointer in different CPU modes
 *
 * Eventually this routine can be inline assembly.
 *
 * Created      : 17/09/94
 *
 * Based of kate/display/setstack.s
 */

#include <machine/asm.h>
#include <arm/armreg.h>

/* To set the stack pointer for a particular mode we must switch
 * to that mode update the banked r13 and then switch back.
 * This routine provides an easy way of doing this for any mode
 *
 * r0 = CPU mode
 * r1 = stackptr
 */

ENTRY(set_stackptr)
        mrs	r3, cpsr		/* Switch to the appropriate mode */
	bic	r2, r3, #(PSR_MODE)
	orr	r2, r2, r0
        msr	cpsr_c, r2

	mov	sp, r1			/* Set the stack pointer */

        msr	cpsr_c, r3		/* Restore the old mode */

	mov	pc, lr			/* Exit */

/* To get the stack pointer for a particular mode we must switch
 * to that mode copy the banked r13 and then switch back.
 * This routine provides an easy way of doing this for any mode
 *
 * r0 = CPU mode
 */

ENTRY(get_stackptr)
        mrs	r3, cpsr		/* Switch to the appropriate mode */
	bic	r2, r3, #(PSR_MODE)
	orr	r2, r2, r0
        msr	cpsr_c, r2

	mov	r0, sp			/* Set the stack pointer */

        msr	cpsr_c, r3		/* Restore the old mode */

	mov	pc, lr			/* Exit */
