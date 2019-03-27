/*-
 * Copyright (c) 2001 Jake Burkholder.
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
 */

#include <machine/asm.h>
__FBSDID("$FreeBSD$");

	.register	%g2, #ignore
	.register	%g3, #ignore
	.register	%g6, #ignore
	.register	%g7, #ignore

#include <machine/tstate.h>
#include <machine/utrap.h>

#include "assym.s"

ENTRY(__sparc_utrap_gen)
	sub	%sp, UF_SIZEOF, %sp

	stx	%o0, [%sp + SPOFF + CCFSZ + UF_TYPE]
	stx	%o3, [%sp + SPOFF + CCFSZ + UF_TAR]
	stx	%o4, [%sp + SPOFF + CCFSZ + UF_SFAR]
	stx	%o5, [%sp + SPOFF + CCFSZ + UF_SFSR]

	stx	%l4, [%sp + SPOFF + CCFSZ + UF_FSR]
	stx	%l5, [%sp + SPOFF + CCFSZ + UF_STATE]
	stx	%l6, [%sp + SPOFF + CCFSZ + UF_PC]
	stx	%l7, [%sp + SPOFF + CCFSZ + UF_NPC]

	stx	%g1, [%sp + SPOFF + CCFSZ + UF_G1]
	stx	%g2, [%sp + SPOFF + CCFSZ + UF_G2]
	stx	%g3, [%sp + SPOFF + CCFSZ + UF_G3]
	stx	%g4, [%sp + SPOFF + CCFSZ + UF_G4]
	stx	%g5, [%sp + SPOFF + CCFSZ + UF_G5]
	stx	%g6, [%sp + SPOFF + CCFSZ + UF_G6]
	stx	%g7, [%sp + SPOFF + CCFSZ + UF_G7]

	stx	%i0, [%sp + SPOFF + CCFSZ + UF_O0]
	stx	%i1, [%sp + SPOFF + CCFSZ + UF_O1]
	stx	%i2, [%sp + SPOFF + CCFSZ + UF_O2]
	stx	%i3, [%sp + SPOFF + CCFSZ + UF_O3]
	stx	%i4, [%sp + SPOFF + CCFSZ + UF_O4]
	stx	%i5, [%sp + SPOFF + CCFSZ + UF_O5]
	stx	%i6, [%sp + SPOFF + CCFSZ + UF_O6]
	stx	%i7, [%sp + SPOFF + CCFSZ + UF_O7]

	rd	%y, %l6

	call	__sparc_utrap
	 add	%sp, SPOFF + CCFSZ, %o0

	wr	%l6, 0, %y	

	ldx	[%sp + SPOFF + CCFSZ + UF_G1], %g1
	ldx	[%sp + SPOFF + CCFSZ + UF_G2], %g2
	ldx	[%sp + SPOFF + CCFSZ + UF_G3], %g3
	ldx	[%sp + SPOFF + CCFSZ + UF_G4], %g4
	ldx	[%sp + SPOFF + CCFSZ + UF_G5], %g5
	ldx	[%sp + SPOFF + CCFSZ + UF_G6], %g6
	ldx	[%sp + SPOFF + CCFSZ + UF_G7], %g7

	ldx	[%sp + SPOFF + CCFSZ + UF_O0], %i0
	ldx	[%sp + SPOFF + CCFSZ + UF_O1], %i1
	ldx	[%sp + SPOFF + CCFSZ + UF_O2], %i2
	ldx	[%sp + SPOFF + CCFSZ + UF_O3], %i3
	ldx	[%sp + SPOFF + CCFSZ + UF_O4], %i4
	ldx	[%sp + SPOFF + CCFSZ + UF_O5], %i5
	ldx	[%sp + SPOFF + CCFSZ + UF_O6], %i6
	ldx	[%sp + SPOFF + CCFSZ + UF_O7], %i7

	ldx	[%sp + SPOFF + CCFSZ + UF_STATE], %l5
	! Restore %asi and %ccr from the passed tstate
	srlx	%l5, TSTATE_CCR_SHIFT, %l4
	and	%l4, CCR_MASK, %l4
	wr	%l4, 0, %ccr
	srlx	%l5, TSTATE_ASI_SHIFT, %l4
	and	%l4, ASI_MASK, %l4
	wr	%l4, 0, %asi
	ldx	[%sp + SPOFF + CCFSZ + UF_PC], %l6
	ldx	[%sp + SPOFF + CCFSZ + UF_NPC], %l7

	jmpl	%l6, %g0
	 return	%l7
END(__sparc_utrap_gen)
