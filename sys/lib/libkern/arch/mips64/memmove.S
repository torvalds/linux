/*	$OpenBSD: memmove.S,v 1.7 2024/09/04 07:54:52 mglocker Exp $	*/
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
 * memcpy(to, from, len)
	always copy forward
 *
 * memmove(to, from, len), bcopy(from, to, len)
 *	both handle overlap
 */
LEAF(memcpy, 0)
	.set	noreorder
	move	v0, a0			# swap from and to
	move	a0, a1
	move	a1, v0
	PTR_ADDU t0, a0, a2		# t0 = end of s1 region
	sltu	t1, a1, t0
	sltu	t2, a0, a1
	j	.Lforward		# do forward copy
	slt	t2, a2, 12		# check for small copy

ALEAF(memmove)
	.set	noreorder
	move	v0, a0			# swap from and to
	move	a0, a1
	move	a1, v0
ALEAF(bcopy)
	.set	noreorder
	PTR_ADDU t0, a0, a2		# t0 = end of s1 region
	sltu	t1, a1, t0
	sltu	t2, a0, a1
	and	t1, t1, t2		# t1 = true if from < to < (from+len)
	beq	t1, zero, .Lforward	# non overlapping, do forward copy
	slt	t2, a2, 12		# check for small copy

	ble	a2, zero, 2f
	PTR_ADDU t1, a1, a2		# t1 = end of to region
1:
	lb	v1, -1(t0)		# copy bytes backwards,
	PTR_SUBU t0, t0, 1		#   doesn't happen often so do slow way
	PTR_SUBU t1, t1, 1
	bne	t0, a0, 1b
	sb	v1, 0(t1)
2:
	j	ra
	nop
.Lforward:
#ifdef _STANDALONE
	bne	t2, zero, .Lsmallcpy	# do a small bcopy
	xor	v1, a0, a1		# compare low two bits of addresses
	and	v1, v1, 3
	PTR_SUBU a3, zero, a1		# compute # bytes to word align address
	beq	v1, zero, .Laligned	# addresses can be word aligned
	and	a3, a3, 3

	beq	a3, zero, 1f
	PTR_SUBU a2, a2, a3		# subtract from remaining count
	LWHI	v1, 0(a0)		# get next 4 bytes (unaligned)
	LWLO	v1, 3(a0)
	PTR_ADDU a0, a0, a3
	SWHI	v1, 0(a1)		# store 1, 2, or 3 bytes to align a1
	PTR_ADDU a1, a1, a3
1:
	and	v1, a2, 3		# compute number of words left
	PTR_SUBU a3, a2, v1
	move	a2, v1
	PTR_ADDU a3, a3, a0		# compute ending address
2:
	LWHI	v1, 0(a0)		# copy words a0 unaligned, a1 aligned
	LWLO	v1, 3(a0)
	PTR_ADDU a0, a0, 4
	sw	v1, 0(a1)
	PTR_ADDU a1, a1, 4
	bne	a0, a3, 2b
	nop				# We have to do this mmu-bug.
	b	.Lsmallcpy
	nop
.Laligned:
	beq	a3, zero, 1f
	PTR_SUBU a2, a2, a3		# subtract from remaining count
	LWHI	v1, 0(a0)		# copy 1, 2, or 3 bytes to align
	PTR_ADDU a0, a0, a3
	SWHI	v1, 0(a1)
	PTR_ADDU a1, a1, a3
1:
	and	v1, a2, 3		# compute number of whole words left
	PTR_SUBU a3, a2, v1
	move	a2, v1
	PTR_ADDU a3, a3, a0		# compute ending address
2:
	lw	v1, 0(a0)		# copy words
	PTR_ADDU a0, a0, 4
	sw	v1, 0(a1)
	bne	a0, a3, 2b
	PTR_ADDU a1, a1, 4
#else
	bne	t2, zero, .Lsmallcpy	# do a small bcopy
	xor	v1, a0, a1		# compare low three bits of addresses
	and	v1, v1, 7
	PTR_SUBU a3, zero, a1		# compute # bytes to dword align address
	beq	v1, zero, .Laligned	# addresses can be dword aligned
	and	a3, a3, 7

	beq	a3, zero, 1f
	PTR_SUBU a2, a2, a3		# subtract from remaining count
	LDHI	v1, 0(a0)		# get next 8 bytes (unaligned)
	LDLO	v1, 7(a0)
	PTR_ADDU a0, a0, a3
	SDHI	v1, 0(a1)		# store 1-7 bytes to align a1
	PTR_ADDU a1, a1, a3
1:
	and	v1, a2, 7		# compute number of dwords left
	PTR_SUBU a3, a2, v1
	beq	a3, zero, .Lsmallcpy
	move	a2, v1
	PTR_ADDU a3, a3, a0		# compute ending address
2:
	LDHI	v1, 0(a0)		# copy dwords a0 unaligned, a1 aligned
	LDLO	v1, 7(a0)
	PTR_ADDU a0, a0, 8
	sd	v1, 0(a1)
	PTR_ADDU a1, a1, 8
	bne	a0, a3, 2b
	nop				# We have to do this mmu-bug.
	b	.Lsmallcpy
	nop
.Laligned:
	beq	a3, zero, 1f
	PTR_SUBU a2, a2, a3		# subtract from remaining count
	LDHI	v1, 0(a0)		# copy 1-7 bytes to align
	PTR_ADDU a0, a0, a3
	SDHI	v1, 0(a1)
	PTR_ADDU a1, a1, a3
1:
	and	v1, a2, 7		# compute number of whole dwords left
	PTR_SUBU a3, a2, v1
	beq	a3, zero, .Lsmallcpy
	move	a2, v1
	PTR_ADDU a3, a3, a0		# compute ending address
2:
	ld	v1, 0(a0)		# copy dwords
	PTR_ADDU a0, a0, 8
	sd	v1, 0(a1)
	bne	a0, a3, 2b
	PTR_ADDU a1, a1, 8
#endif
.Lsmallcpy:
	ble	a2, zero, 2f
	PTR_ADDU a3, a2, a0		# compute ending address
1:
	lbu	v1, 0(a0)		# copy bytes
	PTR_ADDU a0, a0, 1
	sb	v1, 0(a1)
	bne	a0, a3, 1b
	PTR_ADDU a1, a1, 1	# MMU BUG ? can not do -1(a1) at 0x80000000!!
2:
	j	ra
	nop
END(memcpy)
