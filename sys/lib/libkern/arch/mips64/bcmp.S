/*	$OpenBSD: bcmp.S,v 1.5 2022/01/29 05:47:36 visa Exp $	*/
/*-
 * Copyright (c) 1991, 1993
 *      The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Ralph Campbell.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "DEFS.h"


/*
 * bcmp(s1, s2, n)
 */
LEAF(bcmp, 0)
	.set	noreorder
	blt	a2, 16, .Lsmallcmp	# is it worth any trouble?
	xor	v0, a0, a1		# compare low two bits of addresses
	and	v0, v0, 3
	PTR_SUBU a3, zero, a1		# compute # bytes to word align address
	bne	v0, zero, .Lunalignedcmp # not possible to align addresses
	and	a3, a3, 3

	beq	a3, zero, 1f
	PTR_SUBU a2, a2, a3		# subtract from remaining count
	move	v0, v1			# init v0,v1 so unmodified bytes match
	LWHI	v0, 0(a0)		# read 1, 2, or 3 bytes
	LWHI	v1, 0(a1)
	PTR_ADDU a1, a1, a3
	bne	v0, v1, .Lnomatch
	PTR_ADDU a0, a0, a3
1:
	and	a3, a2, ~3		# compute number of whole words left
	PTR_SUBU a2, a2, a3		#   which has to be >= (16-3) & ~3
	PTR_ADDU a3, a3, a0		# compute ending address
2:
	lw	v0, 0(a0)		# compare words
	lw	v1, 0(a1)
	PTR_ADDU a0, a0, 4
	bne	v0, v1, .Lnomatch
	PTR_ADDU a1, a1, 4
	bne	a0, a3, 2b
	nop
	b	.Lsmallcmp		# finish remainder
	nop
.Lunalignedcmp:
	beq	a3, zero, 2f
	PTR_SUBU a2, a2, a3		# subtract from remaining count
	PTR_ADDU a3, a3, a0		# compute ending address
1:
	lbu	v0, 0(a0)		# compare bytes until a1 word aligned
	lbu	v1, 0(a1)
	PTR_ADDU a0, a0, 1
	bne	v0, v1, .Lnomatch
	PTR_ADDU a1, a1, 1
	bne	a0, a3, 1b
	nop
2:
	and	a3, a2, ~3		# compute number of whole words left
	PTR_SUBU a2, a2, a3		#   which has to be >= (16-3) & ~3
	PTR_ADDU a3, a3, a0		# compute ending address
3:
	LWHI	v0, 0(a0)		# compare words a0 unaligned, a1 aligned
	LWLO	v0, 3(a0)
	lw	v1, 0(a1)
	PTR_ADDU a0, a0, 4
	bne	v0, v1, .Lnomatch
	PTR_ADDU a1, a1, 4
	bne	a0, a3, 3b
	nop
.Lsmallcmp:
	ble	a2, zero, .Lmatch
	PTR_ADDU a3, a2, a0		# compute ending address
1:
	lbu	v0, 0(a0)
	lbu	v1, 0(a1)
	PTR_ADDU a0, a0, 1
	bne	v0, v1, .Lnomatch
	PTR_ADDU a1, a1, 1
	bne	a0, a3, 1b
	nop
.Lmatch:
	j	ra
	move	v0, zero
.Lnomatch:
	j	ra
	dli	v0, 1
END(bcmp)
