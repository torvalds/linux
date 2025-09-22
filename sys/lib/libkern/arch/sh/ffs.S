/*	$NetBSD: ffs.S,v 1.1 2005/12/20 19:28:50 christos Exp $	*/

/*-
 * Copyright (c) 2002 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by ITOH Yasufumi.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <machine/asm.h>

/*
 * ffs - find first bit set
 *
 * This code makes use of ``test 8bit'' and ``shift 8bit'' instructions.
 * The remaining 8bit is tested in every 2bit.
 */

ENTRY(ffs)
	mov	r4,r0		! using r0 specific instructions
	tst	#0xff,r0
	bf/s	L8bit
	mov	#0+1,r1		! ret = 1..8

	tst	r0,r0		! ffs(0) is 0
	bt	Lzero		! testing here to accelerate ret=1..8 cases

	shlr8	r0
	tst	#0xff,r0
	bf/s	L8bit
	mov	#8+1,r1		! ret = 9..16

	shlr8	r0
	tst	#0xff,r0
	bf/s	L8bit
	mov	#16+1,r1	! ret = 17..24

	shlr8	r0
	mov	#24+1,r1	! ret = 25..32

L8bit:
	tst	#0x0f,r0
	bt	4f

	tst	#0x03,r0
	bt	2f
	tst	#0x01,r0	! not bit 0 -> T
	mov	#0,r0
	rts
	 addc	r1,r0		! 0 + r1 + T -> r0

2:	tst	#0x04,r0
	mov	#2,r0
	rts
	 addc	r1,r0

4:	tst	#0x30,r0
	bt	6f
	tst	#0x10,r0
	mov	#4,r0
	rts
	 addc	r1,r0

6:	tst	#0x40,r0
	mov	#6,r0
	rts
	 addc	r1,r0

Lzero:	rts
	 nop
