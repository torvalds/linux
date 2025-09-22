/*	$OpenBSD: fpregs.S,v 1.1 2003/07/12 04:08:33 jason Exp $	*/

/*
 * Copyright (c) 2003 Jason L. Wright (jason@thought.net)
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <machine/asm.h>
#include <machine/trap.h>
#include <machine/frame.h>

ENTRY(loadfpregs)
	ldd	[%o0 + 0 * 8], %f0
	ldd	[%o0 + 1 * 8], %f2
	ldd	[%o0 + 2 * 8], %f4
	ldd	[%o0 + 3 * 8], %f6
	ldd	[%o0 + 4 * 8], %f8
	ldd	[%o0 + 5 * 8], %f10
	ldd	[%o0 + 6 * 8], %f12
	ldd	[%o0 + 7 * 8], %f14
	ldd	[%o0 + 8 * 8], %f16
	ldd	[%o0 + 9 * 8], %f18
	ldd	[%o0 + 10 * 8], %f20
	ldd	[%o0 + 11 * 8], %f22
	ldd	[%o0 + 12 * 8], %f24
	ldd	[%o0 + 13 * 8], %f26
	ldd	[%o0 + 14 * 8], %f28
	ldd	[%o0 + 15 * 8], %f30
	ldd	[%o0 + 16 * 8], %f32
	ldd	[%o0 + 17 * 8], %f34
	ldd	[%o0 + 18 * 8], %f36
	ldd	[%o0 + 19 * 8], %f38
	ldd	[%o0 + 20 * 8], %f40
	ldd	[%o0 + 21 * 8], %f42
	ldd	[%o0 + 22 * 8], %f44
	ldd	[%o0 + 23 * 8], %f46
	ldd	[%o0 + 24 * 8], %f48
	ldd	[%o0 + 25 * 8], %f50
	ldd	[%o0 + 26 * 8], %f52
	ldd	[%o0 + 27 * 8], %f54
	ldd	[%o0 + 28 * 8], %f56
	ldd	[%o0 + 29 * 8], %f58
	ldd	[%o0 + 30 * 8], %f60
	retl 
	 ldd	[%o0 + 31 * 8], %f62

ENTRY(savefpregs)
	std	%f0, [%o0 + 0 * 8]
	std	%f2, [%o0 + 1 * 8]
	std	%f4, [%o0 + 2 * 8]
	std	%f6, [%o0 + 3 * 8]
	std	%f8, [%o0 + 4 * 8]
	std	%f10, [%o0 + 5 * 8]
	std	%f12, [%o0 + 6 * 8]
	std	%f14, [%o0 + 7 * 8]
	std	%f16, [%o0 + 8 * 8]
	std	%f18, [%o0 + 9 * 8]
	std	%f20, [%o0 + 10 * 8]
	std	%f22, [%o0 + 11 * 8]
	std	%f24, [%o0 + 12 * 8]
	std	%f26, [%o0 + 13 * 8]
	std	%f28, [%o0 + 14 * 8]
	std	%f30, [%o0 + 15 * 8]
	std	%f32, [%o0 + 16 * 8]
	std	%f34, [%o0 + 17 * 8]
	std	%f36, [%o0 + 18 * 8]
	std	%f38, [%o0 + 19 * 8]
	std	%f40, [%o0 + 20 * 8]
	std	%f42, [%o0 + 21 * 8]
	std	%f44, [%o0 + 22 * 8]
	std	%f46, [%o0 + 23 * 8]
	std	%f48, [%o0 + 24 * 8]
	std	%f50, [%o0 + 25 * 8]
	std	%f52, [%o0 + 26 * 8]
	std	%f54, [%o0 + 27 * 8]
	std	%f56, [%o0 + 28 * 8]
	std	%f58, [%o0 + 29 * 8]
	std	%f60, [%o0 + 30 * 8]
	retl
	 std	%f62, [%o0 + 31 * 8]

ENTRY(initfpregs)
	not	%g0, %o1
	stx	%o1, [%o0 + 0 * 8]
	stx	%o1, [%o0 + 1 * 8]
	stx	%o1, [%o0 + 2 * 8]
	stx	%o1, [%o0 + 3 * 8]
	stx	%o1, [%o0 + 4 * 8]
	stx	%o1, [%o0 + 5 * 8]
	stx	%o1, [%o0 + 6 * 8]
	stx	%o1, [%o0 + 7 * 8]
	stx	%o1, [%o0 + 8 * 8]
	stx	%o1, [%o0 + 9 * 8]
	stx	%o1, [%o0 + 10 * 8]
	stx	%o1, [%o0 + 11 * 8]
	stx	%o1, [%o0 + 12 * 8]
	stx	%o1, [%o0 + 13 * 8]
	stx	%o1, [%o0 + 14 * 8]
	stx	%o1, [%o0 + 15 * 8]
	stx	%o1, [%o0 + 16 * 8]
	stx	%o1, [%o0 + 17 * 8]
	stx	%o1, [%o0 + 18 * 8]
	stx	%o1, [%o0 + 19 * 8]
	stx	%o1, [%o0 + 20 * 8]
	stx	%o1, [%o0 + 21 * 8]
	stx	%o1, [%o0 + 22 * 8]
	stx	%o1, [%o0 + 23 * 8]
	stx	%o1, [%o0 + 24 * 8]
	stx	%o1, [%o0 + 25 * 8]
	stx	%o1, [%o0 + 26 * 8]
	stx	%o1, [%o0 + 27 * 8]
	stx	%o1, [%o0 + 28 * 8]
	stx	%o1, [%o0 + 29 * 8]
	stx	%o1, [%o0 + 30 * 8]
	retl
	 stx	%o1, [%o0 + 31 * 8]
