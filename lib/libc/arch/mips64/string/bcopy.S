/*	$OpenBSD: bcopy.S,v 1.5 2015/08/31 02:53:56 guenther Exp $ */
/*-
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
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

#include "SYS.h"

/* bcopy(s1, s2, n) */


LEAF(bcopy, 0)
	.set	noreorder
	daddu	t0, a0, a2		# t0 = end of s1 region
	sltu	t1, a1, t0
	sltu	t2, a0, a1
	and	t1, t1, t2		# t1 = true if from < to < (from+len)
	beq	t1, zero, forward	# non overlapping, do forward copy
	slt	t2, a2, 12		# check for small copy

	ble	a2, zero, 2f
	daddu	t1, a1, a2		# t1 = end of to region
1:
	lb	v0, -1(t0)		# copy bytes backwards,
	dsubu	t0, t0, 1		#  does not happen often so do slow way
	dsubu	t1, t1, 1
	bne	t0, a0, 1b
	sb	v0, 0(t1)
2:
	j	ra
	nop
forward:
	bne	t2, zero, smallcpy	# do a small bcopy
	xor	v0, a0, a1		# compare low two bits of addresses
	and	v0, v0, 3
	dsubu	a3, zero, a1		# compute # bytes to word align address
	beq	v0, zero, aligned	# addresses can be word aligned
	and	a3, a3, 3

	beq	a3, zero, 1f
	dsubu	a2, a2, a3		# subtract from remaining count
	LWHI	v0, 0(a0)		# get next 4 bytes (unaligned)
	LWLO	v0, 3(a0)
	daddu	a0, a0, a3
	SWHI	v0, 0(a1)		# store 1, 2, or 3 bytes to align a1
	daddu	a1, a1, a3
1:
	and	v0, a2, 3		# compute number of words left
	dsubu	a3, a2, v0
	move	a2, v0
	daddu	a3, a3, a0		# compute ending address
2:
	LWHI	v0, 0(a0)		# copy words a0 unaligned, a1 aligned
	LWLO	v0, 3(a0)
	daddu	a0, a0, 4
	daddu	a1, a1, 4
	bne	a0, a3, 2b
	sw	v0, -4(a1)
	b	smallcpy
	nop
aligned:
	beq	a3, zero, 1f
	dsubu	a2, a2, a3		# subtract from remaining count
	LWHI	v0, 0(a0)		# copy 1, 2, or 3 bytes to align
	daddu	a0, a0, a3
	SWHI	v0, 0(a1)
	daddu	a1, a1, a3
1:
	and	v0, a2, 3		# compute number of whole words left
	dsubu	a3, a2, v0
	move	a2, v0
	daddu	a3, a3, a0		# compute ending address
2:
	lw	v0, 0(a0)		# copy words
	daddu	a0, a0, 4
	daddu	a1, a1, 4
	bne	a0, a3, 2b
	sw	v0, -4(a1)
smallcpy:
	ble	a2, zero, 2f
	daddu	a3, a2, a0		# compute ending address
1:
	lbu	v0, 0(a0)		# copy bytes
	daddu	a0, a0, 1
	daddu	a1, a1, 1
	bne	a0, a3, 1b
	sb	v0, -1(a1)
2:
	j	ra
	nop
	.set	reorder
END_WEAK(bcopy)
