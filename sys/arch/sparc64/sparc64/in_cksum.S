/*	$OpenBSD: in_cksum.S,v 1.4 2023/01/04 14:58:04 jsg Exp $	*/
/*	$NetBSD: in_cksum.S,v 1.2 2001/08/10 20:53:11 eeh Exp $ */

/*
 * Copyright (c) 2001 Eduardo Horvath
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "assym.h"
#include <machine/asm.h>

/*
 * int in_cksum(struct mbuf *m, int len)
 * int in_cksum_internal(struct mbuf *m, int len, int offset, int sum)
 *
 * The only fields of the mbuf we really care about
 * is m_next and m_len and m_data.
 *
 *
 * Register usage:
 *
 *	%o0 -	mbuf
 *	%o1 -	len
 *	%o2 -	mlen
 *	%o3 -	sum
 *	%o4 -	temp
 *	%o5 -	mdata
 *	%g1 -	swapped
 *	%g4 -	temp
 *	%g5 -	temp
 */

#define	IALIGN	.align	32
	
ENTRY(in_cksum)
	clr	%o3		! sum = 0;
	clr	%o2
_ENTRY(in_cksum_internal)
	brz	%o0, Lfinish	! for (; m && len > 0; m->m_next) {
	 clr	%g1		! swapped = 0;
	brlez	%o1, Lfinish
	 mov	%o2, %o4	! Stash this elsewhere for a bit

	lduw	[%o0 + M_LEN], %o2	! Code duplicated at Lloop
	srlx	%o3, 32, %g4	! REDUCE bigtime
	sethi	%hi(0xffff), %g5
	ldx	[%o0 + M_DATA], %o5
	srl	%o3, 0, %o3
	or	%g5, %lo(0xffff), %g5
	
	sub	%o2, %o4, %o2	! Correct for initial offset
	ba,pt	%icc, 0f
	 add	%o5, %o4, %o5
	
	IALIGN
Lloop:			
	lduw	[%o0 + M_LEN], %o2
	srlx	%o3, 32, %g4	! REDUCE bigtime
	sethi	%hi(0xffff), %g5
	ldx	[%o0 + M_DATA], %o5
	srl	%o3, 0, %o3
	or	%g5, %lo(0xffff), %g5
0:	
	add	%o3, %g4, %o3
	brz	%o2, Lnext	! if (m->m_len == 0) continue;
	
	 cmp	%o1, %o2	! if (len < mlen)
	movl	%icc, %o1, %o2	!	mlen = len;

	btst	3, %o5		! if (!(*w & 3)) {
	bz	Lint_aligned
	 sub	%o1, %o2, %o1	! len -= mlen

	srlx	%o3, 16, %o4	! REDUCE {sum = (sum & 0xffff) + (sum >> 16);}
	and	%o3, %g5, %o3
	
	add	%o3, %o4, %o3
	btst	1, %o5		! if (!(*w & 3) &&
	bz	Lshort_aligned
	 nop
	
	deccc	%o2
	bl,a,pn	%icc, Lnext	! mlen >= 1) {
	 inc	%o2
	ldub	[%o5], %o4	! ADDBYTE {ROL; sum += *w; byte_swapped ^= 1;}
	sllx	%o3, 8, %o3	! ROL { sum = sum << 8; }
	inc	%o5		! }
	add	%o3, %o4, %o3
	xor	%g1, 1, %g1	! Flip byte_swapped
	
Lshort_aligned:
	btst	2, %o5		! if (!(*w & 3) &&
	bz	Lint_aligned
	 nop
	
	deccc	2, %o2		! mlen >= 1) {
	bl,a,pn	%icc, Lfinish_byte
	 inc	2, %o2
	lduh	[%o5], %o4	! ADDSHORT {sum += *(u_short *)w;}
	inc	2, %o5		! }
	add	%o3, %o4, %o3	! }
Lint_aligned:
	deccc	0xc, %o2	! while (mlen >= 12) {
	ble,pn	%icc, Ltoofar
	 clr	%g5
	ba,pt	%icc, 0f
	 clr	%g4
	IALIGN
0:	
	lduw	[%o5 + 0x00], %o4
	add	%o3, %g4, %o3
	deccc	0xc, %o2
	lduw	[%o5 + 0x04], %g4
	add	%o3, %g5, %o3
	lduw	[%o5 + 0x08], %g5
	inc	0xc, %o5	! ADVANCE(12) }
	bg,pt	%icc, 0b	
	 add	%o3, %o4, %o3
	add	%o3, %g4, %o3
	add	%o3, %g5, %o3
Ltoofar:
	inc	0xc, %o2
	
Ldo_int:
	deccc	4, %o2
	bl,pn	%icc, Lfinish_short
	 nop
0:	
	lduw	[%o5], %o4
	inc	4, %o5
	deccc	4, %o2
	bge,pt	%icc, 0b
	 add	%o3, %o4, %o3

Lfinish_short:	
	btst	2, %o2
	bz	Lfinish_byte
	 nop
	lduh	[%o5], %o4
	inc	2, %o5
	add	%o3, %o4, %o3

Lfinish_byte:	
	btst	1, %o2
	bz	Lnext
	 nop
	ldub	[%o5], %o4
	sllx	%o3, 8, %o3	! ROL { sum = sum << 8; }
	inc	%o5
	xor	%g1, 1, %g1	! Flip byte_swapped
	add	%o3, %o4, %o3
	
Lnext:
	ldx	[%o0 + M_NEXT], %o0
Lfinish:
	srlx	%o3, 32, %o4	! Reduce to 32-bits
	srl	%o3, 0, %o3
	brz,pt	%o0, 1f		! In general there is only one mbuf
	 add	%o3, %o4, %o3
	brgz,pt	%o1, Lloop	! But usually all need to be fully checksummed
	 nop
1:	
	sethi	%hi(0x0000ffff), %o5	! data ptr not needed any more
	
	srlx	%o3, 16, %o4
	or	%o5, %lo(0x0000ffff), %o5
	
	and	%o3, %o5, %o3
	
	add	%o3, %o4, %o3
	brz,pt	%g1, 0f		! if (byte_swapped) {
	 nop

	sllx	%o3, 8, %o3	! ROL

	srlx	%o3, 16, %o4	! REDUCE
	and	%o3, %o5, %o3
	
	add	%o3, %o4, %o3
0:	
	subcc	%o3, %o5, %o4	! if (sum > 0xffff)
	movg	%icc, %o4, %o3	! sum -= 0xffff;

	clr	%g4		! In case we are using EMBEDANY (ick)
	retl
	 xor	%o3, %o5, %o0	! return (0xffff ^ sum);
