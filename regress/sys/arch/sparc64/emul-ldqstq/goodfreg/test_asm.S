/*	$OpenBSD: test_asm.S,v 1.1 2003/07/12 04:08:33 jason Exp $	*/

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

ENTRY(asm_ldq_f0)
	retl
	 ldq	[%o0], %f0

ENTRY(asm_ldq_f4)
	retl
	 ldq	[%o0], %f4

ENTRY(asm_ldq_f8)
	retl
	 ldq	[%o0], %f8

ENTRY(asm_ldq_f12)
	retl
	 ldq	[%o0], %f12

ENTRY(asm_ldq_f16)
	retl
	 ldq	[%o0], %f16

ENTRY(asm_ldq_f20)
	retl
	 ldq	[%o0], %f20

ENTRY(asm_ldq_f24)
	retl
	 ldq	[%o0], %f24

ENTRY(asm_ldq_f28)
	retl
	 ldq	[%o0], %f28

ENTRY(asm_ldq_f32)
	retl
	 ldq	[%o0], %f32

ENTRY(asm_ldq_f36)
	retl
	 ldq	[%o0], %f36

ENTRY(asm_ldq_f40)
	retl
	 ldq	[%o0], %f40

ENTRY(asm_ldq_f44)
	retl
	 ldq	[%o0], %f44

ENTRY(asm_ldq_f48)
	retl
	 ldq	[%o0], %f48

ENTRY(asm_ldq_f52)
	retl
	 ldq	[%o0], %f52

ENTRY(asm_ldq_f56)
	retl
	 ldq	[%o0], %f56

ENTRY(asm_ldq_f60)
	retl
	 ldq	[%o0], %f60

ENTRY(asm_stq_f0)
	retl
	 stq	%f0, [%o0]

ENTRY(asm_stq_f4)
	retl
	 stq	%f4, [%o0]

ENTRY(asm_stq_f8)
	retl
	 stq	%f8, [%o0]

ENTRY(asm_stq_f12)
	retl
	 stq	%f12, [%o0]

ENTRY(asm_stq_f16)
	retl
	 stq	%f16, [%o0]

ENTRY(asm_stq_f20)
	retl
	 stq	%f20, [%o0]

ENTRY(asm_stq_f24)
	retl
	 stq	%f24, [%o0]

ENTRY(asm_stq_f28)
	retl
	 stq	%f28, [%o0]

ENTRY(asm_stq_f32)
	retl
	 stq	%f32, [%o0]

ENTRY(asm_stq_f36)
	retl
	 stq	%f36, [%o0]

ENTRY(asm_stq_f40)
	retl
	 stq	%f40, [%o0]

ENTRY(asm_stq_f44)
	retl
	 stq	%f44, [%o0]

ENTRY(asm_stq_f48)
	retl
	 stq	%f48, [%o0]

ENTRY(asm_stq_f52)
	retl
	 stq	%f52, [%o0]

ENTRY(asm_stq_f56)
	retl
	 stq	%f56, [%o0]

ENTRY(asm_stq_f60)
	retl
	 stq	%f60, [%o0]
